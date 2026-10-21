const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/grid_events.cpp', 'utf8');
const meter = fs.readFileSync('src/meter.cpp', 'utf8');

// Extrae el cuerpo real de name, sin prototipos; source contiene el archivo C++.
function method(source, name) {
    const match = new RegExp(`(?:static )?(?:void|bool) ${name}\\([^]*?\\)\\s*\\{`).exec(source);
    assert.ok(match, name);
    let end = match.index + match[0].length, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(match.index, end);
}

// El consumidor NVS nunca puede entrar por los productores ni por MeterTask.
for (const name of ['setCurrentState', 'refreshStartTimestamp']) {
    assert.match(method(source, name), /queueSnapshot\(slot, tracker.snapshot\)/);
    assert.doesNotMatch(method(source, name), /saveSnapshot|Preferences|putBytes/);
}
assert.doesNotMatch(method(meter, 'MeterTask'), /gridEventsPersistLoop|saveSnapshot|Preferences|putBytes/);
assert.match(method(meter, 'MeterLoop'), /gridEventsPersistLoop\(\)/);
assert.match(method(meter, 'MeterLoop'), /if \(!waveCaptureBusy\(\)\) gridEventsPersistLoop\(\)/);
assert.equal((source.match(/saveSnapshot\(slot, snapshot\)/g) || []).length, 1);

// Hace evaluables los metodos reales con persistencia simulada; name identifica el metodo.
function simulated(name) {
    return 'constexpr ' + method(source, name).replace(/^static /, '')
        .replace(/^.*port(?:ENTER|EXIT)_CRITICAL.*$/gm, '')
        .replace(/^.*debugW\(.*$/gm, '');
}
const state = source.match(/struct gridStateSnapshot[^]*?};/)[0];
const pending = source.match(/struct pendingGridSnapshot[^]*?\n};/)[0];
const harness = `
#include <cstdint>
${state}
${pending}
struct Store {
    pendingGridSnapshot pendingSnapshots[4];
    uint8_t persistNextSlot = 0;
    bool persistRetry = false, fail = false, inject = false;
    uint32_t persistRetryAtMs = 0, clockMs = 0;
    unsigned attempts = 0, writes = 0;
    gridStateSnapshot stored[4] = {};
    // Expone el tiempo controlado por la prueba; sin parametros.
    constexpr uint32_t millis() const { return clockMs; }
    // Simula NVS e intercalado del productor; slot/snapshot identifican el estado escrito.
    constexpr bool saveSnapshot(uint8_t slot, const gridStateSnapshot& snapshot) {
        ++attempts;
        if (fail) return false;
        ++writes; stored[slot] = snapshot;
        if (inject) {
            inject = false;
            gridStateSnapshot newer = snapshot; newer.state[0] = 'Z';
            queueSnapshot(slot, newer);
        }
        return true;
    }
    ${['deadlineReached', 'queueSnapshot', 'gridEventsPersistLoop'].map(simulated).join('\n')}
};
// Comprueba que publicar solo modifica RAM y se conserva el ultimo estado de cada slot; sin parametros.
constexpr bool latestAndFair() {
    Store s{}; gridStateSnapshot a{}; a.state[0] = 'A';
    for (uint8_t i = 0; i < 4; ++i) s.queueSnapshot(i, a);
    a.state[0] = 'B'; s.queueSnapshot(0, a); s.queueSnapshot(255, a);
    if (s.attempts) return false;
    for (unsigned i = 1; i <= 4; ++i) { s.gridEventsPersistLoop(); if (s.writes != i) return false; }
    s.gridEventsPersistLoop();
    return s.writes == 4 && s.stored[0].state[0] == 'B' && s.stored[3].state[0] == 'A';
}
// Un estado nuevo durante putBytes no debe ser borrado al confirmar el anterior; sin parametros.
constexpr bool updateDuringWrite() {
    Store s{}; gridStateSnapshot a{}; a.state[0] = 'A';
    s.pendingSnapshots[0].revision = UINT32_MAX;
    s.queueSnapshot(0, a); s.inject = true; s.gridEventsPersistLoop();
    if (!s.pendingSnapshots[0].pending || s.stored[0].state[0] != 'A') return false;
    s.gridEventsPersistLoop();
    return !s.pendingSnapshots[0].pending && s.stored[0].state[0] == 'Z' && s.writes == 2;
}
// Los fallos conservan el pendiente y reintentan tras un segundo incluso con wrap; sin parametros.
constexpr bool retryAndWrap() {
    Store s{}; gridStateSnapshot a{}; s.queueSnapshot(2, a);
    s.clockMs = UINT32_MAX - 499; s.fail = true; s.gridEventsPersistLoop();
    if (!s.pendingSnapshots[2].pending || s.attempts != 1) return false;
    s.clockMs = 499; s.fail = false; s.gridEventsPersistLoop();
    if (s.attempts != 1) return false;
    s.clockMs = 500; s.gridEventsPersistLoop();
    if (s.pendingSnapshots[2].pending || s.writes != 1) return false;
    s.clockMs = 0x90000000U; s.queueSnapshot(1, a); s.gridEventsPersistLoop();
    return s.writes == 2;
}
static_assert(latestAndFair(), "Ultimo estado y una escritura por pasada");
static_assert(updateDuringWrite(), "Cambio concurrente y revision con wrap");
static_assert(retryAndWrap(), "Reintentos sin perder datos ni trabarse por overflow");
`;
const output = path.join('.pio', 'build', 'grid-persistence-test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: NVS fuera de MeterTask, ultimo estado, una escritura por pasada, concurrencia, fallo y overflow.');
