#include "nand_read_session.h"
#include "ESPAsyncWebServer.h"

constexpr size_t NandReadSession::pageSize;

// Prepara las colas y arranca el productor que lee la particion solicitada.
bool NandReadSession::begin(NandPartition& partition, size_t totalBytes)
{
	if (!totalBytes || totalBytes % pageSize) return false;
	return begin(partition, 0, 0, uint32_t(totalBytes / pageSize));
}

// Prepara una lectura contigua de paginas NAND desde una ubicacion relativa concreta.
bool NandReadSession::begin(NandPartition& partition, uint16_t block, uint8_t page, uint32_t pageCount, void (*finished)())
{
	const size_t totalBytes = size_t(pageCount) * pageSize;
	if (isActive() || !partition.isEnabled() || !totalBytes) return false;
	onReadFinished = finished;
	freeQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), freeQueueData, &freeQueueStorage);
	readyQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), readyQueueData, &readyQueueStorage);
	completionQueue = xQueueCreateStatic(1, sizeof(NandIoResult), completionQueueData, &completionQueueStorage);
	if (!freeQueue || !readyQueue || !completionQueue) return false;
	for (uint8_t index = 0; index < bufferCount; ++index) xQueueSend(freeQueue, &index, 0);
	source = &partition; total = totalBytes; produced = 0; currentBuffer = bufferCount; lastConsumerMs = millis();
	firstBlock = block; firstPage = page;
	currentLength = 0; currentOffset = 0; producerFailed = false; active = true;
	if (xTaskCreatePinnedToCore(readTaskEntry, "nandRead", 2048, this, 1, &taskHandle, tskNO_AFFINITY) != pdPASS) {
		active = false; taskHandle = nullptr; return false;
	}
	return true;
}

// Copia datos ya precargados o pide al servidor que reintente cuando el productor aun esta leyendo.
size_t NandReadSession::copyChunk(uint8_t* dest, size_t maxLen, size_t index)
{
	if (!active || index >= total) return 0;
	lastConsumerMs = millis();
	if (currentBuffer == bufferCount) {
		if (xQueueReceive(readyQueue, &currentBuffer, 0) != pdPASS)
			return producerFailed ? 0 : RESPONSE_TRY_AGAIN;
		currentLength = min(pageSize, total - index); currentOffset = 0;
	}
	const size_t count = min(maxLen, currentLength - currentOffset);
	memcpy(dest, buffers[currentBuffer] + currentOffset, count);
	currentOffset += count;
	if (currentOffset == currentLength) {
		xQueueSend(freeQueue, &currentBuffer, 0); currentBuffer = bufferCount;
	}
	if (index + count == total) active = false;
	return count;
}

// Indica si la sesion aun reserva buffers para una respuesta HTTP.
bool NandReadSession::isActive() const { return active || taskHandle; }

// Entra al productor de paginas y elimina la tarea al finalizar.
void NandReadSession::readTaskEntry(void* parameter)
{
	static_cast<NandReadSession*>(parameter)->readTaskLoop();
	vTaskDelete(nullptr);
}

// Lee paginas en orden desde la particion y entrega sus buffers al callback HTTP.
void NandReadSession::readTaskLoop()
{
	while (produced < total && active) {
		uint8_t bufferIndex;
		if (xQueueReceive(freeQueue, &bufferIndex, pdMS_TO_TICKS(250)) != pdPASS) {
			if (millis() - lastConsumerMs > 5000U) active = false;
			continue;
		}
		const uint32_t pageIndex = produced / pageSize + firstPage;
		const uint16_t block = firstBlock + pageIndex / WinbondSpiNand::pagesPerBlock;
		const uint8_t page = pageIndex % WinbondSpiNand::pagesPerBlock;
		const size_t length = min(pageSize, total - produced);
		NandIoResult result;
		while (xQueueReceive(completionQueue, &result, 0) == pdPASS) {}
		if (!source->submitReadPage(block, page, 0, buffers[bufferIndex], length,
			StoragePriority::low, completionQueue)) {
			producerFailed = true; break;
		}
		// Un timeout de consumidor no permite reciclar el buffer mientras NAND lo esta usando.
		while (xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(100)) != pdPASS) vTaskDelay(1);
		if (!result.success) {
			producerFailed = true; break;
		}
		produced += length;
		while (active && xQueueSend(readyQueue, &bufferIndex, pdMS_TO_TICKS(250)) != pdPASS) {
			if (millis() - lastConsumerMs > 5000U) active = false;
		}
	}
	if (produced < total) active = false;
	if (onReadFinished) { onReadFinished(); onReadFinished = nullptr; }
	taskHandle = nullptr;
}
