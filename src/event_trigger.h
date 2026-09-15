#pragma once
#include <stdint.h>

// Estima el inicio respecto del frame actual: part son fragmentos ya copiados,
// elapsedUs/precise refinan la ultima frontera y lookbackUs descuenta la confirmacion.
constexpr int64_t estimateWaveTriggerOffset(uint32_t part, uint32_t elapsedUs, bool precise, uint64_t lookbackUs)
{
    return int64_t(part * 128) + (precise && elapsedUs < 16000 ? elapsedUs / 125 : 0) - int64_t(lookbackUs / 125);
}

// Convierte una posicion firmada en indice; sample negativo indica inicio fuera de la captura.
constexpr int32_t eventTriggerIndex(int64_t sample)
{
    return sample >= 0 && sample <= INT32_MAX ? int32_t(sample) : -1;
}
