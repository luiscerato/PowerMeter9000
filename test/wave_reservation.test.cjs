const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const header = fs.readFileSync('src/storage/wave_event_store.h', 'utf8');
const source = fs.readFileSync('src/storage/wave_event_store.cpp', 'utf8');
const capacity = header.match(/slot slots\[([^\]]+)\]/)[1];
const wanted = source.match(/const uint8_t wanted = ([^;]+);/)[1].replace('metrics.requestedPreMs', 'preMs');
const output = '.pio/build/wave-reservation-test.cpp';
const code = `#include "storage/wave_event_format.h"
using namespace waveEventFormat;
constexpr unsigned capacity = ${capacity};
// Comprueba la reserva real para preMs, incluido el margen de pagina parcial y escritura.
constexpr bool fits(unsigned preMs) {
    const unsigned wanted = ${wanted};
    return wanted <= capacity && (wanted - 3) * pageUs == preMs * 1000;
}
static_assert(fits(48) && fits(96) && fits(144) && fits(192) && fits(240) && fits(288), "Every valid prehistory fits");
static_assert(capacity == 15, "Maximum prehistory needs fifteen slots");
static_assert((192 / 24 + 3) * pagePayloadBytes + 3 * (sliceBytes + 16) == 26256, "Default RAM reservation");
static_assert(samplesPerPage * 7 * 12 / 8 + sizeof(pageHeader) == 2048, "24 ms fills one NAND page");
`;
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Isrc', '-fsyntax-only', output], {stdio: 'inherit'});
assert.match(source, /slotCount < sizeof\(slots\) \/ sizeof\(slots\[0\]\)/);
console.log('PASS: reserva 48..288 ms, limite de slots, 26256 bytes por defecto y paginas de 2048 bytes.');
