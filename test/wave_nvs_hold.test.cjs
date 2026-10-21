const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/wave_event_store.cpp','utf8');
const start = source.indexOf('bool waveEventStore::isAcquiring()');
let end = source.indexOf('{',start) + 1, depth = 1;
while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
const method = source.slice(start,end).replace('bool waveEventStore::','constexpr bool ');
const code = `#include <stdint.h>
struct atomicWord {
    uint32_t value = 0;
    // Simula lectura atomica; sin parametros.
    constexpr uint32_t load() const {return value;}
    // Simula comparacion atomica; expected identifica el valor que se puede reemplazar por desired.
    constexpr bool compare_exchange_strong(uint32_t& expected,uint32_t desired) {
        if (expected != value) {expected = value; return false;} value = desired; return true;
    }
};
struct store {
    bool paused = false, acquisitionActive = false; atomicWord nvsHoldUntilMs; uint32_t nowMs = 0;
    // Expone el reloj de prueba; sin parametros.
    constexpr uint32_t millis() const {return nowMs;}
    ${method}
};
// Comprueba comandos pendientes, captura larga, pausa, vencimiento y wrap; sin parametros.
constexpr bool holds() {
    store s; s.nowMs = 500; s.nvsHoldUntilMs.value = 1000;
    if (!s.isAcquiring()) return false;
    s.paused = true; if (s.isAcquiring()) return false; s.paused = false;
    s.nowMs = 1000; if (s.isAcquiring() || s.nvsHoldUntilMs.value) return false;
    s.acquisitionActive = true; if (!s.isAcquiring()) return false; s.acquisitionActive = false;
    s.nvsHoldUntilMs.value = 500; s.nowMs = UINT32_MAX - 500;
    if (!s.isAcquiring()) return false;
    s.nowMs = 500;
    return !s.isAcquiring() && !s.nvsHoldUntilMs.value;
}
static_assert(holds(), "NVS espera Wave y reanuda sin trabarse en el wrap");
`;
const output = '.pio/build/wave-nvs-hold-test.cpp';
fs.mkdirSync(path.dirname(output),{recursive:true}); fs.writeFileSync(output,code);
const compiler = path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler,['-std=c++14','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: NVS diferida durante comando/captura Wave, vencimiento, pausa y wrap.');
