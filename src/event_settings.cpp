#include "event_settings.h"
#include "meter.h"
#include "storage/storage_manager.h"
#include "storage/wave_event_format.h"
#include <Preferences.h>
#include <string.h>
extern StorageManager storageManager;

namespace {
constexpr uint32_t settingsMagic = 0x45564331;
struct settingsRecord {
    uint32_t magic = settingsMagic, revision = 1;
    uint16_t version = 1, reserved = 0;
    eventSettingsData config;
    uint32_t crc = 0;
};
portMUX_TYPE settingsMutex = portMUX_INITIALIZER_UNLOCKED;
eventSettingsSnapshot current;
eventSettingsData pending;
uint32_t lastRevision = 1;
uint16_t bootPreMs = maxWavePreMs;
}

// Comprueba data sin modificarla ni persistir valores fuera de rango.
bool validEventSettings(const eventSettingsData& data) {
    const auto& point = data.points;
    return validGridSettings(data.grid) && isfinite(point.overCurrent) && point.overCurrent > 0 && point.overCurrent <= 163.82f &&
        isfinite(point.currentHysteresis) && point.currentHysteresis >= 0 && point.currentHysteresis < point.overCurrent &&
        point.dipConfirmMs == limitEventConfirmMs(point.dipConfirmMs) &&
        point.swellConfirmMs == limitEventConfirmMs(point.swellConfirmMs) &&
        point.overCurrentConfirmMs == limitEventConfirmMs(point.overCurrentConfirmMs) &&
        data.preMs >= 48 && data.preMs <= maxWavePreMs && data.preMs % 48 == 0 && data.waveMask <= 7;
}

// Identifica el primer campo que invalida la configuración completa; data contiene el candidato recibido.
const char* eventSettingsInvalidKey(const eventSettingsData& data) {
    if (validEventSettings(data)) return nullptr;
    const auto& grid = data.grid;
    const auto& point = data.points;
    const char* lowKeys[] = {"volt.lowR", "volt.lowS", "volt.lowT"};
    const char* highKeys[] = {"volt.highR", "volt.highS", "volt.highT"};
    if (!(grid.noVoltage >= 1 && grid.noVoltage <= 100)) return "volt.loss";
    if (!(grid.hysteresis >= 0 && grid.hysteresis <= 50)) return "volt.hyst";
    for (size_t i = 0; i < 3; ++i) {
        if (!(grid.phases[i].lowVoltage > grid.noVoltage + grid.hysteresis)) return lowKeys[i];
        if (!(grid.phases[i].highVoltage > grid.phases[i].lowVoltage + 2 * grid.hysteresis && grid.phases[i].highVoltage <= 500)) return highKeys[i];
    }
    if (grid.lossConfirmMs < 20 || grid.lossConfirmMs > 2000) return "time.loss";
    if (grid.voltageConfirmMs != eventPointLimitMs() || grid.capturePages != eventCapturePages) return "time.loss";
    if (grid.recoveryConfirmMs < 20 || grid.recoveryConfirmMs > 1000) return "time.back";
    if (!grid.neutral.enabled || !(grid.neutral.highVoltage >= 0.025f && grid.neutral.highVoltage <= 409.55f)) return "neut.high";
    if (!(grid.neutral.hysteresis >= 0 && grid.neutral.hysteresis < grid.neutral.highVoltage)) return "neut.hyst";
    if (grid.neutral.confirmMs < 20 || grid.neutral.confirmMs > 2000) return "neut.wait";
    if (grid.neutral.recoveryMs < 20 || grid.neutral.recoveryMs > 1000) return "neut.back";
    if (!isfinite(point.overCurrent) || point.overCurrent <= 0 || point.overCurrent > 163.82f) return "curr.high";
    if (!isfinite(point.currentHysteresis) || point.currentHysteresis < 0 || point.currentHysteresis >= point.overCurrent) return "curr.hyst";
    if (point.dipConfirmMs != limitEventConfirmMs(point.dipConfirmMs)) return "time.dip";
    if (point.swellConfirmMs != limitEventConfirmMs(point.swellConfirmMs)) return "time.swell";
    if (point.overCurrentConfirmMs != limitEventConfirmMs(point.overCurrentConfirmMs)) return "time.oc";
    if (data.preMs < 48 || data.preMs > maxWavePreMs || data.preMs % 48) return "wave.pre";
    if (data.waveMask > 7) return "wave.dip";
    return "config";
}

// Recupera ajustes antiguos en dest solo durante la migracion inicial, sin cambiarlos en NVS.
static void loadPreviousEventSettings(eventSettingsData& dest) {
    Preferences prefs;
    if (prefs.begin("grid", true)) {
        gridSettings grid;
        if (prefs.getBytesLength("settings") == sizeof(grid) && prefs.getBytes("settings", &grid, sizeof(grid)) == sizeof(grid) && validGridSettings(grid)) dest.grid = grid;
        prefs.end();
    }
    if (prefs.begin("meter", true)) {
        auto& point = dest.points;
        point.dipEnabled = prefs.getBool("dipEnabled", true); point.swellEnabled = prefs.getBool("swellEnabled", true);
        point.overCurrentEnabled = prefs.getBool("overcurrent", true);
        point.dipConfirmMs = limitEventConfirmMs(prefs.getInt("dipMs", 50));
        point.swellConfirmMs = limitEventConfirmMs(prefs.getInt("swellMs", 50));
        point.overCurrentConfirmMs = limitEventConfirmMs(prefs.getInt("ocMs", 10));
        point.overCurrent = prefs.getFloat("ocCurrent", 50); point.currentHysteresis = prefs.getFloat("ocHyst", 1);
        prefs.end();
        if (!validEventSettings(dest)) point = eventCaptureConfig{};
    }
    if (prefs.begin("wave", true)) {
        dest.preMs = prefs.getUShort("preMs", 96); dest.waveEnabled = prefs.getBool("saveEnabled", true);
        dest.waveMask = prefs.getUChar("pointMask", 7) & 7U; prefs.end();
        if (dest.preMs < 48 || dest.preMs > maxWavePreMs || dest.preMs % 48) dest.preMs = maxWavePreMs;
    }
}

// Carga el registro versionado antes de iniciar storage y MeterTask; sin parametros.
void eventSettingsBegin() {
    settingsRecord record{};
    Preferences prefs;
    bool found = false;
    if (prefs.begin("events", true)) {
        found = prefs.getBytesLength("cfg") == sizeof(record) && prefs.getBytes("cfg", &record, sizeof(record)) == sizeof(record) &&
            record.magic == settingsMagic && record.version == 1 && record.revision &&
            record.crc == waveEventFormat::crc32(&record, offsetof(settingsRecord, crc)) && validEventSettings(record.config);
        prefs.end();
    }
    if (!found) { record = settingsRecord{}; loadPreviousEventSettings(record.config); }
    current.config = record.config; current.revision = record.revision;
    lastRevision = record.revision;
    bootPreMs = record.config.preMs;
}

// Copia dest bajo una seccion critica breve; no abre Preferences.
void eventSettingsGet(eventSettingsSnapshot& dest) {
    portENTER_CRITICAL(&settingsMutex);
    dest = current; dest.restartRequired = current.config.preMs != bootPreMs;
    portEXIT_CRITICAL(&settingsMutex);
}

// Reserva un cambio completo data con control optimista de revision.
eventQueueResult eventSettingsQueue(const eventSettingsData& data, uint32_t revision, uint32_t* queuedRevision) {
    if (!validEventSettings(data)) return eventQueueResult::invalid;
    portENTER_CRITICAL(&settingsMutex);
    eventQueueResult result = eventQueueResult::accepted;
    if (revision != current.revision) result = eventQueueResult::conflict;
    else if (current.state == eventSaveState::waiting || current.state == eventSaveState::saving) result = eventQueueResult::busy;
    else {
        pending = data; if (!++lastRevision) ++lastRevision;
        current.nextRevision = lastRevision;
        if (queuedRevision) *queuedRevision = current.nextRevision;
        current.state = eventSaveState::waiting;
    }
    portEXIT_CRITICAL(&settingsMutex);
    return result;
}

// Guarda una sola clave NVS y aplica solo despues del exito; se llama desde el loop principal.
void eventSettingsPoll() {
    eventSettingsSnapshot snapshot; eventSettingsGet(snapshot);
    if (snapshot.state != eventSaveState::waiting || waveCaptureBusy()) return;
    GridEventStoreMetrics grid; storageManager.gridEvents().getMetrics(grid);
    if (grid.captureActive) return;
    settingsRecord record{};
    portENTER_CRITICAL(&settingsMutex);
    record.config = pending; record.revision = current.nextRevision; current.state = eventSaveState::saving;
    portEXIT_CRITICAL(&settingsMutex);
    record.crc = waveEventFormat::crc32(&record, offsetof(settingsRecord, crc));
    Preferences prefs;
    const bool opened = prefs.begin("events", false);
    settingsRecord stored{};
    const bool written = opened && prefs.putBytes("cfg", &record, sizeof(record)) == sizeof(record);
    const bool saved = written && prefs.getBytes("cfg", &stored, sizeof(stored)) == sizeof(stored) &&
        memcmp(&record, &stored, sizeof(record)) == 0;
    if (opened) prefs.end();
    if (saved) {
        meterQueueEventSettings(record.config.grid, record.config.points);
        storageManager.waveEvents().applyConfig(record.config.waveEnabled, record.config.waveMask);
    }
    portENTER_CRITICAL(&settingsMutex);
    if (saved) { current.config = record.config; current.revision = record.revision; current.nextRevision = 0; }
    current.state = saved ? eventSaveState::idle : eventSaveState::failed;
    portEXIT_CRITICAL(&settingsMutex);
}
