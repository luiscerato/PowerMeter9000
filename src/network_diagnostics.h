#pragma once

#include <Arduino.h>

// Habilita la prueba HTTP local cuando el servidor ya esta iniciado; no recibe parametros.
void networkDiagnosticsStart();

// Resume los avisos acumulados de AsyncTCP y programa las pruebas pendientes; sin parametros.
void networkDiagnosticsPoll();

// Registra un poll coalescido y la profundidad de cola observada.
extern "C" void meterAsyncTcpPollCoalesced(uint8_t queueDepth);

// Registra un poll descartado y la profundidad de cola observada.
extern "C" void meterAsyncTcpPollDiscarded(uint8_t queueDepth);

// Registra un error TCP reportado por lwIP; errorCode es el codigo de error firmado.
extern "C" void meterAsyncTcpError(int8_t errorCode);
