#include "webfs_read_session.h"
#include "ESPAsyncWebServer.h"
#include "../memory_diagnostics.h"
#include <new>

// Inicia la lectura de un archivo ya localizado; store contiene los bancos y file describe el rango.
bool WebFsReadSession::begin(WebFsStore& store, const WebFsFile& file)
{
	if (active || taskHandle || file.bank == WebFsBank::none || !file.entry.storedSize ||
		file.entry.dataOffset % WebFsFormat::pageSize) return false;
	releaseBuffers();
	freeQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), freeQueueData, &freeQueueStorage);
	readyQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), readyQueueData, &readyQueueStorage);
	completionQueue = xQueueCreateStatic(1, sizeof(NandIoResult), completionQueueData, &completionQueueStorage);
	if (!freeQueue || !readyQueue || !completionQueue) return false;
	for (uint8_t index = 0; index < bufferCount; ++index) {
		buffers[index] = new (std::nothrow) uint8_t[WebFsFormat::pageSize];
		if (!buffers[index]) {
			releaseBuffers();
			memoryReportState("WebFS page allocation failed", WebFsFormat::pageSize);
			return false;
		}
		xQueueSend(freeQueue, &index, 0);
	}
	source = &store;
	bank = file.bank;
	firstPage = file.entry.dataOffset / WebFsFormat::pageSize;
	firstColumn = file.entry.dataOffset % WebFsFormat::pageSize;
	totalBytes = file.entry.storedSize;
	producedBytes = 0;
	currentBuffer = bufferCount;
	currentOffset = 0;
	currentLength = 0;
	producerFailed = false;
	if (++sessionId == 0) ++sessionId;
	active = true;
	if (xTaskCreatePinnedToCore(readTaskEntry, "webfsRead", 2048, this, 1, &taskHandle, tskNO_AFFINITY) != pdPASS) {
		active = false;
		taskHandle = nullptr;
		releaseBuffers();
		memoryReportState("WebFS task allocation failed", 2048);
		return false;
	}
	return true;
}

// Copia bytes precargados o indica que AsyncWebServer debe reintentar; data, maxLength y offset describen la respuesta.
size_t WebFsReadSession::copyChunk(uint8_t* data, size_t maxLength, size_t offset, uint32_t id)
{
	if (id != sessionId || !active) return 0;
	if (offset >= totalBytes) { cancel(id); return 0; }
	if (currentBuffer == bufferCount) {
		if (xQueueReceive(readyQueue, &currentBuffer, 0) != pdPASS) {
			if (!producerFailed) return RESPONSE_TRY_AGAIN;
			cancel(id);
			return 0;
		}
		currentOffset = 0;
		currentLength = min<size_t>(WebFsFormat::pageSize - firstColumn, totalBytes - offset);
		if (offset >= WebFsFormat::pageSize - firstColumn) currentLength = min<size_t>(WebFsFormat::pageSize, totalBytes - offset);
	}
	const size_t count = min(maxLength, currentLength - currentOffset);
	const uint16_t sourceOffset = offset ? 0 : firstColumn;
	memcpy(data, buffers[currentBuffer] + sourceOffset + currentOffset, count);
	currentOffset += count;
	if (currentOffset == currentLength) {
		xQueueSend(freeQueue, &currentBuffer, 0);
		currentBuffer = bufferCount;
	}
	if (offset + count == totalBytes) cancel(id);
	return count;
}

// Devuelve la generacion actual de la sesion; sin parametros.
uint32_t WebFsReadSession::getId() const { return sessionId; }

// Detiene la generacion id sin reciclar memoria que NAND todavia puede estar usando.
void WebFsReadSession::cancel(uint32_t id)
{
	if (id != sessionId) return;
	active = false;
}

// Indica si una respuesta sigue reteniendo la sesion; sin parametros.
bool WebFsReadSession::isActive() const
{
	return active || taskHandle;
}

// Libera las paginas reservadas para esta descarga; sin parametros.
void WebFsReadSession::releaseBuffers()
{
	for (uint8_t index = 0; index < bufferCount; ++index) {
		delete[] buffers[index];
		buffers[index] = nullptr;
	}
}

// Deriva la tarea FreeRTOS a esta sesion; parameter identifica el objeto.
void WebFsReadSession::readTaskEntry(void* parameter)
{
	static_cast<WebFsReadSession*>(parameter)->readTaskLoop();
	vTaskDelete(nullptr);
}

// Precarga paginas enteras de NAND y las ofrece al callback HTTP; sin parametros.
void WebFsReadSession::readTaskLoop()
{
	while (producedBytes < totalBytes && active) {
		uint8_t bufferIndex = 0;
		if (xQueueReceive(freeQueue, &bufferIndex, pdMS_TO_TICKS(50)) != pdPASS) continue;
		if (!active) break;
		NandIoResult stale;
		while (xQueueReceive(completionQueue, &stale, 0) == pdPASS) {}
		const uint32_t page = firstPage + (producedBytes + firstColumn) / WebFsFormat::pageSize;
		if (!source->readImagePage(bank, page, buffers[bufferIndex], completionQueue, uint32_t(producedBytes + 1))) {
			producerFailed = true;
			break;
		}
		NandIoResult result;
		// Una lectura aceptada conserva el buffer hasta recibir su resultado, incluso si HTTP se desconecta.
		while (xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(100)) != pdPASS) vTaskDelay(1);
		if (!result.success) {
			producerFailed = true;
			break;
		}
		const size_t pageBytes = producedBytes ? WebFsFormat::pageSize : WebFsFormat::pageSize - firstColumn;
		producedBytes += min(pageBytes, totalBytes - producedBytes);
		while (active && xQueueSend(readyQueue, &bufferIndex, pdMS_TO_TICKS(50)) != pdPASS) {}
	}
	// El ultimo bloque precargado no implica que HTTP ya lo haya consumido.
	while (active) ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
	releaseBuffers();
	taskHandle = nullptr;
}
