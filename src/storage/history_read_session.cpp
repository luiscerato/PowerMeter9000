#include "history_read_session.h"
#include "ESPAsyncWebServer.h"
#include <new>

constexpr size_t HistoryReadSession::pageSize;
static portMUX_TYPE historyReadMetricsMux = portMUX_INITIALIZER_UNLOCKED;
static HistoryReadSessionMetrics historyReadMetrics;

// Reserva recursos de esta descarga y dispara una tarea que lee NAND con prioridad baja.
bool HistoryReadSession::begin(NandPartition& partition, const HistoryReadPlan& plan, const uint8_t* activeSnapshot)
{
	if (active || taskHandle || !partition.isEnabled() || (!plan.storedPageCount && !plan.includesActivePage)) return false;
	releaseBuffers();
	for (uint8_t index = 0; index < bufferCount; ++index) {
		buffers[index] = new (std::nothrow) uint8_t[pageSize];
		if (!buffers[index]) {
			Serial.printf("[history] start failed session=%p reason=buffer_alloc index=%u heap=%u\n",
				this, index, ESP.getFreeHeap());
			releaseBuffers();
			return false;
		}
	}
	if (plan.includesActivePage) {
		snapshot = new (std::nothrow) uint8_t[pageSize];
		if (!snapshot || !activeSnapshot) {
			releaseBuffers();
			return false;
		}
		memcpy(snapshot, activeSnapshot, pageSize);
	}
	freeQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), freeQueueData, &freeQueueStorage);
	readyQueue = xQueueCreateStatic(bufferCount, sizeof(uint8_t), readyQueueData, &readyQueueStorage);
	completionQueue = xQueueCreateStatic(1, sizeof(NandIoResult), completionQueueData, &completionQueueStorage);
	if (!freeQueue || !readyQueue || !completionQueue) {
		releaseBuffers();
		return false;
	}
	for (uint8_t index = 0; index < bufferCount; ++index) xQueueSend(freeQueue, &index, 0);
	source = &partition;
	nextBlock = plan.firstBlock;
	nextPage = plan.firstPage;
	nextSequence = plan.firstSequence;
	remainingPhysicalPages = plan.physicalPageCount;
	totalPhysicalPages = plan.physicalPageCount;
	fromTimestamp = plan.fromTimestamp;
	toTimestamp = plan.toTimestamp;
	snapshotPending = plan.includesActivePage;
	// La respuesta conserva todas las paginas fisicas del tramo. Las paginas invalidas
	// se transmiten como huecos para mantener el archivo alineado en 2 KiB.
	totalBytes = size_t(plan.physicalPageCount + (plan.includesActivePage ? 1U : 0U)) * pageSize;
	producedBytes = 0;
	currentBuffer = bufferCount;
	currentLength = currentOffset = 0;
	producerFailed = false;
	waitingForBuffer = false;
	bufferWaitStartedUs = 0;
	lastConsumerMs = millis();
	portENTER_CRITICAL(&historyReadMetricsMux);
	sessionMetrics = {};
	portEXIT_CRITICAL(&historyReadMetricsMux);
	active = true;
	Serial.printf("[history] start session=%p pages=%lu bytes=%lu activePage=%u range=%lu..%lu heap=%u minHeap=%u\n",
		this, (unsigned long)totalPhysicalPages, (unsigned long)totalBytes,
		plan.includesActivePage ? 1U : 0U, (unsigned long)fromTimestamp,
		(unsigned long)toTimestamp, ESP.getFreeHeap(), ESP.getMinFreeHeap());
	if (xTaskCreatePinnedToCore(readTaskEntry, "historyRead", 3072, this, 1, &taskHandle, tskNO_AFFINITY) != pdPASS) {
		active = false;
		taskHandle = nullptr;
		Serial.printf("[history] start failed session=%p reason=task_create heap=%u\n", this, ESP.getFreeHeap());
		releaseBuffers();
		return false;
	}
	return true;
}

// Copia metricas acumuladas desde el arranque; output recibe la instantanea protegida.
void HistoryReadSession::getMetrics(HistoryReadSessionMetrics& output)
{
	portENTER_CRITICAL(&historyReadMetricsMux);
	output = historyReadMetrics;
	portEXIT_CRITICAL(&historyReadMetricsMux);
}

// Entrega bytes en orden al servidor web, sin tocar NAND desde el callback asincronico.
size_t HistoryReadSession::copyChunk(uint8_t* destination, size_t maxLength, size_t offset)
{
	if (!active || offset >= totalBytes || !destination) return 0;
	lastConsumerMs = millis();
	if (currentBuffer == bufferCount) {
		if (xQueueReceive(readyQueue, &currentBuffer, 0) != pdPASS) {
			const uint32_t nowUs = micros();
			const bool failed = producerFailed;
			portENTER_CRITICAL(&historyReadMetricsMux);
			++historyReadMetrics.emptyBufferCalls;
			++sessionMetrics.emptyBufferCalls;
			if (!waitingForBuffer) {
				waitingForBuffer = true;
				bufferWaitStartedUs = nowUs;
				++historyReadMetrics.emptyBufferEpisodes;
				++sessionMetrics.emptyBufferEpisodes;
			}
			if (failed) {
				const uint32_t waitUs = nowUs - bufferWaitStartedUs;
				historyReadMetrics.totalBufferWaitUs += waitUs;
				historyReadMetrics.lastBufferWaitUs = waitUs;
				if (waitUs > historyReadMetrics.maxBufferWaitUs) historyReadMetrics.maxBufferWaitUs = waitUs;
				sessionMetrics.totalBufferWaitUs += waitUs;
				sessionMetrics.lastBufferWaitUs = waitUs;
				if (waitUs > sessionMetrics.maxBufferWaitUs) sessionMetrics.maxBufferWaitUs = waitUs;
				waitingForBuffer = false;
			}
			portEXIT_CRITICAL(&historyReadMetricsMux);
			return failed ? 0 : RESPONSE_TRY_AGAIN;
		}
		if (waitingForBuffer) {
			const uint32_t waitUs = micros() - bufferWaitStartedUs;
			portENTER_CRITICAL(&historyReadMetricsMux);
			historyReadMetrics.totalBufferWaitUs += waitUs;
			historyReadMetrics.lastBufferWaitUs = waitUs;
			if (waitUs > historyReadMetrics.maxBufferWaitUs) historyReadMetrics.maxBufferWaitUs = waitUs;
			sessionMetrics.totalBufferWaitUs += waitUs;
			sessionMetrics.lastBufferWaitUs = waitUs;
			if (waitUs > sessionMetrics.maxBufferWaitUs) sessionMetrics.maxBufferWaitUs = waitUs;
			waitingForBuffer = false;
			portEXIT_CRITICAL(&historyReadMetricsMux);
		}
		currentLength = min(pageSize, totalBytes - offset);
		currentOffset = 0;
	}
	const size_t count = min(maxLength, currentLength - currentOffset);
	memcpy(destination, buffers[currentBuffer] + currentOffset, count);
	currentOffset += count;
	if (currentOffset == currentLength) {
		xQueueSend(freeQueue, &currentBuffer, 0);
		currentBuffer = bufferCount;
	}
	if (offset + count == totalBytes) active = false;
	return count;
}

// Indica que no queda una respuesta ni una tarea usando los buffers de esta sesion y libera su memoria temporal.
bool HistoryReadSession::isAvailable()
{
	if (active || taskHandle) return false;
	releaseBuffers();
	return true;
}

// Ingresa a la tarea productora y se elimina al terminar la sesion.
void HistoryReadSession::readTaskEntry(void* parameter)
{
	static_cast<HistoryReadSession*>(parameter)->readTaskLoop();
	vTaskDelete(nullptr);
}

// Precarga paginas de NAND y agrega la copia RAM congelada como ultima pagina del archivo.
void HistoryReadSession::readTaskLoop()
{
	const uint32_t startedAtMs = millis();
	uint32_t lastReadElapsedUs = 0;
	uint32_t nandFailureCount = 0;
	uint32_t invalidPageCount = 0;
	const char* stopReason = "complete";
	while (active && producedBytes < totalBytes) {
		uint8_t bufferIndex = 0;
		if (xQueueReceive(freeQueue, &bufferIndex, pdMS_TO_TICKS(250)) != pdPASS) {
			if (millis() - lastConsumerMs > 5000U) {
				stopReason = "consumer_idle_timeout";
				active = false;
			}
			continue;
		}
		bool ready = false;
		const bool readPhysicalPage = remainingPhysicalPages != 0;
		if (readPhysicalPage) {
			NandIoResult result{};
			const uint16_t readBlock = nextBlock;
			const uint8_t readPage = nextPage;
			while (xQueueReceive(completionQueue, &result, 0) == pdPASS) {}
			bool submitted = false;
			const uint32_t submitStartedMs = millis();
			while (active && !submitted && uint32_t(millis() - submitStartedMs) < 5000U) {
				submitted = source->submitReadPage(nextBlock, nextPage, 0, buffers[bufferIndex], pageSize,
					StoragePriority::low, completionQueue);
				if (!submitted) {
					if (uint32_t(millis() - lastConsumerMs) > 5000U) {
						stopReason = "consumer_idle_submit";
						active = false;
					}
					else vTaskDelay(pdMS_TO_TICKS(10));
				}
			}
			if (!submitted) {
				if (active) stopReason = "submit_timeout";
				producerFailed = true;
				active = false;
				xQueueSend(freeQueue, &bufferIndex, 0);
				break;
			}

			bool completed = false;
			while (!completed) {
				completed = xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(250)) == pdPASS;
				if (!completed && uint32_t(millis() - lastConsumerMs) > 5000U) {
					stopReason = "consumer_idle_waiting_nand";
					active = false;
				}
			}
			if (!active) {
				if (!strcmp(stopReason, "complete")) stopReason = "consumer_closed";
				producerFailed = true;
				break;
			}
			lastReadElapsedUs = result.elapsedUs;
			if (!result.success) {
				++nandFailureCount;
				Serial.printf("[history] nand_fail session=%p block=%u page=%u error=%u status=%02X elapsedUs=%lu\n",
					this, readBlock, readPage, uint8_t(result.error), result.nandStatus,
					(unsigned long)result.elapsedUs);
			}
			const bool validPage = result.success && isValidPage(buffers[bufferIndex]);
			if (result.success && !validPage) ++invalidPageCount;
			if (!validPage) {
				// Un payload con ECC o CRC fallido ocupa su lugar con una pagina invalida.
				// Asi el cliente conserva el archivo alineado y puede omitir solo ese hueco.
				memset(buffers[bufferIndex], 0xFF, pageSize);
			}
			ready = true;
			--remainingPhysicalPages;
			if (remainingPhysicalPages && !advancePage()) {
				stopReason = "partition_end";
				break;
			}
		} else if (snapshotPending) {
			memcpy(buffers[bufferIndex], snapshot, pageSize);
			snapshotPending = false;
			ready = true;
		}
		if (!ready) {
			if (remainingPhysicalPages || snapshotPending) {
				xQueueSend(freeQueue, &bufferIndex, 0);
				continue;
			}
			producerFailed = true;
			stopReason = "no_data_ready";
			active = false;
			break;
		}
		producedBytes += pageSize;
		while (active && xQueueSend(readyQueue, &bufferIndex, pdMS_TO_TICKS(250)) != pdPASS) {
			if (millis() - lastConsumerMs > 5000U) {
				stopReason = "consumer_idle_ready_queue";
				active = false;
			}
		}
		const uint32_t pagesRead = totalPhysicalPages - remainingPhysicalPages;
		if (readPhysicalPage && ready && pagesRead && pagesRead % 128U == 0) {
			HistoryReadSessionMetrics metrics;
			uint32_t currentWaitUs = 0;
			portENTER_CRITICAL(&historyReadMetricsMux);
			metrics = sessionMetrics;
			if (waitingForBuffer) currentWaitUs = micros() - bufferWaitStartedUs;
			portEXIT_CRITICAL(&historyReadMetricsMux);
			Serial.printf("[history] progress session=%p pages=%lu/%lu bytes=%lu/%lu elapsedMs=%lu nandUs=%lu nandFail=%lu invalid=%lu emptyCalls=%lu emptyEpisodes=%lu waitCurrentUs=%lu waitLastUs=%lu waitMaxUs=%lu waitTotalUs=%llu readyQ=%u freeQ=%u heap=%u minHeap=%u stackFree=%u\n",
				this, (unsigned long)pagesRead, (unsigned long)totalPhysicalPages,
				(unsigned long)producedBytes, (unsigned long)totalBytes,
				(unsigned long)(millis() - startedAtMs), (unsigned long)lastReadElapsedUs,
				(unsigned long)nandFailureCount, (unsigned long)invalidPageCount,
				(unsigned long)metrics.emptyBufferCalls, (unsigned long)metrics.emptyBufferEpisodes,
				(unsigned long)currentWaitUs, (unsigned long)metrics.lastBufferWaitUs,
				(unsigned long)metrics.maxBufferWaitUs, (unsigned long long)metrics.totalBufferWaitUs,
				(unsigned)uxQueueMessagesWaiting(readyQueue), (unsigned)uxQueueMessagesWaiting(freeQueue),
				ESP.getFreeHeap(), ESP.getMinFreeHeap(), (unsigned)uxTaskGetStackHighWaterMark(nullptr));
		}
	}
	if (producedBytes < totalBytes) {
		producerFailed = true;
		active = false;
		if (!strcmp(stopReason, "complete")) stopReason = "incomplete";
	}
	HistoryReadSessionMetrics metrics;
	uint32_t currentWaitUs = 0;
	portENTER_CRITICAL(&historyReadMetricsMux);
	metrics = sessionMetrics;
	if (waitingForBuffer) currentWaitUs = micros() - bufferWaitStartedUs;
	portEXIT_CRITICAL(&historyReadMetricsMux);
	Serial.printf("[history] producer_end session=%p reason=%s failed=%u active=%u pages=%lu/%lu bytes=%lu/%lu elapsedMs=%lu nandFail=%lu invalid=%lu emptyCalls=%lu emptyEpisodes=%lu waitCurrentUs=%lu waitLastUs=%lu waitMaxUs=%lu waitTotalUs=%llu heap=%u minHeap=%u stackFree=%u\n",
		this, stopReason, producerFailed ? 1U : 0U, active ? 1U : 0U,
		(unsigned long)(totalPhysicalPages - remainingPhysicalPages), (unsigned long)totalPhysicalPages,
		(unsigned long)producedBytes, (unsigned long)totalBytes,
		(unsigned long)(millis() - startedAtMs), (unsigned long)nandFailureCount,
		(unsigned long)invalidPageCount, (unsigned long)metrics.emptyBufferCalls,
		(unsigned long)metrics.emptyBufferEpisodes, (unsigned long)currentWaitUs,
		(unsigned long)metrics.lastBufferWaitUs, (unsigned long)metrics.maxBufferWaitUs,
		(unsigned long long)metrics.totalBufferWaitUs, ESP.getFreeHeap(), ESP.getMinFreeHeap(),
		(unsigned)uxTaskGetStackHighWaterMark(nullptr));
	taskHandle = nullptr;
}

// Libera memoria temporal de una descarga que ya termino.
void HistoryReadSession::releaseBuffers()
{
	for (uint8_t index = 0; index < bufferCount; ++index) {
		delete[] buffers[index];
		buffers[index] = nullptr;
	}
	delete[] snapshot;
	snapshot = nullptr;
}

// Avanza una pagina dentro de la particion, con salto sobre bloques no utilizables.
bool HistoryReadSession::advancePage()
{
	if (++nextPage < WinbondSpiNand::pagesPerBlock) return true;
	nextPage = 0;
	nextBlock = source->nextUsableBlock(nextBlock);
	return nextBlock != NandPartition::invalidBlock;
}

// Verifica el formato y los CRC de una pagina leida desde NAND.
bool HistoryReadSession::isValidPage(const uint8_t* data) const
{
	const auto* header = reinterpret_cast<const HistoryFormat::PageHeader*>(data);
	const uint32_t config = uint32_t(header->groupConfig[0]) | uint32_t(header->groupConfig[1]) << 8 | uint32_t(header->groupConfig[2]) << 16;
	const size_t sampleSize = HistoryFormat::sampleSize(header->formatVersion, config);
	const size_t payloadBytes = size_t(header->sampleCount) * sampleSize;
	return sampleSize && header->sampleCount && header->samplePeriod &&
		HistoryFormat::isValidGroupConfig(header->formatVersion, config) && payloadBytes <= HistoryFormat::payloadSize &&
		header->headerCrc16 == HistoryFormat::crc16(data, 16) &&
		header->payloadCrc16 == HistoryFormat::crc16(data + HistoryFormat::headerSize, payloadBytes);
}

// Indica si una pagina valida se solapa con el intervalo temporal de esta descarga.
bool HistoryReadSession::pageIntersectsRange(const uint8_t* data) const
{
	const auto* header = reinterpret_cast<const HistoryFormat::PageHeader*>(data);
	const uint32_t endTimestamp = header->timestamp + uint32_t(header->sampleCount - 1) * header->samplePeriod / 10U;
	return endTimestamp >= fromTimestamp && header->timestamp <= toTimestamp;
}
