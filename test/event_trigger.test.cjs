const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const meter = fs.readFileSync(path.join(root, 'src/meter.cpp'), 'utf8');
const meterHeader = fs.readFileSync(path.join(root, 'src/meter.h'), 'utf8');
const store = fs.readFileSync(path.join(root, 'src/event_store.cpp'), 'utf8');

// Extrae el metodo real de marcado para probar la misma aritmetica del firmware; sin parametros.
function markMethod() {
    const start = meterHeader.indexOf('void markTriggerSample(');
    assert.ok(start >= 0);
    const end = meterHeader.indexOf('};', start);
    return meterHeader.slice(start, end + 1).replace('void markTriggerSample', 'constexpr void markTriggerSample');
}

assert.match(meter, /meterWaveTriggerOffset\(lookbackUs\)/);
assert.match(meter, /EventsData\.markTriggerSample\(lookbackUs\)/);
assert.match(store, /eventTriggerIndex\(int64_t\(entry\.wavePreBlockCount\) \* waveSamplesPerBlock \+ triggerOffset\)/);
assert.match(store, /entry\.fastRmsTriggerSample >= 0 \? String\(entry\.fastRmsTriggerSample\) : String\("null"\)/);
const harness = `
#include "event_trigger.h"
struct FastCapture {
    uint32_t sampleCount;
    int32_t triggerSample = -1;
    ${markMethod()}
};
// Copia la frontera del prebuffer y descuenta delayUs; count representa muestras previas validas.
constexpr int32_t fastIndex(uint32_t count, uint64_t delayUs) {
    FastCapture capture{count};
    capture.markTriggerSample(delayUs);
    return capture.triggerSample;
}
static_assert(fastIndex(100, 0) == 100, "Manual no resta confirmacion");
static_assert(fastIndex(100, 50000) == 95, "Cinco semiciclos a 100 Hz");
static_assert(fastIndex(3, 50000) == -1, "Inicio fuera del prebuffer RMS");
static_assert(fastIndex(100, 3600000000ULL) == -1, "Cierre aislado antiguo no desborda");
static_assert(estimateWaveTriggerOffset(3, 200000, false, 0) == 384, "Fallback conserva tres partes");
static_assert(estimateWaveTriggerOffset(3, 16000, true, 0) == 384, "IRQ vencida no elimina el indice");
static_assert(estimateWaveTriggerOffset(3, 17580, true, 0) == 384, "No inventar una parte no copiada");
static_assert(estimateWaveTriggerOffset(3, 2000, true, 50000) == 0, "Inicio en frame actual");
static_assert(estimateWaveTriggerOffset(3, 0, false, 50000) == -16, "Inicio en frame anterior");
static_assert(eventTriggerIndex(768 + estimateWaveTriggerOffset(3, 0, false, 50000)) == 752, "Offset negativo valido");
static_assert(eventTriggerIndex(768 + estimateWaveTriggerOffset(0, 0, false, 50000)) == 368, "Cambio de frame");
static_assert(eventTriggerIndex(estimateWaveTriggerOffset(0, 0, false, 50000)) == -1, "Sin preframe no inventar inicio");
static_assert(eventTriggerIndex(768 + estimateWaveTriggerOffset(0, 0, false, 200000)) == -1, "Confirmacion mayor al historial");
static_assert(eventTriggerIndex(768 + estimateWaveTriggerOffset(5, 15999, true, 0)) == 1535, "Ultima muestra previa al cambio");
static_assert(estimateWaveTriggerOffset(0, uint32_t(100U - 0xffffff00U), true, 0) == 2, "Wrap de micros");
// Recorre todas las partes y tiempos validos; verifica continuidad interna y correccion de 400 muestras.
constexpr bool allPositions() {
    for (uint32_t part = 0; part < 6; part++) {
        for (uint32_t sample = 0; sample < 128; sample++) {
            auto offset = estimateWaveTriggerOffset(part, sample * 125, true, 50000);
            if (eventTriggerIndex(768 + offset) != int32_t(368 + part * 128 + sample)) return false;
        }
    }
    return true;
}
static_assert(allPositions(), "Todas las posiciones validas del frame");
`;
const output = path.join(root, '.pio/build/esp32doit-devkit-v1/event_trigger_test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-I', path.join(root, 'src'), '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: trigger RMS/Wave, fallback, confirmacion, prebuffer, limites y wrap de micros.');
