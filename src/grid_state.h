#pragma once
#include <stdint.h>

enum class phaseCondition : uint8_t { unknown, normal, undervoltage, overvoltage, phaseLoss };
enum class gridCondition : uint8_t { unknown, normal, abnormal, outage };

struct phaseLimits {
    float lowVoltage = 185;
    float highVoltage = 250;
};

struct gridSettings {
    phaseLimits phases[3];
    float noVoltage = 20;
    float hysteresis = 5;
    uint32_t lossConfirmMs = 250;
    uint32_t voltageConfirmMs = 5000;
    uint32_t recoveryConfirmMs = 250;
};

struct phaseState {
    phaseCondition state = phaseCondition::unknown;
    phaseCondition pending = phaseCondition::unknown;
    uint32_t pendingSinceMs = 0;
    uint32_t changedAtMs = 0;
    uint32_t lastSampleMs = 0;
    bool hasSample = false;
};

// Valida limites y tiempos de config antes de persistirlos o aplicarlos.
inline bool validGridSettings(const gridSettings& config) {
    if (!(config.noVoltage >= 1 && config.noVoltage <= 100) ||
        !(config.hysteresis >= 0 && config.hysteresis <= 50)) return false;
    for (const auto& phase : config.phases) {
        if (!(phase.lowVoltage > config.noVoltage + config.hysteresis &&
              phase.highVoltage > phase.lowVoltage + 2 * config.hysteresis &&
              phase.highVoltage <= 500)) return false;
    }
    return config.lossConfirmMs >= 20 && config.lossConfirmMs <= 60000 &&
        config.voltageConfirmMs >= 20 && config.voltageConfirmMs <= 600000 &&
        config.recoveryConfirmMs >= 20 && config.recoveryConfirmMs <= 60000;
}

// Devuelve el nombre estable del estado confirmado state para JSON.
inline const char* phaseStateName(phaseCondition state) {
    switch (state) {
    case phaseCondition::normal: return "normal";
    case phaseCondition::undervoltage: return "undervoltage";
    case phaseCondition::overvoltage: return "overvoltage";
    case phaseCondition::phaseLoss: return "phase_loss";
    default: return "unknown";
    }
}

// Devuelve el nombre estable del estado general state para JSON.
inline const char* gridStateName(gridCondition state) {
    switch (state) {
    case gridCondition::normal: return "normal";
    case gridCondition::abnormal: return "abnormal";
    case gridCondition::outage: return "outage";
    default: return "unknown";
    }
}

// Clasifica volts usando limits y settings; current determina la banda de histeresis.
inline phaseCondition classifyPhase(float volts, phaseCondition current,
                                      const phaseLimits& limits, const gridSettings& settings) {
    if (!(volts >= 0) || volts > 1000000) return phaseCondition::unknown;
    if (volts < settings.noVoltage ||
        (current == phaseCondition::phaseLoss && volts <= settings.noVoltage + settings.hysteresis))
        return phaseCondition::phaseLoss;
    if (volts < limits.lowVoltage ||
        (current == phaseCondition::undervoltage && volts <= limits.lowVoltage + settings.hysteresis))
        return phaseCondition::undervoltage;
    if (volts > limits.highVoltage ||
        (current == phaseCondition::overvoltage && volts >= limits.highVoltage - settings.hysteresis))
        return phaseCondition::overvoltage;
    return phaseCondition::normal;
}

// Procesa una muestra nueva volts en nowMs; modifica phase y devuelve si confirmo una transicion.
inline bool updatePhaseState(phaseState& phase, float volts, uint32_t nowMs,
                                const phaseLimits& limits, const gridSettings& settings) {
    const auto next = classifyPhase(volts, phase.state, limits, settings);
    // No contar muestras ausentes como confirmacion continua de una condicion.
    if (!phase.hasSample || uint32_t(nowMs - phase.lastSampleMs) > 50 || next != phase.pending) {
        phase.pending = next;
        phase.pendingSinceMs = nowMs;
    }
    phase.lastSampleMs = nowMs;
    phase.hasSample = true;
    if (next == phaseCondition::unknown || next == phase.state) return false;
    const uint32_t delayMs = next == phaseCondition::phaseLoss ? settings.lossConfirmMs :
        next == phaseCondition::normal ? settings.recoveryConfirmMs : settings.voltageConfirmMs;
    if (uint32_t(nowMs - phase.pendingSinceMs) < delayMs) return false;
    phase.state = next;
    phase.changedAtMs = phase.pendingSinceMs;
    return true;
}

// Deriva el estado de red de las tres fases confirmadas r, s y t, sin otra demora.
constexpr gridCondition deriveGridState(phaseCondition r, phaseCondition s, phaseCondition t) {
    return r == phaseCondition::unknown || s == phaseCondition::unknown || t == phaseCondition::unknown
        ? gridCondition::unknown
        : r == phaseCondition::phaseLoss && s == r && t == r ? gridCondition::outage
        : r == phaseCondition::normal && s == r && t == r ? gridCondition::normal
        : gridCondition::abnormal;
}
