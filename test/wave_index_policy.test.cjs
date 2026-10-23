const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const header = fs.readFileSync('src/storage/wave_event_store.h', 'utf8');
const source = fs.readFileSync('src/storage/wave_event_store.cpp', 'utf8');

// Extrae el bloque delimitado por llaves que comienza con signature dentro de text.
function block(text, signature) {
    const start = text.indexOf(signature); assert.ok(start >= 0, signature);
    let end = text.indexOf('{', start) + 1, depth = 1;
    while (depth) { if (text[end] === '{') ++depth; if (text[end] === '}') --depth; ++end; }
    return text.slice(start, end);
}
const lookup = block(source, 'waveIndexStatus waveEventStore::lookup(').replace('waveIndexStatus waveEventStore::', 'constexpr waveIndexStatus ').replaceAll('millis()', 'nowMs');
const service = block(source, 'void waveEventStore::serviceIndex(').replace('void waveEventStore::', 'constexpr void ').replaceAll('millis()', 'nowMs');
const code = `#include "storage/wave_event_format.h"
using namespace waveEventFormat;
#define portENTER_CRITICAL(value) do {} while (0)
#define portEXIT_CRITICAL(value) do {} while (0)
${block(header, 'enum class waveIndexStatus')};
${block(header, 'struct waveIndexEntry')};
${block(header, 'struct waveIndexSnapshot')};
struct Store {
    ${block(header, 'struct indexQuery')};
    indexQuery indexQueries[2];
    bool paused = false; int task = 1, mutex = 0, ioQueue = 0;
    uint32_t nowMs = 100, ids[640] = {};
    uint16_t blockCount = 320;
    uint8_t committedSlots[80] = {}, slotVersion[640] = {};
    constexpr void xTaskNotifyGive(int task) {}
    constexpr bool read(uint16_t block, uint8_t page, void* dest, size_t size, int queue) {return false;}
    ${lookup}
    ${service}
};
// Usa los metodos reales para dos clientes, ocupado, resultados vacios validos y reutilizacion.
constexpr bool clients() {
    Store store; waveIndexSnapshot result;
    if (store.lookup(1,100,result) != waveIndexStatus::pending) return false;
    if (store.lookup(2,200,result) != waveIndexStatus::pending) return false;
    if (store.lookup(3,300,result) != waveIndexStatus::busy) return false;
    store.serviceIndex(); store.serviceIndex();
    store.nowMs += 3000; // Cloudflare puede entregar el primer resultado varios segundos despues.
    if (store.lookup(1,100,result) != waveIndexStatus::ready || result.count) return false;
    if (store.lookup(3,300,result) != waveIndexStatus::pending) return false;
    if (store.lookup(2,200,result) != waveIndexStatus::ready) return false;
    store.paused = true;
    return store.lookup(2,200,result) == waveIndexStatus::unavailable;
}
// No vuelve a leer el slot cero si el cursor ya llego al final con ese slot ocupado.
constexpr bool endOfScan() {
    Store store; store.committedSlots[0] = 1;
    auto& query = store.indexQueries[0]; query.status = waveIndexStatus::pending; query.cursor = 640;
    store.serviceIndex();
    return query.status == waveIndexStatus::ready;
}
// Reutiliza consultas abandonadas y conserva el vencimiento a traves del wrap de millis.
constexpr bool expiration() {
    Store store; waveIndexSnapshot result;
    for (auto& query : store.indexQueries) {query.status = waveIndexStatus::ready; query.completedMs = UINT32_MAX - 100;}
    store.nowMs = 10000;
    return store.lookup(5,500,result) == waveIndexStatus::pending;
}
// Un error de lectura se publica como failed, nunca como resultado vacio ready.
constexpr bool failedRead() {
    Store store; waveIndexSnapshot result; store.committedSlots[0] = 1;
    store.lookup(1,100,result); store.serviceIndex();
    return store.lookup(1,100,result) == waveIndexStatus::failed;
}
static_assert(clients(), "Pending is never reported as an empty completed index");
static_assert(endOfScan(), "End of scan cannot repeat slot zero");
static_assert(expiration(), "Abandoned queries expire with clock wrap");
static_assert(failedRead(), "NAND failure cannot masquerade as an empty index");
`;
const output = '.pio/build/wave-index-policy-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive:true}); fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Isrc', '-fsyntax-only', output], {stdio:'inherit'});
assert.doesNotMatch(block(source, 'waveIndexStatus waveEventStore::lookup('), /\bread\(|waitIo|xSemaphoreTake/);
console.log('PASS: indice Wave real, dos clientes, ocupado, final, TTL, wrap y HTTP sin NAND.');
