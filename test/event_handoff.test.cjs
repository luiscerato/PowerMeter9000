const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'src/event_store.cpp'), 'utf8');

// Extrae una funcion del archivo actual; signature identifica su declaracion.
function functionText(signature) {
    const start = source.indexOf(signature);
    assert.ok(start >= 0, signature);
    const open = source.indexOf('{', start);
    let depth = 1, end = open + 1;
    while (depth && end < source.length) {
        if (source[end] === '{') depth++;
        if (source[end] === '}') depth--;
        end++;
    }
    return source.slice(start, end);
}

for (const name of ['eventStoreBeginEvent', 'eventStoreAddMeta', 'eventStoreServiceCapture',
    'eventStoreSetWaveUnavailable', 'eventStoreEndWave', 'eventStoreProcessWaveFrame']) {
    assert.doesNotMatch(functionText('void ' + name + '('), /lockEventStore\(|calc(?:Wave)?Crc|publishEventNotification\(/, name);
}
assert.doesNotMatch(functionText('bool eventStoreStartWave('), /lockEventStore\(|calc(?:Wave)?Crc|publishEventNotification\(/);
assert.doesNotMatch(functionText('static void finishWaveLocked('), /calcWaveCrc/);

// Se conserva el cuerpo de entrega; colas y liberaciones se simulan para evaluar su propiedad.
const service = functionText('void eventStoreServiceCapture(')
    .replace('void eventStoreServiceCapture()', 'constexpr void eventStoreServiceCapture(TestState& state)')
    .replace('PendingFastRms result = {};', `
        auto& activeCapture = state.active;
        auto& completionDrops = state.drops;
        const int fastRmsQueue = 1, completedEventQueue = 2;
        PendingFastRms result = {};`)
    .replaceAll('xQueueReceive(', 'queueReceive(state, ')
    .replaceAll('xQueueSend(', 'queueSend(state, ')
    .replaceAll('free(', 'freeBytes(state, ')
    .replaceAll('clearEventLocked(', 'clearData(state, ')
    .replace('delete oldest;', 'state.deleted++;');

const harness = `
#include <stdint.h>
#include <stddef.h>
constexpr int pdTRUE = 1;
constexpr size_t fastRmsSampleBytes = 16;
struct TestId {
    const char* value = "A";
    // Compara ids cortos de las fixtures; other contiene el id solicitado.
    constexpr bool operator!=(const char* other) const { return value[0] != other[0]; }
};
struct EventEntry {
    TestId eventId;
    uint8_t* fastRmsData = nullptr;
    size_t fastRmsSize = 0;
    uint32_t fastRmsCrc32 = 0;
    int32_t fastRmsTriggerSample = -1;
    bool fastRmsAvailable = false, fastRmsFinalized = false, waveFinalized = false;
    bool ready = false;
    const char* fastRmsReason = "";
};
struct PendingFastRms {
    char eventId[48] = {'A'};
    uint8_t* data = nullptr;
    size_t size = 0;
    int32_t triggerSample = -1;
    uint32_t crc32 = 0;
};
struct TestState {
    EventEntry* active = nullptr;
    EventEntry* events[2] = {};
    int count = 0;
    PendingFastRms rms;
    bool hasRms = false, waited = false, doubleFree = false;
    uint8_t* freed[8] = {};
    int freeCount = 0, deleted = 0, drops = 0;
};
// Registra free de p sin liberar fixtures de stack; s mantiene el historial.
constexpr void freeBytes(TestState& s, uint8_t* p) {
    if (!p) return;
    for (int i = 0; i < s.freeCount; i++) if (s.freed[i] == p) s.doubleFree = true;
    s.freed[s.freeCount++] = p;
}
// Simula limpiar entry y registra sus datos en s.
constexpr void clearData(TestState& s, EventEntry& entry) { freeBytes(s, entry.fastRmsData); entry.fastRmsData = nullptr; }
// Simula cierre Wave sin CRC para entry.
constexpr void finishWaveLocked(EventEntry& entry) { entry.waveFinalized = true; }
${functionText('static bool updateReadyLocked(').replace('static bool', 'constexpr bool').replace('entry.lastNotifyMs = 0;', '')}
// Recibe RMS en out sin esperar; queue identifica la cola y wait debe ser cero.
constexpr int queueReceive(TestState& s, int queue, PendingFastRms* out, int wait) {
    s.waited = s.waited || wait != 0;
    if (queue != 1 || !s.hasRms) return 0;
    *out = s.rms; s.hasRms = false; return 1;
}
// Recibe el evento mas antiguo en out; no libera ni copia sus buffers.
constexpr int queueReceive(TestState& s, int queue, EventEntry** out, int wait) {
    s.waited = s.waited || wait != 0;
    if (queue != 2 || !s.count) return 0;
    *out = s.events[0]; s.events[0] = s.events[1]; s.count--; return 1;
}
// Entrega el puntero in a la cola 2 sin copiar el evento ni esperar.
constexpr int queueSend(TestState& s, int queue, EventEntry** in, int wait) {
    s.waited = s.waited || wait != 0;
    if (queue != 2 || s.count == 2) return 0;
    s.events[s.count++] = *in; return 1;
}
${service}
// Prueba una entrega normal, un id vencido o falta de memoria; mode selecciona la variante.
constexpr bool normal(int mode) {
    EventEntry event;
    uint8_t data = 7;
    TestState s;
    s.active = &event; s.hasRms = true;
    s.rms.data = mode == 2 ? nullptr : &data;
    s.rms.size = 160; s.rms.triggerSample = 99; s.rms.crc32 = 123;
    if (mode == 1) s.rms.eventId[0] = 'B';
    eventStoreServiceCapture(s);
    if (mode == 1) return s.active == &event && s.count == 0 && s.freeCount == 1 && !s.waited;
    if (s.active || s.count != 1 || s.events[0] != &event || !event.ready) return false;
    if (event.fastRmsData != s.rms.data || event.fastRmsTriggerSample != -1 || s.freeCount) return false;
    eventStoreServiceCapture(s);
    if (s.count != 1) return false;
    clearData(s, *s.events[0]);
    return s.freeCount == (mode == 2 ? 0 : 1) && !s.doubleFree && !s.waited;
}
// La cola llena descarta solo el mas antiguo y conserva la propiedad de los otros; sin parametros.
constexpr bool overflow() {
    EventEntry old, newer, current;
    uint8_t oldData = 0;
    old.fastRmsData = &oldData;
    current.fastRmsFinalized = current.waveFinalized = true;
    TestState s;
    s.active = &current; s.events[0] = &old; s.events[1] = &newer; s.count = 2;
    eventStoreServiceCapture(s);
    return !s.active && s.events[0] == &newer && s.events[1] == &current &&
        s.count == 2 && s.freeCount == 1 && s.deleted == 1 && s.drops == 1 && !s.doubleFree && !s.waited;
}
static_assert(normal(0), "Entrega unica de los buffers");
static_assert(normal(1), "Id vencido libera su RMS");
static_assert(normal(2), "RMS sin memoria conserva metadata");
static_assert(overflow(), "Overflow descarta solo el mas antiguo");
`;
const output = path.join(root, '.pio/build/esp32doit-devkit-v1/event_handoff_test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: sin mutex/CRC/MQTT en captura; entrega, id vencido, sin memoria y overflow sin doble liberacion.');
