const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/wave_sample_clock.h','utf8').replace('#pragma once','').replace('bool update(','constexpr bool update(');
const code = `#include <stdint.h>\n${source}
// Reproduce latencia de atencion sin perder datos; sin parametros, medio segundo de jitter alternado.
constexpr bool jitter() {
    waveSampleClock clock;
    for (uint32_t i = 0; i < 500; ++i) {
        const uint64_t before = clock.endUs;
        const uint64_t observed = 1000000ULL + i * 16000ULL + (i % 3 == 1 ? 3500 : 0);
        if (!clock.update(observed, i % 2) || clock.gap) return false;
        if (i && (clock.endUs - before < 15992 || clock.endUs - before > 16008)) return false;
    }
    return true;
}
// Distingue mitades repetidas de una o dos perdidas; sin parametros.
constexpr bool missed() {
    waveSampleClock one, two;
    one.update(1000000,0); two.update(1000000,0);
    if (one.update(1001000,0)) return false;
    if (!one.update(1032000,0) || !one.gap || one.endUs != 1032000) return false;
    if (!one.update(1048000,1) || one.gap) return false;
    return two.update(1048000,1) && two.gap && two.endUs == 1048000;
}
static_assert(jitter(), "Latencia de tareas no borra muestras ni crea solapamientos");
static_assert(missed(), "Duplicados y saltos reales distinguidos por reloj y mitad");
`;
const output = '.pio/build/wave-clock-test.cpp';
fs.mkdirSync(path.dirname(output),{recursive:true}); fs.writeFileSync(output,code);
const compiler = path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler,['-std=c++14','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: reloj Wave real, jitter de 3.5 ms sin descartar muestras, duplicados y mitades perdidas.');
