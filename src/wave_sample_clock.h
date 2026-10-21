#pragma once
#include <stdint.h>

// Sigue mitades de 128 muestras a 8 kHz sin confundir latencia de servicio con muestras perdidas.
struct waveSampleClock {
    uint64_t endUs = 0, lastObservedUs = 0;
    uint8_t lastHalf = 0;
    bool ready = false, gap = false;

    // Incorpora observedEndUs y half (0/1); false indica repeticion, gap indica mitades perdidas.
    bool update(uint64_t observedEndUs, uint8_t half) {
        gap = false;
        if (!ready) {
            endUs = lastObservedUs = observedEndUs; lastHalf = half; ready = true; return true;
        }
        if (observedEndUs <= lastObservedUs) return false;
        const uint64_t observedDelta = observedEndUs - lastObservedUs;
        if (half == lastHalf && observedDelta < 24000) return false;
        const uint64_t elapsed = observedEndUs > endUs ? observedEndUs - endUs : 0;
        uint32_t steps = half != lastHalf ? 1U + uint32_t(elapsed / 32000) * 2U :
            uint32_t((elapsed + 16000) / 32000) * 2U;
        if (!steps) steps = 2;
        gap = steps > 1;
        const uint64_t expected = endUs + uint64_t(steps) * 16000;
        // Corrige deriva lentamente; nunca mueve un bloque completo por unos ms de demora de la tarea.
        int64_t correction = (int64_t(observedEndUs) - int64_t(expected)) / 64;
        if (correction > 8) correction = 8;
        if (correction < -8) correction = -8;
        endUs = gap ? observedEndUs : uint64_t(int64_t(expected) + correction);
        lastObservedUs = observedEndUs; lastHalf = half;
        return true;
    }
};
