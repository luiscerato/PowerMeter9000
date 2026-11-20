const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/wave_event_store.cpp','utf8');
const start = source.indexOf('static void copyWaveWords(');
let end = source.indexOf('{',start) + 1, depth = 1;
while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
const output = '.pio/build/wave-memory-test.cpp', object = '.pio/build/wave-memory-test.o';
const code = '#include <stdint.h>\n#include <stddef.h>\n' + source.slice(start,end) +
    '\n// Expone la copia real para inspeccionar sus instrucciones; dest/source/length conservan alineacion y longitud.\n' +
    'void exerciseWordCopy(void* dest, const void* source, size_t length) {copyWaveWords(dest,source,length);}\n';
fs.mkdirSync(path.dirname(output),{recursive:true}); fs.writeFileSync(output,code);
const tools = path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin');
execFileSync(path.join(tools,'xtensa-esp32-elf-g++.exe'),['-std=c++11','-O2','-c',output,'-o',object],{stdio:'inherit'});
const assembly = execFileSync(path.join(tools,'xtensa-esp32-elf-objdump.exe'),['-d',object],{encoding:'utf8'});
assert.match(assembly,/\bl32i(?:\.n)?\b/); assert.match(assembly,/\bs32i(?:\.n)?\b/);
assert.doesNotMatch(assembly,/\b(?:l8ui|s8i|l16ui|s16i|lsi|ssi)\b|memcpy/);
assert.match(source,/MALLOC_CAP_EXEC \| MALLOC_CAP_32BIT/);
assert.doesNotMatch(source,/ESP\.getFreeHeap\(\)|reinterpret_cast<packetHeader\*>\(item\./);
assert.match(source,/copyWaveWords\(pageBuffer, header, sizeof\(\*header\)\)/);
assert.match(source,/copyWaveWords\(pageBuffer \+ sizeof\(\*header\), item.words, pagePayloadBytes\)/);
assert.match(source,/capture\.firstPage \+ packetIndex, pageBuffer, pageBytes/);
console.log('PASS: copia real solo con L32I/S32I, IRAM sin accesos byte/float y pagina NAND en DRAM.');
