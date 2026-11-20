#include "scope_stream.h"
#include "webserver.h"
#include "memory_diagnostics.h"
#include "storage/wave_event_store.h"
#include <Preferences.h>
#include <esp_timer.h>
#include <math.h>
#include <new>
#include <stdio.h>
#include <string.h>
#include <algorithm>

namespace {
constexpr uint16_t scopeMaxPoints = 512;
constexpr uint16_t scopeNormalPoints = 200;
constexpr uint8_t scopeChannelCount = 7;
constexpr uint8_t scopeHeaderBytes = 24;
constexpr size_t scopeRingBytes = size_t(scopeMaxPoints) * scopeChannelCount * 12 / 8;
constexpr uint32_t scopeFrameIntervalUs = 125000;
constexpr uint8_t scopeFormatVersion = 1;
constexpr uint8_t scopeTriggerBit = 0x80;

struct scopeSettings {
    float voltageScale;
    float currentScale;
    float timeDivMs;
    float triggerLevel;
    uint8_t triggerChannel;
    uint8_t rising;
    uint8_t channelMask;
};

struct savedScopeSettings {
    uint8_t version;
    scopeSettings settings;
};

alignas(4) uint8_t scopeRing[scopeRingBytes] = {};
uint8_t scopeFlagsRing[scopeMaxPoints] = {};
int32_t scopeSums[scopeChannelCount] = {};
int16_t scopePrevious = 0;
scopeSettings activeScope = {0.244140625f, 0.0244140625f, 10.0f, 0.0f, 0, 1, 0x7f};
scopeSettings qualityScope = activeScope;
bool scopeQualityActive = false;
portMUX_TYPE scopeSettingsMux = portMUX_INITIALIZER_UNLOCKED;
Preferences scopePreferences;
uint32_t scopeChangedAtMs = 0;
uint32_t scopeLastFrameUs = 0;
uint32_t scopePointCount = 0;
uint32_t scopeFrameSequence = 0;
uint32_t scopeLastSentTrigger = UINT32_MAX;
uint32_t scopeConfigGeneration = 0;
uint32_t scopeAppliedGeneration = UINT32_MAX;
uint64_t scopeCurrentEndUs = 0;
uint16_t scopeRingHead = 0;
uint16_t scopeRingCount = 0;
uint16_t scopeDecimation = 4;
uint16_t scopeTargetPoints = scopeNormalPoints;
uint16_t scopeDecimationCount = 0;
uint8_t scopePointFlags = 0;
uint8_t scopeSlopeCount = 0;
uint8_t scopePreviousValid = 0;
bool scopeSavePending = false;
bool scopeWasActive = false;
constexpr uint16_t scopeFftPoints = 512;
constexpr uint32_t scopeFftIntervalUs = 500000;
AsyncWebSocketMessageBuffer* scopeFftPacket = nullptr;
uint16_t scopeFftCount = 0;
uint8_t scopeFftChannels = 0;
bool scopeFftEnabled = true;
constexpr uint8_t scopeFftHeaderBytes = 52;
uint32_t scopeFftStartedUs = 0;
uint64_t scopeFftPreviousEndUs = 0;
uint64_t scopeFftFirstUs = 0;
uint32_t scopeFftId = 0;
uint32_t scopeFftWaitUs = 0;
uint32_t scopeFftTrigger = UINT32_MAX;
uint32_t scopeFftCancelId = 0;
volatile uint8_t scopeFftCancelReason = 0;
uint8_t scopeFftPart = 0;
uint16_t scopeFftTriggerIndex = UINT16_MAX;
float scopeFftFrequencies[3] = {};
bool scopeFftCapturing = false;
AsyncWebSocketMessageBuffer* scopeTransmitPending[2] = {};
uint8_t scopeTransmitHead = 0, scopeTransmitCount = 0;
volatile uint8_t scopeTransmitCredit = 0;
volatile uint32_t scopeTransmitFailedId = 0;
constexpr uint32_t scopeViewerTimeoutMs = 30000;
constexpr uint32_t scopeQualityTimeoutMs = 5000;
struct scopeViewer {
    uint32_t id = 0;
    uint32_t lastSeenMs = 0;
    uint32_t qualitySeenMs = 0;
    uint8_t streams = 1;
};
scopeViewer scopeViewers[DEFAULT_MAX_WS_CLIENTS];

// Determina si vencio un navegador incluso al desbordarse millis; lastSeenMs/nowMs son marcas monotónicas.
bool scopeViewerExpired(uint32_t lastSeenMs, uint32_t nowMs)
{
    return uint32_t(nowMs - lastSeenMs) >= scopeViewerTimeoutMs;
}

// Registra actividad o retira un navegador; id es el cliente, connected permite crear y remove elimina su registro.
bool scopeTouchViewer(uint32_t id, bool connected = false, bool remove = false)
{
    bool found = false;
    portENTER_CRITICAL(&scopeSettingsMux);
    scopeViewer* free = nullptr;
    for (scopeViewer& viewer : scopeViewers) {
        if (viewer.id == id) {
            viewer.id = remove ? 0 : id;
            viewer.lastSeenMs = millis();
            found = true;
            break;
        }
        if (!viewer.id) free = &viewer;
    }
    if (!found && connected && free) {
        free->id = id;
        free->lastSeenMs = millis();
        free->streams = 1;
        free->qualitySeenMs = 0;
        found = true;
    }
    portEXIT_CRITICAL(&scopeSettingsMux);
    return found;
}

// Cierra conexiones sin actividad del navegador en un maximo aproximado de 31 s; sin parametros.
void scopeExpireViewers()
{
    static uint32_t checkedAtMs = 0;
    const uint32_t nowMs = millis();
    if (uint32_t(nowMs - checkedAtMs) < 1000) return;
    checkedAtMs = nowMs;
    uint32_t expired[DEFAULT_MAX_WS_CLIENTS];
    uint8_t count = 0;
    portENTER_CRITICAL(&scopeSettingsMux);
    const uint32_t activityNowMs = millis();
    for (scopeViewer& viewer : scopeViewers) {
        if (viewer.id && scopeViewerExpired(viewer.lastSeenMs, activityNowMs)) {
            expired[count++] = viewer.id;
            viewer.id = 0;
        }
    }
    portEXIT_CRITICAL(&scopeSettingsMux);
    AsyncWebSocket* server = webServerGetMeterWS();
    if (server) for (uint8_t index = 0; index < count; ++index) {
        AsyncWebSocketClient* viewer = server->client(expired[index]);
        // No esperar el cierre WebSocket de un proxy que pudo dejar de leer y retener la cola.
        if (viewer && viewer->client()) viewer->client()->close(true);
    }
}

// Libera una captura espectral pendiente, sin tocar los mensajes ya transmitidos; sin parametros.
void scopeResetFft()
{
    delete scopeFftPacket;
    scopeFftPacket = nullptr;
    scopeFftCount = 0;
    scopeFftCapturing = false;
    scopeFftPart = 0;
    scopeFftTrigger = UINT32_MAX;
    scopeFftWaitUs = 0;
}

// Escribe una muestra signed de 12 bits conservando el nibble del vecino.
// buffer: destino. sampleIndex: indice lineal. value: muestra limitada al rango signed de 12 bits.
void scopeWrite12(uint8_t* buffer, size_t sampleIndex, int16_t value)
{
    const size_t offset = sampleIndex * 3 / 2;
    const uint16_t raw = uint16_t(value) & 0x0fff;
    if (sampleIndex & 1) {
        buffer[offset] = (buffer[offset] & 0x0f) | uint8_t(raw << 4);
        buffer[offset + 1] = uint8_t(raw >> 4);
    } else {
        buffer[offset] = uint8_t(raw);
        buffer[offset + 1] = (buffer[offset + 1] & 0xf0) | uint8_t(raw >> 8);
    }
}

// Lee una muestra signed de 12 bits del anillo empaquetado.
// buffer: origen. sampleIndex: indice lineal de la muestra.
int16_t scopeRead12(const uint8_t* buffer, size_t sampleIndex)
{
    const size_t offset = sampleIndex * 3 / 2;
    const uint16_t pair = uint16_t(buffer[offset]) | (uint16_t(buffer[offset + 1]) << 8);
    const uint16_t value = (sampleIndex & 1 ? pair >> 4 : pair) & 0x0fff;
    return value & 0x0800 ? int16_t(value | 0xf000) : int16_t(value);
}

// Convierte un valor ADE al rango signed de 12 bits del grafico.
// raw: muestra ADE. factor: conversion precalculada por bloque. clipped: recibe si hubo saturacion.
int16_t scopeScaleSample(int32_t raw, float factor, bool& clipped)
{
    const float scaled = float(raw) * factor;
    clipped = true;
    if (scaled > 2047.0f) return 2047;
    if (scaled < -2048.0f) return -2048;
    clipped = false;
    return int16_t(scaled);
}

// Ajusta factor de reduccion y cantidad de puntos a la ventana seleccionada.
// settings: escalas y tiempo compartidos del osciloscopio.
void scopeConfigureWindow(const scopeSettings& settings)
{
    const uint32_t inputPoints = uint32_t(lroundf(settings.timeDivMs * 80.0f));
    const uint16_t pointLimit = settings.timeDivMs >= 50.0f ? 500 : scopeNormalPoints;
    // Conserva un ciclo de 50 Hz de margen para encontrar un trigger completo dentro del anillo.
    const uint32_t triggerDecimation = (inputPoints + 160 + scopeMaxPoints - 1) / scopeMaxPoints;
    scopeDecimation = uint16_t(std::max<uint32_t>(triggerDecimation,
        (inputPoints + pointLimit - 1) / pointLimit));
    scopeTargetPoints = uint16_t(std::min<uint32_t>(pointLimit,
        (inputPoints + scopeDecimation - 1) / scopeDecimation));
    if (scopeTargetPoints < 2) scopeTargetPoints = 2;
}

// Limpia el historial reducido al cambiar ajustes que afectan la captura.
// sin parametros.
void scopeResetCapture()
{
    scopeRingHead = scopeRingCount = 0;
    scopePointCount = 0;
    scopeLastSentTrigger = UINT32_MAX;
    scopePreviousValid = scopeSlopeCount = 0;
    scopeDecimationCount = 0;
    scopePointFlags = 0;
    memset(scopeSums, 0, sizeof(scopeSums));
    memset(scopeRing, 0, sizeof(scopeRing));
    memset(scopeFlagsRing, 0, sizeof(scopeFlagsRing));
}

// Crea un mensaje de texto compacto con la configuracion vigente.
// settings: configuracion a serializar. buffer: destino. size: capacidad del destino.
size_t scopeFormatSettings(const scopeSettings& settings, char* buffer, size_t size)
{
    const int written = snprintf(buffer, size, "scope_cfg,%.9g,%.9g,%.5g,%.6g,%u,%u,%u",
        settings.voltageScale, settings.currentScale, settings.timeDivMs, settings.triggerLevel,
        settings.triggerChannel, settings.rising, settings.channelMask);
    return written > 0 ? std::min<size_t>(size - 1, size_t(written)) : 0;
}

// Publica configuracion a todos los clientes conectados.
// server: WebSocket compartido. settings: configuracion vigente.
void scopeBroadcastSettings(AsyncWebSocket* server, const scopeSettings& settings)
{
    char message[112];
    const size_t length = scopeFormatSettings(settings, message, sizeof(message));
    if (length) server->textAll(reinterpret_cast<const uint8_t*>(message), length);
}

// Envia estado de prioridad y ajustes efectivos; server es el destino y client limita la respuesta a un navegador.
void scopePublishConfiguration(AsyncWebSocket* server, AsyncWebSocketClient* client = nullptr)
{
    scopeSettings settings, quality;
    bool locked, fftEnabled;
    uint32_t generation;
    portENTER_CRITICAL(&scopeSettingsMux);
    locked = scopeQualityActive;
    fftEnabled = scopeFftEnabled;
    settings = locked ? qualityScope : activeScope;
    quality = qualityScope;
    generation = scopeConfigGeneration;
    portEXIT_CRITICAL(&scopeSettingsMux);
    char message[256], scopeMessage[112];
    scopeFormatSettings(settings, scopeMessage, sizeof(scopeMessage));
    snprintf(message, sizeof(message), "quality_cfg,%u,%.9g,%.9g,%u,%lu\n%s\nscope_fft_on,%u", unsigned(locked),
        quality.voltageScale * 2048.0f, quality.currentScale * 2048.0f, unsigned(quality.channelMask),
        static_cast<unsigned long>(generation), scopeMessage, unsigned(fftEnabled));
    if (client) client->text(message); else server->textAll(message);
}

// Recalcula la prioridad por lease sin escribir NVS; server distribuye cambios a todos los navegadores.
void scopeRefreshPriority(AsyncWebSocket* server)
{
    bool quality = false, changed;
    portENTER_CRITICAL(&scopeSettingsMux);
    const uint32_t now = millis();
    for (const scopeViewer& viewer : scopeViewers)
        if (viewer.id && (viewer.streams & 2) && uint32_t(now - viewer.qualitySeenMs) < scopeQualityTimeoutMs) quality = true;
    quality = quality && scopeFftEnabled;
    changed = quality != scopeQualityActive;
    if (changed) { scopeQualityActive = quality; ++scopeConfigGeneration; }
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (changed && server) scopePublishConfiguration(server);
}

// Comprueba credito de la cola compartida; server es el WebSocket y limit la cantidad maxima permitida antes de agregar uno.
bool scopeQueueHasRoom(AsyncWebSocket* server, size_t limit)
{
    for (const auto& client : server->getClients())
        if (client.status() == WS_CONNECTED && client.queueLen() >= limit) return false;
    return true;
}

// Cancela una ventana incompleta sin asignar memoria; reason identifica hueco, memoria, cola o cambio de configuracion.
void scopeCancelFft(uint8_t reason)
{
    if (scopeFftCapturing) {
        portENTER_CRITICAL(&scopeSettingsMux);
        scopeFftCancelId = scopeFftId; scopeFftCancelReason = reason;
        portEXIT_CRITICAL(&scopeSettingsMux);
    }
    scopeResetFft();
}

// Intenta avisar una cancelacion sin superar dos mensajes pendientes; server es la conexion compartida.
void scopeFlushFftCancel(AsyncWebSocket* server)
{
    uint32_t id;
    uint8_t reason;
    portENTER_CRITICAL(&scopeSettingsMux);
    id = scopeFftCancelId; reason = scopeFftCancelReason;
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (!reason || !scopeQueueHasRoom(server, 2) || !memoryHasRoom(256, 24 * 1024)) return;
    uint8_t message[10] = {'S', 'F', 3, 1};
    memcpy(message + 4, &id, 4);
    message[8] = reason;
    try {
        if (server->binaryAll(message, sizeof(message)) == AsyncWebSocket::ENQUEUED) {
            portENTER_CRITICAL(&scopeSettingsMux);
            if (scopeFftCancelId == id && scopeFftCancelReason == reason) scopeFftCancelReason = 0;
            portEXIT_CRITICAL(&scopeSettingsMux);
        }
    } catch (const std::bad_alloc&) { /* La web tambien vence capturas incompletas por tiempo o por nuevo ID. */ }
}

// Envia fuera de meterTask y publica credito sin mutex de red en la adquisicion; server es el WebSocket compartido.
void scopeDrainTransmissions(AsyncWebSocket* server)
{
    if (!server) return;
    if (!server->count()) {
        AsyncWebSocketMessageBuffer* unused[2] = {};
        portENTER_CRITICAL(&scopeSettingsMux);
        for (uint8_t index = 0; index < scopeTransmitCount; ++index) unused[index] = scopeTransmitPending[(scopeTransmitHead + index) % 2];
        scopeTransmitCount = scopeTransmitCredit = 0;
        scopeFftCancelReason = 0;
        portEXIT_CRITICAL(&scopeSettingsMux);
        for (auto packet : unused) delete packet;
        return;
    }
    AsyncWebSocketMessageBuffer* packet = nullptr;
    portENTER_CRITICAL(&scopeSettingsMux);
    if (scopeTransmitCount) {
        packet = scopeTransmitPending[scopeTransmitHead];
        scopeTransmitHead = (scopeTransmitHead + 1) % 2;
        --scopeTransmitCount;
    }
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (packet) {
        uint32_t id;
        memcpy(&id, packet->get() + 4, 4);
        if (!server->count() || !scopeQueueHasRoom(server, 2)) {
            delete packet;
            portENTER_CRITICAL(&scopeSettingsMux);
            scopeFftCancelId = scopeTransmitFailedId = id;
            scopeFftCancelReason = 3;
            portEXIT_CRITICAL(&scopeSettingsMux);
        } else if (server->binaryAll(packet) != AsyncWebSocket::ENQUEUED) {
            portENTER_CRITICAL(&scopeSettingsMux);
            scopeFftCancelId = scopeTransmitFailedId = id;
            scopeFftCancelReason = 3;
            portEXIT_CRITICAL(&scopeSettingsMux);
        }
    }
    portENTER_CRITICAL(&scopeSettingsMux);
    const bool pending = scopeTransmitCount != 0;
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (!pending) scopeFlushFftCancel(server);
    size_t queued = 0;
    for (const auto& client : server->getClients()) if (client.status() == WS_CONNECTED) queued = std::max(queued, client.queueLen());
    portENTER_CRITICAL(&scopeSettingsMux);
    const size_t used = queued + scopeTransmitCount;
    scopeTransmitCredit = used < 2 ? uint8_t(2 - used) : 0;
    portEXIT_CRITICAL(&scopeSettingsMux);
}

// Comprueba que ningun cliente tenga envios pendientes; server es el WebSocket compartido.
bool scopeCanSend(AsyncWebSocket* server)
{
    for (const auto& client : server->getClients()) {
        if (client.status() == WS_CONNECTED && client.queueLen() != 0) return false;
    }
    return true;
}

// Reserva un mensaje WebSocket solo si todos los clientes pueden recibirlo sin cola atrasada.
// server: WebSocket compartido. size: tamano final del paquete.
AsyncWebSocketMessageBuffer* scopeCreatePacket(AsyncWebSocket* server, size_t size)
{
    if (ESP.getFreeHeap() < 50 * 1024 || !memoryHasRoom(size + 1024, 32 * 1024) || !scopeCanSend(server)) return nullptr;
    try {
        return server->makeBuffer(size);
    } catch (const std::bad_alloc&) {
        memoryReportState("scope frame allocation rejected", size);
        return nullptr;
    }
}

// Reserva solo el fragmento actual; server/settings/generation describen el destino y los factores congelados de la captura.
bool scopePrepareFftPart(AsyncWebSocket* server, const scopeSettings& settings, uint32_t generation)
{
    const size_t size = scopeFftHeaderBytes + size_t(scopeFftPoints) * scopeFftChannels * 3 / 2;
    if (!scopeTransmitCredit) { scopeCancelFft(3); return false; }
    if (!memoryHasRoom(size + 1024, 24 * 1024)) { scopeCancelFft(2); return false; }
    try { scopeFftPacket = server->makeBuffer(size); }
    catch (const std::bad_alloc&) { scopeCancelFft(2); return false; }
    if (!scopeFftPacket || !scopeFftPacket->get()) { scopeCancelFft(2); return false; }
    scopeFftCount = 0;
    uint8_t* packet = scopeFftPacket->get();
    memset(packet, 0, size);
    packet[0] = 'S'; packet[1] = 'F'; packet[2] = 3;
    memcpy(packet + 4, &scopeFftId, 4);
    packet[8] = scopeFftPart; packet[9] = 4; packet[10] = settings.channelMask;
    const uint16_t period = 125;
    memcpy(packet + 12, &scopeFftPoints, 2);
    memcpy(packet + 14, &period, 2);
    const uint16_t trigger = scopeFftPart == 0 ? scopeFftTriggerIndex : UINT16_MAX;
    memcpy(packet + 16, &trigger, 2);
    const uint64_t firstUs = scopeFftFirstUs + uint64_t(scopeFftPart) * scopeFftPoints * 125;
    memcpy(packet + 20, &firstUs, 8);
    memcpy(packet + 28, &settings.voltageScale, 4);
    memcpy(packet + 32, &settings.currentScale, 4);
    memcpy(packet + 36, scopeFftFrequencies, sizeof(scopeFftFrequencies));
    memcpy(packet + 48, &generation, 4);
    return true;
}

// Empaqueta el ultimo tramo completo del anillo sin reservar un segundo buffer; settings fija sus canales activos.
void scopeFillFftFromRing(const scopeSettings& settings)
{
    uint8_t* packet = scopeFftPacket->get();
    size_t index = 0;
    for (uint16_t point = 0; point < scopeFftPoints; ++point) {
        const uint16_t source = uint16_t((scopeRingHead + point) % scopeMaxPoints);
        packet[11] |= scopeFlagsRing[source] & settings.channelMask;
        for (uint8_t channel = 0; channel < scopeChannelCount; ++channel)
            if (settings.channelMask & (1U << channel)) scopeWrite12(packet + scopeFftHeaderBytes, index++,
                scopeRead12(scopeRing, size_t(source) * scopeChannelCount + channel));
    }
    scopeFftCount = scopeFftPoints;
}

// Entrega el fragmento al loop principal sin copiarlo; server identifica el destino que lo transmitira fuera de meterTask.
bool scopePublishFftPart(AsyncWebSocket* server)
{
    if (ESP.getFreeHeap() < 32 * 1024) { scopeCancelFft(2); return false; }
    bool accepted = false;
    portENTER_CRITICAL(&scopeSettingsMux);
    if (scopeTransmitCredit && scopeTransmitCount < 2) {
        scopeTransmitPending[(scopeTransmitHead + scopeTransmitCount) % 2] = scopeFftPacket;
        ++scopeTransmitCount;
        --scopeTransmitCredit;
        scopeFftPacket = nullptr;
        accepted = true;
    }
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (!accepted) { scopeCancelFft(3); return false; }
    scopeFftCount = 0;
    if (++scopeFftPart == 4) scopeResetFft();
    return true;
}

// Busca el ultimo disparo con ambas mitades de la ventana disponibles; sin parametros.
uint32_t scopeFindFrameTrigger()
{
    if (scopeRingCount < scopeTargetPoints) return UINT32_MAX;
    const uint16_t post = scopeTargetPoints - scopeTargetPoints / 2;
    const uint16_t candidates = scopeRingCount - scopeTargetPoints + 1;
    for (uint16_t offset = 0; offset < candidates; ++offset) {
        const uint32_t point = scopePointCount - post - offset;
        if (point == UINT32_MAX || !(scopeFlagsRing[point % scopeMaxPoints] & scopeTriggerBit)) continue;
        return point == scopeLastSentTrigger ? UINT32_MAX : point;
    }
    return UINT32_MAX;
}

// Encola una ventana cronologica y devuelve si se envio.
// triggerPoint: disparo que centra la ventana, o UINT32_MAX para una captura automatica.
bool scopePublishFrame(uint32_t triggerPoint)
{
    AsyncWebSocket* server = webServerGetMeterWS();
    if (!server || server->count() == 0 || scopeRingCount < scopeTargetPoints) return false;

    const uint16_t points = scopeTargetPoints;
    uint32_t first = scopePointCount - points;
    uint16_t triggerIndex = 0xffff;
    if (triggerPoint != UINT32_MAX) {
        const uint32_t pre = points / 2;
        const uint32_t age = scopePointCount - triggerPoint;
        if (age >= points - pre && age <= scopeRingCount - pre) {
            first = triggerPoint - pre;
            triggerIndex = uint16_t(pre);
        }
    }

    const uint16_t periodUs = uint16_t(scopeDecimation * 125U);
    const uint64_t elapsedUs = uint64_t(scopePointCount - 1 - first) * periodUs;
    const uint64_t firstUs = scopeCurrentEndUs > elapsedUs ? scopeCurrentEndUs - elapsedUs : 0;
    const size_t payloadBytes = (size_t(points) * scopeChannelCount * 12 + 7) / 8;
    AsyncWebSocketMessageBuffer* message = scopeCreatePacket(server, scopeHeaderBytes + payloadBytes);
    if (!message) return false;
    uint8_t* packet = message->get();
    memset(packet, 0, scopeHeaderBytes);
    packet[0] = 'S'; packet[1] = 'C'; packet[2] = scopeFormatVersion;
    packet[3] = triggerIndex == 0xffff ? 0 : 1;
    packet[4] = uint8_t(points); packet[5] = uint8_t(points >> 8);
    packet[6] = uint8_t(triggerIndex); packet[7] = uint8_t(triggerIndex >> 8);
    packet[8] = uint8_t(periodUs); packet[9] = uint8_t(periodUs >> 8);
    packet[12] = uint8_t(scopeFrameSequence); packet[13] = uint8_t(scopeFrameSequence >> 8);
    packet[14] = uint8_t(scopeFrameSequence >> 16); packet[15] = uint8_t(scopeFrameSequence >> 24);
    ++scopeFrameSequence;
    for (uint8_t i = 0; i < 8; ++i) packet[16 + i] = uint8_t(firstUs >> (8 * i));

    const uint16_t ringStart = uint16_t(first % scopeMaxPoints);
    uint8_t clipMask = 0;
    for (uint16_t point = 0; point < points; ++point) {
        const uint16_t sourcePoint = uint16_t((ringStart + point) % scopeMaxPoints);
        clipMask |= scopeFlagsRing[sourcePoint] & 0x7f;
        for (uint8_t channel = 0; channel < scopeChannelCount; ++channel) {
            const int16_t sample = scopeRead12(scopeRing, size_t(sourcePoint) * scopeChannelCount + channel);
            scopeWrite12(packet + scopeHeaderBytes, size_t(point) * scopeChannelCount + channel, sample);
        }
    }
    packet[10] = clipMask;
    if (server->binaryAll(message) != AsyncWebSocket::ENQUEUED) return false;
    if (triggerIndex != 0xffff) scopeLastSentTrigger = triggerPoint;
    return true;
}

// Detecta un cruce con la pendiente configurada en el canal seleccionado.
// current: muestra a 8 kHz. level: umbral escalado. rising: pendiente ascendente.
bool scopeFindTrigger(int16_t current, int16_t level, bool rising)
{
    const int16_t previous = scopePrevious;
    scopePrevious = current;
    if (!scopePreviousValid) {
        scopePreviousValid = 1;
        return false;
    }
    const bool slope = rising ? current > previous : current < previous;
    if (!slope) scopeSlopeCount = 0;
    else if (scopeSlopeCount < 255) ++scopeSlopeCount;
    const bool crossed = rising ? previous < level && current >= level : previous > level && current <= level;
    return crossed && scopeSlopeCount > 3;
}

// Forma cuatro tramos consecutivos reutilizando el anillo como pretrigger; values/clip/sampleUs son la muestra actual y settings/frequencies/generation su contexto.
void scopeProcessQuality(AsyncWebSocket* server, const int16_t* values, uint8_t clip, uint64_t sampleUs,
                         const scopeSettings& settings, const float* frequencies, uint32_t generation, int16_t level)
{
    const bool crossed = scopeFindTrigger(values[settings.triggerChannel], level, settings.rising);
    const uint16_t slot = scopeRingHead;
    for (uint8_t channel = 0; channel < scopeChannelCount; ++channel)
        scopeWrite12(scopeRing, size_t(slot) * scopeChannelCount + channel, values[channel]);
    scopeFlagsRing[slot] = clip;
    scopeRingHead = uint16_t((slot + 1) % scopeMaxPoints);
    scopeRingCount = std::min<uint16_t>(scopeRingCount + 1, scopeMaxPoints);
    ++scopePointCount;

    if (scopeFftCapturing) {
        if (++scopeFftCount < scopeFftPoints) return;
        if (!scopePrepareFftPart(server, settings, generation)) return;
        scopeFillFftFromRing(settings);
        scopePublishFftPart(server);
        return;
    }
    const uint32_t nowUs = uint32_t(sampleUs);
    if (!settings.channelMask || scopeFftCancelReason || uint32_t(nowUs - scopeFftStartedUs) < scopeFftIntervalUs) return;
    if (scopeRingCount < 257) return;
    if (!scopeFftWaitUs) scopeFftWaitUs = nowUs;
    if (scopeFftTrigger == UINT32_MAX && crossed) scopeFftTrigger = scopePointCount - 1;
    if (scopeRingCount < scopeMaxPoints) return;
    if (scopeFftTrigger != UINT32_MAX) {
        if (uint32_t(scopePointCount - scopeFftTrigger) < 256) return;
    } else if (uint32_t(nowUs - scopeFftWaitUs) < 250000) return;

    scopeFftStartedUs = nowUs;
    ++scopeFftId;
    scopeFftCapturing = true;
    scopeFftChannels = uint8_t(__builtin_popcount(unsigned(settings.channelMask)));
    scopeFftFirstUs = sampleUs - uint64_t(scopeFftPoints - 1) * 125;
    scopeFftTriggerIndex = scopeFftTrigger == UINT32_MAX ? UINT16_MAX : 256;
    for (uint8_t phase = 0; phase < 3; ++phase) {
        const float hz = frequencies ? frequencies[phase] : 0;
        scopeFftFrequencies[phase] = isfinite(hz) && hz >= 40 && hz <= 70 ? hz : 0;
    }
    if (!scopePrepareFftPart(server, settings, generation)) return;
    scopeFillFftFromRing(settings);
    scopePublishFftPart(server);
}

// Decodifica y valida un mensaje de configuracion enviado por cualquier cliente.
// message: texto terminado en NUL. settings: copia actual. maxScale: maximo V/A por cuenta, ampliado solo en Calidad.
bool scopeParseSettings(const char* message, scopeSettings& settings, float maxScale = 0.2442f)
{
    unsigned channel, rising, mask;
    if (sscanf(message, "scope,%f,%f,%f,%f,%u,%u,%u", &settings.voltageScale,
               &settings.currentScale, &settings.timeDivMs, &settings.triggerLevel,
               &channel, &rising, &mask) != 7) return false;
    settings.triggerChannel = uint8_t(channel);
    settings.rising = uint8_t(rising);
    settings.channelMask = uint8_t(mask);
    return isfinite(settings.voltageScale) && settings.voltageScale >= 0.000024f && settings.voltageScale <= maxScale &&
        isfinite(settings.currentScale) && settings.currentScale >= 0.000024f && settings.currentScale <= maxScale &&
        isfinite(settings.timeDivMs) && settings.timeDivMs >= 0.01f && settings.timeDivMs <= 100.0f &&
        isfinite(settings.triggerLevel) && channel < scopeChannelCount && rising <= 1 && mask <= 0x7f;
}
}

// Carga la configuracion compartida del osciloscopio desde NVS; sin parametros.
void scopeStreamInit()
{
    savedScopeSettings saved = {1, activeScope};
    if (scopePreferences.begin("scope", false)) {
        scopeFftEnabled = scopePreferences.getBool("fftOn", true);
        if (scopePreferences.getBytesLength("cfg") == sizeof(saved) &&
            scopePreferences.getBytes("cfg", &saved, sizeof(saved)) == sizeof(saved) && saved.version == 1) {
            scopeSettings checked = saved.settings;
            char validation[112];
            snprintf(validation, sizeof(validation), "scope,%.9g,%.9g,%.5g,%.6g,%u,%u,%u",
                checked.voltageScale, checked.currentScale, checked.timeDivMs, checked.triggerLevel,
                checked.triggerChannel, checked.rising, checked.channelMask);
            if (scopeParseSettings(validation, checked)) activeScope = checked;
        }
        scopePreferences.end();
    }
    qualityScope = activeScope;
    qualityScope.timeDivMs = 5;
    qualityScope.triggerLevel = 0;
    qualityScope.rising = 1;
    if (scopePreferences.begin("scope", true)) {
        if (scopePreferences.getBytesLength("qCfg") == sizeof(saved) &&
            scopePreferences.getBytes("qCfg", &saved, sizeof(saved)) == sizeof(saved) && saved.version == 1) {
            scopeSettings checked = saved.settings;
            char validation[112];
            snprintf(validation, sizeof(validation), "scope,%.9g,%.9g,5,0,%u,1,%u", checked.voltageScale,
                checked.currentScale, checked.triggerChannel, checked.channelMask);
            if (scopeParseSettings(validation, checked, 0.4884f)) qualityScope = checked;
        }
        scopePreferences.end();
    }
    scopeConfigureWindow(activeScope);
    scopeAppliedGeneration = scopeConfigGeneration;
}

// Procesa muestras ADE, detecta trigger a resolucion completa y publica ventanas reducidas.
// samples: bloque ADE. count: muestras por canal. endUs: fin del bloque. frequencies: Hz R/S/T ya medidos.
void scopeStreamProcess(const WFBFixedDataRate_t* samples, uint16_t count, uint64_t endUs, const float* frequencies)
{
    // La adquisicion y la captura NAND siguen antes de esta funcion; el grafico solo se prepara con clientes.
    AsyncWebSocket* server = webServerGetMeterWS();
    if (!server || server->count() == 0) {
        scopeWasActive = false;
        scopeResetFft();
        return;
    }
    scopeSettings settings;
    uint32_t generation;
    bool fftEnabled;
    bool temporalEnabled = false;
    portENTER_CRITICAL(&scopeSettingsMux);
    settings = scopeQualityActive ? qualityScope : activeScope;
    generation = scopeConfigGeneration;
    uint8_t streams = 0;
    for (const scopeViewer& viewer : scopeViewers) if (viewer.id) streams |= viewer.streams;
    temporalEnabled = (streams & 1) != 0;
    fftEnabled = scopeQualityActive;
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (!temporalEnabled && !fftEnabled) {
        scopeWasActive = false;
        scopeResetFft();
        return;
    }
    scopeCurrentEndUs = endUs;
    if (!scopeWasActive || scopeAppliedGeneration != generation) {
        scopeCancelFft(4);
        scopeConfigureWindow(settings);
        scopeResetCapture();
        scopeFftStartedUs = micros() - scopeFftIntervalUs;
        scopeAppliedGeneration = generation;
    }
    scopeWasActive = true;
    if (!fftEnabled) scopeCancelFft(4);
    if (scopeFftCapturing && scopeTransmitFailedId == scopeFftId) scopeCancelFft(3);
    // No unir bloques separados por una perdida de adquisicion para calcular el espectro.
    const uint64_t blockUs = uint64_t(count) * 125;
    if (scopeFftPreviousEndUs && (endUs <= scopeFftPreviousEndUs ||
        endUs - scopeFftPreviousEndUs < blockUs - 16 || endUs - scopeFftPreviousEndUs > blockUs + 16)) {
        scopeCancelFft(1);
        scopeResetCapture();
    }
    scopeFftPreviousEndUs = endUs;

    const float voltageFactor = 1.0f / (float(CAL_V_PCF) * settings.voltageScale);
    const float currentFactor = 1.0f / (float(CAL_I_PCF) * settings.currentScale);
    const float triggerScale = (settings.triggerChannel & 1) ? settings.voltageScale : settings.currentScale;
    const int16_t triggerLevel = int16_t(std::max(-2048.0f, std::min(2047.0f, settings.triggerLevel / triggerScale)));
    int16_t fullValues[scopeChannelCount];
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t clippedMask = 0;
        for (uint8_t channel = 0; channel < scopeChannelCount; ++channel) {
            bool clipped = false;
            fullValues[channel] = scopeScaleSample(samples[i].buffer[channel], (channel & 1) ? voltageFactor : currentFactor, clipped);
            if (clipped) clippedMask |= uint8_t(1U << channel);
        }
        if (fftEnabled) {
            const uint64_t sampleUs = endUs - uint64_t(count - 1 - i) * 125;
            scopeProcessQuality(server, fullValues, clippedMask, sampleUs, settings, frequencies, generation, triggerLevel);
            continue;
        }
        if (!temporalEnabled) continue;
        // El bit libre conserva todos los cruces a 8 kHz hasta poder completar su ventana posterior.
        if (scopeFindTrigger(fullValues[settings.triggerChannel], triggerLevel, settings.rising)) scopePointFlags |= scopeTriggerBit;
        for (uint8_t channel = 0; channel < scopeChannelCount; ++channel) scopeSums[channel] += fullValues[channel];
        scopePointFlags |= clippedMask;
        if (++scopeDecimationCount < scopeDecimation) continue;

        const int32_t divisor = scopeDecimationCount;
        const uint16_t slot = scopeRingHead;
        for (uint8_t channel = 0; channel < scopeChannelCount; ++channel) {
            const int16_t average = int16_t(scopeSums[channel] / divisor);
            scopeWrite12(scopeRing, size_t(slot) * scopeChannelCount + channel, average);
            scopeSums[channel] = 0;
        }
        scopeFlagsRing[slot] = scopePointFlags;
        scopePointFlags = 0;
        scopeRingHead = uint16_t((slot + 1) % scopeMaxPoints);
        scopeRingCount = std::min<uint16_t>(scopeRingCount + 1, scopeMaxPoints);
        ++scopePointCount;
        scopeDecimationCount = 0;
    }

    const uint32_t nowUs = micros();
    if (fftEnabled) return;
    if (!temporalEnabled) return;
    if (nowUs - scopeLastFrameUs < scopeFrameIntervalUs) return;
    if (scopePublishFrame(scopeFindFrameTrigger())) scopeLastFrameUs = nowUs;
}

// Guarda configuracion despues de un periodo sin cambios; sin parametros.
void scopeStreamLoop()
{
    scopeExpireViewers();
    scopeRefreshPriority(webServerGetMeterWS());
    scopeDrainTransmissions(webServerGetMeterWS());
    if (waveCaptureBusy()) return;
    scopeSettings snapshot, quality;
    bool fftEnabled;
    portENTER_CRITICAL(&scopeSettingsMux);
    if (!scopeSavePending || millis() - scopeChangedAtMs < 1500) {
        portEXIT_CRITICAL(&scopeSettingsMux);
        return;
    }
    snapshot = activeScope;
    quality = qualityScope;
    fftEnabled = scopeFftEnabled;
    scopeSavePending = false;
    portEXIT_CRITICAL(&scopeSettingsMux);

    const savedScopeSettings saved = {1, snapshot};
    if (scopePreferences.begin("scope", false)) {
        scopePreferences.putBytes("cfg", &saved, sizeof(saved));
        const savedScopeSettings qualitySaved = {1, quality};
        scopePreferences.putBytes("qCfg", &qualitySaved, sizeof(qualitySaved));
        scopePreferences.putBool("fftOn", fftEnabled);
        scopePreferences.end();
    }
}

// Recibe ajustes de clientes y distribuye la configuracion compartida.
// server: WebSocket del osciloscopio. client: cliente que genero el evento. type/arg/data/len: evento WebSocket.
void scopeStreamWebSocketEvents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type,
                                void* arg, uint8_t* data, size_t len)
{
    if (type == WS_EVT_CONNECT) {
        if (!scopeTouchViewer(client->id(), true)) {
            client->close(1013, "scope viewers full");
            return;
        }
        scopePublishConfiguration(server, client);
        return;
    }
    if (type == WS_EVT_DISCONNECT) {
        scopeTouchViewer(client->id(), false, true);
        scopeRefreshPriority(server);
        return;
    }
    if (type != WS_EVT_DATA || !arg || !data || len == 0 || len >= 112) return;
    const AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
    if (info->opcode != WS_TEXT || !info->final || info->index != 0 || info->len != len) return;
    char message[112];
    memcpy(message, data, len);
    message[len] = 0;

    if (strcmp(message, "scope_alive") == 0) {
        scopeTouchViewer(client->id());
        return;
    }
    if (strcmp(message, "scope_get") == 0) {
        scopeTouchViewer(client->id());
        scopePublishConfiguration(server, client);
        return;
    }

    // Cada navegador declara su vista; se conserva una sola adquisicion para todos los consumidores.
    if (strncmp(message, "scope_view,", 11) == 0) {
        if (len != 12 || message[11] < '1' || message[11] > '3') return;
        scopeTouchViewer(client->id());
        portENTER_CRITICAL(&scopeSettingsMux);
        for (scopeViewer& viewer : scopeViewers) if (viewer.id == client->id()) {
            viewer.streams = uint8_t(message[11] - '0');
            if (viewer.streams & 2) viewer.qualitySeenMs = millis();
            break;
        }
        portEXIT_CRITICAL(&scopeSettingsMux);
        scopeRefreshPriority(server);
        return;
    }

    bool qualityClient = false;
    portENTER_CRITICAL(&scopeSettingsMux);
    for (const scopeViewer& viewer : scopeViewers)
        if (viewer.id == client->id() && (viewer.streams & 2) && uint32_t(millis() - viewer.qualitySeenMs) < scopeQualityTimeoutMs)
            qualityClient = true;
    portEXIT_CRITICAL(&scopeSettingsMux);

    if (strncmp(message, "quality_set,", 12) == 0) {
        float voltage, current;
        unsigned mask;
        char trailing;
        if (!qualityClient || sscanf(message, "quality_set,%f,%f,%u%c", &voltage, &current, &mask, &trailing) != 3 ||
            !isfinite(voltage) || !isfinite(current) || voltage < 0.05f || voltage > 1000 || current < 0.05f || current > 1000 || mask > 127) return;
        portENTER_CRITICAL(&scopeSettingsMux);
        qualityScope.voltageScale = voltage / 2048.0f;
        qualityScope.currentScale = current / 2048.0f;
        qualityScope.channelMask = uint8_t(mask);
        qualityScope.triggerLevel = 0;
        qualityScope.rising = 1;
        qualityScope.triggerChannel = 0;
        for (uint8_t channel = 0; channel < scopeChannelCount; ++channel)
            if (mask & (1U << channel)) { qualityScope.triggerChannel = channel; break; }
        for (uint8_t channel = 1; channel < scopeChannelCount; channel += 2)
            if (mask & (1U << channel)) { qualityScope.triggerChannel = channel; break; }
        scopeChangedAtMs = millis();
        scopeSavePending = true;
        ++scopeConfigGeneration;
        portEXIT_CRITICAL(&scopeSettingsMux);
        scopePublishConfiguration(server);
        return;
    }

    if (strncmp(message, "scope_fft_on,", 13) == 0) {
        int enabled;
        char trailing;
        if (!qualityClient || sscanf(message, "scope_fft_on,%d%c", &enabled, &trailing) != 1 || enabled < 0 || enabled > 1) return;
        scopeTouchViewer(client->id());
        portENTER_CRITICAL(&scopeSettingsMux);
        scopeFftEnabled = enabled != 0;
        scopeChangedAtMs = millis();
        scopeSavePending = true;
        ++scopeConfigGeneration;
        portEXIT_CRITICAL(&scopeSettingsMux);
        server->textAll(message);
        scopeRefreshPriority(server);
        return;
    }

    scopeSettings next;
    portENTER_CRITICAL(&scopeSettingsMux);
    if (scopeQualityActive) {
        portEXIT_CRITICAL(&scopeSettingsMux);
        scopePublishConfiguration(server, client);
        return;
    }
    next = activeScope;
    portEXIT_CRITICAL(&scopeSettingsMux);
    if (!scopeParseSettings(message, next)) return;
    scopeTouchViewer(client->id());

    portENTER_CRITICAL(&scopeSettingsMux);
    if (scopeQualityActive) {
        portEXIT_CRITICAL(&scopeSettingsMux);
        scopePublishConfiguration(server, client);
        return;
    }
    activeScope = next;
    scopeChangedAtMs = millis();
    scopeSavePending = true;
    ++scopeConfigGeneration;
    portEXIT_CRITICAL(&scopeSettingsMux);
    scopePublishConfiguration(server);
}
