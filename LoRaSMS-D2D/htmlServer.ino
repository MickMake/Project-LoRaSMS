#define ESP_TARGET

// This define allows us to use this code on non-ESP based boards.
#ifdef ESP_TARGET

#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
ESP8266WebServer server ( 80 );
#else
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiAP.h>
WebServer server ( 80 );
#endif

#include "LoRaSMS-D2D.h"

// Create aREST instance - not using it yet!
//aREST rest = aREST();


const char *ssid = "ESP32ap";
const char *password = "12345678";

const char strHeader[]	= "<html><head><meta http-equiv='refresh' content='60'/><title>LoRa SMS</title></head><body>";
const char strCSS[]	= "body {font-family:\"Helvetica Neue\";font-size:20px;font-weight:normal;}\
section {max-width:450px;margin:50px auto;}\
section div {max-width:255px;word-wrap:break-word;margin-bottom:12px;line-height:24px;}\
section div:after {content:\"\";display:table;clear:both;}\
.clear {clear:both;}\
.TX {position:relative;padding:10px 20px;color:white;background:#0B93F6;border-radius:25px;float:right;}\
.TX:before {content:\"\";position:absolute;z-index:-1;bottom:-2px;right:-7px;height:20px;border-right:20px solid #0B93F6;border-bottom-left-radius:16px 14px;-webkit-transform:translate(0, -2px);}\
.TX:after {content:\"\";position:absolute;z-index:1;bottom:-2px;right:-56px;width:26px;height:20px;background:white;border-bottom-left-radius:10px;-webkit-transform:translate(-30px, -2px);}\
.RX {position:relative;padding:10px 20px;background:#E5E5EA;border-radius:25px;color:black;float:left;}\
.RX:before {content:\"\";position:absolute;z-index:2;bottom:-2px;left:-7px;height:20px;border-left:20px solid #E5E5EA;border-bottom-right-radius:16px 14px;-webkit-transform:translate(0, -2px);}\
.RX:after {content:\"\";position:absolute;z-index:3;bottom:-2px;left:4px;width:26px;height:20px;background:white;border-bottom-right-radius:10px;-webkit-transform:translate(-30px, -2px);}\
";
const char strTitle[]	= "<h1>Send a LoRa SMS</h1>";
const char strForm[]	= "<form action=\"/\" method=\"post\"><p>Text:<input type=\"text\" name=\"sms\"><input type=\"submit\" value=\"send\"></p></form>";
const char strFooter[]	= "<link rel=\"stylesheet\" href=\"/style.css\" type=\"text/css\" /></body></html>";
const char strSep[]	= "<div class=\"clear\"></div>";
const char strTxPre[]	= "<div class=\"TX\"><p>";
const char strRxPre[]	= "<div class=\"RX\"><p>";
const char strPost[]	= "</p></div>";


void handleRoot(void)
{
	char html[2500] = {0x00};
	char uptime[32];
	int sec = millis() / 1000;
	int min = sec / 60;
	int hr = min / 60;

	// Print out the web page with historic messages, regardless of state.
	strcat(html, strHeader);
	strcat(html, "<h1>"); strcat(html, ssid); strcat(html, "</h1>");
	strcat(html, "<style>");
	strcat(html, strCSS);
	strcat(html, "</style>");
	//strcat(html, strTitle);
	//snprintf(uptime, 32, "<p>Uptime: %02d:%02d:%02d</p>", hr, min % 60, sec % 60);
	//strcat(html, uptime);
	strcat(html, "<section>");

	PRINT("NM:"); PRINTLN(numMessages());
	// Loop through the messages.
	for(uint8_t msgIndex = 0x00; (msgIndex < numMessages()); msgIndex++)
	{
		PRINT("I:"); PRINTLN(msgIndex);
		uint8_t printStr[SMS_LENGTH];
		uint8_t Type;
        	Type = getMessage(msgIndex, printStr);

		if (Type != typeACK)
		{
			strcat(html, strSep);
			if (Type == typeTX)
				strcat(html, strTxPre);
			else if (Type == typeRX)
				strcat(html, strRxPre);
			strcat(html, (char *)printStr);
			strcat(html, strPost);
		}
	}

	strcat(html, strSep);
	strcat(html, strForm);
	strcat(html, "</section>");
	strcat(html, strFooter);


	// HTML POST processing.
	if (server.method() == HTTP_GET)
	{
		PRINTLN("# GET /");
	}
	else if (server.method() == HTTP_POST)
	{
		PRINTLN("# POST /");
		if (server.hasArg("sms"))
		{
			PRINT("SMS:");
			PRINTLN(server.arg("sms"));
			sendMessage((uint8_t *)(server.arg("sms").c_str()), server.arg("sms").length());
		}
	}
	else if (server.method() == HTTP_POST)
	{
		PRINTLN("# ?");
	}

	server.send(200, "text/html", html);
}


// Throw a 404.
void handleNotFound(void)
{
	  String message = "File Not Found\n\n";
	  message += "URI: ";
	  message += server.uri();
	  message += "\nMethod: ";
	  message += (server.method() == HTTP_GET) ? "GET" : "POST";
	  message += "\nArguments: ";
	  message += server.args();
	  message += "\n";

	  for(uint8_t i = 0; i < server.args(); i++)
  		  message += " " + server.argName(i) + ": " + server.arg(i) + "\n";

	  server.send(404, "text/plain", message);
}


// Handle all the Wiffeee setup. I said, Wiffeeee.
void setup_wifi(void)
{
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);
    IPAddress myIP = WiFi.softAPIP();
    PRINT("AP IP address: ");
    PRINTLN(myIP);
    WiFi.softAPsetHostname(ssid);
    WiFi.setHostname(ssid);

	  if (MDNS.begin("esp32"))
	  {
		    PRINTLN("MDNS responder started");
	  }

	  server.on("/", handleRoot);
	  server.on("/style.css", []() {PRINTLN("# GET /style.css"); server.sendHeader("Cache-Control","max-age=2592000, public"); server.send(200, "text/css", strCSS);});
	  server.on("/inline", []() {server.send(200, "text/plain", "this works as well");});
	  server.onNotFound(handleNotFound);

	Serial.setDebugOutput(true);
	PRINT("ESP32 SDK: ");
	PRINTLN(ESP.getSdkVersion());

	server.begin();
	PRINTLN("HTTP server started");
}


void wifi_loop(void)
{
	server.handleClient();
}

#endif

