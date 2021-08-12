#include <U8x8lib.h> //Display library.
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

const uint8_t* boldFont = u8x8_font_amstrad_cpc_extended_f;
const uint8_t* lightFont = u8x8_font_5x7_f;
const int codedConnections = 8;
//Motor 1 will just be array[0], 2 -> array[1], etc.
const gpio_num_t dataPin [codedConnections] = { GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_19, GPIO_NUM_23, GPIO_NUM_18, GPIO_NUM_5, GPIO_NUM_17, GPIO_NUM_2 };
const gpio_num_t checkerPin [codedConnections] = { GPIO_NUM_38, GPIO_NUM_39, GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_27, GPIO_NUM_12 };
//PWM channels will just be 1 through 8.
const int pwmFrequency = 5000;
const int pwmResolution = 8;

U8X8_SSD1306_128X64_NONAME_SW_I2C display(/*clockPin*/ 15, /*dataPin*/ 4, /* resetPin*/ 16);
Preferences preferences; //https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
AsyncWebServer server(80); //https://randomnerdtutorials.com/esp32-web-server-spiffs-spi-flash-file-system/
AsyncWebSocket websocket("/websocket"); //https://randomnerdtutorials.com/esp32-websocket-server-arduino/
String networksJson; //Only defined if SetupAPNetwork() is run.
int speeds [codedConnections] = { -1, -1, -1, -1, -1, -1, -1, -1 };

void Log(const String message)
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
    Log("SSID: " + String(ssid));
    Log("IP: " + IP.toString());

    //Setup the AP server.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Log("GET/");
        const String data = "<!DOCTYPE html><html lang='en'><head> <meta charset='UTF-8'> <meta http-equiv='X-UA-Compatible' content='IE=edge'> <meta name='viewport' content='width=device-width, initial-scale=1.0'> <title>Network configuration</title> <style>*{background-color: #f5f5f5; color: black;}</style></head><body> <form id='networkForm'> <select name='networks' id='ssid'> </select> <input type='password' name='password' id='password' placeholder='Password' disabled> <input type='submit' value='Connect' id='connectButton'> </form> <p id='message'>Loading...</p><a id='link'></a> <script>var networkForm; var networksList; var passwordField; var connectButton; var message; var link; function Init(){networkForm=document.querySelector('#networkForm'); networksList=document.querySelector('#ssid'); passwordField=document.querySelector('#password'); connectButton=document.querySelector('#connectButton'); message=document.querySelector('#message'); link=document.querySelector('#link'); networkForm.addEventListener('submit', (e)=>{e.preventDefault(); e.returnValue=false; var data=`ssid=${encodeURIComponent(networksList.options[networksList.selectedIndex].value)}`; if (passwordField.value !==''){data +=`&password=${encodeURIComponent(passwordField.value)}`;}var xhttp=new XMLHttpRequest(); xhttp.open('POST', './network', true); xhttp.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded'); xhttp.onreadystatechange=function(){if (xhttp.readyState==4 && xhttp.status==200){var response=JSON.parse(xhttp.responseText); if (!response.success){message.innerText='Failed to connect to the network.'}else{message.innerText='Network configured, please click the link below if you have not automatically been redirected.'; link.href='http://' + response.ip; link.innerText='Reload'; window.location.href='http://' + response.ip;}}}; xhttp.send(data);}); networksList.addEventListener('change', ()=>{if (networksList.value !=''){var value=networksList.options[networksList.selectedIndex]; passwordField.disabled=parseInt(value.getAttribute('data-encryptionType'))==0; passwordField.value='';}}); GetNetworks();}function GetNetworks(){var xhttp=new XMLHttpRequest(); xhttp.open('GET', './networks', true); xhttp.onreadystatechange=function(){if (this.readyState==4 && this.status==200){message.innerHTML=''; var networks=JSON.parse(this.responseText); var usedSSIDs=[]; for (var i=0; i < networks.length; i++){var network=networks[i]; if (usedSSIDs.includes(network.SSID)){continue;}usedSSIDs.push(network.SSID); var option=document.createElement('option'); option.value=network.SSID; option.innerHTML=`${network.SSID}(Secure: ${network.encryptionType !=0 ? 'yes' : 'no'})`; option.setAttribute('data-ssid', network.SSID); option.setAttribute('data-rssi', network.RSSI); option.setAttribute('data-encryptionType', network.encryptionType); networksList.appendChild(option);}if (networks.length >=1){networksList.dispatchEvent(new Event('change'));}}}; xhttp.send();}window.addEventListener('load', ()=>{Init();}); </script></body></html>";
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
        String data = "{\"success\":" + success + ",\"ip\":\"" + ipString + "\"}";
        request->send(200, "application/json", data);
        Reboot();
    });

    Log("Done SetupAPNetwork()");
}

//I am playing a game of shit in shit out. If incorrect data is sent in then incorrect data will be sent back out. This is not a tool to be used by other programs so I am coding it knowing what data I will get, I am not adding more checks because I cant be bothered and it will slow the program down.
void HandleWebSocketMessage(AsyncWebSocketClient* client, void *arg, uint8_t *data, size_t len)
{
	AwsFrameInfo *info = (AwsFrameInfo*)arg;
	//Checks if all packets have been recieved (I think).
  	if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  	{
		char* message = (char*)data;
        bool broadcast = false;
		String response;
		DynamicJsonDocument JsonMessage(JSON_OBJECT_SIZE(2) + JSON_ARRAY_SIZE(codedConnections)); //This should be enough memory, it may be more than needed but it should be fine.
		StaticJsonDocument<JSON_ARRAY_SIZE(codedConnections)> JsonResponse;
		DeserializationError error = deserializeJson(JsonMessage, message);

		if (!error && !JsonMessage["command"].isNull())
		{
			if (JsonMessage["command"] == "get")
            {
                //Not quite sure how to write the data straight to the root object so I am looping through the array and adding it to the root object.
                for (int i = 0; i < codedConnections; ++i)
                {
                    JsonResponse.add(speeds[i]);
                }
            }
            else if (JsonMessage["command"] == "set" && !JsonMessage["data"].isNull())
            {
                broadcast = true;

                int counter = 0;
                for (auto &&speed : JsonMessage["data"].as<JsonArray>())
                {
                    if (speeds[counter] != -1)
                    {
                        int _speed = speed.as<int>();
                        if (speed <= 0) { _speed = 0; }
                        else if (speed >= 256) { _speed = 256; }

                        ledcWrite(counter + 1, 256 - _speed);
                        speeds[counter] = _speed;
                        JsonResponse.add(_speed);
                    }
                    else
                    {
                        JsonResponse.add(-1);
                    }
                    
                    counter++;
                }
            }
            else { JsonResponse["error"] = true; }
		}
        else { JsonResponse["error"] = true; }

		serializeJson(JsonResponse, response);
        if (broadcast) { websocket.textAll(response); }
        else { client->text(response); }
  	}
}

void WebsocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
    // Log("WS/Websocket"); //Websocket event logs will happen frequently which will slow down the program.

	switch (type)
	{
		case WS_EVT_CONNECT:
			break;
		case WS_EVT_DISCONNECT:
			break;
		case WS_EVT_DATA:
			HandleWebSocketMessage(client, arg, data, len);
			break;
		case WS_EVT_PONG:
			break;
		case WS_EVT_ERROR:
			break;
		default:
			//Unknown event type.
			break;
	}
}

void SetupPins()
{
    Log("SetupPins()");

    int connectedPins = 0;
    for (int i = 0; i < codedConnections; i++)
    {
        const int j = i + 1;
        ledcSetup(j, pwmFrequency, pwmResolution);
        ledcAttachPin(dataPin[i], j);
        ledcWrite(j, 0);

        pinMode(checkerPin[i], INPUT);
        bool isConnected = (digitalRead(checkerPin[i]) + digitalRead(checkerPin[i]) + digitalRead(checkerPin[i])) == 3;
        if (isConnected)
        {
            speeds[i] = 0;
            connectedPins++;
        }
        Log("GPIO" + String(checkerPin[i]) + "(GPIO" + String(dataPin[i]) + "): " + String(isConnected));

        ledcWrite(j, 256);
    }
    DisplayString(0, 2, "Motors:" + String(connectedPins));

    Log("Done SetupPins()");
}

void SetupSTANetwork()
{
    Log("SetupSTANetwork()");

    WiFi.setAutoReconnect(true);

    DisplayString(0, 0, "SSID:" + WiFi.SSID());
    DisplayString(0, 1, "IP:" + WiFi.localIP().toString());
    Log("SSID: " + WiFi.SSID());
    Log("IP: " + WiFi.localIP().toString());

    SetupPins();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Log("GET/");
        const String data = "<!DOCTYPE html><html lang='en'><head> <meta charset='UTF-8'> <meta http-equiv='X-UA-Compatible' content='IE=edge'> <meta name='viewport' content='width=device-width, initial-scale=1.0'> <title>Controls</title> <style id='themeColours'></style> <style>body{background-color: rgb(var(--backgroundColour)); color: rgb(var(--foregroundColour)); font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;}p{font-weight: 500; margin-bottom: 10px;}.sliderContainer{position: relative; width: max-content; height: max-content; height: 15px; width: 130px; background-color: rgb(var(--backgroundAltColour));}.sliderBackground{position: absolute; background-color: rgb(var(--accentColour)); height: 15px; width: 130px;}input[type='range']{position: absolute; -webkit-appearance: none; appearance: none; background: transparent; width: 100%; height: 100%; margin: 0;}input[type='range']::-webkit-slider-thumb{-webkit-appearance: none; appearance: none; width: 15px; height: 15px; background: rgb(var(--foregroundColour)); cursor: pointer;}input[type='text']{outline-width: 0; font-weight: normal; border: 0; background-color: transparent; border-bottom: 1px solid rgba(var(--foregroundColour), 1); color: rgb(var(--foregroundColour)); font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;}</style></head><body> <p>IP:</p><input type='text' id='ip'> <div id='controls'></div><p id='messages'></p><p>Dark mode:</p><input type='checkbox' id='darkMode' checked> <script>var themeColours; var themeToggle; var ip; var controls; var messages; /*var fade;*/ var motorsObject=null; var websocket; function Init(_ip=location.hostname){themeColours=document.querySelector('#themeColours'); themeToggle=document.querySelector('#darkMode'); websocket=null; motorsObject=null; ip=document.querySelector('#ip'); ip.value=_ip; controls=document.querySelector('#controls'); controls.innerHTML=''; messages=document.querySelector('#messages'); messages.innerHTML=''; /*fade=document.querySelector('#fade');*/ themeToggle.onchange=ToggleTheme; ToggleTheme(); ip.onchange=function(){Init(ip.value);}; WebsocketInit();}function ToggleTheme(){themeColours.innerHTML=` :root{--foregroundColour: ${darkMode.checked ? '255, 255, 255' : '0, 0, 0'}; --backgroundColour: ${darkMode.checked ? '13, 17, 23' : '255, 255, 255'}; --backgroundAltColour: ${darkMode.checked ? '39, 43, 46' : '225, 225, 225'}; --accentColour: ${darkMode.checked ? '100, 0, 255' : '255, 120, 0'};}`;}function WebsocketInit(){websocket=new WebSocket(`ws://${ip.value}/websocket`); websocket.onopen=WebsocketOnOpen; websocket.onmessage=WebsocketOnMessage; websocket.onerror=WebsocketOnError;}function WebsocketOnOpen(){websocket.send(JSON.stringify({command: 'get'}));}function WebsocketOnMessage(evt){var data=JSON.parse(evt.data); if (motorsObject==null){motorsObject=[]; for (var i=0; i < data.length; i++){const speed=data[i]; var input=null; if (speed !=-1){var p=document.createElement('p'); p.innerText=`Motor ${i + 1}:`; var container=document.createElement('div'); container.className='sliderContainer'; var background=document.createElement('span'); background.className='sliderBackground'; input=document.createElement('input'); input.type='range'; input.min=0; input.max=256; input.value=speed; input.step=1; background.style.width=`${(speed / input.max) * 100}%`; /*This sends messages too fast for the server to keep up. As a work around, for speed fading I could change the speed gradually on the server side or send over extra data telling the server if it should fade and at what speed.*/ input.oninput=function(e){e.target.parentElement.querySelector('.sliderBackground').style.width=`${(e.target.value / e.target.max) * 100}%`;}; input.onchange=function(){var data=[]; motorsObject.forEach(element=>{if (element !=null){data.push(element.value);}else{data.push(-1);}}); websocket.send(JSON.stringify({command: 'set', data: data/*, fade: fade.checked*/}));}; controls.appendChild(p); container.appendChild(background); container.appendChild(input); controls.appendChild(container);}motorsObject.push(input);}}else{for (let i=0; i < data.length; i++){if (motorsObject[i] !=null){motorsObject[i].value=data[i]; motorsObject[i].parentElement.querySelector('.sliderBackground').style.width=`${(data[i] / motorsObject[i].max) * 100}%`;}}}}function WebsocketOnError(){WebsocketInit();}window.addEventListener('load', ()=>{var search=new URLSearchParams(location.search); if (search.has('ip')){Init(search.get('ip'));}else{Init(location.hostname=='' ? '127.0.0.1' : location.hostname);}}); </script></body></html>";
        request->send(200, "text/html", data);
    });

    //TODO Rescan for new devices instead of having to reboot to check the connections.
    // server.on("/setupPins", HTTP_GET, [](AsyncWebServerRequest *request)
    // {
    //     Log("GET/setupPins");
    //     SetupPins();
    //     request->send(200, "text/html", "OK");
    // });

	websocket.onEvent(WebsocketEvent);
	server.addHandler(&websocket);

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
    //These are just placed here to reduce duplicated code.
    server.on("/network_reset", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Log("GET/network_reset");
        request->send(521, "text/plain", "GET/network_reset");
        preferences.clear();
        Reboot(false);
    });
    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) { Log("GET/reboot"); request->send(521, "text/plain", "GET/reboot"); Reboot(true); });
    server.onNotFound([](AsyncWebServerRequest *request) { Log("404"); request->send(404, "text/plain", "404"); });
    server.begin();

    Log("Done NetworkSetup()");
}

void setup()
{
	Serial.begin(115200);
	display.begin();
  	Log("setup()");

	NetworkSetup();

	Log("Done setup()");
}

void loop()
{
    //Stop this task as it is not needed.
	vTaskDelete(NULL);
}