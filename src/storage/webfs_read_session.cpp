#include "webfs_read_session.h"
#include "ESPAsyncWebServer.h"
#include <new>

// Inicia la lectura de un archivo ya localizado; store contiene los bancos y file describe el rango.
bool WebFsReadSession::begin(WebFsStore& store, const WebFsFile& file)
{
	if (active || taskHandle || file.bank == WebFsBank::none || !file.entry.storedSize ||
		file.entry.dataOffset % WebFsFormat::pageSize) return false;
	freeQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), freeQueueData, &freeQueueStorage);
	readyQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), readyQueueData, &readyQueueStorage);
	completionQueue = xQueueCreateStatic(1, sizeof(NandIoResult), completionQueueData, &completionQueueStorage);
	if (!freeQueue || !readyQueue || !completionQueue) return false;
	for (uint8_t index = 0; index < bufferCount; ++index) {
		buffers[index] = new (std::nothrow) uint8_t[WebFsFormat::pageSize];
		if (!buffers[index]) {
			releaseBuffers();
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
	lastConsumerMs = millis();
	active = true;
	if (xTaskCreatePinnedToCore(readTaskEntry, "webfsRead", 2048, this, 1, &taskHandle, tskNO_AFFINITY) != pdPASS) {
		active = false;
		taskHandle = nullptr;
		releaseBuffers();
		return false;
	}
	return true;
}

// Copia bytes precargados o indica que AsyncWebServer debe reintentar; data, maxLength y offset describen la respuesta.
size_t WebFsReadSession::copyChunk(uint8_t* data, size_t maxLength, size_t offset)
{
	if (!active || offset >= totalBytes) {
		if (!taskHandle) releaseBuffers();
		return 0;
	}
	lastConsumerMs = millis();
	if (currentBuffer == bufferCount) {
		if (xQueueReceive(readyQueue, &currentBuffer, 0) != pdPASS)
			return producerFailed ? 0 : RESPONSE_TRY_AGAIN;
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
	if (offset + count == totalBytes) {
		active = false;
		releaseBuffers();
	}
	return count;
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
		if (xQueueReceive(freeQueue, &bufferIndex, pdMS_TO_TICKS(250)) != pdPASS) {
			if (millis() - lastConsumerMs > 5000U) active = false;
			continue;
		}
		NandIoResult stale;
		while (xQueueReceive(completionQueue, &stale, 0) == pdPASS) {}
		const uint32_t page = firstPage + (producedBytes + firstColumn) / WebFsFormat::pageSize;
		if (!source->readImagePage(bank, page, buffers[bufferIndex], completionQueue, uint32_t(producedBytes + 1))) {
			producerFailed = true;
			break;
		}
		NandIoResult result;
		if (xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(750)) != pdPASS || !result.success) {
			producerFailed = true;
			break;
		}
		const size_t pageBytes = producedBytes ? WebFsFormat::pageSize : WebFsFormat::pageSize - firstColumn;
		producedBytes += min(pageBytes, totalBytes - producedBytes);
		while (active && xQueueSend(readyQueue, &bufferIndex, pdMS_TO_TICKS(250)) != pdPASS) {
			if (millis() - lastConsumerMs > 5000U) active = false;
		}
	}
	if (producedBytes < totalBytes) active = false;
	taskHandle = nullptr;
}
