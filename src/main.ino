#include <U8x8lib.h> //Display library.
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

const uint8_t* boldFont = u8x8_font_amstrad_cpc_extended_f;
const uint8_t* lightFont = u8x8_font_5x7_f;

U8X8_SSD1306_128X64_NONAME_SW_I2C display(/*clockPin*/ 15, /*dataPin*/ 4, /* resetPin*/ 16);
Preferences preferences; //https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
AsyncWebServer server(80); //https://randomnerdtutorials.com/esp32-web-server-spiffs-spi-flash-file-system/
AsyncWebSocket websocket("/websocket/"); //https://randomnerdtutorials.com/esp32-websocket-server-arduino/
String networksJson; //Only defined if SetupAPNetwork() is run.

//Change the font for the timestamp and log then remove the ':' to save space.
void Log(const String message) //Look into using char* instead of String, I've heard that String is bad for memory leaks.
{    
	Serial.println(message);
	display.clearLine(7);
	display.setFont(lightFont); //Use a different font here -> https://github.com/olikraus/u8g2/wiki/fntlist8x8.
	//Millis is used as a timestamp.
	//With the current config the display shows a max of 12-13 chars before wrapping around.
	display.drawString(0, 7, (String(millis() % 100) + ": " + message.substring(0, 12)).c_str());
}

void Reboot(bool _delay = false)
{
	if (_delay) { delay(5000); }
	Log("Rebooting");
	delay(100);
	ESP.restart();
}

//Make this keep a buffer of all the currently displayed data so that it can wrap around onto a new line if needed.
void DisplayString(int x, int y, String message)
{
    int semicolonIndex = message.indexOf(":");
    if (semicolonIndex == -1)
    {
	    display.setFont(lightFont);
	    display.drawString(x, y, message.c_str());
    }
    else
    {
	    display.setFont(boldFont);
	    display.drawString(x, y, message.substring(0, semicolonIndex).c_str());
        display.setFont(lightFont);
        display.drawString(x + semicolonIndex + 1, y, message.substring(semicolonIndex + 1).c_str());
    }
}

IPAddress JoinNetwork(const char *SSID, const char *Password)
{
	Log("JoinNetwork()");

	WiFi.begin(SSID, Password);
	int connectionAttempts = 0;
	while (WiFi.status() != WL_CONNECTED)
	{
		vTaskDelay(500 / portTICK_RATE_MS);
		feedLoopWDT();
		if (connectionAttempts++ > 10) { break; }
	}

    IPAddress ip;
    if (WiFi.status() != WL_CONNECTED)
    {
        ip = IPAddress(0, 0, 0, 0);
    }
    else
    {
        preferences.putString("ssid", SSID);
        preferences.putString("password", Password);
        ip = WiFi.localIP();
    }

    Log("Done JoinNetwork()");
    return ip;
}

void SetupAPNetwork()
{
    Log("SetupAPNetwork()");

    //Create a list of found networks at startup.
    int networks = WiFi.scanNetworks();
    DynamicJsonDocument doc(JSON_ARRAY_SIZE(networks) + ((networks) * JSON_OBJECT_SIZE(3)));
    for (int i = 0; i < networks; ++i)
    {
        JsonObject obj = doc.createNestedObject();
        obj["SSID"] = WiFi.SSID(i);
        obj["RSSI"] = WiFi.RSSI(i);
        obj["encryptionType"] = WiFi.encryptionType(i);
    }
    serializeJson(doc, networksJson);

    //Keep generating SSIDs until a unique one is found.
    long ssidRandom;
    while (true)
    {
        ssidRandom = random(1000, 9999);
        boolean ssidMatch = false;
        for (int i = 0; i < networks; ++i)
        {
            if ("esp32_" + String(ssidRandom) == WiFi.SSID(i))
            {
                ssidMatch = true;
                break;
            }
        }
        if (!ssidMatch) { break; }
    }
    const String ssid = "esp32_" + String(ssidRandom);
    WiFi.softAP(ssid.c_str());

    //Display the AP info.
    IPAddress IP = WiFi.softAPIP();
    DisplayString(0, 0, "SSID:" + String(ssid));
    DisplayString(0, 1, "IP:" + IP.toString());

    //Setup the AP server.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Log("GET/");
        const String data = "<!DOCTYPE html><html lang='en'><head> <meta charset='UTF-8'> <meta http-equiv='X-UA-Compatible' content='IE=edge'> <meta name='viewport' content='width=device-width, initial-scale=1.0'> <title>Network configuration</title> <style>*{background-color: #f5f5f5; color: black;}</style></head><body> <form id='networkForm'> <select name='networks' id='ssid'> </select> <input type='password' name='password' id='password' placeholder='Password' disabled> <input type='submit' value='Connect' id='connectButton'> </form> <p id='message'>Loading...</p><a id='link'></a> <script>var networkForm; var networksList; var passwordField; var connectButton; var message; var link; function Init(){networkForm=document.querySelector('#networkForm'); networksList=document.querySelector('#ssid'); passwordField=document.querySelector('#password'); connectButton=document.querySelector('#connectButton'); message=document.querySelector('#message'); link=document.querySelector('#link'); networkForm.addEventListener('submit', (e)=>{e.preventDefault(); e.returnValue=false; var data=`ssid=${encodeURIComponent(networksList.options[networksList.selectedIndex].value)}`; if (passwordField.value !==''){data +=`&password=${encodeURIComponent(passwordField.value)}`;}var xhttp=new XMLHttpRequest(); xhttp.open('POST', './network', true); xhttp.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhttp.onreadystatechange=function(){if (xhttp.readyState==4 && xhttp.status==200){message.innerText='Network configured, please click the link below if you have not automatically been redirected.'; link.href=xhttp.responseText; link.innerText='Reload'; window.location.href=xhttp.responseText;}}; xhttp.send(data);}); networksList.addEventListener('change', ()=>{if (networksList.value !=''){var value=networksList.options[networksList.selectedIndex]; passwordField.disabled=parseInt(value.getAttribute('data-encryptionType'))==0; passwordField.value='';}}); GetNetworks();}function GetNetworks(){var xhttp=new XMLHttpRequest(); xhttp.open('GET', './networks', true); xhttp.onreadystatechange=function(){if (this.readyState==4 && this.status==200){message.innerHTML=''; var networks=JSON.parse(this.responseText); var usedSSIDs=[]; for (var i=0; i < networks.length; i++){var network=networks[i]; if (usedSSIDs.includes(network.SSID)){continue;}usedSSIDs.push(network.SSID); var option=document.createElement('option'); option.value=network.SSID; option.innerHTML=`${network.SSID}(Secure: ${network.encryptionType !=0 ? 'yes' : 'no'})`; option.setAttribute('data-ssid', network.SSID); option.setAttribute('data-rssi', network.RSSI); option.setAttribute('data-encryptionType', network.encryptionType); networksList.appendChild(option);}if (networks.length >=1){networksList.dispatchEvent(new Event('change'));}}}; xhttp.send();}window.addEventListener('load', ()=>{Init();}); </script></body></html>";
        request->send(200, "text/html", data);
    });
    server.on("/networks", HTTP_GET, [](AsyncWebServerRequest *request) { Log("GET/networks"); request->send(200, "application/json", networksJson); });
    server.on("/network", HTTP_POST, [](AsyncWebServerRequest *request)
    {
        Log("POST/network");
        String ssid;
        String password;
        for (int i = 0; i < request->args(); ++i)
        {
            String key = request->argName(i);

            if (key == "ssid") { ssid = request->arg(i); }
            else if (key == "password") { password = request->arg(i); }
            else { continue; }
        }
        IPAddress ip = JoinNetwork(ssid.c_str(), password.length() != 0 ? password.c_str() : NULL);
        String ipString = ip.toString();
        String success = ipString != "0.0.0.0" ? "true" : "false";
        String data = "{\"success\":\"" + success + "\",\"ip\":\"" + ipString + "\"}";
        request->send(200, "application/json", data);
        Reboot(true); //Reboot is given delay to give the server time to send the message.
    });

    Log("Done SetupAPNetwork()");
}

void SetupSTANetwork()
{
    Log("SetupSTANetwork()");

    DisplayString(0, 0, "SSID:" + WiFi.SSID());
    DisplayString(0, 1, "IP:" + WiFi.localIP().toString());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) { Log("GET/"); request->send(200, "text/html", "GET/"); });

    Log("Done SetupSTANetwork()");
}

void NetworkSetup()
{
	Log("NetworkSetup()");
    
    preferences.begin("network", false);

    WiFi.mode(WIFI_MODE_APSTA);

    String ssid = preferences.getString("ssid", "");
    String password = preferences.getString("password", "");
    if (ssid == "" || password == "")
    {
        SetupAPNetwork();
    }
    else
    {
        WiFi.begin(ssid.c_str(), password.c_str());
        int connectionAttempts = 0;
        while (WiFi.status() != WL_CONNECTED)
        {
            vTaskDelay(500 / portTICK_RATE_MS);
            feedLoopWDT();
            if (connectionAttempts++ > 10) { break; }
        }
        if (WiFi.status() != WL_CONNECTED)
        {
            //Failed to connect to the saved network, so run the setup again.
            SetupAPNetwork();
        }
        else
        {
            SetupSTANetwork();
        }
    }

    //Other server configurations are made in the other network setup functions.
    //These two are just placed here to reduce duplicated code.
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) { Log("GET/reboot"); request->send(521, "text/plain", "GET/reboot"); Reboot(true); });
    server.onNotFound([](AsyncWebServerRequest *request) { Log("404"); request->send(404, "text/plain", "404"); });
    server.begin();

    Log("Done NetworkSetup()");
}

void setup()
{
  	Log("setup()");
	Serial.begin(115200);
	display.begin();

	NetworkSetup();

	Log("Done setup()");
}

void loop()
{
    //Stop this task as it is not needed.
	vTaskDelete(NULL);
}