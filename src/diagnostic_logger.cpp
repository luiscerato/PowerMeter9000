#include "diagnostic_logger.h"
#include "storage/diagnostic_store.h"
#include <Preferences.h>
#include <esp_timer.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

DiagnosticLogger diagnosticLogger;

namespace {
struct ModuleName { uint32_t id; const char* name; };
constexpr ModuleName moduleNames[] = {
	{DiagnosticModule::logger, "logger"},
	{DiagnosticModule::main, "main"},
	{DiagnosticModule::storage, "storage"},
	{DiagnosticModule::wifi, "wifi"},
	{DiagnosticModule::mqtt, "mqtt"},
	{DiagnosticModule::tcp, "tcp"},
	{DiagnosticModule::health, "health"}
};

// Devuelve el nombre compilado de moduleId, o un nombre generico si no existe.
const char* moduleName(uint32_t moduleId)
{
	for (const auto& item : moduleNames) if (item.id == moduleId) return item.name;
	return "global";
}

// Convierte level a una etiqueta breve para Serial.
const char* levelName(uint8_t level)
{
	switch (DiagnosticLevel(level)) {
	case DiagnosticLevel::error: return "ERR";
	case DiagnosticLevel::warn: return "WRN";
	case DiagnosticLevel::info: return "INF";
	case DiagnosticLevel::verbose: return "VRB";
	case DiagnosticLevel::debug: return "DBG";
	default: return "OFF";
	}
}
}

// Busca moduleId en la lista fija de modulos.
int8_t DiagnosticLogger::moduleIndex(uint32_t moduleId)
{
	for (uint8_t i = 0; i < moduleCount; ++i) if (moduleNames[i].id == moduleId) return i;
	return -1;
}

// Entrega la entrada fija de modulo solicitada por index.
bool DiagnosticLogger::getModuleInfo(uint8_t index, uint32_t& moduleId, const char*& name)
{
	if (index >= moduleCount) return false;
	moduleId = moduleNames[index].id;
	name = moduleNames[index].name;
	return true;
}

// Cuenta los modulos de la tabla compilada.
uint8_t DiagnosticLogger::getModuleCount()
{
	return moduleCount;
}

// Reserva las colas estaticas y carga niveles/destinos persistidos.
bool DiagnosticLogger::begin()
{
	if (queue) return true;
	if (!recentChunks) recentChunks = static_cast<DiagnosticRecentChunk*>(malloc(recentChunkCount * sizeof(DiagnosticRecentChunk)));
	if (!recentChunks) return false;
	memset(recentChunks, 0, recentChunkCount * sizeof(DiagnosticRecentChunk));
	queue = xQueueCreateStatic(queueLength, sizeof(DiagnosticChunk), queueData, &queueControl);
	serialBuffer = xStreamBufferCreateStatic(serialBufferSize, 1, serialData, &serialControl);
	storeAccess = xSemaphoreCreateMutexStatic(&storeAccessControl);
	if (!queue || !serialBuffer || !storeAccess) return false;
	Preferences settings;
	if (settings.begin("diaglog", true)) {
		const uint8_t loadedLevel = settings.getUChar("level", uint8_t(DiagnosticLevel::info));
		if (loadedLevel <= uint8_t(DiagnosticLevel::debug)) globalLevel = loadedLevel;
		serialOutput = settings.getBool("serial", true);
		nandOutput = settings.getBool("nand", true);
		for (uint8_t i = 0; i < moduleCount; ++i) {
			char key[12];
			snprintf(key, sizeof(key), "m%lu", static_cast<unsigned long>(moduleNames[i].id));
			const uint8_t level = settings.getUChar(key, inheritLevel);
			if (level <= uint8_t(DiagnosticLevel::debug)) moduleLevels[i] = level;
		}
		settings.end();
	}
	return true;
}

// Inicia el consumidor y asocia el almacenamiento opcional store.
bool DiagnosticLogger::start(DiagnosticStore* targetStore)
{
	if (!queue || !serialBuffer) return false;
	attachStore(targetStore);
	if (task) return true;
	return xTaskCreatePinnedToCore(taskEntry, "diagLog", taskStackWords, this, 1, &task, tskNO_AFFINITY) == pdPASS;
}

// Cambia el destino NAND sin modificar la cola existente.
void DiagnosticLogger::attachStore(DiagnosticStore* targetStore)
{
	if (storeAccess) xSemaphoreTake(storeAccess, portMAX_DELAY);
	portENTER_CRITICAL(&stateMutex);
	store = targetStore;
	portEXIT_CRITICAL(&stateMutex);
	if (storeAccess) xSemaphoreGive(storeAccess);
}

// Consulta la asociacion del escritor NAND.
bool DiagnosticLogger::hasStore() const
{
	portENTER_CRITICAL(&stateMutex);
	const bool attached = store != nullptr;
	portEXIT_CRITICAL(&stateMutex);
	return attached;
}

// Detiene el escritor persistente, limpia la cola previa y prepara el Store asociado.
bool DiagnosticLogger::prepareStoreFormat()
{
	if (!queue || !storeAccess) return false;
	portENTER_CRITICAL(&stateMutex);
	const bool canStart = store && !formatTaskRunning;
	if (canStart) formatTaskRunning = true;
	portEXIT_CRITICAL(&stateMutex);
	if (!canStart) return false;
	xSemaphoreTake(storeAccess, portMAX_DELAY);
	portENTER_CRITICAL(&stateMutex);
	DiagnosticStore* targetStore = store;
	portEXIT_CRITICAL(&stateMutex);
	const bool prepared = targetStore && targetStore->prepareFormat();
	if (prepared) {
		xQueueReset(queue);
		if (recentChunks) {
			portENTER_CRITICAL(&recentMutex);
			memset(recentChunks, 0, recentChunkCount * sizeof(DiagnosticRecentChunk));
			recentSequence = 0;
			portEXIT_CRITICAL(&recentMutex);
		}
	}
	xSemaphoreGive(storeAccess);
	if (prepared) return true;
	portENTER_CRITICAL(&stateMutex);
	formatTaskRunning = false;
	portEXIT_CRITICAL(&stateMutex);
	return false;
}

// Finaliza el formato bajo el mutex del escritor y reactiva el logger.
bool DiagnosticLogger::finishStoreFormat(bool success)
{
	if (!storeAccess || xSemaphoreTake(storeAccess, portMAX_DELAY) != pdTRUE) return false;
	portENTER_CRITICAL(&stateMutex);
	DiagnosticStore* targetStore = store;
	portEXIT_CRITICAL(&stateMutex);
	bool ready = false;
	if (targetStore) {
		if (success) ready = targetStore->completeFormat();
		else targetStore->abortFormat();
	}
	xSemaphoreGive(storeAccess);
	portENTER_CRITICAL(&stateMutex);
	formatTaskRunning = false;
	portEXIT_CRITICAL(&stateMutex);
	return ready;
}

// Devuelve el estado de la tarea de formateo.
bool DiagnosticLogger::isFormatRunning() const
{
	portENTER_CRITICAL(&stateMutex);
	const bool running = formatTaskRunning;
	portEXIT_CRITICAL(&stateMutex);
	return running;
}

// Protege el escritor mientras copia sus dos buffers y la ultima pagina NAND confirmada.
bool DiagnosticLogger::captureStoreSnapshot(DiagnosticBinarySnapshot& snapshot)
{
	if (!storeAccess || xSemaphoreTake(storeAccess, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
	const bool captured = store && store->captureSnapshot(snapshot);
	if (captured) {
		portENTER_CRITICAL(&recentMutex);
		snapshot.recentSequence = recentSequence;
		portEXIT_CRITICAL(&recentMutex);
	}
	xSemaphoreGive(storeAccess);
	return captured;
}

// Evalua el umbral particular o global antes de construir un mensaje.
bool DiagnosticLogger::isEnabled(uint32_t moduleId, DiagnosticLevel level) const
{
	if (level == DiagnosticLevel::none) return false;
	const int8_t index = moduleIndex(moduleId);
	portENTER_CRITICAL(&stateMutex);
	const uint8_t selected = index >= 0 && moduleLevels[index] != inheritLevel ? moduleLevels[index] : globalLevel;
	portEXIT_CRITICAL(&stateMutex);
	return uint8_t(level) <= selected;
}

// Formatea una vez y encola hasta cuatro fragmentos sin esperar espacio.
void DiagnosticLogger::log(uint32_t moduleId, DiagnosticLevel level, uint16_t sourceLine, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	enqueueMessage(moduleId, level, sourceLine, format, args, 0);
	va_end(args);
}

// Encola un mensaje con espera acotada; timeoutMs evita bloquear indefinidamente al consumidor.
bool DiagnosticLogger::logWait(uint32_t moduleId, DiagnosticLevel level, uint16_t sourceLine,
	uint32_t timeoutMs, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const bool accepted = enqueueMessage(moduleId, level, sourceLine, format, args, pdMS_TO_TICKS(timeoutMs));
	va_end(args);
	return accepted;
}

// Formatea una vez y encola hasta cuatro fragmentos usando la espera indicada.
bool DiagnosticLogger::enqueueMessage(uint32_t moduleId, DiagnosticLevel level, uint16_t sourceLine,
	const char* format, va_list args, TickType_t waitTicks)
{
	if (!queue || !format || !isEnabled(moduleId, level)) return false;
	char message[maxParts * textPerChunk + 1];
	const int requested = vsnprintf(message, sizeof(message), format, args);
	if (requested < 0) return false;
	const size_t textLength = strnlen(message, sizeof(message) - 1);
	const uint8_t partCount = textLength ? uint8_t((textLength + textPerChunk - 1) / textPerChunk) : 1;
	const bool truncated = size_t(requested) >= sizeof(message);
	const int64_t uptimeUs = esp_timer_get_time();
	struct timeval now;
	gettimeofday(&now, nullptr);
	const bool utcValid = now.tv_sec >= 1577836800;
	const uint64_t unixUs = utcValid ? uint64_t(now.tv_sec) * 1000000 + uint64_t(now.tv_usec) : 0;
	uint8_t accepted = 0;
	bool complete = true;
	for (uint8_t part = 0; part < partCount; ++part) {
		DiagnosticChunk chunk;
		chunk.uptimeUs = uint64_t(uptimeUs);
		chunk.unixUs = unixUs;
		chunk.moduleId = moduleId;
		chunk.sourceLine = sourceLine;
		chunk.level = uint8_t(level);
		chunk.flags = (part + 1 < partCount ? DiagnosticFlags::hasMore : 0) |
			(truncated ? DiagnosticFlags::truncated : 0) | (utcValid ? DiagnosticFlags::utcValid : 0);
		chunk.partIndex = part + 1;
		const size_t offset = size_t(part) * textPerChunk;
		chunk.textLength = uint8_t(min(size_t(textPerChunk), textLength - offset));
		memcpy(chunk.text, message + offset, chunk.textLength);
		if (xQueueSend(queue, &chunk, waitTicks) != pdPASS) {
			complete = false;
			recordQueueLoss(partCount - part);
			portENTER_CRITICAL(&stateMutex);
			++metrics.queueFull;
			++pendingFullEvents;
			metrics.queueHighWater = queueLength;
			if (accepted) ++metrics.incompleteMessages;
			portEXIT_CRITICAL(&stateMutex);
			break;
		}
		++accepted;
		const uint8_t depth = uint8_t(uxQueueMessagesWaiting(queue));
		portENTER_CRITICAL(&stateMutex);
		++metrics.acceptedChunks;
		if (depth > metrics.queueHighWater) metrics.queueHighWater = depth;
		portEXIT_CRITICAL(&stateMutex);
	}
	if (truncated) {
		portENTER_CRITICAL(&stateMutex);
		++metrics.truncatedMessages;
		portEXIT_CRITICAL(&stateMutex);
	}
	return complete;
}

// Drena un tramo que cabe en el transmisor sin esperar espacio UART.
void DiagnosticLogger::pollSerial()
{
	if (!serialBuffer) return;
	const size_t available = Serial.availableForWrite();
	if (!available) return;
	char bytes[128];
	const size_t count = xStreamBufferReceive(serialBuffer, bytes, min(available, sizeof(bytes)), 0);
	if (count) Serial.write(reinterpret_cast<const uint8_t*>(bytes), count);
}

// Copia contadores protegidos para una consulta externa.
void DiagnosticLogger::getMetrics(DiagnosticLoggerMetrics& result) const
{
	portENTER_CRITICAL(&stateMutex);
	result = metrics;
	portEXIT_CRITICAL(&stateMutex);
	result.queueUsed = queue ? uint8_t(uxQueueMessagesWaiting(queue)) : 0;
	result.stackFreeWords = getStackFreeWords();
}

// Devuelve el minimo de palabras libres observado por FreeRTOS en la tarea del logger.
uint16_t DiagnosticLogger::getStackFreeWords() const
{
	return task ? uint16_t(uxTaskGetStackHighWaterMark(task)) : 0;
}

// Copia como maximo capacity fragmentos nuevos desde el anillo circular reciente.
uint8_t DiagnosticLogger::getRecentChunks(uint32_t after, uint32_t before, DiagnosticRecentChunk* output, uint8_t capacity,
	uint32_t& oldest, uint32_t& newest, bool& gap) const
{
	if (!output || !capacity) return 0;
	if (!recentChunks) return 0;
	capacity = min(capacity, uint8_t(recentChunkCount));
	uint8_t copied = 0;
	portENTER_CRITICAL(&recentMutex);
	newest = recentSequence;
	const uint32_t available = min(newest, uint32_t(recentChunkCount));
	oldest = available ? newest - available + 1 : 0;
	gap = false;
	uint32_t first;
	uint32_t last;
	if (before) {
		last = min(before - 1, newest);
		first = last >= capacity ? last - capacity + 1 : oldest;
		if (first < oldest) first = oldest;
		gap = last && last < oldest;
	} else {
		gap = after && oldest && after < oldest - 1;
		first = after ? after + 1 : (newest > capacity ? newest - capacity + 1 : oldest);
		if (first < oldest) first = oldest;
		last = newest;
	}
	for (uint32_t sequence = first; sequence && sequence <= last && copied < capacity; ++sequence) {
		const DiagnosticRecentChunk& item = recentChunks[(sequence - 1) % recentChunkCount];
		if (item.sequence != sequence) { gap = true; continue; }
		output[copied++] = item;
	}
	portEXIT_CRITICAL(&recentMutex);
	return copied;
}

// Copia los umbrales y destinos actuales sin consultar NVS.
void DiagnosticLogger::getConfig(DiagnosticLoggerConfig& result) const
{
	portENTER_CRITICAL(&stateMutex);
	result.globalLevel = DiagnosticLevel(globalLevel);
	result.serialEnabled = serialOutput;
	result.nandEnabled = nandOutput;
	memcpy(result.moduleLevels, moduleLevels, sizeof(moduleLevels));
	portEXIT_CRITICAL(&stateMutex);
}

// Guarda un nuevo nivel global en NVS.
bool DiagnosticLogger::setGlobalLevel(DiagnosticLevel level)
{
	if (uint8_t(level) > uint8_t(DiagnosticLevel::debug)) return false;
	Preferences settings;
	if (!settings.begin("diaglog", false)) return false;
	const bool saved = settings.putUChar("level", uint8_t(level)) == 1;
	settings.end();
	if (saved) {
		portENTER_CRITICAL(&stateMutex);
		globalLevel = uint8_t(level);
		portEXIT_CRITICAL(&stateMutex);
	}
	return saved;
}

// Guarda el nivel de un modulo conocido en NVS.
bool DiagnosticLogger::setModuleLevel(uint32_t moduleId, DiagnosticLevel level)
{
	const int8_t index = moduleIndex(moduleId);
	if (index < 0 || uint8_t(level) > uint8_t(DiagnosticLevel::debug)) return false;
	char key[12];
	snprintf(key, sizeof(key), "m%lu", static_cast<unsigned long>(moduleId));
	Preferences settings;
	if (!settings.begin("diaglog", false)) return false;
	const bool saved = settings.putUChar(key, uint8_t(level)) == 1;
	settings.end();
	if (saved) {
		portENTER_CRITICAL(&stateMutex);
		moduleLevels[index] = uint8_t(level);
		portEXIT_CRITICAL(&stateMutex);
	}
	return saved;
}

// Borra de NVS el umbral particular de un modulo conocido.
bool DiagnosticLogger::clearModuleLevel(uint32_t moduleId)
{
	const int8_t index = moduleIndex(moduleId);
	if (index < 0) return false;
	char key[12];
	snprintf(key, sizeof(key), "m%lu", static_cast<unsigned long>(moduleId));
	Preferences settings;
	if (!settings.begin("diaglog", false)) return false;
	const bool saved = !settings.isKey(key) || settings.remove(key);
	settings.end();
	if (saved) {
		portENTER_CRITICAL(&stateMutex);
		moduleLevels[index] = inheritLevel;
		portEXIT_CRITICAL(&stateMutex);
	}
	return saved;
}

// Guarda los dos destinos habilitados en NVS.
bool DiagnosticLogger::setOutputs(bool serialEnabled, bool nandEnabled)
{
	Preferences settings;
	if (!settings.begin("diaglog", false)) return false;
	const bool saved = settings.putBool("serial", serialEnabled) == 1 &&
		settings.putBool("nand", nandEnabled) == 1;
	settings.end();
	if (saved) {
		portENTER_CRITICAL(&stateMutex);
		serialOutput = serialEnabled;
		nandOutput = nandEnabled;
		portEXIT_CRITICAL(&stateMutex);
	}
	return saved;
}

// Deriva la tarea FreeRTOS al consumidor del logger.
void DiagnosticLogger::taskEntry(void* parameter)
{
	static_cast<DiagnosticLogger*>(parameter)->taskLoop();
	vTaskDelete(nullptr);
}

// Drena fragmentos y procesa resultados NAND sin bloquear productores.
void DiagnosticLogger::taskLoop()
{
	DiagnosticChunk chunk;
	for (;;) {
		DiagnosticStore* targetStore;
		xSemaphoreTake(storeAccess, portMAX_DELAY);
		portENTER_CRITICAL(&stateMutex);
		targetStore = store;
		portEXIT_CRITICAL(&stateMutex);
		if (targetStore) targetStore->poll();
		xSemaphoreGive(storeAccess);
		if (xQueueReceive(queue, &chunk, pdMS_TO_TICKS(10)) == pdPASS) {
			emit(chunk);
			emitLossNotice();
		}
		else emitLossNotice();
		xSemaphoreTake(storeAccess, portMAX_DELAY);
		portENTER_CRITICAL(&stateMutex);
		targetStore = store;
		portEXIT_CRITICAL(&stateMutex);
		if (targetStore) targetStore->flushIfDue();
		xSemaphoreGive(storeAccess);
	}
}

// Entrega un fragmento a NAND y Serial sin producir mas entradas de cola.
void DiagnosticLogger::emit(const DiagnosticChunk& chunk)
{
	DiagnosticStore* targetStore;
	bool useSerial, useNand;
	xSemaphoreTake(storeAccess, portMAX_DELAY);
	portENTER_CRITICAL(&stateMutex);
	targetStore = store;
	useSerial = serialOutput;
	useNand = nandOutput;
	portEXIT_CRITICAL(&stateMutex);
	const bool nandAccepted = !useNand || (targetStore &&
		(targetStore->isFormatting() || targetStore->append(chunk)));
	recordRecentChunk(chunk);
	xSemaphoreGive(storeAccess);
	if (!nandAccepted) {
		portENTER_CRITICAL(&stateMutex);
		++metrics.nandDrops;
		portEXIT_CRITICAL(&stateMutex);
	}
	if (useSerial) enqueueSerial(chunk);
}

// Guarda un fragmento emitido en la ventana circular consultable por la web.
void DiagnosticLogger::recordRecentChunk(const DiagnosticChunk& chunk)
{
	if (!recentChunks) return;
	portENTER_CRITICAL(&recentMutex);
	if (++recentSequence == 0) ++recentSequence;
	DiagnosticRecentChunk& item = recentChunks[(recentSequence - 1) % recentChunkCount];
	item.sequence = recentSequence;
	item.chunk = chunk;
	portEXIT_CRITICAL(&recentMutex);
}

// Copia una linea entera al buffer serie o cuenta su descarte.
void DiagnosticLogger::enqueueSerial(const DiagnosticChunk& chunk)
{
	char line[192];
	const int size = snprintf(line, sizeof(line), "[%llu] %s [%s:%u] %u %.*s\n",
		static_cast<unsigned long long>(chunk.unixUs ? chunk.unixUs : chunk.uptimeUs),
		levelName(chunk.level), moduleName(chunk.moduleId), unsigned(chunk.sourceLine),
		unsigned(chunk.partIndex), int(chunk.textLength), chunk.text);
	if (size <= 0) return;
	const size_t length = min(size_t(size), sizeof(line) - 1);
	if (xStreamBufferSpacesAvailable(serialBuffer) < length ||
		xStreamBufferSend(serialBuffer, line, length, 0) != length) {
		portENTER_CRITICAL(&stateMutex);
		++metrics.serialDrops;
		portEXIT_CRITICAL(&stateMutex);
	}
}

// Persiste una advertencia sintetica cuando vuelven a avanzar los mensajes.
void DiagnosticLogger::emitLossNotice()
{
	DiagnosticStore* targetStore;
	uint32_t lost;
	uint32_t fullEvents;
	uint8_t peak;
	bool useNand, useSerial;
	xSemaphoreTake(storeAccess, portMAX_DELAY);
	portENTER_CRITICAL(&stateMutex);
	targetStore = store;
	lost = pendingLostChunks;
	fullEvents = pendingFullEvents;
	peak = metrics.queueHighWater;
	useNand = nandOutput;
	useSerial = serialOutput;
	portEXIT_CRITICAL(&stateMutex);
	if (!lost || (useNand && (!targetStore || !targetStore->isReady()))) {
		xSemaphoreGive(storeAccess);
		return;
	}
	DiagnosticChunk notice;
	notice.moduleId = DiagnosticModule::logger;
	notice.level = uint8_t(DiagnosticLevel::warn);
	notice.partIndex = 1;
	notice.uptimeUs = uint64_t(esp_timer_get_time());
	const int size = snprintf(notice.text, sizeof(notice.text),
		"Cola llena %lu veces: %lu partes perdidas, pico %u/%u",
		static_cast<unsigned long>(fullEvents), static_cast<unsigned long>(lost),
		unsigned(peak), unsigned(queueLength));
	notice.textLength = uint8_t(min(size, int(sizeof(notice.text) - 1)));
	if (useNand && !targetStore->append(notice)) { xSemaphoreGive(storeAccess); return; }
	recordRecentChunk(notice);
	xSemaphoreGive(storeAccess);
	portENTER_CRITICAL(&stateMutex);
	pendingLostChunks -= min(pendingLostChunks, lost);
	pendingFullEvents -= min(pendingFullEvents, fullEvents);
	portEXIT_CRITICAL(&stateMutex);
	if (useSerial) enqueueSerial(notice);
}

// Cuenta fragmentos perdidos sin intentar escribir en la cola saturada.
void DiagnosticLogger::recordQueueLoss(uint32_t count)
{
	portENTER_CRITICAL(&stateMutex);
	metrics.lostChunks += count;
	pendingLostChunks += count;
	portEXIT_CRITICAL(&stateMutex);
}
