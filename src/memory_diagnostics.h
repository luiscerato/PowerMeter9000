#pragma once

#include <stddef.h>

// Registra el diagnostico de reservas fallidas; llamar despues de iniciar el puerto serie.
void memoryDiagnosticsBegin();

// Persiste el ultimo fallo en el logger desde el loop, fuera del callback del heap.
void memoryDiagnosticsPoll();

// Informa memoria y su integridad; context identifica la operacion y size sus bytes previstos.
void memoryReportState(const char* context, size_t size);

// Comprueba espacio util y bloque contiguo; size incluye los datos y margin el margen restante.
bool memoryHasRoom(size_t size, size_t margin);
