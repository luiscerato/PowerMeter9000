#pragma once
class AsyncWebServer;
// Registra config/status de eventos en server, usando formularios y respuestas JSON acotadas.
void eventsHttpBegin(AsyncWebServer& server);
