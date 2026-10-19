#pragma once


#include "Arduino.h"
#include "ESPAsyncWebServer.h"

class lcdUi;

extern  AsyncWebServer server;
extern  AsyncWebSocket remoteScreen;
extern  AsyncWebSocket debugWS;


typedef void (*WSeventFunctionCallback)(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);
typedef bool (*webServerJsonCallback)(char* buffer, size_t capacity);
typedef void (*webServerEndpointsCallback)(AsyncWebServer& server);

AsyncWebSocket* webServerGetDebugWS();
uint32_t webServerClientCount();

// Asocia la interfaz utilizada para recibir teclas desde la pantalla remota.
// ui es la instancia que procesara las teclas inyectadas.
void webServerSetUi(lcdUi& ui);

// Registra el generador JSON usado por la ruta /hardware.
// callback recibe buffer y capacity, y devuelve true si pudo escribir el JSON completo.
void webServerSetHardwareStatusJsonCallback(webServerJsonCallback callback);

// Registra el generador JSON usado por la ruta /app.
// callback recibe buffer y capacity, y devuelve true si pudo escribir el JSON completo.
void webServerSetApplicationStatusJsonCallback(webServerJsonCallback callback);

// Registra endpoints propios de la aplicacion antes de serveStatic y onNotFound.
// callback recibe el servidor para agregar server.on(), handlers o websockets propios.
void webServerSetApplicationEndpointsCallback(webServerEndpointsCallback callback);

void webServerBegin();
void webServerLoop();
void webServerBroadcastOled(const uint8_t* buffer, size_t size, const uint8_t* changedTiles, uint8_t changedCount, bool fullFrame);
void webServerPlayRtttl(const char* rtttl, bool loop = false);
void webServerStopRtttl();
void webServerQueueDebugMessage(const char* message);
void webServerBroadcastDebugConfig();


const char* getWifiAuthMode(uint8_t mode);
