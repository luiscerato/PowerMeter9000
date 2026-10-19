const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'src/memory_diagnostics.cpp'), 'utf8');
const start = source.indexOf('bool memoryHasRoom(');
assert.ok(start >= 0);
const body = source.slice(start, source.indexOf('\n}', start) + 2)
    .replace('bool memoryHasRoom(', 'constexpr bool memoryHasRoom(const Heap& heap, ')
    .replaceAll('heap_caps_get_free_size(', 'heapFree(heap, ')
    .replaceAll('heap_caps_get_largest_free_block(', 'heapLargest(heap, ');
const harness = `
#include <stddef.h>
#include <stdint.h>
constexpr int MALLOC_CAP_8BIT = 4;
struct Heap { size_t free8; size_t largest8; };
// Simula la memoria utilizable de heap; caps exige acceso por bytes.
constexpr size_t heapFree(const Heap& heap, int caps) { return caps == 4 ? heap.free8 : 0; }
// Simula el mayor bloque de heap con las capacidades caps.
constexpr size_t heapLargest(const Heap& heap, int caps) { return caps == 4 ? heap.largest8 : 0; }
${body}
static_assert(memoryHasRoom({90000, 50000}, 4800, 32768), "Captura normal con margen");
static_assert(!memoryHasRoom({90000, 1024}, 4800, 32768), "Heap fragmentado rechaza el paquete");
static_assert(!memoryHasRoom({34000, 34000}, 4800, 32768), "No consumir el margen reservado");
static_assert(!memoryHasRoom({16000, 16000}, 1460, 16384), "No desbordar la resta al faltar margen");
static_assert(memoryHasRoom({37568, 4800}, 4800, 32768), "Frontera exacta de memoria disponible");
static_assert(!memoryHasRoom({90000, 90000}, SIZE_MAX, 32768), "Un tamano invalido nunca desborda la suma");
`;
const output = path.join(root, '.pio/build/serial-migration/memory_budget_test.cpp');
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-c', output, '-o', output + '.o'], { stdio: 'inherit' });
console.log('PASS: margen real, fragmentacion, frontera exacta y tamanos invalidos.');
