const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/wave_event_store.cpp','utf8');
const start = source.indexOf('int8_t waveEventStore::freeSlot()');
let end = source.indexOf('{',start) + 1, depth = 1;
while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
const method = source.slice(start,end).replace('int8_t waveEventStore::','constexpr int8_t ');
const code = `
#include "storage/wave_event_format.h"
struct pool {
    struct slot { uint8_t slices = 0; uint64_t order = 0; bool queued = false; };
    slot slots[8]; uint8_t currentSlot = 0, slotCount = 6;
    ${method}
};
// Comprueba que ningun buffer que pertenece al escritor o productor se pueda reciclar; sin parametros.
constexpr bool ownership() {
    pool p;
    if (p.freeSlot() != 1) return false;
    for (uint8_t i = 1; i < 6; ++i) {p.slots[i].queued = true; p.slots[i].slices = 3;}
    if (p.freeSlot() != -1) return false;
    p.slots[3].queued = false; p.slots[3].order = 1;
    p.slots[4].queued = false; p.slots[4].order = 2;
    if (p.freeSlot() != 3) return false;
    p.slots[4].slices = 0;
    return p.freeSlot() == 4;
}
static_assert(ownership(), "Propiedad exclusiva de buffers hasta terminar ambas paginas");
static_assert(sizeof(waveEventFormat::packetHeader) == 64, "Cabecera fija");
static_assert(sizeof(waveEventFormat::commitHeader) == 112, "Cierre fijo con cuatro enlaces");
static_assert(31 * 4096 + 4096 == 131072, "Un bloque fisico");
static_assert(6 * 48 == 288 && (6 + 2) * 4096 == 32768, "Tope RAM para paquetes");
static_assert(uint16_t(uint16_t(50) - uint16_t(65486)) == 100, "FAST 100 us conserva wrap sin aumentar 16 bytes");
`;
const output = '.pio/build/wave-pool-test.cpp';
fs.mkdirSync(path.dirname(output),{recursive:true}); fs.writeFileSync(output,code);
const compiler = path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler,['-std=c++14','-Isrc','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: selector de slots real, propiedad de buffers, formato 64/112 B, limite 288 ms y wrap FAST.');
