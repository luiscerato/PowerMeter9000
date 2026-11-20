#pragma once

#include <Arduino.h>

enum class bootPhase : uint8_t { clock, settings, meter, screen, nand, history, wifi, ntp, web, wave, scope, battery,
    firstFast, firstState, firstEventPage, complete, count };

// Inicia la medicion de phase desde el reloj monotonico del ESP.
void bootTimingStart(bootPhase phase);
// Finaliza phase y conserva su primera marca de finalizacion.
void bootTimingEnd(bootPhase phase);
// Agrega a json los tiempos de arranque en microsegundos, sin consultar almacenamiento.
void bootTimingAppendJson(String& json);
