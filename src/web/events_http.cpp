#include "events_http.h"
#include <ESPAsyncWebServer.h>
#include <esp_system.h>
#include <errno.h>
#include "../event_settings.h"
#include "../storage/storage_manager.h"
#include "../../base/json_writer.h"
extern StorageManager storageManager;

namespace {
enum class fieldType : uint8_t { number, time, toggle, pre, mask };
struct configField {
    const char* key; uint16_t offset; fieldType type; uint8_t flagBit;
    // Describe key en offset con su tipo kind; mask indica el bit si es una habilitacion empaquetada.
    constexpr configField(const char* key, uint16_t offset, fieldType kind, uint8_t mask = 0) : key(key), offset(offset), type(kind), flagBit(mask) {}
};
#define GRID(member) offsetof(eventSettingsData, grid) + offsetof(gridSettings, member)
#define POINT(member) offsetof(eventSettingsData, points) + offsetof(eventCaptureConfig, member)
#define PHASE(index, member) GRID(phases) + index * sizeof(phaseLimits) + offsetof(phaseLimits, member)
#define NEUT(member) GRID(neutral) + offsetof(neutralSettings, member)
const configField fields[] = {
    {"volt.lowR", PHASE(0, lowVoltage), fieldType::number}, {"volt.lowS", PHASE(1, lowVoltage), fieldType::number},
    {"volt.lowT", PHASE(2, lowVoltage), fieldType::number}, {"volt.highR", PHASE(0, highVoltage), fieldType::number},
    {"volt.highS", PHASE(1, highVoltage), fieldType::number}, {"volt.highT", PHASE(2, highVoltage), fieldType::number},
    {"volt.loss", GRID(noVoltage), fieldType::number}, {"volt.hyst", GRID(hysteresis), fieldType::number},
    {"time.dip", POINT(dipConfirmMs), fieldType::time}, {"time.swell", POINT(swellConfirmMs), fieldType::time},
    {"time.oc", POINT(overCurrentConfirmMs), fieldType::time}, {"time.loss", GRID(lossConfirmMs), fieldType::time},
    {"time.back", GRID(recoveryConfirmMs), fieldType::time},
    {"neut.high", NEUT(highVoltage), fieldType::number}, {"neut.hyst", NEUT(hysteresis), fieldType::number},
    {"neut.wait", NEUT(confirmMs), fieldType::time}, {"neut.back", NEUT(recoveryMs), fieldType::time},
    {"curr.high", POINT(overCurrent), fieldType::number}, {"curr.hyst", POINT(currentHysteresis), fieldType::number},
    {"rec.dip", POINT(dipEnabled), fieldType::toggle}, {"rec.swell", POINT(swellEnabled), fieldType::toggle},
    {"rec.oc", POINT(overCurrentEnabled), fieldType::toggle},
    {"wave.on", offsetof(eventSettingsData, waveEnabled), fieldType::toggle},
    {"wave.pre", offsetof(eventSettingsData, preMs), fieldType::pre},
    {"wave.dip", offsetof(eventSettingsData, waveMask), fieldType::mask, 1},
    {"wave.swell", offsetof(eventSettingsData, waveMask), fieldType::mask, 2},
    {"wave.oc", offsetof(eventSettingsData, waveMask), fieldType::mask, 4}
};
static_assert(sizeof(fields) / sizeof(fields[0]) <= 32, "Field bitmap capacity exceeded");
#undef GRID
#undef POINT
#undef PHASE
#undef NEUT
const char* groups[] = {"volt", "time", "neut", "curr", "rec", "wave"};

// Nombra state con etiquetas cortas para que el cliente distinga espera, guardado y fallo.
const char* saveName(eventSaveState state) {
    const char* names[] = {"ok", "wait", "save", "fail"};
    return names[uint8_t(state)];
}

// Envia json solo si cabe completo; request/code describen la respuesta y json contiene el documento.
void sendJson(AsyncWebServerRequest* request, int code, const JsonWriter& json) {
    if (!json.valid()) { request->send(500, "application/json", "{\"error\":\"response_overflow\"}"); return; }
    auto* response = request->beginResponse(code, "application/json", json.c_str());
    response->addHeader("Cache-Control", "no-store"); request->send(response);
}

// Publica error JSON; request/code/error definen respuesta y key es el campo rechazado si se conoce.
void sendError(AsyncWebServerRequest* request, int code, const char* error, const char* key = nullptr) {
    char buffer[256]; JsonWriter json(buffer, sizeof(buffer));
    json.objBegin(); json.objAppendString("error", error);
    if (key) json.objAppendString("key", key);
    json.objEnd(); sendJson(request, code, json);
}

// Interpreta text como numero finito, sin aceptar espacios iniciales ni sufijos.
bool parseNumber(const String& text, double& value) {
    if (!text.length() || !((text[0] >= '0' && text[0] <= '9') || text[0] == '-')) return false;
    char* end = nullptr; errno = 0; value = strtod(text.c_str(), &end);
    return end != text.c_str() && !*end && errno != ERANGE && isfinite(value);
}

// Agrega a json el estado de persistencia de settings, sin cambiar su revision efectiva.
void appendSave(JsonWriter& json, const eventSettingsSnapshot& settings) {
    json.objBegin("save"); json.objAppendString("state", saveName(settings.state));
    json.objAppendUInt("rev", settings.nextRevision); json.objEnd();
}

// Serializa settings con buffer externo; group filtra un grupo o nullptr incluye todos.
void sendConfig(AsyncWebServerRequest* request, const eventSettingsSnapshot& settings, const char* group) {
    char buffer[1024]; JsonWriter json(buffer, sizeof(buffer));
    json.objBegin(); json.objAppendUInt("rev", settings.revision); appendSave(json, settings);
    json.objAppendBool("restartRequired", settings.restartRequired);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&settings.config);
    for (const char* name : groups) {
        if (group && strcmp(group, name)) continue;
        json.objBegin(name);
        for (const auto& field : fields) {
            const char* dot = strchr(field.key, '.');
            if (size_t(dot - field.key) != strlen(name) || strncmp(field.key, name, strlen(name))) continue;
            const auto* source = bytes + field.offset; const char* key = dot + 1;
            switch (field.type) {
                case fieldType::number: {
                    char number[24];
                    snprintf(number, sizeof(number), "%.9g", double(*reinterpret_cast<const float*>(source)));
                    json.objAppendRaw(key, number); break;
                }
                case fieldType::time: json.objAppendUInt(key, *reinterpret_cast<const uint32_t*>(source)); break;
                case fieldType::toggle: json.objAppendBool(key, *reinterpret_cast<const bool*>(source)); break;
                case fieldType::pre: json.objAppendUInt(key, *reinterpret_cast<const uint16_t*>(source)); break;
                case fieldType::mask: json.objAppendBool(key, (*source & field.flagBit) != 0); break;
            }
        }
        json.objEnd();
    }
    json.objEnd(); sendJson(request, 200, json);
}

// Valida todos los parametros POST de request sobre una copia; solo encola tras completar la validacion.
void postConfig(AsyncWebServerRequest* request, const eventSettingsSnapshot& current) {
    if (!request->hasParam("rev", true)) { sendError(request, 400, "rev_required"); return; }
    double revision = 0;
    if (!parseNumber(request->getParam("rev", true)->value(), revision) || revision < 1 || revision > UINT32_MAX || floor(revision) != revision) {
        sendError(request, 400, "invalid_rev"); return;
    }
    eventSettingsData config = current.config;
    uint32_t seen = 0; bool hasRevision = false;
    for (size_t index = 0; index < request->params(); ++index) {
        const auto* param = request->getParam(index);
        if (!param->isPost()) { sendError(request, 400, "form_only"); return; }
        if (param->name() == "rev") {
            if (hasRevision) { sendError(request, 400, "duplicate_field"); return; }
            hasRevision = true; continue;
        }
        uint8_t fieldIndex = 0;
        for (; fieldIndex < sizeof(fields) / sizeof(fields[0]); ++fieldIndex) if (param->name() == fields[fieldIndex].key) break;
        if (fieldIndex == sizeof(fields) / sizeof(fields[0])) { sendError(request, 400, "unknown_field", param->name().c_str()); return; }
        if (seen & (1UL << fieldIndex)) { sendError(request, 400, "duplicate_field", fields[fieldIndex].key); return; }
        seen |= 1UL << fieldIndex;
        const auto& field = fields[fieldIndex]; double value = 0;
        if (!parseNumber(param->value(), value) || value < 0 || value > UINT32_MAX ||
            (field.type != fieldType::number && floor(value) != value) ||
            ((field.type == fieldType::toggle || field.type == fieldType::mask) && value > 1) ||
            (field.type == fieldType::pre && value > UINT16_MAX)) { sendError(request, 400, "invalid_value", field.key); return; }
        auto* dest = reinterpret_cast<uint8_t*>(&config) + field.offset;
        switch (field.type) {
            case fieldType::number: *reinterpret_cast<float*>(dest) = value; break;
            case fieldType::time: *reinterpret_cast<uint32_t*>(dest) = value; break;
            case fieldType::toggle: *reinterpret_cast<bool*>(dest) = value != 0; break;
            case fieldType::pre: *reinterpret_cast<uint16_t*>(dest) = value; break;
            case fieldType::mask: *dest = value ? *dest | field.flagBit : *dest & ~field.flagBit; break;
        }
    }
    if (!seen) { sendError(request, 400, "fields_required"); return; }
    const char* invalidKey = eventSettingsInvalidKey(config);
    if (invalidKey) { sendError(request, 400, "invalid_config", invalidKey); return; }
    uint32_t next = 0;
    const auto result = eventSettingsQueue(config, uint32_t(revision), &next);
    if (result != eventQueueResult::accepted) {
        sendError(request, result == eventQueueResult::invalid ? 400 : 409,
            result == eventQueueResult::invalid ? "invalid_config" : result == eventQueueResult::busy ? "save_busy" : "rev_conflict"); return;
    }
    eventSettingsSnapshot queued; eventSettingsGet(queued);
    char buffer[128]; JsonWriter json(buffer, sizeof(buffer));
    json.objBegin(); json.objAppendBool("ok", true); json.objAppendUInt("rev", next); appendSave(json, queued); json.objEnd();
    sendJson(request, 202, json);
}

// Atiende request en config, usando exclusivamente parametros POST para guardar.
void handleConfig(AsyncWebServerRequest* request) {
    if (request->url() != "/events/config") { sendError(request, 404, "not_found"); return; }
    eventSettingsSnapshot settings; eventSettingsGet(settings);
    if (request->method() == HTTP_POST) { postConfig(request, settings); return; }
    const char* group = nullptr;
    if (request->params()) {
        if (request->params() != 1 || !request->hasParam("group")) { sendError(request, 400, "invalid_query"); return; }
        for (const char* name : groups) if (request->getParam("group")->value() == name) group = name;
        if (!group) { sendError(request, 400, "unknown_group"); return; }
    }
    sendConfig(request, settings, group);
}

// Publica cambios relevantes de estado; request puede incluir since para recibir 204 sin cambios.
void handleStatus(AsyncWebServerRequest* request) {
    if (request->url() != "/events/status") { sendError(request, 404, "not_found"); return; }
    double since = 0;
    if (request->params() && (request->params() != 1 || !request->hasParam("since") ||
        !parseNumber(request->getParam("since")->value(), since) || since < 0 || since > UINT32_MAX || floor(since) != since)) {
        sendError(request, 400, "invalid_since"); return;
    }
    eventSettingsSnapshot settings; eventSettingsGet(settings);
    GridEventStoreMetrics grid; storageManager.gridEvents().getMetrics(grid);
    waveStoreMetrics wave; storageManager.waveEvents().getMetrics(wave);
    uint64_t fastEpoch = 0, waveEpoch = 0;
    storageManager.getEventCacheEpochs(fastEpoch, waveEpoch);
    const bool ready = storageManager.isReady() && !storageManager.partition(StoragePartitionId::gridEvents).isFormatting() &&
        !storageManager.partition(StoragePartitionId::waveEvents).isFormatting();
    const uint32_t values[] = {settings.revision, settings.nextRevision, uint32_t(settings.state), grid.nextEventId,
        grid.captureActive, grid.failedEvents, grid.writtenPages, wave.capturing, wave.storedCaptures, wave.occupiedSlots,
        wave.pendingLinks, wave.gaps, wave.failedWrites, wave.committed, wave.orphans, wave.droppedTriggers,
        wave.erasedBlocks, wave.actualPreMs, wave.reservedBytes, wave.enabled, settings.config.waveEnabled,
        ready, uint32_t(fastEpoch), uint32_t(fastEpoch >> 32), uint32_t(waveEpoch), uint32_t(waveEpoch >> 32)};
    static uint32_t previous[sizeof(values) / sizeof(values[0])] = {}, sequence = 0;
    static portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mutex);
    if (!sequence) { sequence = esp_random(); if (!sequence) sequence = 1; }
    if (memcmp(previous, values, sizeof(values))) { memcpy(previous, values, sizeof(values)); if (!++sequence) ++sequence; }
    const uint32_t seq = sequence;
    portEXIT_CRITICAL(&mutex);
    if (request->hasParam("since") && uint32_t(since) == seq) {
        auto* response = request->beginResponse(204); response->addHeader("Cache-Control", "no-store"); request->send(response); return;
    }
    char buffer[768]; JsonWriter json(buffer, sizeof(buffer));
    json.objBegin(); json.objAppendUInt("seq", seq); json.objAppendUInt("cfg", settings.revision); appendSave(json, settings);
    json.objAppendBool("ready", ready);
    json.objAppendUInt("last", grid.nextEventId - 1U); json.objAppendBool("busy", grid.captureActive || wave.capturing);
    json.objBegin("grid"); json.objAppendUInt("pages", grid.writtenPages); json.objAppendUInt("fail", grid.failedEvents); json.objEnd();
    json.objBegin("wave"); json.objAppendUInt("count", wave.storedCaptures); json.objAppendUInt("used", wave.occupiedSlots);
    json.objAppendUInt("slots", wave.totalSlots); json.objAppendBool("on", wave.enabled); json.objAppendUInt("pre", wave.actualPreMs);
    json.objAppendUInt("ram", wave.reservedBytes); json.objAppendUInt("done", wave.committed); json.objAppendUInt("orph", wave.orphans);
    json.objAppendUInt("gaps", wave.gaps); json.objAppendUInt("drop", wave.droppedTriggers); json.objAppendUInt("fail", wave.failedWrites);
    json.objAppendUInt("free", wave.erasedBlocks); json.objAppendUInt("pending", wave.pendingLinks); json.objEnd();
    json.objBegin("lim"); json.objAppendUInt("point", eventPointLimitMs()); json.objAppendUInt("pages", eventCapturePages); json.objEnd();
    json.objEnd(); sendJson(request, 200, json);
}
}

// Instala las dos rutas agrupadas en server; no crea tareas ni buffers persistentes grandes.
void eventsHttpBegin(AsyncWebServer& server) {
    server.on("/events/config", HTTP_GET | HTTP_POST, handleConfig);
    server.on("/events/status", HTTP_GET, handleStatus);
}
