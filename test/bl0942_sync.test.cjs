const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/bl0942/bl0942.cpp', 'utf8');
const header = fs.readFileSync('src/bl0942/bl0942.h', 'utf8');
const meter = fs.readFileSync('src/meter.cpp', 'utf8');
const finish = meter.match(/static void meterFinishFastRmsSample\(bool force\)\s*\{[\s\S]*?^\}/m)[0]
    .replace('static void ', 'constexpr void ').replace(/^.*meterTimingScope.*$/gm, '');

// Extrae un metodo real para evaluarlo con UART simulada; name es el nombre C++.
function method(name) {
    const start = source.search(new RegExp(`(?:bool|void|uint32_t) BL0942::${name}\\(`));
    assert.ok(start >= 0, name);
    let end = source.indexOf('{', start) + 1, depth = 1;
    while (depth) { if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end; }
    return 'constexpr ' + source.slice(start, end).replace('BL0942::', '')
        .replace(/^.*task(?:ENTER|EXIT)_CRITICAL.*$/gm, '');
}

const harness = `
#include <cstdint>
#include <cstddef>
#include <cmath>
using std::isfinite;
${header.slice(header.indexOf('struct bl0942Status'), header.indexOf('struct neutralCalibration'))}
${source.match(/^static constexpr.*$/gm).join('\n')}
struct Driver {
    bl0942Status state;
    struct { float rmsFactor = 1, fastFactor = 1; } calibration;
    uint32_t clockUs = 100, nextFastUs = 0, nextVoltageUs = 0, requestUs = 0;
    uint32_t lastServiceUs = 0, recoveryUntilUs = 0, initializationDueUs = 0;
    uint8_t initializationStep = 0, consecutiveFailures = 0, pendingRegister = 0;
    uint8_t response[4] = {}, responseLength = 0, rx[4] = {}, rxCount = 0, rxAt = 0;
    bool recovering = false, fastCyclePending = false, slowSlotOpen = false;
    float fastCycleVoltage = NAN, rmsFactor = 1, fastFactor = 1, voltageReference = 15873.35944299f;
    // Reloj controlado por el caso de prueba; sin parametros.
    constexpr uint32_t micros() const { return clockUs; }
    // Cantidad de bytes pendientes; sin parametros.
    constexpr int available() const { return rxCount - rxAt; }
    // Simula una UART libre; sin parametros.
    constexpr int availableForWrite() const { return 128; }
    // Extrae el siguiente byte simulado; sin parametros.
    constexpr int read() { return rx[rxAt++]; }
    // Acepta la transmision sin bloquear; data/size representan el comando.
    constexpr size_t write(const uint8_t* data, size_t size) { return size; }
    // La prueba no necesita consumidores concurrentes; sin parametros.
    constexpr void publishState() {}
    // La inicializacion se cubre en bl0942_retry; nowUs conserva su firma real.
    constexpr void pollInitialization(uint32_t nowUs) {}
    // Carga una respuesta; reg/raw determinan sus datos y checksum.
    constexpr void reply(uint8_t reg, uint32_t raw) {
        rxAt = 0; rxCount = 4;
        rx[0] = raw; rx[1] = raw >> 8; rx[2] = raw >> 16;
        rx[3] = uint8_t(~(readCommand + reg + rx[0] + rx[1] + rx[2]));
    }
    ${['retryInitialization', 'requestReg', 'recover', 'acceptResponse', 'loop', 'startFastSample', 'timeUntilPollUs'].map(method).join('\n')}
};
// Verifica que solo el ADE dispare Fast y que RMS comun use otro turno; sin parametros.
constexpr bool successAndSlow() {
    Driver d; d.state.ready = true;
    d.loop(true);
    if (d.state.fastRequests || !d.startFastSample()) return false;
    d.clockUs += 1000; d.reply(fastRmsRegister, 10000); d.rxCount = 2;
    d.loop(true);
    if (!d.fastCyclePending || d.state.fastReads) return false;
    d.clockUs += 1000; d.rxCount = 4; d.loop(true);
    if (d.fastCyclePending || d.state.fastReads != 1 || d.state.rmsRequests != 1) return false;
    d.clockUs += 1800; d.reply(currentRmsRegister, 20000); d.loop(true);
    return d.state.voltageReads == 1 && d.state.completeResponses == 2 && d.state.fastRequests == 1;
}
// Verifica vencimiento de 3 ms y recuperacion antes del proximo turno de 10 ms; sin parametros.
constexpr bool timeoutAndNext() {
    Driver d; d.state.ready = true; d.startFastSample();
    const uint32_t start = d.clockUs;
    d.clockUs = start + 2999; d.loop(true);
    if (!d.fastCyclePending) return false;
    d.clockUs = start + 3000; d.loop(true);
    if (d.fastCyclePending || d.state.timeouts != 1 || d.state.emptyTimeouts != 1) return false;
    d.clockUs = start + 10000;
    return d.startFastSample() && d.state.fastRequests == 2;
}
// Conserva respuestas completas atendidas tarde y vence las incompletas; sin parametros.
constexpr bool lateAndPartial() {
    Driver d; d.state.ready = true; d.startFastSample();
    d.clockUs += 5000; d.reply(fastRmsRegister, 100); d.loop(false);
    if (d.state.lateResponses != 1 || d.state.fastReads != 1 || d.state.partialResponses || d.state.timeouts) return false;
    d.clockUs += 10000; d.startFastSample();
    d.clockUs += 1000; d.reply(fastRmsRegister, 100); d.rxCount = 2; d.loop(true);
    d.clockUs += 2000; d.loop(true);
    return !d.fastCyclePending && d.state.partialResponses == 1 && d.state.emptyTimeouts == 0;
}
// Comprueba checksum, overflow de micros y ausencia de consultas Fast autonomas; sin parametros.
constexpr bool crcAndWrap() {
    Driver d; d.state.ready = true; d.clockUs = 0xfffffff0U; d.startFastSample();
    d.clockUs += 2000; d.reply(fastRmsRegister, 100); ++d.rx[3]; d.loop(true);
    if (d.fastCyclePending || d.state.checksumErrors != 1) return false;
    d.clockUs += 8000; if (!d.startFastSample()) return false;
    d.clockUs += 2000; d.reply(fastRmsRegister, 200); d.loop(false);
    if (d.state.fastReads != 1 || d.fastCyclePending) return false;
    d.clockUs += 50001; d.loop(false);
    return d.state.fastRequests == 2 && !std::isfinite(d.state.fastVoltage);
}
static_assert(successAndSlow(), "Fast ADE y RMS intercalada");
static_assert(timeoutAndNext(), "Timeout 3 ms sin perder proximo turno");
static_assert(lateAndPartial(), "UART completa atendida tarde no se descarta; parcial vence");
static_assert(crcAndWrap(), "Checksum, overflow y sin reloj Fast independiente");
// Los turnos ADE no deben interrumpir la deteccion a otros baudios; sin parametros.
constexpr bool preserveInitialization() {
    Driver d; d.pendingRegister = modeRegister; d.initializationStep = 9;
    d.requestUs = d.clockUs; d.clockUs += 1000;
    return !d.startFastSample() && d.pendingRegister == modeRegister && d.initializationStep == 9;
}
static_assert(preserveInitialization(), "ADE no cancela deteccion UART pendiente");
struct MeterCase {
    struct { float voltages[3] = {123, 124, 125}, currents[4] = {1, 2, 3, 4};
        uint32_t sampleMs = 1000, lastEventMs = 900; uint16_t timestamp = 65530;
        bool active = true; } pendingFastSample;
    struct { struct { float FastVrms = 0; } neutral; bool neutralFastValid = false; } meterVals;
    struct Bl {
        bool pending = true; float value = 222;
        // Expone el estado simulado del turno; sin parametros.
        constexpr bool isFastSamplePending() const { return pending; }
        // Expone el resultado simulado del turno; sin parametros.
        constexpr float getCycleFastVoltage() const { return value; }
    } bl0942;
    struct Events {
        // La captura secundaria no interviene en esta prueba; sin parametros.
        constexpr bool isCapturing() const { return false; }
        // Conserva la interfaz de captura; sin parametros.
        constexpr bool isSampleBufferFull() const { return false; }
        // Conserva la interfaz de cierre; sin parametros.
        constexpr void stopCapture() {}
        // Conserva la interfaz de copia; sample es la muestra preparada.
        constexpr void pushSamples(int* sample) {}
    } EventsData;
    int halfCycleSamples[1] = {}, halfCycleSamplesIndex = 0, meterUpdated = 0, pushes = 0;
    uint32_t storedMs = 0, observedMs = 0; uint16_t storedTimestamp = 0;
    float storedAde = 0;
    // Registra el instante observado; v/i son las lecturas originales y ms su tiempo.
    constexpr void meterApplyFastRms(const float* v, const float* i, uint32_t ms) {
        storedAde = v[0]; observedMs = ms;
    }
    // Registra la salida sin NAND; ms/timestamp deben conservarse a pesar de la demora UART.
    constexpr void meterFastDataSamplePush(uint32_t ms, uint16_t timestamp) {
        ++pushes; storedMs = ms; storedTimestamp = timestamp;
    }
    ${finish}
};
// Comprueba espera sin bloquear, instante ADE original y cierre unico; sin parametros.
constexpr bool pendingSnapshot() {
    MeterCase m; m.meterFinishFastRmsSample(false);
    if (m.pushes || !m.pendingFastSample.active) return false;
    m.bl0942.pending = false; m.meterFinishFastRmsSample(false);
    if (m.pushes != 1 || m.storedMs != 1000 || m.observedMs != 1000 ||
        m.storedTimestamp != 65530 || m.storedAde != 123 || m.meterVals.neutral.FastVrms != 222) return false;
    m.meterFinishFastRmsSample(false);
    return m.pushes == 1 && !m.pendingFastSample.active;
}
// Al llegar otro turno se conserva ADE y se marca BL ausente; sin parametros.
constexpr bool forcedSnapshot() {
    MeterCase m; m.meterFinishFastRmsSample(true);
    return m.pushes == 1 && m.storedAde == 123 && !m.meterVals.neutralFastValid &&
        !std::isfinite(m.meterVals.neutral.FastVrms);
}
static_assert(pendingSnapshot(), "Muestra ADE pendiente con fecha original y sin duplicados");
static_assert(forcedSnapshot(), "Cierre anterior conserva ADE y marca BL ausente");
`;
const output = path.join('.pio', 'build', 'bl0942-sync-test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: driver real, Fast disparada por ADE, RMS intercalada, limite 3 ms, UART tardia/parcial, CRC y overflow.');
