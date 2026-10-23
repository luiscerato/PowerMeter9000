const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {codec} = require('./grid_fixture.cjs');
const code = `
#include "capture_policy.h"
static_assert(eventPreHistoryComplete(113, 1130), "Previo completo");
static_assert(eventPreHistoryComplete(112, 1117), "Jitter normal no genera aviso");
static_assert(!eventPreHistoryComplete(111, 1130), "No oculta muestras realmente faltantes");
static_assert(!eventPreHistoryComplete(112, 1000), "No oculta cobertura corta");
static_assert(!eventPreHistoryComplete(0, 0), "Arranque sin historial");
`;
const output = '.pio/build/pre-history-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Isrc', '-fsyntax-only', output], {stdio: 'inherit'});
const event = {timeStatus: 16, preSampleCount: 112, settings: {preMs: 1130}, samples: [{relativeMs: -1120}]};
assert.equal(codec.gridPreHistoryMissing(event), false);
assert.equal(codec.gridPreHistoryMissing({...event, preSampleCount: 111}), true);
assert.equal(codec.gridPreHistoryMissing({...event, samples: [{relativeMs: -1000}]}), true);
assert.equal(codec.gridPreHistoryMissing({...event, samples: []}), true);
assert.equal(codec.gridPreHistoryMissing({...event, timeStatus: 0}), false);
for (const file of process.argv.slice(2)) {
    const bytes = fs.readFileSync(file);
    const decoded = codec.decodeGridEvent(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length));
    assert.equal(decoded.partial, false);
    assert.equal(codec.gridPreHistoryMissing(decoded), false);
    console.log('PASS captura real #' + decoded.id + ': previo suficiente, sin aviso.');
}
console.log('PASS: firmware/web toleran jitter y conservan avisos por historial realmente insuficiente.');
