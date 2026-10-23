const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/wave_event_store.cpp', 'utf8');
const start = source.indexOf('void waveEventStore::confirm(');
let end = source.indexOf('{', start) + 1, depth = 1;
while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
const method = source.slice(start, end).replace('void waveEventStore::', 'constexpr void ');
const code = `#include <stdint.h>
constexpr int pdTRUE = 1;
struct Store {
    struct command {uint32_t eventId = 0; uint64_t startUs = 0, endUs = 0, rootUs = 0, unixUs = 0;};
    struct Mask {uint8_t value = 7; constexpr uint8_t load() const {return value;}} wavePointMask;
    bool paused = false, queueReady = true;
    int commands = 0, task = 0, queued = 0, triggerDrops = 0;
    constexpr int xQueueSend(int queue, const command* item, int wait) {if (!queueReady) return 0; ++queued; return 1;}
    constexpr void xTaskNotifyGive(int handle) {}
    ${method}
};
// Comprueba la politica real para mask, incluidos estados, puntuales y capturas compartidas.
constexpr bool matches(uint8_t mask) {
    for (uint8_t events = 1; events < 16; ++events) {
        Store store; store.wavePointMask.value = mask;
        store.confirm(1, 0, 1000, 0, 0, events);
        if (bool(store.queued) != bool(events & (1U | (mask << 1)))) return false;
    }
    return true;
}
constexpr bool failures() {
    Store store; store.paused = true; store.confirm(1,0,1000,0,0,15);
    if (store.queued) return false;
    store.paused = false; store.confirm(0,0,1000,0,0,15);
    if (store.queued) return false;
    store.queueReady = false; store.confirm(1,0,1000,0,0,15);
    return store.triggerDrops == 1;
}
static_assert(matches(0) && matches(1) && matches(2) && matches(3) && matches(4) && matches(5) && matches(6) && matches(7), "Per-type Wave policy");
static_assert(failures(), "Pause, invalid ID and full queue");
`;
const output = '.pio/build/wave-config-policy-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive:true});
fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio:'inherit'});
console.log('PASS: politica Wave real, ocho habilitaciones, tipos compartidos, pausa y cola llena.');
