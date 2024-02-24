#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "webserver.h"


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create a WebSocket object
AsyncWebSocket scopeWS("/ws");
AsyncWebSocket remoteScreen("/screen");

WSeventFunctionCallback meterEvents = nullptr, screenEvents = nullptr;

void webServerSetMeterEvents(WSeventFunctionCallback func)
{
	if (func != nullptr)
		meterEvents = func;
}

AsyncWebSocket* webServerGetMeterWS()
{
	return &scopeWS;
}

void Init_WebServer()
{
	scopeWS.onEvent(meterEvents);
	server.addHandler(&scopeWS);
	server.addHandler(&remoteScreen);

	// Web Server Root URL
	server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(SPIFFS, "/index.html", "text/html");
		});

	server.serveStatic("/", SPIFFS, "/").setCacheControl("max-age=31536000");

	// Para habilitar CORS. Al simular desde un servidor local no se puede llamar a la API. Con esto se soluciona.
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

	server.onNotFound([](AsyncWebServerRequest* request) {
		request->send(404, "text/plain", "Page Not Found");

		Serial.printf("File not found\n");
		});

	//Start server
	server.begin();
}