#pragma once
#include <stdint.h>
#include <cmath>

struct fastEventDetector {
    bool active = false, pending = false, recovering = false;
    uint32_t startedMs = 0, lastMs = 0, endedMs = 0;
    float extreme = NAN, lastLimit = NAN;
    uint16_t lastConfirmMs = 0;
    float lastHysteresis = 0;
    uint16_t lastRecoveryMs = 0;

    // Evalua value/limit en nowMs; below elige caida y enabled habilita la deteccion.
    // confirmMs filtra el inicio; hysteresis y recoveryMs definen el retorno estable.
    void update(float value, float limit, uint16_t confirmMs, bool below, bool enabled, uint32_t nowMs,
                float hysteresis = 0, uint16_t recoveryMs = 0) {
        if (!enabled || limit != lastLimit || confirmMs != lastConfirmMs || hysteresis != lastHysteresis || recoveryMs != lastRecoveryMs) {
            active = pending = recovering = false;
            lastLimit = limit;
            lastConfirmMs = confirmMs;
            lastHysteresis = hysteresis;
            lastRecoveryMs = recoveryMs;
        }
        if (!enabled) return;
        if (!std::isfinite(value) || value < 0) {
            if (!active) pending = false;
            recovering = false;
            return;
        }
        if (!active && uint32_t(nowMs - lastMs) > 30U) pending = false;
        lastMs = nowMs;
        const float release = below ? limit + hysteresis : limit - hysteresis;
        const bool abnormal = below ? value < (pending ? release : limit) : value > (pending ? release : limit);
        if (!abnormal) {
            if (!active) { pending = false; return; }
            if (!recovering) { recovering = true; endedMs = nowMs; }
            if (uint32_t(nowMs - endedMs) >= recoveryMs) active = pending = recovering = false;
            return;
        }
        recovering = false;
        if (!pending) {
            pending = true;
            startedMs = nowMs;
            extreme = value;
        } else if (below ? value < extreme : value > extreme) extreme = value;
        if (uint32_t(nowMs - startedMs) >= confirmMs) active = true;
    }
};
