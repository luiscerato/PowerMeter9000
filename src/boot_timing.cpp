#include "boot_timing.h"

struct bootTimingEntry { uint32_t startUs = 0, endUs = 0; };
static bootTimingEntry entries[uint8_t(bootPhase::count)];
static portMUX_TYPE timingMutex = portMUX_INITIALIZER_UNLOCKED;
static const char* const phaseNames[] = {"clock", "settings", "meter", "screen", "nand", "history", "wifi", "ntp",
    "web", "wave", "scope", "battery", "firstFast", "firstState", "firstEventPage", "complete"};

// Abre la etapa phase; los hitos sin inicio explicito miden desde el arranque.
void bootTimingStart(bootPhase phase) {
    const auto index = uint8_t(phase);
    if (index >= uint8_t(bootPhase::count)) return;
    portENTER_CRITICAL(&timingMutex);
    if (!entries[index].endUs) entries[index].startUs = micros();
    portEXIT_CRITICAL(&timingMutex);
}

// Registra solo la primera finalizacion de phase; permite llamados desde adquisicion y setup.
void bootTimingEnd(bootPhase phase) {
    const auto index = uint8_t(phase);
    if (index >= uint8_t(bootPhase::count)) return;
    portENTER_CRITICAL(&timingMutex);
    if (!entries[index].endUs) entries[index].endUs = micros();
    portEXIT_CRITICAL(&timingMutex);
}

// Serializa una copia por etapa en json; atUs es el instante final y us la duracion de la etapa.
void bootTimingAppendJson(String& json) {
    json += "{\"unit\":\"us\"";
    for (uint8_t index = 0; index < uint8_t(bootPhase::count); ++index) {
        bootTimingEntry entry;
        portENTER_CRITICAL(&timingMutex);
        entry = entries[index];
        portEXIT_CRITICAL(&timingMutex);
        char buffer[104];
        snprintf(buffer, sizeof(buffer), ",\"%s\":{\"atUs\":%lu,\"us\":%lu,\"ready\":%s}", phaseNames[index],
            static_cast<unsigned long>(entry.endUs), static_cast<unsigned long>(entry.endUs ? entry.endUs - entry.startUs : 0),
            entry.endUs ? "true" : "false");
        json += buffer;
    }
    json += '}';
}
