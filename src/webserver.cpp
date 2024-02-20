#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create a WebSocket object
AsyncWebSocket ws("/ws");
AsyncWebSocket remoteScreen("/screen");



void onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
	switch (type) {
	case WS_EVT_CONNECT:
		Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
		break;
	case WS_EVT_DISCONNECT:
		Serial.printf("WebSocket client #%u disconnected\n", client->id());
		break;
	case WS_EVT_DATA:
		//handleWebSocketMessage(arg, data, len);
		break;
	case WS_EVT_PONG:
	case WS_EVT_ERROR:
		break;
	}
}


void Init_WebServer()
{
	ws.onEvent(onEvent);
	server.addHandler(&ws);
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

	// server.onNotFound([](AsyncWebServerRequest* request) {
	//   request->send(404, "text/plain", "Page Not Found");

	//   Serial.printf("File not found\n");
	//   });

	//Start server
	server.begin();
}