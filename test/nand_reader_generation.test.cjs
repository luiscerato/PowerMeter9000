const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/nand_read_session.cpp', 'utf8');

// Extrae una funcion real de text identificada por signature y conserva su cuerpo.
function extract(text, signature) {
    const start = text.indexOf(signature);
    assert.ok(start >= 0, signature);
    let end = text.indexOf('{', start) + 1, depth = 1;
    while (depth) { if (text[end] === '{') ++depth; if (text[end] === '}') --depth; ++end; }
    return text.slice(start, end);
}
const copy = extract(source, 'size_t NandReadSession::copyChunk(').replace('size_t NandReadSession::', 'constexpr size_t ');
const cancel = extract(source, 'void NandReadSession::cancel(').replace('void NandReadSession::', 'constexpr void ');
const code = `#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <algorithm>
using std::min;
constexpr size_t RESPONSE_TRY_AGAIN = size_t(-1);
constexpr int pdPASS = 1;
constexpr uint32_t millis() {return 100;}
constexpr int xQueueReceive(int queue, uint8_t* value, int wait) {return 0;}
constexpr int xQueueSend(int queue, uint8_t* value, int wait) {return 1;}
struct Session {
    static constexpr size_t pageSize = 2048;
    static constexpr uint8_t bufferCount = 2;
    uint32_t sessionId = 8, lastConsumerMs = 50;
    bool active = true, producerFailed = false;
    size_t total = 4096, currentLength = 0, currentOffset = 0;
    uint8_t currentBuffer = 2, buffers[2][2048] = {};
    int readyQueue = 0, freeQueue = 0;
    ${cancel}
    ${copy}
};
// Comprueba los callbacks reales con generacion vieja, cancelacion y productor fallido.
constexpr bool generations() {
    Session value; uint8_t byte = 0;
    if (value.copyChunk(&byte,1,0,7) != 0 || value.lastConsumerMs != 50 || !value.active) return false;
    value.cancel(7); if (!value.active) return false;
    if (value.copyChunk(&byte,1,0,8) != RESPONSE_TRY_AGAIN || value.lastConsumerMs != 100) return false;
    value.producerFailed = true;
    if (value.copyChunk(&byte,1,0,8) != 0 || value.active) return false;
    value.active = true; value.cancel(8); return !value.active;
}
static_assert(generations(), "Old callbacks cannot read or cancel the next response");
`;
const output = '.pio/build/nand-reader-generation-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive:true}); fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio:'inherit'});
const web = fs.readFileSync('src/webserver.cpp', 'utf8');
assert.equal((web.match(/nandReadSession\.copyChunk\([^;]+sessionId\)/g) || []).length, 3);
assert.equal((web.match(/nandReadSession\.cancel\(sessionId\)/g) || []).length, 3);
console.log('PASS: lector NAND real, generaciones, error del productor y tres rutas con desconexion protegida.');
