#pragma once
#include <Arduino.h>

enum class meterTimingPart : uint8_t {
    wait, wakeLate, cycle, irq0, irqDelay, wave, waveSpi, waveScale, wavePack,
    waveStore, waveWeb, fast, slowRms, thd, energy, calibration, bl, commit,
    events, commands, irq1, grid, gridNvs, count
};

// Acumula tiempo de pared sin imprimir; part identifica el bloque y elapsedUs su duracion.
void meterTimingRecord(meterTimingPart part, uint32_t elapsedUs);
// Copia contadores y hasta ocho operaciones lentas a json; solo formatea al consultar por HTTP.
void meterTimingGetJson(String& json);

class meterTimingScope {
    meterTimingPart part;
    uint32_t startedUs;
public:
    // Inicia la medicion de part en la pila, sin reservar memoria dinamica.
    explicit meterTimingScope(meterTimingPart part) : part(part), startedUs(micros()) {}
    // Acumula al salir del bloque; sin parametros, incluye esperas y preempciones.
    ~meterTimingScope() { meterTimingRecord(part, uint32_t(micros() - startedUs)); }
    meterTimingScope(const meterTimingScope&) = delete;
    meterTimingScope& operator=(const meterTimingScope&) = delete;
};
