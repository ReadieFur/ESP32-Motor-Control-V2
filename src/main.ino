#include <U8x8lib.h> //Display library.
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>

const uint8_t* boldFont = u8x8_font_amstrad_cpc_extended_f;
const uint8_t* lightFont = u8x8_font_5x7_f;
const int pwmFrequency = 5000;
const int pwmResolution = 8;

U8X8_SSD1306_128X64_NONAME_SW_I2C display(/*clockPin*/ 15, /*dataPin*/ 4, /* resetPin*/ 16);
Preferences preferences; //https://randomnerdtutorials.com/esp32-save-data-permanently-preferences/
AsyncWebServer server(80); //https://randomnerdtutorials.com/esp32-web-server-spiffs-spi-flash-file-system/
AsyncWebSocket websocket("/websocket"); //https://randomnerdtutorials.com/esp32-websocket-server-arduino/
String networksJson; //Only defined if SetupAPNetwork() is run.
DynamicJsonDocument motorsObject(64); //Only defined if SetupSTANetwork() is run.

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

void SamplePins(bool &GPIO38, bool &GPIO39, bool &GPIO34, bool &GPIO35)
{
    Log("SamplePins()");

    //Why these pins? They are all next to each other.
    //Why not start from the end of the board? Pin 37 on my two boards seemed to always read positive even when nothing was connected.
    pinMode(GPIO_NUM_38, INPUT);
    pinMode(GPIO_NUM_39, INPUT);
    pinMode(GPIO_NUM_34, INPUT);
    pinMode(GPIO_NUM_35, INPUT);

    //Take multiple samples to make sure we're not getting a false positive.
    GPIO38 = (digitalRead(GPIO_NUM_38) + digitalRead(GPIO_NUM_38) + digitalRead(GPIO_NUM_38)) == 3;
    GPIO39 = (digitalRead(GPIO_NUM_39) + digitalRead(GPIO_NUM_39) + digitalRead(GPIO_NUM_39)) == 3;
    GPIO34 = (digitalRead(GPIO_NUM_34) + digitalRead(GPIO_NUM_34) + digitalRead(GPIO_NUM_34)) == 3;
    GPIO35 = (digitalRead(GPIO_NUM_35) + digitalRead(GPIO_NUM_35) + digitalRead(GPIO_NUM_35)) == 3;

    Log("GPIO38: " + String(GPIO38));
    Log("GPIO39: " + String(GPIO39));
    Log("GPIO34: " + String(GPIO34));
    Log("GPIO35: " + String(GPIO35));

    Log("Done SamplePins()");
}

void HandleWebSocketMessage(AsyncWebSocketClient* client, void *arg, uint8_t *data, size_t len)
{
	AwsFrameInfo *info = (AwsFrameInfo*)arg;
	//Checks if all packets have been recieved (I think).
  	if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  	{
		char* message = (char*)data;
        bool broadcast = false;
		String response;
		DynamicJsonDocument JsonMessage(128); //Reserve at least 128 bytes for the JsonDocument, if more bytes are required it will allocate itself more.
		DynamicJsonDocument JsonResponse(64);
		DeserializationError error = deserializeJson(JsonMessage, message);

		if (!error && !JsonMessage["command"].isNull())
		{
            JsonResponse["error"] = false;

			if (JsonMessage["command"] == "getMotors")
            {
                JsonObject data = JsonResponse.createNestedObject("data");

                for (JsonPair kv : motorsObject.as<JsonObject>())
                {
                    data[kv.key().c_str()] = kv.value().as<JsonObject>()["speed"];
                }
            }
            else if (JsonMessage["command"] == "setMotors" && !JsonMessage["data"].isNull())
            {
                broadcast = true;

                JsonObject data = JsonResponse.createNestedObject("data");

                int fade = !JsonMessage["fade"].isNull() && JsonMessage["fade"] == true;

                for (JsonPair kv : JsonMessage["data"].as<JsonObject>())
                {
                    if (kv.key().c_str() != "")
                    {
                        int speed = kv.value().as<int>();
                        if (speed <= 0) { speed = 0; }
                        else if (speed >= 255) { speed = 255; }

                        //TODO Fade.
                        // if (fade)
                        // {
                        //     bool reverse = speed < motorsObject[kv.key().c_str()]["speed"];
                        //     int change = reverse ? motorsObject[kv.key().c_str()]["speed"] - speed : speed - motorsObject[kv.key().c_str()]["speed"];
                        //     int fadeSpeed = 100 / change; //100 is for 100ms fade.
                        //     // for (size_t i = motorsObject[kv.key().c_str()]["speed"]; (reverse ? i < speed : i > speed); (reverse ? i++ : i--))
                        //     // {
                        //     //     motorsObject[kv.key().c_str()]["speed"] = i;
                        //     //     vTaskDelay(fade / portTICK_PERIOD_MS);
                        //     //     feedLoopWDT();
                        //     // }
                        // }
                        // else
                        // {
                        //     ledcWrite(motorsObject[kv.key().c_str()]["channel"], 255 - speed);
                        // }

                        ledcWrite(motorsObject[kv.key().c_str()]["channel"], 255 - speed);
                        motorsObject[kv.key().c_str()]["speed"] = speed;
                        data[kv.key().c_str()] = speed;
                        Log(String(speed));
                    }
                }
            }
            else
            {
                JsonResponse["error"] = true;
            }
		}
		else
		{
			JsonResponse["error"] = true;
		}

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

void SetupSTANetwork()
{
    Log("SetupSTANetwork()");

    WiFi.setAutoReconnect(true);

    DisplayString(0, 0, "SSID:" + WiFi.SSID());
    DisplayString(0, 1, "IP:" + WiFi.localIP().toString());
    Log("SSID: " + WiFi.SSID());
    Log("IP: " + WiFi.localIP().toString());

    ledcSetup(1, pwmFrequency, pwmResolution);
    ledcAttachPin(GPIO_NUM_21, 1);
    ledcWrite(1, 255);
    ledcSetup(2, pwmFrequency, pwmResolution);
    ledcAttachPin(GPIO_NUM_22, 2);
    ledcWrite(2, 255);
    ledcSetup(3, pwmFrequency, pwmResolution);
    ledcAttachPin(GPIO_NUM_19, 3);
    ledcWrite(3, 255);
    ledcSetup(4, pwmFrequency, pwmResolution);
    ledcAttachPin(GPIO_NUM_23, 4);
    ledcWrite(4, 255);

    int numMotors = 0;
    bool GPIO38, GPIO39, GPIO34, GPIO35;
    SamplePins(GPIO38, GPIO39, GPIO34, GPIO35);
    if (GPIO38)
    {
        JsonObject GPIO38 = motorsObject.createNestedObject("GPIO38");
        GPIO38["speed"] = 0;
        GPIO38["channel"] = 1;
        numMotors++;
    }
    if (GPIO39)
    {
        JsonObject GPIO39 = motorsObject.createNestedObject("GPIO39");
        GPIO39["speed"] = 0;
        GPIO39["channel"] = 2;
        numMotors++;
    }
    if (GPIO34)
    {
        JsonObject GPIO34 = motorsObject.createNestedObject("GPIO34");
        GPIO34["speed"] = 0;
        GPIO34["channel"] = 3;
        numMotors++;
    }
    if (GPIO35)
    {
        JsonObject GPIO35 = motorsObject.createNestedObject("GPIO35");
        GPIO35["speed"] = 0;
        GPIO35["channel"] = 4;
        numMotors++;
    }
    DisplayString(0, 2, "Motors:" + String(numMotors));

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
    {
        Log("GET/");
        const String data = "<!DOCTYPE html><html lang='en'><head> <meta charset='UTF-8'> <meta http-equiv='X-UA-Compatible' content='IE=edge'> <meta name='viewport' content='width=device-width, initial-scale=1.0'> <title>Controls</title> <style>*{background-color: #f5f5f5; color: black;}</style></head><body> <input type='text' id='ip'> <div id='controls'></div><p id='messages'></p><script>var ip; var controls; var messages; /*var fade;*/ var motorsObject=null; var websocket; function Init(_ip=location.hostname){websocket=null; motorsObject=null; ip=document.querySelector('#ip'); ip.value=_ip; controls=document.querySelector('#controls'); controls.innerHTML=''; messages=document.querySelector('#messages'); messages.innerHTML=''; /*fade=document.querySelector('#fade');*/ ip.onchange=function(){Init(ip.value);}websocket=new WebSocket(`ws://${ip.value}/websocket`); /*websocket=new WebSocket(`ws://192.168.1.247/websocket`);*/ websocket.onopen=websocketOnOpen; websocket.onmessage=websocketOnMessage; websocket.onerror=websocketOnError;}function websocketOnOpen(){websocket.send(JSON.stringify({command: 'getMotors'}));}function websocketOnMessage(evt){var data=JSON.parse(evt.data); console.log(data); if (motorsObject==null){motorsObject={}; Object.keys(data.data).forEach(key=>{const value=data.data[key]; var p=document.createElement('p'); p.innerText=key; var input=document.createElement('input'); input.type='range'; input.min=0; input.max=255; input.value=value; /*input.oninput=function() //This sends messages too fast for the server to keep up. As a work around, for speed fading I could change the speed gradually on the server side or send over extra data telling the server if it should fade and at what speed.*/ input.onchange=function(){var data={}; Object.keys(motorsObject).forEach(key=>{const value=motorsObject[key]; data[key]=value.value;}); websocket.send(JSON.stringify({command: 'setMotors', data: data/*, fade: fade.checked*/}));}; motorsObject[key]=input; controls.appendChild(p); controls.appendChild(input);});}else{Object.keys(data.data).forEach(key=>{const value=data.data[key]; motorsObject[key].value=value;});}}function websocketOnError(){Init();}window.addEventListener('load', ()=>{Init();}); </script></body></html>";
        request->send(200, "text/html", data);
    });

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