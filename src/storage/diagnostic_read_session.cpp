#include "diagnostic_read_session.h"
#include "../diagnostic_logger.h"
#include "ESPAsyncWebServer.h"
#include <esp_system.h>
#include <new>
#include <string.h>

constexpr size_t DiagnosticReadSession::pageSize;

// Captura ambos buffers RAM y la ultima pagina NAND visible para esta sesion.
bool DiagnosticReadSession::beginLatest(DiagnosticLogger& logger, DiagnosticStore& store, NandPartition& partition)
{
	if (!isAvailable()) return false;
	releaseBuffers();
	snapshot = new (std::nothrow) DiagnosticBinarySnapshot;
	if (!snapshot || !logger.captureStoreSnapshot(*snapshot)) {
		releaseBuffers();
		return false;
	}
	sessionToken = esp_random();
	if (!sessionToken) sessionToken = 1;
	blockIndex = 0;
	if (!beginBlock(partition, snapshot->block, snapshot->ramPageCount,
		uint8_t(snapshot->lastCommittedPage + 1))) {
		releaseBuffers();
		return false;
	}
	return true;
}

// Selecciona el bloque escrito inmediatamente anterior sin modificar el snapshot original.
bool DiagnosticReadSession::beginPrevious(DiagnosticStore& store, uint32_t token, uint16_t beforeBlock, bool& done)
{
	done = false;
	if (!matches(token) || !isAvailable() || !source) return false;
	uint16_t previous = NandPartition::invalidBlock;
	if (!store.findPreviousSnapshotBlock(beforeBlock, snapshot->block, previous)) return false;
	if (previous == NandPartition::invalidBlock) {
		done = true;
		lastUsedMs = millis();
		return true;
	}
	++blockIndex;
	return beginBlock(*source, previous, 0, WinbondSpiNand::pagesPerBlock);
}

// Reserva dos paginas de lectura y empieza una respuesta de longitud conocida.
bool DiagnosticReadSession::beginBlock(NandPartition& partition, uint16_t block, uint8_t ramCount, uint8_t nandCount)
{
	if (!nandCount || nandCount > WinbondSpiNand::pagesPerBlock || !partition.isEnabled()) return false;
	for (uint8_t index = 0; index < bufferCount; ++index) {
		if (!buffers[index]) buffers[index] = new (std::nothrow) uint8_t[pageSize];
		if (!buffers[index]) return false;
	}
	freeQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), freeData, &freeControl);
	readyQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), readyData, &readyControl);
	completionQueue = xQueueCreateStatic(1, sizeof(NandIoResult), completionData, &completionControl);
	if (!freeQueue || !readyQueue || !completionQueue) return false;
	for (uint8_t index = 0; index < bufferCount; ++index) xQueueSend(freeQueue, &index, 0);
	source = &partition;
	responseBlock = block;
	ramPageCount = ramCount;
	nandPageCount = nandCount;
	producedPages = 0;
	currentBuffer = bufferCount;
	currentOffset = 0;
	lastConsumerMs = lastUsedMs = millis();
	totalBytes = responseHeaderSize + size_t(ramCount + nandCount) * pageSize;
	memcpy(responseHeader, "DLOG", 4);
	responseHeader[4] = 1;
	responseHeader[5] = ramCount;
	responseHeader[6] = nandCount;
	responseHeader[7] = 0;
	for (uint8_t index = 0; index < 4; ++index) responseHeader[8 + index] = uint8_t(sessionToken >> (8 * index));
	responseHeader[12] = uint8_t(block);
	responseHeader[13] = uint8_t(block >> 8);
	responseHeader[14] = uint8_t(blockIndex);
	responseHeader[15] = uint8_t(blockIndex >> 8);
	for (uint8_t index = 0; index < 4; ++index)
		responseHeader[16 + index] = uint8_t(snapshot->recentSequence >> (8 * index));
	active = true;
	taskRunning = true;
	if (xTaskCreatePinnedToCore(taskEntry, "diagRead", 3072, this, 1, nullptr,
		tskNO_AFFINITY) != pdPASS) {
		active = false;
		taskRunning = false;
		return false;
	}
	return true;
}

// Entrega primero la cabecera, luego paginas completas ya precargadas.
size_t DiagnosticReadSession::copyChunk(uint8_t* destination, size_t capacity, size_t offset)
{
	if (!active || !destination || offset >= totalBytes) return 0;
	lastConsumerMs = millis();
	if (offset < responseHeaderSize) {
		const size_t count = min(capacity, size_t(responseHeaderSize) - offset);
		memcpy(destination, responseHeader + offset, count);
		return count;
	}
	if (currentBuffer == bufferCount) {
		if (xQueueReceive(readyQueue, &currentBuffer, 0) != pdPASS)
			return RESPONSE_TRY_AGAIN;
		currentOffset = 0;
	}
	const size_t count = min(capacity, pageSize - currentOffset);
	memcpy(destination, buffers[currentBuffer] + currentOffset, count);
	currentOffset += count;
	if (currentOffset == pageSize) {
		xQueueSend(freeQueue, &currentBuffer, 0);
		currentBuffer = bufferCount;
	}
	if (offset + count == totalBytes) active = false;
	return count;
}

// Indica que la respuesta y su tarea terminaron.
bool DiagnosticReadSession::isAvailable() const
{
	return !active && !taskRunning;
}

// Compara el token del cliente con la sesion vigente.
bool DiagnosticReadSession::matches(uint32_t token) const
{
	return token && token == sessionToken && snapshot;
}

// Limita la memoria de snapshots que un cliente dejo sin continuar.
bool DiagnosticReadSession::isExpired() const
{
	return isAvailable() && (!sessionToken || uint32_t(millis() - lastUsedMs) > 120000U);
}

// Descarta la sesion y libera sus buffers si ya no estan en uso.
void DiagnosticReadSession::invalidate()
{
	if (isAvailable()) releaseBuffers();
}

// Devuelve el largo binario exacto para el encabezado Content-Length.
size_t DiagnosticReadSession::contentLength() const
{
	return totalBytes;
}

// Elimina el snapshot y los buffers una vez que ninguna tarea los usa.
void DiagnosticReadSession::releaseBuffers()
{
	if (!isAvailable()) return;
	delete snapshot;
	snapshot = nullptr;
	for (uint8_t index = 0; index < bufferCount; ++index) {
		delete[] buffers[index];
		buffers[index] = nullptr;
	}
	sessionToken = 0;
}

// Ejecuta el productor de paginas en su propia tarea.
void DiagnosticReadSession::taskEntry(void* parameter)
{
	static_cast<DiagnosticReadSession*>(parameter)->taskLoop();
	vTaskDelete(nullptr);
}

// Mantiene hasta dos paginas listas para HTTP sin bloquear el callback en una lectura NAND.
void DiagnosticReadSession::taskLoop()
{
	const uint8_t totalPages = ramPageCount + nandPageCount;
	while (active && producedPages < totalPages) {
		uint8_t index = 0;
		if (xQueueReceive(freeQueue, &index, pdMS_TO_TICKS(250)) != pdPASS) {
			if (uint32_t(millis() - lastConsumerMs) > 5000U) active = false;
			continue;
		}
		bool ready = true;
		if (producedPages < ramPageCount)
			memcpy(buffers[index], snapshot->ramPages[producedPages], pageSize);
		else {
			const uint8_t page = uint8_t(nandPageCount - 1 - (producedPages - ramPageCount));
			NandIoResult result;
			while (xQueueReceive(completionQueue, &result, 0) == pdPASS) {}
			ready = source->submitReadPage(responseBlock, page, 0, buffers[index], pageSize,
				StoragePriority::low, completionQueue) &&
				xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(5000)) == pdPASS && result.success;
			if (!ready) memset(buffers[index], 0xFF, pageSize);
		}
		++producedPages;
		while (active && xQueueSend(readyQueue, &index, pdMS_TO_TICKS(250)) != pdPASS) {
			if (uint32_t(millis() - lastConsumerMs) > 5000U) active = false;
		}
	}
	taskRunning = false;
}
