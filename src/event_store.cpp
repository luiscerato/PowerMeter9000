#include "event_store.h"
#include "manager_api.h"
#include "meter.h"
#include "event_test_pattern.h"
#include "memory_diagnostics.h"
#include <WiFi.h>
#include <cstring>
#include <atomic>
#include <memory>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

static const uint8_t maxStoredEvents = 5;
static const uint8_t maxWaveBlocks = 6;
static const uint16_t waveHeaderBytes = 128;
static const uint16_t waveFrameBytes = 8064;
static const uint16_t waveSamplesPerBlock = 768;
static const uint16_t fastRmsSampleBytes = sizeof(fastRMSData_t);
static const uint16_t waveChannels = 7;
static const uint32_t waveMinFreeHeap = 30 * 1024;
static const uint32_t eventNotifyPeriodMs = 5000;

struct EventEntry {
	bool used = false;
	bool ready = false;
	bool testRamp = false;
	String eventId;
	String type;
	String phase;
	String unit;
	String fastRmsReason;
	String waveReason;
	float value = 0.0;
	uint64_t timestampMs = 0;
	uint64_t durationMs = 0;
	uint32_t createdMs = 0;
	uint32_t lastNotifyMs = 0;
	bool durationKnown = false;
	bool fastRmsFinalized = false;
	bool fastRmsAvailable = false;
	uint8_t* fastRmsData = nullptr;
	size_t fastRmsSize = 0;
	int32_t fastRmsTriggerSample = -1;
	uint32_t fastRmsCrc32 = 0;
	bool waveFinalized = false;
	bool waveAvailable = false;
	bool waveCapturing = false;
	uint8_t wavePostFramesLeft = 0;
	uint8_t waveBlockCount = 0;
	uint8_t wavePreBlockCount = 0;
	int32_t waveTriggerSample = -1;
	uint8_t waveReservedBlocks = 0;
	uint8_t waveHeader[waveHeaderBytes] = {};
	uint8_t* waveBlocks[maxWaveBlocks] = {};
	uint32_t waveCrc32 = 0;
};

static EventEntry eventEntries[maxStoredEvents];
static SemaphoreHandle_t eventStoreMutex = nullptr;
static uint8_t previousWaveFrame[waveFrameBytes];
static bool hasPreviousWaveFrame = false;
// Solo MeterTask accede a activeCapture y previousWaveFrame.
static EventEntry* activeCapture = nullptr;
struct PendingFastRms {
	char eventId[48];
	uint8_t* data;
	size_t size;
	int32_t triggerSample;
	uint32_t crc32;
};
static QueueHandle_t fastRmsQueue = nullptr;
static QueueHandle_t completedEventQueue = nullptr;
static std::atomic<uint32_t> completionDrops{0};
static String bootId;
static std::atomic<uint32_t> bootEventCounter{0};

// Protege solo eventos terminados para HTTP y el loop principal; nunca se usa desde MeterTask.
static bool lockEventStore()
{
	return eventStoreMutex != nullptr && xSemaphoreTake(eventStoreMutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

// Libera el mutex del almacen si estaba tomado.
static void unlockEventStore()
{
	if (eventStoreMutex != nullptr) xSemaphoreGive(eventStoreMutex);
}

// Escapa texto para incluirlo como string JSON.
// dest: String destino. value: texto a escapar.
static void appendJsonString(String& dest, const String& value)
{
	dest += '"';
	for (uint32_t i = 0; i < value.length(); i++) {
		char c = value[i];
		if (c == '"' || c == '\\') {
			dest += '\\';
			dest += c;
		}
		else if ((uint8_t)c < 0x20) {
			char escaped[7];
			snprintf(escaped, sizeof(escaped), "\\u%04x", c);
			dest += escaped;
		}
		else dest += c;
	}
	dest += '"';
}

// Actualiza un CRC32 IEEE con un bloque de datos.
// crc: estado CRC previo. data: datos origen. size: cantidad de bytes.
static uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
	}
	return crc;
}

// Calcula un CRC32 IEEE completo.
// data: datos origen. size: cantidad de bytes.
static uint32_t calcCrc32(const uint8_t* data, size_t size)
{
	return ~updateCrc32(0xFFFFFFFF, data, size);
}

// Convierte un CRC32 a ocho digitos hexadecimales.
// crc: CRC binario.
static String crcToHex(uint32_t crc)
{
	char value[9];
	snprintf(value, sizeof(value), "%08X", crc);
	return String(value);
}

// Escribe un entero little-endian de 16 bits.
// data: buffer destino. offset: posicion. value: valor a escribir.
static void writeLe16(uint8_t* data, uint16_t offset, uint16_t value)
{
	data[offset] = value & 0xFF;
	data[offset + 1] = (value >> 8) & 0xFF;
}

// Escribe un entero little-endian de 32 bits.
// data: buffer destino. offset: posicion. value: valor a escribir.
static void writeLe32(uint8_t* data, uint16_t offset, uint32_t value)
{
	for (uint8_t i = 0; i < 4; i++) data[offset + i] = (value >> (8 * i)) & 0xFF;
}

// Escribe un entero little-endian de 64 bits.
// data: buffer destino. offset: posicion. value: valor a escribir.
static void writeLe64(uint8_t* data, uint16_t offset, uint64_t value)
{
	for (uint8_t i = 0; i < 8; i++) data[offset + i] = (value >> (8 * i)) & 0xFF;
}

// Devuelve el codigo compacto del tipo de evento para la cabecera PMEV.
// type: tipo textual del evento.
static uint16_t waveTypeCode(const String& type)
{
	if (type == "voltage_dip") return 1;
	if (type == "voltage_swell") return 2;
	if (type == "over_current") return 3;
	if (type == "manual_capture") return 4;
	return 0;
}

// Devuelve la mascara de fase de la cabecera PMEV.
// phase: fase textual del evento.
static uint8_t wavePhaseMask(const String& phase)
{
	if (phase == "R") return 1;
	if (phase == "S") return 2;
	if (phase == "T") return 4;
	if (phase == "N") return 8;
	return 0;
}

// Libera todos los buffers que pertenecen a una entrada.
// entry: entrada a reiniciar.
static void clearEventLocked(EventEntry& entry)
{
	if (entry.fastRmsData != nullptr) free(entry.fastRmsData);
	for (uint8_t i = 0; i < maxWaveBlocks; i++) {
		if (entry.waveBlocks[i] != nullptr) free(entry.waveBlocks[i]);
	}
	entry = EventEntry();
}

// Busca una entrada por identificador.
// eventId: identificador a buscar.
static int8_t findEventLocked(const String& eventId)
{
	for (uint8_t i = 0; i < maxStoredEvents; i++) {
		if (eventEntries[i].used && eventEntries[i].eventId == eventId) return i;
	}
	return -1;
}

// Reserva una entrada, descartando el evento mas antiguo si esta llena.
// eventId: identificador a reservar.
static EventEntry* reserveEventLocked(const String& eventId)
{
	int8_t existing = findEventLocked(eventId);
	if (existing >= 0) return &eventEntries[existing];
	for (uint8_t i = 0; i < maxStoredEvents; i++) {
		if (!eventEntries[i].used) return &eventEntries[i];
	}

	uint8_t oldest = 0;
	for (uint8_t i = 1; i < maxStoredEvents; i++) {
		if (eventEntries[i].createdMs < eventEntries[oldest].createdMs) oldest = i;
	}
	clearEventLocked(eventEntries[oldest]);
	return &eventEntries[oldest];
}

// Actualiza el estado listo cuando los dos artefactos tienen resultado.
// entry: entrada a evaluar.
static bool updateReadyLocked(EventEntry& entry)
{
	bool becameReady = !entry.ready && entry.fastRmsFinalized && entry.waveFinalized;
	if (becameReady) {
		entry.ready = true;
		entry.lastNotifyMs = 0;
	}
	return becameReady;
}

// Inicializa la cabecera PMEV para una entrada de onda.
// entry: entrada que contiene la cabecera. timestampMs: Unix ms o 0.
static void initWaveHeaderLocked(EventEntry& entry, uint64_t timestampMs)
{
	memset(entry.waveHeader, 0, sizeof(entry.waveHeader));
	memcpy(entry.waveHeader, "PMEV", 4);
	writeLe16(entry.waveHeader, 4, 1);
	writeLe16(entry.waveHeader, 6, waveHeaderBytes);
	writeLe64(entry.waveHeader, 8, timestampMs);
	writeLe32(entry.waveHeader, 16, 8000);
	writeLe16(entry.waveHeader, 20, waveChannels);
	writeLe16(entry.waveHeader, 22, 0);
	writeLe16(entry.waveHeader, 24, 0);
	writeLe16(entry.waveHeader, 26, 250);
	writeLe16(entry.waveHeader, 28, 100);
	writeLe16(entry.waveHeader, 30, 0);
	entry.waveHeader[32] = 0;
	writeLe16(entry.waveHeader, 34, waveSamplesPerBlock);
	strncpy((char*)entry.waveHeader + 64, entry.eventId.c_str(), 47);
}

// Reserva un bloque Wave manteniendo el margen minimo de heap.
// entry: entrada propietaria. blockIndex: indice del bloque a reservar.
static bool allocateWaveBlockLocked(EventEntry& entry, uint8_t blockIndex)
{
	if (blockIndex >= maxWaveBlocks) return false;
	if (entry.waveBlocks[blockIndex] != nullptr) return true;
	if (!memoryHasRoom(waveFrameBytes, waveMinFreeHeap)) return false;
	entry.waveBlocks[blockIndex] = (uint8_t*)malloc(waveFrameBytes);
	if (entry.waveBlocks[blockIndex] == nullptr) return false;
	if (blockIndex >= entry.waveReservedBlocks) entry.waveReservedBlocks = blockIndex + 1;
	return true;
}

// Calcula el CRC de la cabecera y los bloques Wave almacenados.
// entry: entrada Wave finalizada.
static uint32_t calcWaveCrcLocked(const EventEntry& entry)
{
	uint32_t crc = updateCrc32(0xFFFFFFFF, entry.waveHeader, waveHeaderBytes);
	for (uint8_t i = 0; i < entry.waveBlockCount; i++)
		crc = updateCrc32(crc, entry.waveBlocks[i], waveFrameBytes);
	return ~crc;
}

// Cierra la onda y completa su cabecera; el CRC se calcula despues, fuera de MeterTask.
// entry: entrada Wave a finalizar.
static void finishWaveLocked(EventEntry& entry)
{
	if (entry.waveFinalized) return;
	writeLe16(entry.waveHeader, 22, entry.waveBlockCount);
	writeLe16(entry.waveHeader, 24, entry.wavePreBlockCount);
	writeLe16(entry.waveHeader, 30, waveTypeCode(entry.type));
	entry.waveHeader[32] = wavePhaseMask(entry.phase);
	writeLe32(entry.waveHeader, 36, (uint32_t)entry.waveBlockCount * waveSamplesPerBlock);
	entry.waveCapturing = false;
	entry.waveFinalized = true;
	entry.waveAvailable = entry.waveBlockCount > 0;
	if (!entry.waveAvailable && !entry.waveReason.length()) entry.waveReason = "no_wave_frames";
}

// Calcula el tamano binario Wave incluyendo cabecera y CRC final.
// entry: entrada Wave finalizada.
static size_t getWaveSizeLocked(const EventEntry& entry)
{
	if (!entry.waveAvailable) return 0;
	return waveHeaderBytes + ((size_t)entry.waveBlockCount * waveFrameBytes) + sizeof(uint32_t);
}

// Agrega la descripcion de canales y escalas del binario Fast RMS.
// dest: JSON destino al que se agregan los campos.
static void appendFastRmsLayoutJson(String& dest)
{
	dest += ",\"channels\":[\"vr\",\"vs\",\"vt\",\"ir\",\"is\",\"it\",\"in\",\"vn\"],\"sampleBytes\":16,\"channelBits\":14,\"noData\":16383";
	dest += ",\"scales\":{\"voltageVPerBit\":0.025,\"currentAPerBit\":0.01}";
}

// Agrega la descripcion de canales y escalas del binario Wave.
// dest: JSON destino al que se agregan los campos.
static void appendWaveLayoutJson(String& dest)
{
	dest += ",\"channels\":[\"ir\",\"vr\",\"is\",\"vs\",\"it\",\"vt\",\"in\"]";
	dest += ",\"scales\":{\"voltageVPerBit\":0.25,\"currentAPerBit\":0.1}";
}

// Arma el JSON final que comparten REST y MQTT.
// entry: entrada lista. dest: String donde se escribe el JSON. includeSequence: agrega sequence para MQTT.
static void buildEventJsonLocked(const EventEntry& entry, String& dest, bool includeSequence)
{
	dest = "{\"schemaVersion\":1";
	if (entry.testRamp) dest += ",\"testPattern\":\"triangle-int12-v1\"";
	if (includeSequence) {
		dest += ",\"sequence\":";
		dest += String(managerNextSequence());
	}
	dest += ",\"eventId\":";
	appendJsonString(dest, entry.eventId);
	dest += ",\"deviceId\":";
	appendJsonString(dest, managerGetDeviceId());
	dest += ",\"category\":\"wave\",\"type\":";
	appendJsonString(dest, entry.type);
	dest += ",\"timestamp\":";
	if (entry.timestampMs) managerAppendUint64(dest, entry.timestampMs);
	else dest += "null";
	dest += ",\"phases\":[";
	if (entry.phase.length()) appendJsonString(dest, entry.phase);
	dest += "],\"value\":";
	dest += String(entry.value, 3);
	dest += ",\"unit\":";
	appendJsonString(dest, entry.unit);
	dest += ",\"durationMs\":";
	if (entry.durationKnown) managerAppendUint64(dest, entry.durationMs);
	else dest += "null";
	dest += ",\"fastRms\":{\"available\":";
	dest += entry.fastRmsAvailable ? "true" : "false";
	if (entry.fastRmsAvailable) {
		dest += ",\"channelCount\":8,\"sampleRateHz\":100,\"sampleCount\":";
		dest += String(entry.fastRmsSize / fastRmsSampleBytes);
		appendFastRmsLayoutJson(dest);
		dest += ",\"triggerSample\":";
		dest += entry.fastRmsTriggerSample >= 0 ? String(entry.fastRmsTriggerSample) : String("null");
		dest += ",\"checksum\":";
		appendJsonString(dest, crcToHex(entry.fastRmsCrc32));
	}
	else {
		dest += ",\"reason\":";
		appendJsonString(dest, entry.fastRmsReason);
	}
	dest += "},\"wave\":{\"available\":";
	dest += entry.waveAvailable ? "true" : "false";
	if (entry.waveAvailable) {
		dest += ",\"channelCount\":7,\"sampleRateHz\":8000,\"blockCount\":";
		dest += String(entry.waveBlockCount);
		dest += ",\"samplesPerChannel\":";
		dest += String((uint32_t)entry.waveBlockCount * waveSamplesPerBlock);
		appendWaveLayoutJson(dest);
		dest += ",\"triggerSample\":";
		if (entry.waveTriggerSample >= 0 && entry.waveTriggerSample < entry.waveBlockCount * waveSamplesPerBlock)
			dest += String(entry.waveTriggerSample);
		else dest += "null";
		dest += ",\"size\":";
		dest += String(getWaveSizeLocked(entry));
		dest += ",\"checksum\":";
		appendJsonString(dest, crcToHex(entry.waveCrc32));
	}
	else {
		dest += ",\"reason\":";
		appendJsonString(dest, entry.waveReason);
	}
	dest += "},\"links\":{\"fastRms\":\"/events/fast-rms?eventId=";
	dest += entry.eventId;
	dest += "\",\"wave\":\"/events/wave?eventId=";
	dest += entry.eventId;
	dest += "\",\"clear\":\"/events/clear\"}}";
}

// Publica la notificacion MQTT final de una entrada aun disponible.
// eventId: identificador que se debe publicar.
static void publishEventNotification(const String& eventId)
{
	String payload;
	if (!lockEventStore()) return;
	int8_t index = findEventLocked(eventId);
	if (index >= 0 && eventEntries[index].ready) {
		buildEventJsonLocked(eventEntries[index], payload, true);
		eventEntries[index].lastNotifyMs = millis();
	}
	unlockEventStore();
	if (payload.length()) managerPublishEventNotification(payload);
}

void eventStoreInit()
{
	if (eventStoreMutex == nullptr) eventStoreMutex = xSemaphoreCreateMutex();
	if (fastRmsQueue == nullptr) fastRmsQueue = xQueueCreate(2, sizeof(PendingFastRms));
	if (completedEventQueue == nullptr) completedEventQueue = xQueueCreate(2, sizeof(EventEntry*));
	if (!fastRmsQueue || !completedEventQueue) debugE("No se pudieron crear las colas de eventos");
	bootId = String((uint32_t)esp_random(), HEX);
	if (bootId.length() > 4) bootId = bootId.substring(bootId.length() - 4);
}

String eventStoreNextEventId()
{
	return "boot-" + bootId + "-" + String(++bootEventCounter);
}

// Construye los buffers fuera del mutex y publica por el flujo normal; salidas: eventId y reason.
bool eventStoreCreateRampTest(String& eventId, String& reason)
{
	static std::atomic<bool> generating{false};
	if (generating.exchange(true)) {
		reason = "test_busy";
		return false;
	}
	EventEntry entry;
	entry.used = entry.testRamp = true;
	entry.eventId = eventStoreNextEventId();
	entry.type = "manual_capture";
	entry.phase = "R";
	entry.unit = "V";
	entry.value = 511.75f;
	entry.timestampMs = managerNowMs();
	entry.createdMs = millis();
	initWaveHeaderLocked(entry, entry.timestampMs);
	entry.waveTriggerSample = 0;
	entry.fastRmsTriggerSample = 0;
	const size_t rmsSize = 300 * fastRmsSampleBytes;
	const size_t rawSize = 128 * 7 * sizeof(int32_t);
	bool complete = memoryHasRoom(rmsSize + rawSize + maxWaveBlocks * waveFrameBytes, waveMinFreeHeap);
	std::unique_ptr<int32_t[]> raw(complete ? new (std::nothrow) int32_t[128 * 7] : nullptr);
	complete = complete && raw != nullptr;
	if (complete) entry.fastRmsData = (uint8_t*)malloc(rmsSize);
	complete = complete && entry.fastRmsData != nullptr;
	for (uint8_t block = 0; complete && block < maxWaveBlocks; block++) {
		complete = allocateWaveBlockLocked(entry, block);
		if (!complete) break;
		for (uint8_t part = 0; part < 6; part++) {
			for (uint16_t sample = 0; sample < 128; sample++) {
				int32_t value = eventTestRampValue(block * waveSamplesPerBlock + part * 128 + sample) * 65536;
				for (uint8_t channel = 0; channel < 7; channel++) raw[sample * 7 + channel] = value;
			}
			compressWaveBuffer12(raw.get(), entry.waveBlocks[block], part);
		}
		entry.waveBlockCount++;
	}
	if (complete) {
		for (uint16_t sample = 0; sample < 300; sample++) {
			uint16_t offset = sample * fastRmsSampleBytes;
			fastRMSData_t packed;
			packed.timeStamp = sample * 10;
			for (uint8_t channel = 0; channel < 8; channel++)
				packed.setRaw(channel, sample + 1);
			memcpy(entry.fastRmsData + offset, &packed, sizeof(packed));
		}
		entry.fastRmsSize = rmsSize;
		entry.fastRmsAvailable = entry.fastRmsFinalized = true;
		entry.fastRmsCrc32 = calcCrc32(entry.fastRmsData, rmsSize);
		finishWaveLocked(entry);
		entry.waveCrc32 = calcWaveCrcLocked(entry);
		updateReadyLocked(entry);
		complete = lockEventStore();
		reason = "event_store_busy";
		if (complete) {
			EventEntry* slot = reserveEventLocked(entry.eventId);
			clearEventLocked(*slot);
			*slot = std::move(entry);
			eventId = slot->eventId;
			unlockEventStore();
		}
	}
	else reason = "insufficient_heap";
	if (!complete) clearEventLocked(entry);
	generating = false;
	if (complete) {
		reason = "";
		publishEventNotification(eventId);
	}
	return complete;
}

void eventStoreBeginEvent(const String& eventId, uint64_t timestampMs)
{
	eventStoreServiceCapture();
	if (!eventId.length() || !fastRmsQueue || !completedEventQueue) return;
	if (activeCapture) {
		clearEventLocked(*activeCapture);
		delete activeCapture;
	}
	activeCapture = new (std::nothrow) EventEntry;
	if (!activeCapture) return;
	EventEntry* entry = activeCapture;
	entry->used = true;
	entry->eventId = eventId;
	entry->timestampMs = timestampMs;
	entry->createdMs = millis();
}

void eventStoreAddMeta(const String& eventId, const char* type, const char* phase, float value, const char* unit, uint64_t timestampMs, uint64_t durationMs, bool eventEnded)
{
	if (activeCapture && activeCapture->eventId == eventId) {
		EventEntry& entry = *activeCapture;
		if (!entry.type.length()) {
			entry.type = type;
			entry.phase = phase;
			entry.unit = unit;
			entry.value = value;
		}
		if (!entry.timestampMs && timestampMs) entry.timestampMs = timestampMs;
		if (eventEnded && entry.type == type && entry.phase == phase) {
			entry.durationKnown = true;
			entry.durationMs = durationMs;
		}
	}
}

bool eventStoreFinishFastRms(const String& eventId, const uint8_t* rmsData, size_t rmsDataSize, int32_t triggerSample)
{
	if (!eventId.length() || !fastRmsQueue) return false;
	PendingFastRms result = {};
	strncpy(result.eventId, eventId.c_str(), sizeof(result.eventId) - 1);
	result.size = rmsData != nullptr ? rmsDataSize : 0;
	result.triggerSample = triggerSample;
	if (result.size) {
		result.data = (uint8_t*)malloc(result.size);
		if (result.data) {
			memcpy(result.data, rmsData, result.size);
			result.crc32 = calcCrc32(result.data, result.size);
		}
	}
	if (xQueueSend(fastRmsQueue, &result, 0) == pdTRUE) return true;
	free(result.data);
	return false;
}

// Recibe el resultado RMS y entrega la propiedad del evento completo; solo MeterTask, sin parametros.
void eventStoreServiceCapture()
{
	PendingFastRms result = {};
	while (fastRmsQueue && xQueueReceive(fastRmsQueue, &result, 0) == pdTRUE) {
		if (!activeCapture || activeCapture->eventId != result.eventId) {
			free(result.data);
			continue;
		}
		EventEntry& entry = *activeCapture;
		free(entry.fastRmsData);
		entry.fastRmsData = result.data;
		entry.fastRmsSize = result.size;
		entry.fastRmsCrc32 = result.crc32;
		uint16_t count = result.size / fastRmsSampleBytes;
		entry.fastRmsTriggerSample = result.triggerSample >= 0 && result.triggerSample < count ? result.triggerSample : -1;
		entry.fastRmsAvailable = result.data != nullptr;
		entry.fastRmsFinalized = true;
		if (!entry.fastRmsAvailable) entry.fastRmsReason = result.size ? "allocation_failed" : "no_samples";
		// RMS fija el limite total de captura si dejaron de llegar frames Wave.
		if (!entry.waveFinalized) finishWaveLocked(entry);
	}
	if (!activeCapture || !completedEventQueue) return;
	if (!updateReadyLocked(*activeCapture) && !activeCapture->ready) return;
	if (xQueueSend(completedEventQueue, &activeCapture, 0) != pdTRUE) {
		EventEntry* oldest = nullptr;
		if (xQueueReceive(completedEventQueue, &oldest, 0) == pdTRUE) {
			clearEventLocked(*oldest);
			delete oldest;
			completionDrops++;
		}
		if (xQueueSend(completedEventQueue, &activeCapture, 0) != pdTRUE) return;
	}
	activeCapture = nullptr;
}

bool eventStoreStartWave(const String& eventId, uint64_t timestampMs, int64_t triggerOffset)
{
	if (!activeCapture || activeCapture->eventId != eventId) return false;
	EventEntry& entry = *activeCapture;
	initWaveHeaderLocked(entry, timestampMs);
	entry.waveFinalized = false;
	entry.waveAvailable = false;
	entry.waveReason = "";
	entry.waveCapturing = false;
	entry.waveBlockCount = 0;
	entry.wavePreBlockCount = 0;
	entry.waveReservedBlocks = 0;
	bool started = allocateWaveBlockLocked(entry, 0) && allocateWaveBlockLocked(entry, 1);
	if (!started) {
		for (uint8_t i = 0; i < maxWaveBlocks; i++) {
			if (entry.waveBlocks[i] != nullptr) free(entry.waveBlocks[i]);
			entry.waveBlocks[i] = nullptr;
		}
		entry.waveReservedBlocks = 0;
		entry.waveFinalized = true;
		entry.waveReason = !memoryHasRoom(waveFrameBytes, waveMinFreeHeap) ? "insufficient_heap" : "allocation_failed";
	}
	else {
		entry.waveCapturing = true;
		if (hasPreviousWaveFrame) {
			memcpy(entry.waveBlocks[0], previousWaveFrame, waveFrameBytes);
			entry.waveBlockCount = 1;
			entry.wavePreBlockCount = 1;
		}
		entry.waveTriggerSample = eventTriggerIndex(int64_t(entry.wavePreBlockCount) * waveSamplesPerBlock + triggerOffset);
	}
	return started;
}

void eventStoreSetWaveUnavailable(const String& eventId, const char* reason)
{
	if (activeCapture && activeCapture->eventId == eventId) {
		EventEntry& entry = *activeCapture;
		for (uint8_t i = 0; i < maxWaveBlocks; i++) {
			if (entry.waveBlocks[i] != nullptr) free(entry.waveBlocks[i]);
			entry.waveBlocks[i] = nullptr;
		}
		entry.waveBlockCount = 0;
		entry.wavePreBlockCount = 0;
		entry.waveReservedBlocks = 0;
		entry.waveCapturing = false;
		entry.waveFinalized = true;
		entry.waveAvailable = false;
		entry.waveReason = reason;
	}
}

void eventStoreEndWave(const String& eventId, const char* type, const char* phase)
{
	if (activeCapture && activeCapture->eventId == eventId) {
		EventEntry& entry = *activeCapture;
		if (entry.waveCapturing && entry.type == type && entry.phase == phase)
			entry.wavePostFramesLeft = 1;
	}
}

void eventStoreProcessWaveFrame(const uint8_t* frame, size_t frameSize, bool scopeActive)
{
	if (frame == nullptr || frameSize != waveFrameBytes) return;
	memcpy(previousWaveFrame, frame, frameSize);
	hasPreviousWaveFrame = true;
	if (activeCapture && activeCapture->waveCapturing) {
		EventEntry& entry = *activeCapture;
		if (scopeActive) {
			for (uint8_t block = 0; block < maxWaveBlocks; block++) {
				if (entry.waveBlocks[block] != nullptr) free(entry.waveBlocks[block]);
				entry.waveBlocks[block] = nullptr;
			}
			entry.waveBlockCount = 0;
			entry.wavePreBlockCount = 0;
			entry.waveReservedBlocks = 0;
			entry.waveCapturing = false;
			entry.waveFinalized = true;
			entry.waveAvailable = false;
			entry.waveReason = "scope_active";
		}
		else {
			uint8_t blockIndex = entry.waveBlockCount;
			if (!allocateWaveBlockLocked(entry, blockIndex)) {
				finishWaveLocked(entry);
			}
			else {
				memcpy(entry.waveBlocks[blockIndex], frame, waveFrameBytes);
				entry.waveBlockCount++;
				if (entry.wavePostFramesLeft > 0) entry.wavePostFramesLeft--;
				if (entry.waveBlockCount >= maxWaveBlocks || entry.wavePostFramesLeft == 0 && entry.durationKnown)
					finishWaveLocked(entry);
			}
		}
	}
}

void eventStoreLoop()
{
	uint32_t drops = completionDrops.exchange(0);
	if (drops) debugW("Cola de finalizacion llena: descartados %u eventos antiguos", drops);
	static EventEntry* pending = nullptr;
	if (!pending && completedEventQueue && xQueueReceive(completedEventQueue, &pending, 0) == pdTRUE) {
		if (pending->waveAvailable) pending->waveCrc32 = calcWaveCrcLocked(*pending);
	}
	if (pending && lockEventStore()) {
		EventEntry* slot = reserveEventLocked(pending->eventId);
		clearEventLocked(*slot);
		*slot = std::move(*pending);
		unlockEventStore();
		delete pending;
		pending = nullptr;
	}
	String notifications[maxStoredEvents];
	uint8_t notificationCount = 0;
	if (!lockEventStore()) return;
	for (uint8_t i = 0; i < maxStoredEvents; i++) {
		EventEntry& entry = eventEntries[i];
		if (!entry.used || !entry.ready) continue;
		if (millis() - entry.lastNotifyMs < eventNotifyPeriodMs) continue;
		notifications[notificationCount++] = entry.eventId;
	}
	unlockEventStore();
	for (uint8_t i = 0; i < notificationCount; i++) publishEventNotification(notifications[i]);
}

void eventStoreGetListJson(String& dest)
{
	dest = "{\"schemaVersion\":1,\"events\":[";
	if (!lockEventStore()) {
		dest += "]}";
		return;
	}
	bool first = true;
	for (uint8_t i = 0; i < maxStoredEvents; i++) {
		if (!eventEntries[i].used || !eventEntries[i].ready) continue;
		if (!first) dest += ',';
		first = false;
		String eventJson;
		buildEventJsonLocked(eventEntries[i], eventJson, false);
		dest += eventJson;
	}
	unlockEventStore();
	dest += "]}";
}

bool eventStoreGetEventJson(const String& eventId, String& dest)
{
	if (!lockEventStore()) return false;
	int8_t index = findEventLocked(eventId);
	bool found = index >= 0 && eventEntries[index].ready;
	if (found) buildEventJsonLocked(eventEntries[index], dest, false);
	unlockEventStore();
	return found;
}

bool eventStoreGetFastRmsSize(const String& eventId, size_t& dataSize)
{
	dataSize = 0;
	if (!lockEventStore()) return false;
	int8_t index = findEventLocked(eventId);
	bool found = index >= 0 && eventEntries[index].ready && eventEntries[index].fastRmsAvailable;
	if (found) dataSize = eventEntries[index].fastRmsSize;
	unlockEventStore();
	return found && dataSize > 0;
}

bool eventStoreGetWaveSize(const String& eventId, size_t& dataSize)
{
	dataSize = 0;
	if (!lockEventStore()) return false;
	int8_t index = findEventLocked(eventId);
	bool found = index >= 0 && eventEntries[index].ready && eventEntries[index].waveAvailable;
	if (found) dataSize = getWaveSizeLocked(eventEntries[index]);
	unlockEventStore();
	return found && dataSize > 0;
}

size_t eventStoreCopyFastRmsChunk(const String& eventId, size_t index, uint8_t* dest, size_t maxLen)
{
	if (dest == nullptr || maxLen == 0 || !lockEventStore()) return 0;
	int8_t itemIndex = findEventLocked(eventId);
	if (itemIndex < 0) {
		unlockEventStore();
		return 0;
	}
	EventEntry& entry = eventEntries[itemIndex];
	if (!entry.ready || !entry.fastRmsAvailable || index >= entry.fastRmsSize) {
		unlockEventStore();
		return 0;
	}
	size_t count = entry.fastRmsSize - index;
	if (count > maxLen) count = maxLen;
	memcpy(dest, entry.fastRmsData + index, count);
	unlockEventStore();
	return count;
}

size_t eventStoreCopyWaveChunk(const String& eventId, size_t index, uint8_t* dest, size_t maxLen)
{
	if (dest == nullptr || maxLen == 0 || !lockEventStore()) return 0;
	int8_t itemIndex = findEventLocked(eventId);
	if (itemIndex < 0) {
		unlockEventStore();
		return 0;
	}
	EventEntry& entry = eventEntries[itemIndex];
	size_t totalSize = getWaveSizeLocked(entry);
	if (!entry.ready || !entry.waveAvailable || index >= totalSize) {
		unlockEventStore();
		return 0;
	}
	size_t count = totalSize - index;
	if (count > maxLen) count = maxLen;
	size_t copied = 0;
	while (copied < count) {
		size_t offset = index + copied;
		if (offset < waveHeaderBytes) {
			size_t part = waveHeaderBytes - offset;
			if (part > count - copied) part = count - copied;
			memcpy(dest + copied, entry.waveHeader + offset, part);
			copied += part;
			continue;
		}
		size_t payloadSize = (size_t)entry.waveBlockCount * waveFrameBytes;
		if (offset < waveHeaderBytes + payloadSize) {
			size_t payloadOffset = offset - waveHeaderBytes;
			uint8_t block = payloadOffset / waveFrameBytes;
			size_t blockOffset = payloadOffset % waveFrameBytes;
			size_t part = waveFrameBytes - blockOffset;
			if (part > count - copied) part = count - copied;
			memcpy(dest + copied, entry.waveBlocks[block] + blockOffset, part);
			copied += part;
			continue;
		}
		uint8_t crcBytes[4] = {
			(uint8_t)(entry.waveCrc32 & 0xFF), (uint8_t)((entry.waveCrc32 >> 8) & 0xFF),
			(uint8_t)((entry.waveCrc32 >> 16) & 0xFF), (uint8_t)((entry.waveCrc32 >> 24) & 0xFF)
		};
		size_t crcOffset = offset - waveHeaderBytes - payloadSize;
		size_t part = sizeof(crcBytes) - crcOffset;
		if (part > count - copied) part = count - copied;
		memcpy(dest + copied, crcBytes + crcOffset, part);
		copied += part;
	}
	unlockEventStore();
	return copied;
}

bool eventStoreClearEvent(const String& eventId)
{
	if (!eventId.length() || !lockEventStore()) return false;
	int8_t index = findEventLocked(eventId);
	if (index >= 0) clearEventLocked(eventEntries[index]);
	unlockEventStore();
	return index >= 0;
}

uint32_t eventStoreEventCount()
{
	if (!lockEventStore()) return 0;
	uint32_t count = 0;
	for (uint8_t i = 0; i < maxStoredEvents; i++) {
		if (eventEntries[i].used && eventEntries[i].ready) count++;
	}
	unlockEventStore();
	return count;
}

uint32_t eventStoreWaveCount()
{
	if (!lockEventStore()) return 0;
	uint32_t count = 0;
	for (uint8_t i = 0; i < maxStoredEvents; i++) {
		if (eventEntries[i].used && eventEntries[i].ready && eventEntries[i].waveAvailable) count++;
	}
	unlockEventStore();
	return count;
}
