const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const store = fs.readFileSync('src/storage/grid_event_store.cpp', 'utf8');
const meter = fs.readFileSync('src/meter.cpp', 'utf8');
const main = fs.readFileSync('src/main.cpp', 'utf8');

// Extrae una funcion real por nombre; source contiene su codigo C++.
function method(source, name) {
    const start = source.indexOf(name + '('), body = source.indexOf('{', start);
    assert.ok(start >= 0 && body > start, name);
    let end = body + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return source.slice(start, end);
}

for (const file of ['src/grid_events.cpp', 'src/grid_events.h']) assert.equal(fs.existsSync(file), false);
assert.doesNotMatch(store + meter, /gridEventsStartupState|gridEventsPersistLoop|gridEventsReadyForStorage/);
assert.doesNotMatch(meter, /millis\(\) < 250/);
assert.match(meter, /while \(!meterAcquisitionReady.load\(\)\)/);
assert.ok(main.indexOf('timeInitClock();') < main.indexOf('meterInitAcquisition()'));
assert.ok(main.indexOf('meterInitAcquisition()') < main.indexOf('storageManager.begin('));
assert.ok(main.indexOf('bootScreenShow(lcd)') < main.indexOf('storageManager.begin('));
assert.ok(main.indexOf('meterInitServices();') > main.indexOf('WifiStart();'));
assert.ok(store.indexOf('if (!prepareStartupWrite()) return false;') < store.indexOf('mountedGridEventStore.store(this);'));
const startup = method(store, 'GridEventStore::startStartupCapture');
assert.match(startup, /candidate.previousState = 0;/);
assert.match(startup, /startupReasonMarker \| uint16_t\(reason\)/);
assert.doesNotMatch(startup, /gridChanged|phaseChanged|gridEventsStartupState/);
const harness = `
#include <cstdint>
#include <cstddef>
enum class phaseCondition : uint8_t {unknown, normal, low, high, loss};
enum class gridCondition : uint8_t {unknown, normal, abnormal, outage};
struct phaseState {phaseCondition state{}, pending{}; uint32_t changedAtMs{};};
struct meterValues {}; struct gridSettings {};
${store.match(/struct earlyGridState[^]*?\n};/)[0]}
struct NandIoResult {uintptr_t context = 5; bool success = true;};
constexpr int pdPASS = 1, completionQueue = 0;
constexpr uintptr_t eraseCompletionContext = 5;
constexpr uint32_t pdMS_TO_TICKS(uint32_t ms) {return ms;}
namespace GridEventFormat {constexpr uint8_t maxPagesPerEvent = 5;}
struct Capture {uint8_t pageCount = 0; bool erasePending = false;};
struct Writer {
    Capture capture; bool free = true, accepted = true, completed = true, success = true;
    bool blockHasData[1] = {true}; uint8_t currentBlock = 0;
    constexpr bool ensureWritableBlock() {capture.erasePending = !free && accepted; return free;}
    constexpr int xQueueReceive(int queue, NandIoResult* result, uint32_t timeout) {
        result->success = success; return completed ? pdPASS : 0;
    }
    constexpr bool ${method(store, 'GridEventStore::prepareStartupWrite').replace('GridEventStore::', '').replace('reinterpret_cast<uintptr_t>(result.context)', 'result.context')}
};
// Solo acepta destino libre o borrado completado; sin parametros.
constexpr bool eraseGates() {
    Writer free; if (!free.prepareStartupWrite() || free.capture.pageCount) return false;
    Writer erased; erased.free = false;
    if (!erased.prepareStartupWrite() || erased.blockHasData[0]) return false;
    Writer rejected; rejected.free = false; rejected.accepted = false;
    if (rejected.prepareStartupWrite()) return false;
    Writer timeout; timeout.free = false; timeout.completed = false;
    if (timeout.prepareStartupWrite()) return false;
    Writer failed; failed.free = false; failed.success = false;
    return !failed.prepareStartupWrite();
}
struct FakeStore {
    int seen = 0; uint32_t lastAt = 0, lost = 0;
    constexpr void observe(const phaseState&, const phaseState&, const phaseState&, gridCondition, float, float, float,
        const meterValues&, const gridSettings&, uint32_t at) {++seen; lastAt = at;}
    constexpr void recordDroppedStates(uint32_t count) {lost += count;}
};
struct Pointer {FakeStore* ptr = nullptr; constexpr FakeStore* load() {return ptr;}};
struct QueueTest {
    Pointer mountedGridEventStore; earlyGridState earlyStates[8]{};
    uint8_t earlyRead = 0, earlyCount = 0; uint32_t earlyDropped = 0;
    constexpr int memcmp(const uint8_t* a, const uint8_t* b, size_t count) {
        for (size_t i = 0; i < count; ++i) if (a[i] != b[i]) return 1; return 0;
    }
    constexpr void ${method(store, 'gridEventStoreObserve')}
};
// Evalua la cola real: desconocidos, deduplicacion, overflow y descarga ordenada; sin parametros.
constexpr bool queueCases() {
    QueueTest q; FakeStore sink; phaseState phase; phase.state = phase.pending = phaseCondition::normal;
    meterValues meter; gridSettings settings;
    q.gridEventStoreObserve(phase, phase, phase, gridCondition::unknown, 230, 230, 230, meter, settings, 1);
    if (q.earlyCount) return false;
    for (unsigned i = 0; i < 20; ++i) q.gridEventStoreObserve(phase, phase, phase, gridCondition::normal, 230, 230, 230, meter, settings, i + 2);
    if (q.earlyCount != 1 || q.earlyStates[0].sampleMs != 2) return false;
    for (unsigned i = 0; i < 12; ++i) {
        phase.state = phase.pending = i % 2 ? phaseCondition::normal : phaseCondition::loss;
        q.gridEventStoreObserve(phase, phase, phase, i % 2 ? gridCondition::normal : gridCondition::outage, 0, 0, 0, meter, settings, i + 30);
    }
    if (q.earlyCount != 8 || q.earlyDropped != 5) return false;
    q.mountedGridEventStore.ptr = &sink;
    q.gridEventStoreObserve(phase, phase, phase, gridCondition::normal, 230, 230, 230, meter, settings, 60);
    return !q.earlyCount && !q.earlyDropped && sink.lost == 5 && sink.seen == 9 && sink.lastAt == 60;
}
static_assert(eraseGates(), "Disponibilidad real del destino NAND");
static_assert(sizeof(earlyGridState) == 32, "Cola temprana de 256 bytes");
static_assert(queueCases(), "Estados tempranos conservan orden, tiempo y limites");
`;
const output = path.join('.pio', 'build', 'storage-startup-test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: sin NVS de estados, reloj/ADE tempranos, marca de arranque, cola real de 256 B y preparacion NAND.');
