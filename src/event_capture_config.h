#pragma once
#include <stdint.h>

constexpr int eventConfirmMaxMs = 2000;

// Limita value a 10..2000 ms y redondea hacia abajo en pasos de 10 ms.
constexpr uint16_t limitEventConfirmMs(int32_t value) {
    return uint16_t((value < 10 ? 10 : value > eventConfirmMaxMs ? eventConfirmMaxMs : value) / 10 * 10);
}

// Ajustes efectivos de los detectores FAST; se copian desde RAM, nunca desde NVS en adquisicion.
struct eventCaptureConfig {
    float overCurrent = 50;
    float currentHysteresis = 1;
    uint32_t dipConfirmMs = 50;
    uint32_t swellConfirmMs = 50;
    uint32_t overCurrentConfirmMs = 10;
    bool dipEnabled = true;
    bool swellEnabled = true;
    bool overCurrentEnabled = true;
};

// Copia en config los ajustes efectivos de dip/swell/sobrecorriente, protegidos entre tareas.
void meterGetEventCaptureConfig(eventCaptureConfig& config);
