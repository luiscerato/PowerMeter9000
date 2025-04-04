
#include "Arduino.h"
#include "ESPAsyncWebServer.h"

extern  AsyncWebServer server;
extern  AsyncWebSocket scopeWS;
extern  AsyncWebSocket acEventsWS;
extern  AsyncWebSocket remoteScreen;


typedef void (*WSeventFunctionCallback)(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);

void webServerSetMeterEvents(WSeventFunctionCallback func);
AsyncWebSocket* webServerGetMeterWS();



void webServerSetAcEvents(WSeventFunctionCallback func);

AsyncWebSocket* webServerGetAcWS();

void Init_WebServer();


const char* getWifiAuthMode(uint8_t mode);
int32_t scanWifiGetJson(String& List);
void getStatus(String& str);