const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

// Extrae la funcion real entre firmas conocidas; start/end delimitan codigo C++.
function extract(source, start, end) {
    const first = source.indexOf(start);
    const last = source.indexOf(end, first);
    if (first < 0 || last < 0) throw new Error('No se encontro la funcion a probar');
    return source.slice(first, last).trim();
}

const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'src/event_store.cpp'), 'utf8');
const copy = extract(source, 'size_t eventStoreCopyWaveChunk(', 'bool eventStoreClearEvent(')
    .replace('size_t eventStoreCopyWaveChunk(', 'constexpr size_t eventStoreCopyWaveChunk(EventEntry* eventEntries, ')
    .replaceAll('memcpy(', 'copyBytes(');
const size = extract(source, 'static size_t getWaveSizeLocked(', '// Agrega la descripcion de canales')
    .replace('static size_t', 'constexpr size_t');

// Evaluacion C++ en compilacion: solo se simulan el mutex, String y el almacen.
const harness = `
#include <stdint.h>
#include <stddef.h>
using String = int;
constexpr size_t waveHeaderBytes = 128, waveFrameBytes = 8064;
struct EventEntry {
    bool ready = true, waveAvailable = true;
    uint8_t waveBlockCount = 6;
    uint8_t waveHeader[128] = {};
    uint8_t* waveBlocks[6] = {};
    uint32_t waveCrc32 = 0xA34F219C;
};
constexpr bool lockEventStore() { return true; }
constexpr void unlockEventStore() {}
constexpr int8_t findEventLocked(int id) { return id == 42 ? 0 : -1; }
constexpr void copyBytes(uint8_t* dest, const uint8_t* src, size_t count) {
    for (size_t i = 0; i < count; i++) dest[i] = src[i];
}
${size}
${copy}

// Patron por offset absoluto, independiente de la segmentacion en bloques.
constexpr uint8_t pattern(size_t offset) {
    return (offset * 17) ^ (offset >> 8) ^ (offset >> 5);
}
constexpr bool checkTransfer(size_t chunkSize, uint8_t blockCount, bool outOfOrder) {
    EventEntry entry;
    entry.waveBlockCount = blockCount;
    uint8_t blocks[6][8064] = {};
    uint8_t expected[48516] = {};
    uint8_t dest[50002] = {};
    const size_t total = 128 + blockCount * 8064 + 4;
    for (size_t i = 0; i < total - 4; i++) expected[i] = pattern(i);
    expected[total - 4] = 0x9C; expected[total - 3] = 0x21;
    expected[total - 2] = 0x4F; expected[total - 1] = 0xA3;
    copyBytes(entry.waveHeader, expected, 128);
    for (size_t block = 0; block < blockCount; block++) {
        entry.waveBlocks[block] = blocks[block];
        copyBytes(blocks[block], expected + 128 + block * 8064, 8064);
    }
    const size_t offsets[] = {0, 127, 128, 129, 1472, 8191, 8192, 8193, total - 4, total - 1};
    size_t position = 0, request = 0;
    while (outOfOrder ? request < 10 : position < total) {
        if (outOfOrder) position = offsets[9 - request++];
        dest[0] = 0x55; dest[chunkSize + 1] = 0xAA;
        size_t count = eventStoreCopyWaveChunk(&entry, 42, position, dest + 1, chunkSize);
        const size_t wanted = chunkSize < total - position ? chunkSize : total - position;
        if (count != wanted || dest[0] != 0x55 || dest[chunkSize + 1] != 0xAA) return false;
        for (size_t i = 0; i < count; i++) if (dest[i + 1] != expected[position + i]) return false;
        if (!outOfOrder) position += count;
    }
    if (eventStoreCopyWaveChunk(&entry, 42, total, dest, chunkSize) != 0) return false;
    if (eventStoreCopyWaveChunk(&entry, 42, 85000, dest, chunkSize) != 0) return false;
    if (eventStoreCopyWaveChunk(&entry, 42, 0, dest, 0) != 0) return false;
    if (eventStoreCopyWaveChunk(&entry, 42, 0, nullptr, chunkSize) != 0) return false;
    if (eventStoreCopyWaveChunk(&entry, 99, 0, dest, chunkSize) != 0) return false;
    entry.ready = false;
    return eventStoreCopyWaveChunk(&entry, 42, 0, dest, chunkSize) == 0;
}
`;

const cases = [[1, 1], [127, 6], [128, 6], [129, 6], [1344, 6], [1460, 6],
    [4096, 6], [8063, 6], [8064, 6], [8065, 6], [50000, 6]];
const assertions = cases.map(([chunk, blocks]) =>
    `static_assert(checkTransfer(${chunk}, ${blocks}, false), "chunk=${chunk}");`).join('\n');
const output = path.join(root, '.pio/build/esp32doit-devkit-v1/wave_http_copy_test.cpp');
fs.writeFileSync(output, harness + assertions +
    '\nstatic_assert(checkTransfer(4096, 6, true), "Offsets independientes y fuera de orden");\n');
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Wall', '-Wextra', '-Werror', '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: funcion C++ real, 12 escenarios de fragmentacion, limites y offsets independientes.');
