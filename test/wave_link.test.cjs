const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/wave_event_store.cpp', 'utf8');
const start = source.indexOf('void waveEventStore::linkEvent(');
let end = source.indexOf('{', start) + 1, depth = 1;
while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
const method = source.slice(start, end).replace('void waveEventStore::', 'constexpr void ');
const code = `
#include "storage/wave_event_format.h"
using namespace waveEventFormat;
struct store {
    struct capture { bool used = false; commitHeader commit = {}; };
    struct command { uint32_t eventId; uint64_t startUs, endUs, rootUs, unixUs; };
    capture pending[8];
    struct { unsigned droppedTriggers = 0; } metrics;
    ${method}
};
// Reproduce retorno de 28 s: Wave arranca antes del cruce normal, pero conserva ese instante.
constexpr bool recovery() {
    store s;
    s.pending[0].used = true;
    s.pending[0].commit.triggerUs = 25799102506ULL;
    s.pending[0].commit.endUs = 25800394137ULL;
    s.pending[1].used = true;
    s.pending[1].commit.triggerUs = 25827054124ULL;
    s.pending[1].commit.endUs = 25828329544ULL;
    s.linkEvent({7, 25827072900ULL, 25831792900ULL, 25827072900ULL, 1792575069734800ULL});
    if (s.pending[0].commit.linkCount || s.pending[1].commit.linkCount != 1) return false;
    const auto& link = s.pending[1].commit.links[0];
    if (link.eventId != 7 || link.rootUs != 25827072900ULL || link.unixUs != 1792575069734800ULL) return false;
    s.linkEvent({7, 25827072900ULL, 25831792900ULL, 1, 2});
    return s.pending[1].commit.linkCount == 1;
}
// Verifica fronteras, ventanas invalidas, capturas sin usar y limite de cuatro enlaces.
constexpr bool boundaries() {
    store s; s.pending[0].used = true;
    auto& c = s.pending[0].commit; c.triggerUs = 1000; c.endUs = 2000;
    s.pending[1].commit = c;
    s.linkEvent({1, 2001, 3000, 1, 2});
    s.linkEvent({1, 100, 999, 1, 2});
    s.linkEvent({1, 2000, 1999, 1, 2});
    s.linkEvent({0, 1000, 2000, 1, 2});
    if (c.linkCount) return false;
    s.linkEvent({1, 2000, 3000, 1, 2});
    s.linkEvent({2, 500, 1000, 1, 2});
    s.linkEvent({3, 1200, 1800, 1, 2});
    s.linkEvent({4, 500, 3000, 1, 2});
    s.linkEvent({5, 1000, 2000, 1, 2});
    return c.linkCount == 4 && s.metrics.droppedTriggers == 1 && !s.pending[1].commit.linkCount;
}
static_assert(recovery(), "Wave de retorno no queda huerfana por disparar antes de normal");
static_assert(boundaries(), "No enlaza capturas ajenas, duplicadas o fuera de capacidad");
`;
const output = '.pio/build/wave-link-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Isrc', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: enlace Wave real, retorno tras 28 s, solapamiento, duplicados y capacidad.');
