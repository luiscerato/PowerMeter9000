#pragma once

#include <ESPAsyncWebServer.h>

class StorageManager;

// Registra los endpoints WebFS y asocia el almacenamiento NAND.
// server: servidor HTTP principal. storage: administrador de almacenamiento NAND.
void webFsHttpBegin(AsyncWebServer& server, StorageManager& storage);

// Sirve un recurso estatico desde la imagen WebFS activa en NAND.
// request: solicitud HTTP para el recurso o la pagina principal.
void webFsHttpHandleAsset(AsyncWebServerRequest* request);

// Envia la pagina de recuperacion compilada en el firmware.
// request: solicitud HTTP que debe recibir la pagina minima.
void webFsHttpSendRecovery(AsyncWebServerRequest* request);
