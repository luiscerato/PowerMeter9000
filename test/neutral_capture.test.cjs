const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'src/storage/grid_event_store.cpp'), 'utf8');

// Extrae la implementacion real de name y la hace evaluable en compilacion para la prueba.
function method(name) {
    const start = source.indexOf('void GridEventStore::' + name + '(');
    assert.ok(start >= 0);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth) { if (source[end] === '{') depth++; if (source[end] === '}') depth--; end++; }
    return source.slice(start, end).replace('void GridEventStore::', 'constexpr void ')
        .replace('memcmp(&lastNeutralSettings, &config, sizeof(config))', '!sameSettings(lastNeutralSettings, config)');
}

const states = fs.readFileSync(path.join(root, 'src/grid_state.h'), 'utf8')
    .replace('#pragma once', '').replace(/\binline\b/g, 'constexpr');
const harness = `
#include <algorithm>
#include <limits>
#include <cmath>
${states}
namespace GridEventFormat { enum class Subject : uint8_t { phaseR, phaseS, phaseT, neutral, grid }; }
struct meterValues { struct { float FastVrms = NAN; } neutral; bool neutralFastValid = true; };
struct TestSettings { float noVoltage = 0, hysteresis = 0; uint32_t voltageConfirmMs = 0, recoveryConfirmMs = 0; };
struct Candidate {
    bool active = false;
    GridEventFormat::Subject subject = GridEventFormat::Subject::phaseR;
    uint8_t previousState = 0, currentState = 0;
    uint32_t startedMs = 0;
    float previousVoltage = NAN, currentVoltage = NAN;
    TestSettings settings;
    uint8_t meter = 0;
};
// Compara solo los campos configurables; a/b son las configuraciones de neutro.
constexpr bool sameSettings(const neutralSettings& a, const neutralSettings& b) {
    return a.enabled == b.enabled && a.highVoltage == b.highVoltage && a.hysteresis == b.hysteresis &&
        a.confirmMs == b.confirmMs && a.recoveryMs == b.recoveryMs;
}
struct Store {
    static constexpr uint32_t captureArmMs = 150;
    Candidate candidates[4], last;
    phaseState neutralState;
    neutralSettings lastNeutralSettings;
    float neutralPreviousVoltage = NAN;
    struct { bool active = false, rootAssigned = false; } capture;
    struct { uint32_t discardedCandidates = 0; } storeMetrics;
    unsigned confirmed = 0, arms = 0, finishes = 0;
    // Simula apertura sin NAND; candidate/sampleMs identifican el inicio real.
    constexpr void armCapture(const Candidate& candidate, uint32_t sampleMs) { capture.active = true; ++arms; }
    // Simula confirmacion; previousStartedMs/sampleMs se conservan en el codigo real de almacenamiento.
    constexpr void confirmCandidate(const Candidate& candidate, uint32_t previousStartedMs, uint32_t sampleMs) {
        if (!capture.active) armCapture(candidate, sampleMs);
        capture.rootAssigned = true; last = candidate; ++confirmed;
    }
    // Simula liberacion de una captura cancelada; success no afecta la prueba de propiedad.
    constexpr void finishCapture(bool success) { capture.active = capture.rootAssigned = false; ++finishes; }
    // Contextualiza target usando config; las muestras se verifican por separado con el codec real.
    static constexpr void snapshotSettings(TestSettings& target, const gridSettings& config) { target.noVoltage = config.noVoltage; }
    // Marca el contexto copiado; target y meter representan el snapshot del primer cruce.
    static constexpr void snapshotMeter(uint8_t& target, const meterValues& meter) { target = 1; }
    ${method('discardCandidate')}
    ${method('observeNeutral')}
};
// Alimenta store con volts cada 10 ms entre from/to usando config.
constexpr void feed(Store& store, gridSettings& config, float volts, uint32_t from, uint32_t to) {
    meterValues meter; meter.neutral.FastVrms = volts;
    for (uint32_t now = from; now <= to; now += 10) store.observeNeutral(meter, config, now);
}
// Verifica subida, histeresis, recuperacion y valores del instante de cruce; sin parametros.
constexpr bool transitions() {
    Store store; gridSettings config; config.neutral.enabled = true;
    feed(store, config, 3, 0, 250);
    if (store.confirmed) return false;
    feed(store, config, 30, 260, 350);
    if (store.confirmed) return false;
    feed(store, config, 30, 360, 360);
    if (store.confirmed != 1 || store.last.subject != GridEventFormat::Subject::neutral ||
        store.last.startedMs != 260 || store.last.previousVoltage != 3 || store.last.currentVoltage != 30 ||
        store.last.settings.noVoltage != 25 || store.last.settings.voltageConfirmMs != 100) return false;
    feed(store, config, 23, 370, 600);
    feed(store, config, 22, 610, 850);
    if (store.confirmed != 1) return false;
    feed(store, config, 22, 860, 860);
    return store.confirmed == 2 && store.last.currentState == uint8_t(phaseCondition::normal) &&
        store.last.startedMs == 610 && store.last.currentVoltage == 22;
}
// Verifica que pulsos cortos, NaN y huecos no confirmen ni retengan buffers; sin parametros.
constexpr bool gapsAndCancel() {
    Store store; gridSettings config; config.neutral.enabled = true; config.neutral.confirmMs = 400;
    feed(store, config, 3, 0, 250);
    feed(store, config, 30, 260, 450);
    if (!store.capture.active || store.capture.rootAssigned) return false;
    feed(store, config, NAN, 460, 460);
    if (store.capture.active || store.confirmed || store.finishes != 1) return false;
    feed(store, config, 30, 470, 860);
    feed(store, config, 30, 1200, 1590);
    if (store.confirmed) return false;
    feed(store, config, 30, 1600, 1600);
    return store.confirmed == 1 && store.last.startedMs == 1200;
}
// Verifica silencio tras alarma, opcion desactivada y aislamiento de otros candidatos; sin parametros.
constexpr bool disabledAndMissing() {
    Store store; gridSettings config;
    feed(store, config, 230, 0, 2000);
    if (store.confirmed) return false;
    config.neutral.enabled = true;
    feed(store, config, 30, 2010, 2110);
    if (store.confirmed != 1) return false;
    feed(store, config, NAN, 2120, 3000);
    if (store.confirmed != 1 || store.neutralState.state != phaseCondition::overvoltage) return false;
    Store waiting; config.neutral.confirmMs = 400;
    waiting.capture.active = true; waiting.candidates[0].active = true;
    feed(waiting, config, 30, 0, 200);
    feed(waiting, config, NAN, 210, 210);
    if (!waiting.capture.active || !waiting.candidates[0].active || waiting.finishes != 0) return false;
    Store stale; meterValues meter; meter.neutral.FastVrms = 30; meter.neutralFastValid = false;
    for (uint32_t now = 0; now <= 1000; now += 10) stale.observeNeutral(meter, config, now);
    return stale.confirmed == 0 && !stale.capture.active;
}
static_assert(transitions(), "Neutro: disparo, valores, histeresis y recuperacion");
static_assert(gapsAndCancel(), "Neutro: ausencias cancelan y liberan la captura no confirmada");
static_assert(disabledAndMissing(), "Neutro: sin muestra no hay recuperacion y no afecta otras fases");
`;
const output = path.join(root, '.pio/build/neutral_capture_test.cpp');
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: observador real de neutro, subida/retorno, histéresis, NaN, huecos y cancelación sin liberar otros candidatos.');
