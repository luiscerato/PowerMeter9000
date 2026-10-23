#pragma once
#include "grid_state.h"
#include "event_capture_config.h"

struct eventSettingsData {
    gridSettings grid;
    eventCaptureConfig points;
    uint16_t preMs = 192;
    uint8_t waveMask = 7;
    bool waveEnabled = true;
};
enum class eventSaveState : uint8_t { idle, waiting, saving, failed };
enum class eventQueueResult : uint8_t { accepted, conflict, busy, invalid };
struct eventSettingsSnapshot {
    eventSettingsData config;
    uint32_t revision = 1, nextRevision = 0;
    eventSaveState state = eventSaveState::idle;
};

// Valida data como conjunto, incluidos limites y relaciones entre parametros.
bool validEventSettings(const eventSettingsData& data);
// Lee la configuracion unica o importa ajustes anteriores sin escribirlos; sin parametros.
void eventSettingsBegin();
// Copia la configuracion confirmada y el estado de guardado a dest, sin acceder a NVS.
void eventSettingsGet(eventSettingsSnapshot& dest);
// Encola data si revision coincide; queuedRevision recibe el identificador reservado si no es nullptr.
eventQueueResult eventSettingsQueue(const eventSettingsData& data, uint32_t revision, uint32_t* queuedRevision = nullptr);
// Persiste y publica un conjunto completo fuera de adquisicion y HTTP; sin parametros.
void eventSettingsPoll();
