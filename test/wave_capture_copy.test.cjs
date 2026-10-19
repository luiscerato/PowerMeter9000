const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'src/event_store.cpp'), 'utf8');

// Extrae la funcion real indicada por signature e inyecta el estado de prueba en lugar de globals.
function captureFunction(signature) {
    const start = source.indexOf(signature);
    assert.ok(start >= 0, signature);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < source.length) {
        if (source[end] === '{') depth++;
        if (source[end] === '}') depth--;
        end++;
    }
    return 'constexpr ' + source.slice(start, end)
        .replace('(', '(State& s, ')
        .replace(/\bactiveCapture\b/g, 's.active')
        .replace(/\bpreviousWaveFrame\b/g, 's.previous')
        .replace(/\bhasPreviousWaveFrame\b/g, 's.hasPrevious')
        .replaceAll('allocateWaveBlockLocked(', 'allocateWaveBlockLocked(s, ')
        .replaceAll('memcpy(', 'copyBytes(')
        .replaceAll('free(', 'releaseBytes(s, ');
}

const harness = `
#include <stdint.h>
#include <stddef.h>
#include "event_trigger.h"
using String = int;
constexpr uint8_t maxWaveBlocks = 6;
constexpr size_t waveFrameBytes = 8064, waveSamplesPerBlock = 768, waveMinFreeHeap = 30 * 1024;
struct EventEntry {
    String eventId = 1;
    bool waveFinalized = false, waveAvailable = false, waveCapturing = false, durationKnown = false;
    const char* waveReason = "";
    uint8_t waveBlockCount = 0, wavePreBlockCount = 0, waveReservedBlocks = 0, wavePostFramesLeft = 0;
    int32_t waveTriggerSample = -1;
    uint8_t* waveBlocks[6] = {};
};
struct State {
    EventEntry* active = nullptr;
    uint8_t previous[8064] = {};
    bool hasPrevious = false;
    uint8_t pool[6][8064] = {};
    uint8_t maxAllocated = 6, allocated = 0, released = 0;
    bool freed[6] = {}, doubleFree = false;
};
struct Heap {
    // Simula heap suficiente; las fallas se inyectan en State.maxAllocated, sin parametros.
    constexpr size_t getFreeHeap() const { return 160000; }
};
constexpr Heap ESP;
// Simula margen suficiente; size/margin describen la reserva, las fallas se inyectan en el pool.
constexpr bool memoryHasRoom(size_t size, size_t margin) { return size + margin <= ESP.getFreeHeap(); }
// Copia count bytes de src a dest; reemplaza memcpy para evaluar C++ en compilacion.
constexpr void copyBytes(uint8_t* dest, const uint8_t* src, size_t count) {
    for (size_t i = 0; i < count; i++) dest[i] = src[i];
}
// Simula solo la reserva: entry recibe un bloque independiente del pool s en index.
constexpr bool allocateWaveBlockLocked(State& s, EventEntry& entry, uint8_t index) {
    if (index >= 6) return false;
    if (entry.waveBlocks[index]) return true;
    if (s.allocated >= s.maxAllocated) return false;
    entry.waveBlocks[index] = s.pool[s.allocated++];
    entry.waveReservedBlocks = index + 1;
    return true;
}
// Registra la liberacion de p en s, sin liberar la memoria constexpr del test.
constexpr void releaseBytes(State& s, uint8_t* p) {
    for (int i = 0; i < s.allocated; i++) if (p == s.pool[i]) {
        if (s.freed[i]) s.doubleFree = true;
        s.freed[i] = true;
        s.released++;
    }
}
// La cabecera no participa en la copia; entry/timestamp quedan fuera de esta prueba.
constexpr void initWaveHeaderLocked(EventEntry&, uint64_t) {}
// Cierra entry sin tocar sus muestras, igual que el cierre de produccion.
constexpr void finishWaveLocked(EventEntry& entry) {
    entry.waveCapturing = false; entry.waveFinalized = true;
    entry.waveAvailable = entry.waveBlockCount > 0;
}
${captureFunction('bool eventStoreStartWave(')}
${captureFunction('void eventStoreProcessWaveFrame(')}
// Patron no periodico a 128/768 muestras; frame/offset identifican el byte absoluto.
constexpr uint8_t pattern(uint32_t frame, size_t offset) {
    size_t n = frame * waveFrameBytes + offset;
    return (n * 17) ^ (n >> 8) ^ (n >> 5);
}
// Comprueba que entry contiene count frames consecutivos desde first y no referencias a la fuente.
constexpr bool matches(const EventEntry& entry, uint32_t first, uint8_t count, const uint8_t* source) {
    if (entry.waveBlockCount != count) return false;
    for (uint8_t block = 0; block < count; block++) {
        if (entry.waveBlocks[block] == source) return false;
        for (size_t i = 0; i < waveFrameBytes; i++)
            if (entry.waveBlocks[block][i] != pattern(first + block, i)) return false;
    }
    return true;
}
// Inicia tras startPart partes, conserva o no preframe y opcionalmente falla la cuarta reserva.
constexpr bool stream(uint8_t startPart, bool preframe, bool limited) {
    State s;
    EventEntry entry;
    uint8_t output[8064] = {};
    for (size_t i = 0; i < waveFrameBytes; i++) output[i] = pattern(0, i);
    if (preframe) eventStoreProcessWaveFrame(s, output, waveFrameBytes, false);
    s.active = &entry;
    if (limited) s.maxAllocated = 3;
    for (uint32_t frame = 1; frame <= 7; frame++) {
        for (uint8_t part = 0; part < 6; part++) {
            if (frame == 1 && part == startPart && !eventStoreStartWave(s, 1, 0, part * 128)) return false;
            for (size_t i = part * 1344; i < (part + 1) * 1344; i++) output[i] = pattern(frame, i);
        }
        eventStoreProcessWaveFrame(s, output, waveFrameBytes, false);
        // Reutilizar el origen inmediatamente: ninguna captura debe cambiar por eso.
        for (size_t i = 0; i < waveFrameBytes; i++) output[i] = 0xEE;
    }
    if (!entry.waveFinalized || entry.waveCapturing || s.doubleFree || s.released) return false;
    if (entry.wavePreBlockCount != (preframe ? 1 : 0)) return false;
    return matches(entry, preframe ? 0 : 1, limited ? 3 : 6, output);
}
static_assert(stream(0, true, false), "Disparo al principio del frame");
static_assert(stream(1, true, false), "Disparo con una parte");
static_assert(stream(2, true, false), "Disparo con dos partes");
static_assert(stream(3, true, false), "Disparo con tres partes");
static_assert(stream(4, true, false), "Disparo con cuatro partes");
static_assert(stream(5, true, false), "Disparo en la ultima parte");
static_assert(stream(0, false, false), "Arranque sin frame anterior");
static_assert(stream(3, true, true), "Sin RAM preserva los tres primeros bloques");
`;
const output = path.join(root, '.pio/build/esp32doit-devkit-v1/wave_capture_copy_test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-I', path.join(root, 'src'), '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: copia real de preframe y frames siguientes; seis posiciones de disparo, origen sobrescrito y RAM limitada.');
