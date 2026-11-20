const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const source = fs.readFileSync('src/scope_stream.cpp', 'utf8');
const start = source.indexOf('void scopeWrite12(');
const end = source.indexOf('// Convierte un valor ADE', start);
assert.ok(start >= 0 && end > start);
const helpers = source.slice(start, end).replace('void scopeWrite12(', 'constexpr void scopeWrite12(')
    .replace('int16_t scopeRead12(', 'constexpr int16_t scopeRead12(');
const scaleStart = source.indexOf('int16_t scopeScaleSample(');
const scaleEnd = source.indexOf('// Ajusta factor', scaleStart);
const triggerStart = source.indexOf('bool scopeFindTrigger(');
const triggerEnd = source.indexOf('// Forma cuatro tramos', triggerStart);
assert.ok(scaleStart >= 0 && scaleEnd > scaleStart && triggerStart >= 0 && triggerEnd > triggerStart);
const scale = source.slice(scaleStart, scaleEnd).replace('int16_t scopeScaleSample(', 'constexpr int16_t scopeScaleSample(');
const trigger = source.slice(triggerStart, triggerEnd).replace('bool scopeFindTrigger(', 'constexpr bool scopeFindTrigger(');
const selectStart = source.indexOf('uint32_t scopeFindFrameTrigger(');
const selectEnd = source.indexOf('// Encola una ventana', selectStart);
assert.ok(selectStart >= 0 && selectEnd > selectStart);
const select = source.slice(selectStart, selectEnd).replace('uint32_t scopeFindFrameTrigger(', 'constexpr uint32_t scopeFindFrameTrigger(');
const viewerStart = source.indexOf('bool scopeViewerExpired(');
const viewerEnd = source.indexOf('// Registra actividad', viewerStart);
assert.ok(viewerStart >= 0 && viewerEnd > viewerStart);
const viewer = source.slice(viewerStart, viewerEnd).replace('bool scopeViewerExpired(', 'constexpr bool scopeViewerExpired(');
const code = `#include <stdint.h>
#include <stddef.h>
constexpr uint16_t scopeMaxPoints = 512;
constexpr uint8_t scopeTriggerBit = 0x80;
constexpr uint32_t scopeViewerTimeoutMs = 30000;
${viewer}
static_assert(!scopeViewerExpired(100, 30099), "Do not expire before 30 seconds");
static_assert(scopeViewerExpired(100, 30100), "Expire abandoned viewers at 30 seconds");
static_assert(!scopeViewerExpired(UINT32_MAX - 100, 100), "Keep active viewers across millis wrap");
static_assert(scopeViewerExpired(UINT32_MAX - 30000, 0), "Expire old viewers across millis wrap");
${helpers}
${scale}

struct triggerState {
    int16_t scopePrevious = 0;
    uint8_t scopePreviousValid = 0, scopeSlopeCount = 0;
    ${trigger}
};
struct captureState {
    uint8_t scopeFlagsRing[scopeMaxPoints] = {};
    uint16_t scopeRingCount = 0, scopeTargetPoints = 200;
    uint32_t scopePointCount = 0, scopeLastSentTrigger = UINT32_MAX;
    ${select}
};

// Comprueba todos los valores signed y que cada escritura conserve las muestras vecinas; sin parametros.
constexpr bool verifyPacking() {
    uint8_t bytes[6] = {};
    scopeWrite12(bytes, 0, -1234);
    scopeWrite12(bytes, 3, 567);
    for (int value = -2048; value <= 2047; ++value) {
        scopeWrite12(bytes, 1, value);
        scopeWrite12(bytes, 2, -1 - value);
        if (scopeRead12(bytes, 0) != -1234 || scopeRead12(bytes, 1) != value ||
            scopeRead12(bytes, 2) != -1 - value || scopeRead12(bytes, 3) != 567) return false;
    }
    return true;
}

// Verifica bytes conocidos del protocolo original, incluyendo los extremos y el cambio de signo; sin parametros.
constexpr bool verifyWireBytes() {
    uint8_t bytes[6] = {};
    scopeWrite12(bytes, 0, -2048); scopeWrite12(bytes, 1, 2047);
    scopeWrite12(bytes, 2, -1); scopeWrite12(bytes, 3, 1);
    return bytes[0] == 0x00 && bytes[1] == 0xf8 && bytes[2] == 0x7f &&
        bytes[3] == 0xff && bytes[4] == 0x1f && bytes[5] == 0x00;
}
static_assert(verifyPacking(), "12-bit values or adjacent samples changed");
static_assert(verifyWireBytes(), "Wire format changed");

// Comprueba truncado hacia cero y saturacion antes de convertir, incluidos extremos int32; sin parametros.
constexpr bool verifyScaling() {
    bool clipped = false;
    for (int raw = -2048; raw <= 2047; ++raw) {
        if (scopeScaleSample(raw, 1.0f, clipped) != raw || clipped) return false;
    }
    if (scopeScaleSample(29, 0.1f, clipped) != 2 || clipped) return false;
    if (scopeScaleSample(-29, 0.1f, clipped) != -2 || clipped) return false;
    if (scopeScaleSample(2, 0.25f, clipped) != 0 || clipped) return false;
    if (scopeScaleSample(-2, 0.25f, clipped) != 0 || clipped) return false;
    if (scopeScaleSample(INT32_MAX, 1.0f, clipped) != 2047 || !clipped) return false;
    if (scopeScaleSample(INT32_MIN, 1.0f, clipped) != -2048 || !clipped) return false;
    return true;
}

// Verifica ambos flancos, la primera muestra y el minimo de pendiente continua antes del cruce; sin parametros.
constexpr bool verifyTriggers() {
    triggerState rising, falling, shortSlope, interruptedSlope;
    for (int value = -6; value <= 6; ++value) {
        if (rising.scopeFindTrigger(value, 0, true) != (value == 0)) return false;
        if (falling.scopeFindTrigger(-value, 0, false) != (value == 0)) return false;
    }
    for (int value = -2; value <= 2; ++value) {
        if (shortSlope.scopeFindTrigger(value, 0, true)) return false;
    }
    const int16_t values[] = {-5, -4, -3, -2, -2, -1, 0};
    for (int i = 0; i < 7; ++i) {
        if (interruptedSlope.scopeFindTrigger(values[i], 0, true)) return false;
    }
    return true;
}

// Verifica contexto, cruces nuevos aun incompletos, descarte de marcas usadas y wrap del contador; sin parametros.
constexpr bool verifyFrameSelection() {
    captureState state;
    state.scopePointCount = state.scopeRingCount = 300;
    state.scopeFlagsRing[280] = scopeTriggerBit;
    state.scopeFlagsRing[150] = scopeTriggerBit;
    if (state.scopeFindFrameTrigger() != 150) return false;
    state.scopeLastSentTrigger = 150;
    if (state.scopeFindFrameTrigger() != UINT32_MAX) return false;
    state.scopePointCount = state.scopeRingCount = 400;
    if (state.scopeFindFrameTrigger() != 280) return false;
    captureState early;
    early.scopePointCount = early.scopeRingCount = 200;
    early.scopeFlagsRing[50] = scopeTriggerBit;
    early.scopeFlagsRing[120] = 0x04;
    if (early.scopeFindFrameTrigger() != UINT32_MAX) return false;
    captureState wrapped;
    wrapped.scopePointCount = 60; wrapped.scopeRingCount = scopeMaxPoints;
    const uint32_t mark = UINT32_MAX - 90;
    wrapped.scopeFlagsRing[mark % scopeMaxPoints] = scopeTriggerBit;
    return wrapped.scopeFindFrameTrigger() == mark;
}

// Simula cruces de 50 Hz a 8 kHz y envios cada 125 ms; decimation selecciona ventanas de 50, 100 o 1000 ms.
constexpr bool verifyPeriodicCapture(uint16_t decimation, uint16_t points = 200) {
    captureState state;
    state.scopeTargetPoints = points;
    triggerState detector;
    uint16_t inGroup = 0, captured = 0;
    uint8_t flags = 0;
    uint32_t lastSend = 0;
    for (uint32_t raw = 0; raw < 20000; ++raw) {
        if (detector.scopeFindTrigger(int16_t(int(raw % 160) - 80), 0, true)) flags |= scopeTriggerBit;
        if (++inGroup == decimation) {
            state.scopeFlagsRing[state.scopePointCount % scopeMaxPoints] = flags | 0x04;
            ++state.scopePointCount;
            if (state.scopeRingCount < scopeMaxPoints) ++state.scopeRingCount;
            inGroup = 0; flags = 0;
        }
        const uint32_t now = raw + 1;
        if (now % 128 || now - lastSend < 1000 || state.scopeRingCount < state.scopeTargetPoints) continue;
        const uint32_t mark = state.scopeFindFrameTrigger();
        if (now > uint32_t(state.scopeTargetPoints) * decimation + 160 && mark == UINT32_MAX) return false;
        if (mark != UINT32_MAX) {
            const uint32_t age = state.scopePointCount - mark;
            if (age < points - points / 2 || age > state.scopeRingCount - points / 2) return false;
            state.scopeLastSentTrigger = mark;
            ++captured;
        }
        lastSend = now;
    }
    return captured >= 2;
}
static_assert(verifyScaling(), "Truncation or clipping failed");
static_assert(verifyTriggers(), "Trigger slope or crossing changed");
static_assert(verifyFrameSelection(), "Trigger needs a complete window across ring wrap");
static_assert(verifyPeriodicCapture(2), "50 ms windows lost periodic triggers");
static_assert(verifyPeriodicCapture(4), "100 ms windows lost periodic triggers");
static_assert(verifyPeriodicCapture(40), "1000 ms windows lost periodic triggers");
static_assert(verifyPeriodicCapture(9, 445), "500 ms high-resolution windows lost triggers");
static_assert(verifyPeriodicCapture(16, 500), "1000 ms high-resolution windows lost triggers");
`;
const output = '.pio/build/scope-packing-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: protocolo, truncado, saturacion, flancos y capturas completas de 50/100/1000 ms con trigger periodico.');
