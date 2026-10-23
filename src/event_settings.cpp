#include "event_settings.h"
#include "meter.h"
#include "storage/storage_manager.h"
#include "storage/wave_event_format.h"
#include <Preferences.h>
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
}

// Comprueba data sin modificarla ni persistir valores fuera de rango.
bool validEventSettings(const eventSettingsData& data) {
    const auto& point = data.points;
    return validGridSettings(data.grid) && isfinite(point.overCurrent) && point.overCurrent > 0 && point.overCurrent <= 163.82f &&
        isfinite(point.currentHysteresis) && point.currentHysteresis >= 0 && point.currentHysteresis < point.overCurrent &&
        point.dipConfirmMs == limitEventConfirmMs(point.dipConfirmMs) &&
        point.swellConfirmMs == limitEventConfirmMs(point.swellConfirmMs) &&
        point.overCurrentConfirmMs == limitEventConfirmMs(point.overCurrentConfirmMs) &&
        data.preMs >= 48 && data.preMs <= 288 && data.preMs % 48 == 0 && data.waveMask <= 7;
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
        dest.preMs = prefs.getUShort("preMs", 192); dest.waveEnabled = prefs.getBool("saveEnabled", true);
        dest.waveMask = prefs.getUChar("pointMask", 7) & 7U; prefs.end();
        if (dest.preMs < 48 || dest.preMs > 288 || dest.preMs % 48) dest.preMs = 192;
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
}

// Copia dest bajo una seccion critica breve; no abre Preferences.
void eventSettingsGet(eventSettingsSnapshot& dest) {
    portENTER_CRITICAL(&settingsMutex); dest = current; portEXIT_CRITICAL(&settingsMutex);
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
    const bool saved = opened && prefs.putBytes("cfg", &record, sizeof(record)) == sizeof(record);
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
