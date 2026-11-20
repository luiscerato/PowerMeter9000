const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/grid_event_store.cpp', 'utf8');
const header = fs.readFileSync('src/storage/grid_event_store.h', 'utf8');

// Extrae un bloque real delimitado por llaves; text/signature identifican su inicio.
function block(text, signature) {
    const start = text.indexOf(signature); if (start < 0) throw Error(signature);
    const open = text.indexOf('{', start); let end = open + 1, depth = 1;
    while (depth) { if (text[end] === '{') ++depth; if (text[end] === '}') --depth; ++end; }
    return text.slice(start, end);
}
// Convierte un metodo real en evaluable en compilacion; name/result describen su firma.
function method(name, result = 'void') {
    return block(source, `${result} GridEventStore::${name}(`).replace(`${result} GridEventStore::`, `constexpr ${result} `);
}
const format = fs.readFileSync('src/storage/grid_event_format.h', 'utf8').replace('#pragma once', '').replace('#include <Arduino.h>', '').replace(/inline /g, 'constexpr ');
const states = fs.readFileSync('src/grid_state.h', 'utf8').replace('#pragma once', '').replace(/inline /g, 'constexpr ');
const config = fs.readFileSync('src/event_capture_config.h', 'utf8').replace('#pragma once', '');
const detector = fs.readFileSync('src/fast_event_detector.h', 'utf8').replace('#pragma once', '').replace('void update(', 'constexpr void update(');
const harness = `
#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <cmath>
using std::isfinite;
using esp_reset_reason_t = uint8_t;
constexpr esp_reset_reason_t ESP_RST_POWERON = 1, ESP_RST_BROWNOUT = 9;
constexpr esp_reset_reason_t esp_reset_reason() {return 3;}
enum class bootPhase {firstEventPage};
// Sustituye solo la metrica de arranque; phase identifica el hito sin afectar la politica probada.
constexpr void bootTimingEnd(bootPhase phase) {}
${format}
${states}
${config}
${detector}
struct timeval { long tv_sec = 0, tv_usec = 0; };
struct phaseValues { float Vrms = 220, Irms = 1, FastIrms = 0; phaseState gridState; };
struct meterValues { phaseValues phaseR, phaseS, phaseT, neutral; };
struct NandIoResult { void* context = nullptr; bool success = true; };
constexpr int pdPASS = 1;
constexpr int xQueueReceive(int queue, NandIoResult* result, int wait) { return 0; }
struct StoreFormatGuard { constexpr StoreFormatGuard(bool paused, unsigned calls) {} constexpr operator bool() const { return true; } };
struct NandPartitionInfo {bool enabled = false; uint16_t blockCount = 2;};
struct WinbondSpiNand {static constexpr uint8_t pagesPerBlock = 64;};
struct NandPartition {
    static constexpr uint16_t invalidBlock = 65535;
    constexpr bool isEnabled() const {return true;}
    constexpr bool isUsableBlock(uint16_t block) const {return block < 2;}
    constexpr uint16_t nextUsableBlock(uint16_t block) const {return (block + 1) % 2;}
    constexpr void getInfo(NandPartitionInfo& info) const {info.enabled = false; info.blockCount = 2;}
};
struct Store {
    ${block(header, 'struct Candidate {')};
    ${block(header, 'struct Capture {')};
    ${block(header, 'struct pointTracker {')};
    ${block(header, 'struct deferredRecord {')};
    ${block(header, 'struct GridEventStoreMetrics {')};
    static constexpr uint32_t captureArmMs = 150, estimatedEpoch = 1767225600;
    static constexpr uintptr_t eraseCompletionContext = 5;
    bool formatPaused = false, blockHasData[64] = {}; unsigned activeCalls = 0, currentBlock = 0;
    uint8_t currentPage = 0;
    NandPartition partition;
    int completionQueue = 0;
    uint32_t nowMs = 0, nextEventId = 1, armedThroughMs = 0;
    Capture capture;
    Candidate candidates[4] = {};
    pointTracker points[10] = {};
    uint32_t currentStartedMs[4] = {}, observedStartedMs[4] = {};
    uint8_t observedStates[4] = {1,1,1,1};
    float observedVoltages[3] = {220,220,220};
    deferredRecord deferred[8]; uint8_t deferredRead = 0, deferredCount = 0;
    GridEventStoreMetrics storeMetrics;
    GridEventFormat::ListHeader written[16] = {};
    unsigned writtenCount = 0, finished = 0;
    // Simula reloj y transporte; millis y timestamps conservan el mismo origen monotono.
    constexpr uint32_t millis() const { return nowMs; }
    constexpr uint64_t meterFastTimeUs(uint32_t ms) const { return uint64_t(ms) * 1000; }
    constexpr uint64_t meterUnixTimeUs(uint64_t us) const { return uint64_t(estimatedEpoch) * 1000000 + us; }
    constexpr void waveCaptureTrigger(uint64_t us) const {}
    constexpr uint64_t managerNowMs() const { return uint64_t(estimatedEpoch) * 1000 + nowMs; }
    constexpr void gettimeofday(timeval* now, void* unused) const { now->tv_sec = estimatedEpoch; }
    // Abre RAM sin NAND; candidate/sampleMs fijan contexto y ventana originales.
    constexpr void armCapture(const Candidate& candidate, uint32_t sampleMs) {
        armedThroughMs = sampleMs;
        capture = Capture{}; capture.active = true; capture.pageCount = 5; capture.eventId = nextEventId;
        capture.startedMs = candidate.startedMs; capture.endMs = candidate.startedMs + 4720;
        capture.closeAfterMs = capture.endMs; capture.rootSampleCount = 113;
        capture.firstSampleKnown = true; capture.firstSampleMs = candidate.startedMs - 1130;
        capture.settings = candidate.settings; capture.meter = candidate.meter;
    }
    // Simula terminacion y libera asociacion; success indica si todas las paginas se escribieron.
    constexpr void finishCapture(bool success) { limitPoints(); if (success) {++finished; ++nextEventId;} capture.active = false; }
    constexpr bool ensureWritableBlock() { return true; }
    constexpr void prepareRootPage() { capture.root.sampleCount = 113; capture.root.pageCount = 5; }
    constexpr void prepareContinuationPages() {}
    // Completa NAND inmediatamente para probar politica; page identifica el buffer inmutable.
    constexpr bool submitPage(uint8_t page) {
        if (!capture.pageWritten[page]) {
            capture.pageWritten[page] = true;
            if (!page) {capture.rootWritten = true; written[writtenCount++] = capture.root;}
        }
        return true;
    }
    // Sustituye solo las lecturas de contexto; dest/settings o meter mantienen sus firmas.
    static constexpr void snapshotSettings(GridEventFormat::SettingsSnapshot& dest, const gridSettings& settings) {}
    static constexpr void snapshotMeter(GridEventFormat::MeterSnapshot& dest, const meterValues& meter) {}
    // Simula paginas borradas; header/valid/erased son salidas de recuperacion.
    constexpr bool readHeader(uint16_t block, uint8_t page, GridEventFormat::ListHeader& header, bool& valid, bool& erased) {
        valid = false; erased = true; return true;
    }
    static constexpr uint16_t encodeVoltage(float value) {return isfinite(value) ? uint16_t(value * 100 + .5f) : 65535;}
    ${method('encodeDuration10Ms', 'uint32_t')}
    ${method('appendRecord', 'bool')}
    ${method('addTransition')}
    ${method('confirmCandidate')}
    ${method('startStartupCapture')}
    ${method('startCandidate')}
    ${method('discardCandidate')}
    ${method('observePhase')}
    ${method('observePoint').replace('uint32_t detectedMs, uint32_t recoveredMs)', 'uint32_t detectedMs = UINT32_MAX, uint32_t recoveredMs = UINT32_MAX)')}
    ${method('finishPoint')}
    ${method('limitPoints')}
    ${method('hasPendingCandidatesInWindow', 'bool')}
    ${method('hasPendingCandidatesInPage', 'bool')}
    ${method('poll')}
    ${method('isNewerEventId','bool')}
    ${method('recoverCursor','bool').replace('memset(blockHasData, 0, sizeof(blockHasData));','for (unsigned i = 0; i < 64; ++i) blockHasData[i] = false;').replace(/GridEventFormat::ListHeader header;/g,'GridEventFormat::ListHeader header = {};')}
};
struct Scenario {
    Store store; meterValues meter; gridSettings settings; eventCaptureConfig config; fastEventDetector dip, swell, oc;
    constexpr Scenario() { meter.phaseR.gridState.state = meter.phaseR.gridState.pending = phaseCondition::normal; }
    // Recorre muestras reales de politica; voltage/current se mantienen entre from/to cada 10 ms.
    constexpr void feed(float voltage, float current, uint32_t from, uint32_t to) {
        for (uint32_t ms = from; ms <= to; ms += 10) {
            store.nowMs = ms;
            meter.phaseR.FastIrms = current;
            updatePhaseState(meter.phaseR.gridState, voltage, ms, settings.phases[0], settings);
            store.observePhase(0, meter.phaseR.gridState, voltage, meter, settings, ms);
            dip.update(voltage, settings.phases[0].lowVoltage, config.dipConfirmMs, true, true, ms, settings.hysteresis, settings.recoveryConfirmMs);
            swell.update(voltage, settings.phases[0].highVoltage, config.swellConfirmMs, false, true, ms, settings.hysteresis, settings.recoveryConfirmMs);
            oc.update(current, config.overCurrent, config.overCurrentConfirmMs, false, true, ms, config.currentHysteresis, settings.recoveryConfirmMs);
            store.observePoint(0, dip.active, dip.extreme, meter, config, settings, ms, dip.startedMs, dip.recovering || !dip.active ? dip.endedMs : UINT32_MAX);
            store.observePoint(3, swell.active, swell.extreme, meter, config, settings, ms, swell.startedMs, swell.recovering || !swell.active ? swell.endedMs : UINT32_MAX);
            store.observePoint(6, oc.active, oc.extreme, meter, config, settings, ms, oc.startedMs, oc.recovering || !oc.active ? oc.endedMs : UINT32_MAX);
            store.poll();
        }
    }
};
// Recupera el estado temprano sin perder el intervalo que ya esta en el anillo; sin parametros.
constexpr bool startupBackfill() {
    Store store; meterValues meter; gridSettings settings;
    phaseState phase; phase.state = phase.pending = phaseCondition::normal;
    const phaseState* phases[] = {&phase,&phase,&phase};
    const float voltages[] = {230,230,230};
    store.nowMs = 930;
    store.startStartupCapture(phases,gridCondition::normal,voltages,meter,settings,600);
    return store.capture.startedMs == 600 && store.capture.rootStartedMs == 600 && store.armedThroughMs == 930 &&
        store.capture.settings.reserved == (GridEventFormat::startupReasonMarker | 3) && store.capture.transitionCount == 3;
}
static_assert(startupBackfill(),"Arranque conserva su fecha y recupera hasta la muestra actual");
// Un DIP cerrado no escribe al iniciar y conserva duracion fisica sin sumar recuperacion; sin parametros.
constexpr bool shortDip() {
    Scenario s; s.feed(150, 0, 1000, 1490);
    if (s.store.writtenCount) return false;
    s.feed(220, 0, 1500, 2600);
    return s.store.writtenCount == 1 && s.store.finished == 1 && GridEventFormat::isPointEvent(s.store.written[0].subjectAndStates) &&
        s.store.written[0].previousStateDuration10Ms == 50 && s.store.storeMetrics.completedPoints == 1;
}
// Una condicion persistente sustituye al DIP pendiente, incluso deshabilitado; sin parametros.
constexpr bool sustainedVoltage() {
    Scenario s; s.config.dipEnabled = false;
    s.feed(150, 0, 1000, 4800);
    if (s.store.writtenCount != 1 || GridEventFormat::isPointEvent(s.store.written[0].subjectAndStates)) return false;
    if ((s.store.written[0].subjectAndStates >> 6 & 7) != 2 || s.store.storeMetrics.completedPoints) return false;
    s.feed(150, 0, 4810, 6500); s.feed(220, 0, 6510, 8000);
    return s.store.writtenCount == 2 && (s.store.written[1].subjectAndStates >> 6 & 7) == 1 &&
        s.store.written[1].previousStateDuration10Ms == 551;
}
// Caida inicial de 2 s seguida de corte: solo perdida de fase, captura iniciada antes; sin parametros.
constexpr bool dipToOutage() {
    Scenario s; s.feed(150, 0, 1000, 2990); s.feed(0, 0, 3000, 3400);
    return s.store.writtenCount == 1 && !GridEventFormat::isPointEvent(s.store.written[0].subjectAndStates) &&
        (s.store.written[0].subjectAndStates >> 6 & 7) == 4 && s.store.capture.startedMs == 1000;
}
// Deshabilitar puntuales no impide inicio/retorno de alta corriente; sin parametros.
constexpr bool sustainedCurrent() {
    Scenario s; s.config.overCurrentEnabled = false;
    s.feed(220, 80, 1000, 6000); s.feed(220, 0, 6010, 7500);
    return s.store.writtenCount == 2 && s.store.written[0].subjectAndStates == GridEventFormat::packCurrentState(GridEventFormat::Subject::phaseR, true) &&
        s.store.written[1].subjectAndStates == GridEventFormat::packCurrentState(GridEventFormat::Subject::phaseR, false) &&
        s.store.written[1].previousStateDuration10Ms == 501 && s.store.storeMetrics.completedPoints == 0;
}
// Un transitorio menor al minimo y un puntual deshabilitado no ocupan NAND; sin parametros.
constexpr bool discarded() {
    Scenario s; s.feed(150, 0, 1000, 1020); s.feed(220, 0, 1030, 2500);
    Scenario off; off.config.dipEnabled = false; off.feed(150, 0, 1000, 1500); off.feed(220, 0, 1510, 3000);
    return !s.store.writtenCount && !off.store.writtenCount && !off.store.capture.active;
}
static_assert(shortDip(), "DIP completo: RAM, duracion y posttrigger");
static_assert(sustainedVoltage(), "Estado obligatorio, no DIP duplicado y retorno");
static_assert(dipToOutage(), "Promocion a corte conserva el inicio");
static_assert(sustainedCurrent(), "Alta corriente obligatoria y recuperacion");
static_assert(discarded(), "Descartar candidatos invalidos o puntuales deshabilitados");
// Un corte de 2,77 s conserva el retorno dentro de la ventana original; sin parametros.
constexpr bool outageKeepsRecovery() {
    Scenario s; s.feed(0, 0, 1000, 3760);
    if (!s.store.capture.active || s.store.writtenCount != 1 || s.store.finished) return false;
    s.feed(220, 0, 3770, 4500);
    bool recovered = false;
    for (unsigned index = 0; index < s.store.capture.transitionCount; ++index) {
        const auto& record = s.store.capture.transitions[index];
        recovered |= (record.subjectAndStates >> 6 & 7) == 1 && record.previousStateDuration10Ms == 277;
    }
    if (!recovered || s.store.writtenCount != 1 || s.store.finished) return false;
    s.feed(220, 0, 4510, 5800);
    return s.store.writtenCount == 1 && s.store.finished == 1;
}
static_assert(outageKeepsRecovery(), "Caida y retorno comparten toda la ventana disponible");
// Agrupa dos cortes y retornos sin crear nuevas raices dentro de la ventana; sin parametros.
constexpr bool repeatedOutages() {
    Scenario s; s.feed(0, 0, 1000, 1590); s.feed(220, 0, 1600, 2290);
    s.feed(0, 0, 2300, 2890); s.feed(220, 0, 2900, 5800);
    return s.store.writtenCount == 1 && s.store.finished == 1 && s.store.capture.transitionCount == 3 &&
        s.store.capture.transitions[0].offsetMs == 600 && s.store.capture.transitions[1].offsetMs == 1300 &&
        s.store.capture.transitions[2].offsetMs == 1900 && !s.store.storeMetrics.droppedRecords;
}
// Una raiz de neutro tambien conserva el retorno de fase como transicion; sin parametros.
constexpr bool neutralRootKeepsPhaseRecovery() {
    Scenario s; Store::Candidate c; c.active = true; c.subject = GridEventFormat::Subject::neutral;
    c.previousState = 1; c.currentState = 3; c.startedMs = 1000;
    s.store.nowMs = 1100; s.store.confirmCandidate(c, 0, 1100);
    s.feed(0, 0, 1010, 3760); s.feed(220, 0, 3770, 5800);
    return s.store.writtenCount == 1 && s.store.finished == 1 && s.store.capture.transitionCount == 2 &&
        (s.store.capture.transitions[1].subjectAndStates >> 6 & 7) == 1;
}
static_assert(repeatedOutages(), "Varios cortes quedan juntos con sus retornos");
static_assert(neutralRootKeepsPhaseRecovery(), "Raiz de neutro con transiciones de fase completas");
// El retorno justo en el limite no se transforma en un estado durante el debounce; sin parametros.
constexpr bool exactRecovery() {
    Scenario s; s.feed(150, 80, 1000, 4690); s.feed(220, 0, 4700, 6000);
    return s.store.writtenCount == 1 && GridEventFormat::isPointEvent(s.store.written[0].subjectAndStates) &&
        s.store.storeMetrics.completedPoints == 2 && s.store.storeMetrics.limitedPoints == 0;
}
static_assert(exactRecovery(), "Limite puntual y recuperacion coincidentes");
// Swell y alto voltaje usan el mismo umbral sin duplicar registros; sin parametros.
constexpr bool swellAndHigh() {
    Scenario shortEvent; shortEvent.feed(270,0,1000,1500); shortEvent.feed(220,0,1510,3000);
    if (shortEvent.store.writtenCount != 1 || (shortEvent.store.written[0].subjectAndStates >> 10 & 31) != 2) return false;
    Scenario longEvent; longEvent.config.swellEnabled = false; longEvent.feed(270,0,1000,6000);
    return longEvent.store.writtenCount == 1 && !GridEventFormat::isPointEvent(longEvent.store.written[0].subjectAndStates) &&
        (longEvent.store.written[0].subjectAndStates >> 6 & 7) == 3;
}
// Descriptores posteriores se difieren, no se recortan a int16 ni desaparecen; sin parametros.
constexpr bool deferredAndFull() {
    Store s; Store::Candidate c; c.active = true; c.startedMs = 1000; c.previousState = 1; c.currentState = 2;
    s.nowMs = 1000; s.confirmCandidate(c,0,1000);
    c.startedMs = 6000; c.previousState = 2; c.currentState = 4;
    s.confirmCandidate(c,1000,6000);
    if (s.deferredCount != 1) return false;
    s.nowMs = 6500; s.poll(); s.poll(); s.nowMs = 7500; s.poll();
    if (s.writtenCount != 2 || s.written[1].previousStateDuration10Ms != 500) return false;
    Store full; c.startedMs = 1000; full.confirmCandidate(c,0,1000);
    for (unsigned i = 0; i < 9; ++i) {c.startedMs = 7000 + i * 1000; full.confirmCandidate(c,0,7000);}
    return full.deferredCount == 8 && full.storeMetrics.droppedRecords == 1;
}
// El retorno despues de 99 s genera otra captura y conserva toda la duracion; sin parametros.
constexpr bool longReturn() {
    Scenario s; s.feed(150,0,1000,6000); s.feed(150,0,100000,100000); s.feed(220,0,100010,101500);
    return s.store.writtenCount == 2 && s.store.written[1].previousStateDuration10Ms == 9901;
}
static_assert(swellAndHigh(), "Swell o alto voltaje, habilitacion solo puntual");
static_assert(deferredAndFull(), "Cola de descriptores y overflow visible");
static_assert(longReturn(), "Estado largo sin retener buffers indefinidamente");
// El propietario puede reabrir tras borrar aunque la vista publica siga bloqueada; sin parametros.
constexpr bool recoverDuringFormat() {
    Store s; NandPartitionInfo info; s.partition.getInfo(info);
    return !info.enabled && s.recoverCursor() && s.nextEventId == 1 && s.currentPage == 0;
}
static_assert(recoverDuringFormat(), "Reapertura de particion durante formato");
static_assert(sizeof(GridEventFormat::ListHeader) == 30 && sizeof(GridEventFormat::Transition) == 12 && sizeof(GridEventFormat::SettingsSnapshot) == 48, "Formato compacto fijo");
static_assert(offsetof(GridEventFormat::SettingsSnapshot, flags) == 29, "Ajustes sin duplicacion");
static_assert(eventPointLimitMs() == 3700, "Margen temporal coherente");
`;
const output = '.pio/build/incident-policy-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Isrc', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: metodos reales, DIP completo, bajo voltaje, corte, alta corriente, retorno, habilitaciones y 30/48/12 bytes.');
