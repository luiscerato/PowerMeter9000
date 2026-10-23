#pragma once
#include <stdint.h>

constexpr uint8_t eventCapturePages = 5;
constexpr uint16_t eventPreSamples = 113;
constexpr uint16_t eventContinuationSamples = 118;
constexpr uint32_t eventPostMs = 1000;
// Valida samples y coveredMs previos; tolera una muestra y 20 ms de fase/jitter del muestreo.
constexpr bool eventPreHistoryComplete(uint16_t samples, uint32_t coveredMs) {
    return samples + 1U >= eventPreSamples && coveredMs + 20U >= eventPreSamples * 10U;
}
// Reserva el retorno y dos muestras de margen; sin parametros, depende del formato FAST fijo.
constexpr uint32_t eventPointLimitMs() {
    return (eventCapturePages - 1U) * eventContinuationSamples * 10U - eventPostMs - 20U;
}
