const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const detector = fs.readFileSync('src/fast_event_detector.h', 'utf8').replace('#pragma once', '').replace('void update(', 'constexpr void update(');
const config = fs.readFileSync('src/event_capture_config.h', 'utf8').replace('#pragma once', '');
const harness = `${config}
${detector}
// Comprueba umbrales distintos por fase, limites estrictos y confirmacion comun; sin parametros.
constexpr bool phases() {
    fastEventDetector r, s, t;
    for (unsigned ms = 0; ms <= 50; ms += 10) {
        r.update(180, 190, 50, true, true, ms);
        s.update(180, 170, 50, true, true, ms);
        t.update(180, 180, 50, true, true, ms);
        if (ms < 50 && r.active) return false;
    }
    return r.active && !s.active && !t.active && r.startedMs == 0;
}
// Verifica extremo previo a confirmacion, recuperacion y nuevo inicio; sin parametros.
constexpr bool lifecycle() {
    fastEventDetector d;
    d.update(170, 190, 20, true, true, 100);
    d.update(80, 190, 20, true, true, 110);
    d.update(160, 190, 20, true, true, 120);
    if (!d.active || d.extreme != 80 || d.startedMs != 100) return false;
    d.update(190, 190, 20, true, true, 130);
    if (d.active || d.pending) return false;
    d.update(170, 190, 20, true, true, 140);
    return !d.active && d.startedMs == 140 && d.extreme == 170;
}
// Un hueco o NaN corta la confirmacion pero no inventa una recuperacion; sin parametros.
constexpr bool missing() {
    fastEventDetector d;
    d.update(100, 190, 20, true, true, 0);
    d.update(NAN, 190, 20, true, true, 10);
    d.update(100, 190, 20, true, true, 20);
    if (d.active || d.startedMs != 20) return false;
    d.update(100, 190, 20, true, true, 60);
    if (d.active || d.startedMs != 60) return false;
    d.update(100, 190, 20, true, true, 70);
    d.update(100, 190, 20, true, true, 80);
    d.update(NAN, 190, 20, true, true, 90);
    return d.active && d.extreme == 100;
}
// Prueba OC/swell, habilitacion, cambio de umbral y wrap del reloj; sin parametros.
constexpr bool highAndChanges() {
    fastEventDetector d;
    d.update(100, 85, 10, false, true, 0xfffffff8U);
    d.update(120, 85, 10, false, true, 2);
    if (!d.active || d.extreme != 120) return false;
    d.update(120, 85, 10, false, false, 12);
    if (d.active) return false;
    d.update(260, 250, 10, false, true, 22);
    d.update(270, 250, 10, false, true, 32);
    if (!d.active || d.extreme != 270) return false;
    d.update(260, 255, 10, false, true, 42);
    return !d.active && d.startedMs == 42;
}
// Comprueba el extremo de confirmacion de 2000 ms; sin parametros.
constexpr bool limits() {
    fastEventDetector d;
    for (unsigned ms = 0; ms <= 2000; ms += 10) {
        d.update(260, 250, 2000, false, true, ms);
        if (ms < 2000 && d.active) return false;
    }
    return d.active;
}
static_assert(phases(), "Umbral por fase y confirmacion");
static_assert(lifecycle(), "Extremos, recuperacion y rearme");
static_assert(missing(), "NaN, huecos y continuidad");
static_assert(highAndChanges(), "OC, swell, configuracion y wrap");
static_assert(limits(), "Confirmacion maxima");
static_assert(limitEventConfirmMs(-1) == 10 && limitEventConfirmMs(0) == 10, "Minimo NVS");
static_assert(limitEventConfirmMs(55) == 50 && limitEventConfirmMs(2000) == 2000, "Paso y maximo");
static_assert(limitEventConfirmMs(2010) == 2000 && limitEventConfirmMs(2550) == 2000 && limitEventConfirmMs(INT32_MAX) == 2000, "Recorte de configuraciones anteriores");
`;
const output = '.pio/build/fast-event-detector-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
const meter = fs.readFileSync('src/meter.cpp', 'utf8');
assert.doesNotMatch(meter, /meterReadDipSwell|meterReadOverCurrent|ade\.setDipDetectionLevels|ade\.setSwellDetectionLevels/);
assert.match(meter, /detector\.update\(/);
console.log('PASS: detector real, fases independientes, 10..2000 ms, normalizacion NVS, extremos, recuperacion, NaN, huecos, cambios y wrap.');
