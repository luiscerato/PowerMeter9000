const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const history = fs.readFileSync('src/storage/history_read_session.cpp', 'utf8');
const logs = fs.readFileSync('src/storage/diagnostic_read_session.cpp', 'utf8');

// Extrae una funcion real de source desde signature, incluyendo sus llaves.
function extract(source, signature) {
    const start = source.indexOf(signature);
    if (start < 0) throw new Error('Missing ' + signature);
    let end = source.indexOf('{', start) + 1, depth = 1;
    while (depth) {
        if (source[end] === '{') ++depth;
        if (source[end] === '}') --depth;
        ++end;
    }
    return source.slice(start, end);
}

const expired = extract(logs, 'bool DiagnosticReadSession::isExpired() const')
    .replace('bool DiagnosticReadSession::isExpired() const', 'constexpr bool isExpired(uint32_t nowMs) const')
    .replaceAll('millis()', 'nowMs');
const cancelLogs = extract(logs, 'void DiagnosticReadSession::cancel(uint32_t id)')
    .replace('void DiagnosticReadSession::cancel(uint32_t id)', 'constexpr void cancel(uint32_t id, uint32_t nowMs)')
    .replaceAll('millis()', 'nowMs');
const cancelHistory = extract(history, 'void HistoryReadSession::cancel(uint32_t id)')
    .replace('void HistoryReadSession::cancel(uint32_t id)', 'constexpr void cancelHistory(uint32_t id)');
const matches = extract(logs, 'bool DiagnosticReadSession::matches(uint32_t token) const')
    .replace('bool DiagnosticReadSession::matches', 'constexpr bool matches');
const code = `#include <stdint.h>
struct session {
    bool active = false, running = false;
    uint32_t sessionId = 7, sessionToken = 42, lastUsedMs = 100;
    // Simula disponibilidad sin tareas ni copias pendientes; sin parametros.
    constexpr bool isAvailable() const { return !active && !running; }
    ${expired}
    ${cancelLogs}
    ${cancelHistory}
    ${matches}
};
// Verifica que callbacks viejos no cierren una respuesta nueva; sin parametros.
constexpr bool generations() {
    session value;
    value.active = true;
    value.cancel(6, 500);
    if (!value.active || value.lastUsedMs != 100) return false;
    value.cancelHistory(6);
    if (!value.active) return false;
    value.cancel(7, 500);
    if (value.active || value.lastUsedMs != 500) return false;
    value.active = true;
    value.cancelHistory(7);
    return !value.active;
}
static_assert(generations(), "Only the current response can cancel a reader");
static_assert(!session{}.isExpired(120100), "Cursor remains valid through the deadline");
static_assert(session{}.isExpired(120101), "Idle cursor expires after two minutes");
static_assert(!session{true, false}.isExpired(200000), "Active response cannot expire");
static_assert(!session{false, true}.isExpired(200000), "Pending NAND prevents expiration");
static_assert(session{false, false, 7, 42, UINT32_MAX - 100}.isExpired(120001), "TTL survives millis wrap");
static_assert(session{}.matches(42) && !session{}.matches(0) && !session{}.matches(41), "Cursor works without a retained snapshot");
`;
const output = '.pio/build/read-session-policy-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: vencimiento, wrap del reloj, cursor sin snapshot y cancelaciones de generaciones anteriores.');
