
#include "Arduino.h"
#include "ESPAsyncWebServer.h"

extern  AsyncWebServer server;
extern  AsyncWebSocket scopeWS;
extern  AsyncWebSocket remoteScreen;


typedef void (*WSeventFunctionCallback)(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

void webServerSetMeterEvents(WSeventFunctionCallback func);
AsyncWebSocket* webServerGetMeterWS();

void Init_WebServer();