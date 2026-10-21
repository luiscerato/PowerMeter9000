#pragma once

#include "grid_state.h"

// Restaura la referencia NVS de R/S/T y red antes de iniciar las mediciones.
void gridEventsInit();

// Procesa estados confirmados y genera transiciones; sampleMs es el instante monotonico de lectura.
void gridEventsUpdate(const phaseState& r, const phaseState& s, const phaseState& t,
	gridCondition overall, float vr, float vs, float vt, uint32_t sampleMs);

// Indica si ya termino la guarda de arranque y se pueden capturar cambios reales de red.
bool gridEventsReadyForStorage();

// Devuelve el estado persistido antes del arranque; slot identifica R, S, T o red.
uint8_t gridEventsStartupState(uint8_t slot);

// Guarda como maximo un estado pendiente en NVS por pasada; llamar solo desde el loop principal.
void gridEventsPersistLoop();
