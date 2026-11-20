#pragma once

#include "ade9000/ADE9000.h"
#include <stdint.h>
#include <stddef.h>
#include <ESPAsyncWebServer.h>

// Carga la configuracion compartida del osciloscopio desde NVS; sin parametros.
void scopeStreamInit();

// Procesa muestras ADE, detecta trigger a resolucion completa y publica ventanas reducidas.
// samples: bloque intercalado ADE. count: muestras por canal. endUs: fin aproximado del bloque. frequencies: Hz R/S/T ya medidos.
void scopeStreamProcess(const WFBFixedDataRate_t* samples, uint16_t count, uint64_t endUs, const float* frequencies);

// Guarda configuracion despues de un periodo sin cambios; sin parametros.
void scopeStreamLoop();

// Recibe ajustes de clientes y distribuye la configuracion compartida.
// server: WebSocket del osciloscopio. client: cliente que genero el evento. type/arg/data/len: evento WebSocket.
void scopeStreamWebSocketEvents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type,
                                void* arg, uint8_t* data, size_t len);
