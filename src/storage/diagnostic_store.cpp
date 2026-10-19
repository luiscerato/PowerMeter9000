#include "diagnostic_store.h"
#include <esp_system.h>
#include <esp_timer.h>
#include <stdlib.h>
#include <string.h>

// Conserva la referencia a la particion que administrara este store.
DiagnosticStore::DiagnosticStore(NandPartition& targetPartition) : partition(targetPartition) {}

// Crea la cola de resultados y recupera el ultimo bloque escrito.
bool DiagnosticStore::begin()
{
	ready = false;
	if (!partition.isEnabled()) return false;
	if (!buffers[0]) {
		uint8_t* pages = static_cast<uint8_t*>(malloc(2 * pageSize));
		if (!pages) return false;
		buffers[0] = pages;
		buffers[1] = pages + pageSize;
	}
	if (!completionQueue) completionQueue = xQueueCreateStatic(2, sizeof(NandIoResult), completionData, &completionControl);
	if (!completionQueue) return false;
	if (!viewerBuffer) viewerBuffer = static_cast<uint8_t*>(malloc(pageSize));
	if (!viewerBuffer) return false;
	if (!viewerQueue) viewerQueue = xQueueCreateStatic(1, sizeof(NandIoResult), viewerQueueData, &viewerQueueControl);
	if (!viewerQueue) return false;
	if (!viewerMutex) viewerMutex = xSemaphoreCreateMutexStatic(&viewerMutexControl);
	if (!viewerMutex) return false;
	NandPartitionInfo info;
	partition.getInfo(info);
	metrics.capacityBytes = info.usableBytes;
	metrics.recordsThisRun = 0;
	bootId = esp_random();
	return recover();
}

// Invalida el escritor despues de terminar cualquier solicitud que conserve su buffer.
bool DiagnosticStore::stop()
{
	const bool idle = waitPending();
	ready = false;
	return idle;
}

// Espera la escritura pendiente y desactiva el store antes de borrar la particion.
bool DiagnosticStore::prepareFormat()
{
	if (!partition.isEnabled() || !buffers[0] || !completionQueue) return false;
	const bool wasReady = ready;
	ready = false;
	formatting = true;
	const bool pendingDone = waitPending();
	if (!pendingDone && pendingBuffer != noBuffer) {
		formatting = false;
		ready = wasReady;
		return false;
	}
	formatFirstBlock = partition.nextUsableBlock(NandPartition::invalidBlock);
	NandPartitionInfo info;
	partition.getInfo(info);
	portENTER_CRITICAL(&metricsMutex);
	metrics.formatting = true;
	metrics.formatDone = false;
	metrics.formatSuccess = false;
	metrics.formatBlocksDone = 0;
	metrics.formatBlocksTotal = info.usableBlockCount;
	metrics.formatBytesDone = 0;
	metrics.formatBytesErased = 0;
	metrics.formatBytesTotal = info.usableBytes;
	metrics.formatPercent = 0;
	portEXIT_CRITICAL(&metricsMutex);
	if (formatFirstBlock != NandPartition::invalidBlock && info.usableBlockCount > 0) return true;
	formatting = false;
	ready = wasReady;
	return false;
}

// Borra todos los bloques utilizables y conserva el primer bloque para abrir el anillo vacio.
bool DiagnosticStore::erasePartition()
{
	if (!formatting || !viewerMutex || xSemaphoreTake(viewerMutex, pdMS_TO_TICKS(10000)) != pdTRUE) return false;
	const bool success = partition.eraseAllBlocks();
	NandPartitionFormatProgress progress;
	partition.getFormatProgress(progress);
	portENTER_CRITICAL(&metricsMutex);
	metrics.formatBlocksDone = progress.blocksDone;
	metrics.formatBlocksTotal = progress.blocksTotal;
	metrics.formatBytesDone = progress.bytesDone;
	metrics.formatBytesErased = progress.bytesErased;
	metrics.formatBytesTotal = progress.totalBytes;
	metrics.formatPercent = progress.percent;
	portEXIT_CRITICAL(&metricsMutex);
	xSemaphoreGive(viewerMutex);
	return success;
}

// Reinicia contadores y abre el primer bloque con su diccionario de modulos.
bool DiagnosticStore::completeFormat()
{
	if (!formatting || formatFirstBlock == NandPartition::invalidBlock) return false;
	portENTER_CRITICAL(&metricsMutex);
	const uint32_t capacityBytes = metrics.capacityBytes;
	const uint16_t formatBlocksDone = metrics.formatBlocksDone;
	const uint16_t formatBlocksTotal = metrics.formatBlocksTotal;
	const uint32_t formatBytesDone = metrics.formatBytesDone;
	const uint32_t formatBytesErased = metrics.formatBytesErased;
	const uint32_t formatBytesTotal = metrics.formatBytesTotal;
	const uint8_t formatPercent = metrics.formatPercent;
	metrics = DiagnosticStoreMetrics{};
	metrics.capacityBytes = capacityBytes;
	metrics.formatting = true;
	metrics.formatBlocksDone = formatBlocksDone;
	metrics.formatBlocksTotal = formatBlocksTotal;
	metrics.formatBytesDone = formatBytesDone;
	metrics.formatBytesErased = formatBytesErased;
	metrics.formatBytesTotal = formatBytesTotal;
	metrics.formatPercent = formatPercent;
	portEXIT_CRITICAL(&metricsMutex);
	usedBytes = recordCount = 0;
	pageSequence = blockSequence = 0;
	currentBlock = NandPartition::invalidBlock;
	currentPage = 1;
	activeBuffer = 0;
	pendingBuffer = noBuffer;
	memset(buffers[0], 0xFF, pageSize);
	memset(buffers[1], 0xFF, pageSize);
	const bool opened = beginBlock(formatFirstBlock);
	formatting = false;
	portENTER_CRITICAL(&metricsMutex);
	metrics.formatting = false;
	metrics.formatDone = true;
	metrics.formatSuccess = opened;
	if (opened) {
		metrics.formatBlocksDone = metrics.formatBlocksTotal;
		metrics.formatBytesDone = metrics.formatBytesTotal;
		metrics.formatPercent = 100;
	}
	portEXIT_CRITICAL(&metricsMutex);
	formatFirstBlock = NandPartition::invalidBlock;
	return opened;
}

// Informa que el borrado fallo y evita escribir sobre una particion incompleta.
void DiagnosticStore::abortFormat()
{
	ready = false;
	formatting = false;
	formatFirstBlock = NandPartition::invalidBlock;
	portENTER_CRITICAL(&metricsMutex);
	metrics.formatting = false;
	metrics.formatDone = true;
	metrics.formatSuccess = false;
	portEXIT_CRITICAL(&metricsMutex);
}

// Devuelve el estado atomico del formateo.
bool DiagnosticStore::isFormatting() const
{
	return formatting;
}

// Agrega un fragmento a la pagina actual y aplica flush por nivel.
bool DiagnosticStore::append(const DiagnosticChunk& chunk)
{
	if (!ready || !partition.isEnabled()) return false;
	poll();
	if (!ready) return false;
	const uint16_t recordSize = recordHeaderSize + chunk.textLength;
	if (chunk.textLength > DiagnosticLogger::textPerChunk || recordSize > pageSize - pageHeaderSize) return false;
	if (usedBytes && size_t(usedBytes) + recordSize > pageSize - pageHeaderSize && !flushPage()) return false;
	if (!usedBytes) {
		pageOpenedMs = uint64_t(esp_timer_get_time()) / 1000;
		firstUnixMs = chunk.unixUs / 1000;
	}
	encodeRecord(buffers[activeBuffer] + pageHeaderSize + usedBytes, chunk);
	usedBytes += recordSize;
	++recordCount;
	portENTER_CRITICAL(&metricsMutex);
	metrics.activeBytes = usedBytes;
	++metrics.recordsThisRun;
	portEXIT_CRITICAL(&metricsMutex);
	const DiagnosticLevel level = DiagnosticLevel(chunk.level);
	const bool finalPart = (chunk.flags & DiagnosticFlags::hasMore) == 0;
	if (finalPart && (level == DiagnosticLevel::error ||
		(level == DiagnosticLevel::warn && usedBytes >= (pageSize - pageHeaderSize) / 2))) return flushPage();
	return true;
}

// Libera el buffer cuya programacion NAND ya termino.
void DiagnosticStore::poll()
{
	if (!completionQueue) return;
	NandIoResult result;
	while (xQueueReceive(completionQueue, &result, 0) == pdPASS) processResult(result);
}

// Escribe la pagina parcial al cumplir 15 segundos desde su primer registro.
void DiagnosticStore::flushIfDue()
{
	if (ready && usedBytes && uint64_t(esp_timer_get_time()) / 1000 - pageOpenedMs >= 15000) flushPage();
}

// Indica si el cursor y la particion siguen disponibles.
bool DiagnosticStore::isReady() const
{
	return ready && partition.isEnabled();
}

// Copia contadores y posicion actual a result.
void DiagnosticStore::getMetrics(DiagnosticStoreMetrics& result) const
{
	portENTER_CRITICAL(&metricsMutex);
	result = metrics;
	portEXIT_CRITICAL(&metricsMutex);
	NandPartitionFormatProgress format;
	partition.getFormatProgress(format);
	if (format.active || format.done) {
		result.formatting = format.active;
		result.formatDone = format.done;
		result.formatSuccess = format.success;
		result.formatBlocksDone = format.blocksDone;
		result.formatBlocksTotal = format.blocksTotal;
		result.formatBytesDone = format.bytesDone;
		result.formatBytesErased = format.bytesErased;
		result.formatBytesTotal = format.totalBytes;
		result.formatPercent = format.percent;
	}
	result.ready = isReady();
	const uint32_t committedPages = result.usedBlocks
		? uint32_t(result.usedBlocks - 1) * WinbondSpiNand::pagesPerBlock + result.currentPage : 0;
	result.usedPages = committedPages + (result.activeBytes ? 1U : 0U);
	result.usedBytes = committedPages * pageSize + result.activeBytes;
}

// Copia paginas pendientes y la pagina activa sin obligar a programarlas en NAND.
bool DiagnosticStore::captureSnapshot(DiagnosticBinarySnapshot& snapshot)
{
	if (!isReady()) return false;
	poll();
	if (!isReady()) return false;
	snapshot.block = currentBlock;
	snapshot.lastCommittedPage = uint8_t(currentPage - 1 - (pendingBuffer != noBuffer ? 1 : 0));
	snapshot.ramPageCount = 0;
	if (usedBytes) {
		uint8_t* data = snapshot.ramPages[snapshot.ramPageCount++];
		memcpy(data, buffers[activeBuffer], pageSize);
		write32(data, pageMagic);
		data[4] = formatVersion;
		data[5] = pageHeaderSize;
		write16(data + 6, recordCount);
		write32(data + 8, pageSequence);
		write32(data + 12, bootId);
		write16(data + 16, usedBytes);
		write16(data + 18, 1);
		write64(data + 20, firstUnixMs);
		uint32_t crc = updateCrc(0xFFFFFFFFU, data, 28);
		crc = updateCrc(crc, data + pageHeaderSize, usedBytes) ^ 0xFFFFFFFFU;
		write32(data + 28, crc);
	}
	if (pendingBuffer != noBuffer)
		memcpy(snapshot.ramPages[snapshot.ramPageCount++], buffers[pendingBuffer], pageSize);
	return true;
}

// Recorre las cabeceras escritas hasta hallar el bloque anterior a la captura.
bool DiagnosticStore::findPreviousSnapshotBlock(uint16_t fromBlock, uint16_t anchorBlock, uint16_t& previous)
{
	previous = NandPartition::invalidBlock;
	NandPartitionInfo info;
	partition.getInfo(info);
	if (fromBlock >= info.blockCount || anchorBlock >= info.blockCount) return false;
	if (!isReady() || !viewerMutex || xSemaphoreTake(viewerMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
	uint16_t candidate = fromBlock;
	if (findPreviousBlock(fromBlock, candidate) && candidate != anchorBlock) previous = candidate;
	xSemaphoreGive(viewerMutex);
	return true;
}

// Lee hasta capacity registros anteriores; el cursor señala la pagina y el byte exclusivo.
bool DiagnosticStore::readPrevious(DiagnosticStoredChunk* output, uint8_t capacity, bool hasCursor,
	uint16_t cursorBlock, uint8_t cursorPage, uint16_t cursorOffset, bool hasAnchor,
	uint16_t anchorBlock, uint8_t anchorPage, uint8_t& count, uint16_t& nextBlock,
	uint8_t& nextPage, uint16_t& nextOffset, uint16_t& nextAnchorBlock,
	uint8_t& nextAnchorPage, bool& done)
{
	count = 0;
	done = false;
	nextBlock = NandPartition::invalidBlock;
	nextPage = 0;
	nextOffset = 0;
	nextAnchorBlock = NandPartition::invalidBlock;
	nextAnchorPage = 0;
	if (!isReady() || !output || !capacity || capacity > 16 || !viewerMutex ||
		xSemaphoreTake(viewerMutex, pdMS_TO_TICKS(5000)) != pdTRUE) return false;

	uint16_t block = cursorBlock;
	uint8_t page = cursorPage;
	uint16_t limit = cursorOffset;
	if (hasCursor) {
		if (!hasAnchor || block == NandPartition::invalidBlock || anchorBlock == NandPartition::invalidBlock ||
			page == 0 || page >= WinbondSpiNand::pagesPerBlock || anchorPage == 0 ||
			anchorPage >= WinbondSpiNand::pagesPerBlock ||
			(limit != UINT16_MAX && limit > pageSize - pageHeaderSize)) {
			xSemaphoreGive(viewerMutex);
			return false;
		}
		nextAnchorBlock = anchorBlock;
		nextAnchorPage = anchorPage;
	}
	else {
		DiagnosticStoreMetrics snapshot;
		getMetrics(snapshot);
		block = snapshot.currentBlock;
		page = snapshot.currentPage > 1 ? uint8_t(snapshot.currentPage - 1) : 0;
		limit = UINT16_MAX;
		if (!page && !findPreviousBlock(block, block)) {
			done = true;
			xSemaphoreGive(viewerMutex);
			return true;
		}
		if (!page) page = WinbondSpiNand::pagesPerBlock - 1;
		anchorBlock = block;
		anchorPage = page;
		nextAnchorBlock = block;
		nextAnchorPage = page;
	}

	NandPartitionInfo info;
	partition.getInfo(info);
	const uint32_t maxPages = uint32_t(info.blockCount) * WinbondSpiNand::pagesPerBlock;
	uint32_t scanned = 0;
	while (scanned++ < maxPages) {
		bool validPage = readViewerPage(block, page, pageSize);
		uint16_t usedBytes = 0, recordCount = 0;
		uint16_t offsets[maxRecordsPerPage] = {};
		if (validPage) {
			usedBytes = read16(viewerBuffer + 16);
			recordCount = read16(viewerBuffer + 6);
			validPage = read32(viewerBuffer) == pageMagic && viewerBuffer[4] == formatVersion &&
				viewerBuffer[5] == pageHeaderSize && recordCount > 0 && recordCount <= maxRecordsPerPage &&
				usedBytes > 0 && usedBytes <= pageSize - pageHeaderSize && read16(viewerBuffer + 18) <= 1;
			if (validPage) {
				uint32_t crc = updateCrc(0xFFFFFFFFU, viewerBuffer, 28);
				crc = updateCrc(crc, viewerBuffer + pageHeaderSize, usedBytes) ^ 0xFFFFFFFFU;
				validPage = crc == read32(viewerBuffer + 28);
			}
		}

		uint16_t parsed = 0;
		if (validPage) {
			uint16_t position = 0;
			for (; parsed < recordCount; ++parsed) {
				const uint8_t* record = viewerBuffer + pageHeaderSize + position;
				if (position + recordHeaderSize > usedBytes) break;
				const uint16_t length = read16(record);
				if (length < recordHeaderSize || length > recordHeaderSize + DiagnosticLogger::textPerChunk ||
					position + length > usedBytes) break;
				offsets[parsed] = position;
				position += length;
			}
			validPage = parsed == recordCount && position == usedBytes;
		}

		if (validPage) {
			uint16_t eligible = 0;
			const uint16_t endLimit = limit == UINT16_MAX ? usedBytes : min(limit, usedBytes);
			while (eligible < parsed && offsets[eligible] + read16(viewerBuffer + pageHeaderSize + offsets[eligible]) <= endLimit)
				++eligible;
			const uint32_t pageSequence = read32(viewerBuffer + 8);
			const uint16_t take = min(uint16_t(capacity - count), eligible);
			for (uint16_t item = 0; item < take; ++item) {
				const uint16_t index = eligible - 1 - item;
				const uint8_t* record = viewerBuffer + pageHeaderSize + offsets[index];
				const uint16_t length = read16(record);
				DiagnosticStoredChunk& stored = output[count++];
				stored.chunk.moduleId = read32(record + 2);
				stored.chunk.level = record[6];
				stored.chunk.flags = record[7];
				stored.chunk.partIndex = record[8];
				stored.chunk.sourceLine = read16(record + 9);
				const uint64_t timeScale = read16(viewerBuffer + 18) == 0 ? 1000 : 1;
				stored.chunk.uptimeUs = read64(record + 11) * timeScale;
				stored.chunk.unixUs = read64(record + 19) * timeScale;
				stored.chunk.textLength = uint8_t(length - recordHeaderSize);
				memcpy(stored.chunk.text, record + recordHeaderSize, stored.chunk.textLength);
				stored.pageSequence = pageSequence;
				stored.recordIndex = uint8_t(index);
			}
			if (count == capacity) {
				if (take && eligible > take) {
					nextBlock = block;
					nextPage = page;
					nextOffset = offsets[eligible - take];
				}
				else if (previousPage(block, page) && (block != anchorBlock || page != anchorPage)) {
					nextBlock = block;
					nextPage = page;
					nextOffset = UINT16_MAX;
				}
				else done = true;
				break;
			}
		}
		if (!previousPage(block, page) || (block == anchorBlock && page == anchorPage)) {
			done = true;
			break;
		}
		limit = UINT16_MAX;
	}

	for (uint8_t left = 0, right = count ? uint8_t(count - 1) : 0; left < right; ++left, --right) {
		DiagnosticStoredChunk temp = output[left];
		output[left] = output[right];
		output[right] = temp;
	}
	if (scanned > maxPages) done = true;
	xSemaphoreGive(viewerMutex);
	return true;
}

// Lee una pagina NAND en el buffer independiente del visor.
bool DiagnosticStore::readViewerPage(uint16_t block, uint8_t page, size_t length)
{
	NandIoResult result;
	if (viewerReadPending) {
		if (xQueueReceive(viewerQueue, &result, pdMS_TO_TICKS(10000)) != pdPASS) return false;
		viewerReadPending = false;
	}
	while (xQueueReceive(viewerQueue, &result, 0) == pdPASS) {}
	if (++viewerRequestId == 0) ++viewerRequestId;
	if (!partition.submitReadPage(block, page, 0, viewerBuffer, length, StoragePriority::low,
		viewerQueue, viewerRequestId)) return false;
	viewerReadPending = true;
	if (xQueueReceive(viewerQueue, &result, pdMS_TO_TICKS(3000)) != pdPASS) return false;
	viewerReadPending = false;
	return result.requestId == viewerRequestId && result.success;
}

// Busca hacia atras un bloque con cabecera y CRC validos.
bool DiagnosticStore::findPreviousBlock(uint16_t fromBlock, uint16_t& block)
{
	NandPartitionInfo info;
	partition.getInfo(info);
	uint16_t candidate = fromBlock;
	for (uint16_t checked = 0; checked < info.blockCount; ++checked) {
		candidate = partition.previousUsableBlock(candidate);
		if (candidate == NandPartition::invalidBlock || !readViewerPage(candidate, 0, sizeof(scanBuffer))) continue;
		const uint8_t moduleCount = viewerBuffer[5];
		const uint16_t headerBytes = read16(viewerBuffer + 6);
		if (read32(viewerBuffer) != blockMagic || viewerBuffer[4] != formatVersion ||
			headerBytes != 20U + uint16_t(moduleCount) * 20U || headerBytes > sizeof(scanBuffer)) continue;
		uint32_t crc = updateCrc(0xFFFFFFFFU, viewerBuffer, 16);
		crc = updateCrc(crc, viewerBuffer + 20, headerBytes - 20) ^ 0xFFFFFFFFU;
		if (crc != read32(viewerBuffer + 16)) continue;
		block = candidate;
		return true;
	}
	return false;
}

// Retrocede a la pagina anterior o salta al bloque anterior con cabecera valida.
bool DiagnosticStore::previousPage(uint16_t& block, uint8_t& page)
{
	if (page > 1) {
		--page;
		return true;
	}
	if (!findPreviousBlock(block, block)) return false;
	page = WinbondSpiNand::pagesPerBlock - 1;
	return true;
}

// Lee un rango inicial de pagina mediante el servicio NAND y espera su resultado.
bool DiagnosticStore::readPage(uint16_t block, uint8_t page, uint8_t* data, size_t length)
{
	return partition.submitReadPage(block, page, 0, data, length, StoragePriority::low, completionQueue, 4) &&
		waitResult(4);
}

// Espera una solicitud propia y procesa respuestas de programaciones anteriores.
bool DiagnosticStore::waitResult(uint32_t requestId)
{
	NandIoResult result;
	for (;;) {
		if (xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(2000)) != pdPASS) return false;
		if (result.requestId == requestId) return result.success;
		processResult(result);
	}
}

// Busca el bloque mas reciente y avanza sin reutilizar paginas ya programadas.
bool DiagnosticStore::recover()
{
	NandPartitionInfo info;
	partition.getInfo(info);
	if (info.usableBlockCount < 2) return false;
	uint16_t newestBlock = NandPartition::invalidBlock;
	uint16_t validBlockCount = 0;
	uint32_t newestSequence = 0;
	uint32_t firstPageSequence = 0;
	for (uint16_t block = 0; block < info.blockCount; ++block) {
		if (!partition.isUsableBlock(block)) continue;
		if (!readPage(block, 0, scanBuffer, sizeof(scanBuffer))) return false;
		const uint8_t count = scanBuffer[5];
		const uint16_t headerBytes = read16(scanBuffer + 6);
		if (read32(scanBuffer) != blockMagic || scanBuffer[4] != formatVersion ||
			headerBytes != 20U + uint16_t(count) * 20U || headerBytes > sizeof(scanBuffer)) continue;
		uint32_t crc = updateCrc(0xFFFFFFFFU, scanBuffer, 16);
		crc = updateCrc(crc, scanBuffer + 20, headerBytes - 20) ^ 0xFFFFFFFFU;
		if (crc != read32(scanBuffer + 16)) continue;
		++validBlockCount;
		const uint32_t candidate = read32(scanBuffer + 8);
		if (newestBlock == NandPartition::invalidBlock || int32_t(candidate - newestSequence) > 0) {
			newestBlock = block;
			newestSequence = candidate;
			firstPageSequence = read32(scanBuffer + 12);
		}
	}
	if (newestBlock == NandPartition::invalidBlock) {
		portENTER_CRITICAL(&metricsMutex);
		metrics.usedBlocks = 0;
	portEXIT_CRITICAL(&metricsMutex);
		const uint16_t first = partition.nextUsableBlock(NandPartition::invalidBlock);
		return first != NandPartition::invalidBlock && beginBlock(first);
	}
	portENTER_CRITICAL(&metricsMutex);
	metrics.usedBlocks = validBlockCount;
	portEXIT_CRITICAL(&metricsMutex);
	currentBlock = newestBlock;
	blockSequence = newestSequence;
	uint8_t lastUsedPage = 0;
	for (uint8_t page = 1; page < WinbondSpiNand::pagesPerBlock; ++page) {
		if (!readPage(currentBlock, page, scanBuffer, pageHeaderSize)) return false;
		bool erased = true;
		for (uint8_t i = 0; i < pageHeaderSize; ++i) if (scanBuffer[i] != 0xFF) { erased = false; break; }
		if (erased) continue;
		lastUsedPage = page;
		const uint16_t bytes = read16(scanBuffer + 16);
		if (read32(scanBuffer) != pageMagic || scanBuffer[4] != formatVersion ||
			scanBuffer[5] != pageHeaderSize || !read16(scanBuffer + 6) ||
			bytes > pageSize - pageHeaderSize || !readPage(currentBlock, page, buffers[0], pageSize)) continue;
		uint32_t crc = updateCrc(0xFFFFFFFFU, buffers[0], 28);
		crc = updateCrc(crc, buffers[0] + pageHeaderSize, bytes) ^ 0xFFFFFFFFU;
		if (crc == read32(buffers[0] + 28)) {
			portENTER_CRITICAL(&metricsMutex);
			++metrics.recoveredPages;
			portEXIT_CRITICAL(&metricsMutex);
		}
	}
	currentPage = lastUsedPage + 1;
	pageSequence = firstPageSequence + lastUsedPage;
	if (currentPage >= WinbondSpiNand::pagesPerBlock) {
		const uint16_t next = partition.nextUsableBlock(currentBlock);
		return next != NandPartition::invalidBlock && beginBlock(next);
	}
	clearActive();
	ready = true;
	return true;
}

// Borra block, escribe una cabecera con diccionario de modulos y abre su pagina 1.
bool DiagnosticStore::beginBlock(uint16_t block)
{
	if (!waitPending()) return false;
	if (!partition.submitEraseBlock(block, StoragePriority::low, completionQueue, 2) || !waitResult(2)) return false;
	uint8_t* data = buffers[activeBuffer];
	memset(data, 0xFF, pageSize);
	const uint8_t count = DiagnosticLogger::getModuleCount();
	const uint16_t headerBytes = 20U + uint16_t(count) * 20U;
	if (headerBytes > pageSize) return false;
	write32(data, blockMagic);
	data[4] = formatVersion;
	data[5] = count;
	write16(data + 6, headerBytes);
	write32(data + 8, blockSequence + 1);
	write32(data + 12, pageSequence);
	for (uint8_t index = 0; index < count; ++index) {
		uint32_t moduleId;
		const char* name;
		if (!DiagnosticLogger::getModuleInfo(index, moduleId, name)) return false;
		uint8_t* entry = data + 20 + size_t(index) * 20;
		write32(entry, moduleId);
		memset(entry + 4, 0, 16);
		strncpy(reinterpret_cast<char*>(entry + 4), name, 15);
	}
	uint32_t crc = updateCrc(0xFFFFFFFFU, data, 16);
	crc = updateCrc(crc, data + 20, headerBytes - 20) ^ 0xFFFFFFFFU;
	write32(data + 16, crc);
	if (!partition.submitProgramPage(block, 0, data, pageSize, StoragePriority::low, completionQueue, 3) ||
		!waitResult(3)) return false;
	currentBlock = block;
	++blockSequence;
	currentPage = 1;
	portENTER_CRITICAL(&metricsMutex);
	const uint16_t usableBlocks = uint16_t(metrics.capacityBytes / WinbondSpiNand::blockDataSize);
	if (metrics.usedBlocks < usableBlocks) ++metrics.usedBlocks;
	portEXIT_CRITICAL(&metricsMutex);
	clearActive();
	ready = true;
	return true;
}

// Calcula CRC y programa la pagina activa, dejando libre el otro buffer.
bool DiagnosticStore::flushPage()
{
	if (!usedBytes) return true;
	if (!waitPending()) return false;
	uint8_t* data = buffers[activeBuffer];
	write32(data, pageMagic);
	data[4] = formatVersion;
	data[5] = pageHeaderSize;
	write16(data + 6, recordCount);
	write32(data + 8, pageSequence);
	write32(data + 12, bootId);
	write16(data + 16, usedBytes);
	write16(data + 18, 1);
	write64(data + 20, firstUnixMs);
	uint32_t crc = updateCrc(0xFFFFFFFFU, data, 28);
	crc = updateCrc(crc, data + pageHeaderSize, usedBytes) ^ 0xFFFFFFFFU;
	write32(data + 28, crc);
	if (!partition.submitProgramPage(currentBlock, currentPage, data, pageSize,
		StoragePriority::low, completionQueue, 1, reinterpret_cast<void*>(uintptr_t(activeBuffer)))) return false;
	pendingBuffer = activeBuffer;
	activeBuffer ^= 1U;
	++currentPage;
	++pageSequence;
	clearActive();
	if (currentPage >= WinbondSpiNand::pagesPerBlock) {
		const uint16_t next = partition.nextUsableBlock(currentBlock);
		if (next == NandPartition::invalidBlock || !beginBlock(next)) ready = false;
	}
	return true;
}

// Espera la unica programacion de datos pendiente antes de reutilizar su buffer.
bool DiagnosticStore::waitPending()
{
	if (pendingBuffer == noBuffer) return true;
	while (pendingBuffer != noBuffer) {
		NandIoResult result;
		if (xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(2000)) != pdPASS) return false;
		processResult(result);
	}
	return ready;
}

// Actualiza el estado del buffer cuya programacion se completo.
void DiagnosticStore::processResult(const NandIoResult& result)
{
	if (result.requestId != 1 || pendingBuffer == noBuffer) return;
	pendingBuffer = noBuffer;
	portENTER_CRITICAL(&metricsMutex);
	if (result.success) ++metrics.writtenPages;
	else ++metrics.failedWrites;
	portEXIT_CRITICAL(&metricsMutex);
	if (!result.success) ready = false;
}

// Inicializa los bytes borrados de la siguiente pagina sin tocar la pagina pendiente.
void DiagnosticStore::clearActive()
{
	memset(buffers[activeBuffer], 0xFF, pageSize);
	usedBytes = 0;
	recordCount = 0;
	firstUnixMs = 0;
	pageOpenedMs = 0;
	portENTER_CRITICAL(&metricsMutex);
	metrics.currentBlock = currentBlock;
	metrics.currentPage = currentPage;
	metrics.pageSequence = pageSequence;
	metrics.activeBytes = 0;
	portEXIT_CRITICAL(&metricsMutex);
}

// Serializa los metadatos y texto real de chunk, sin padding del struct RAM.
uint16_t DiagnosticStore::encodeRecord(uint8_t* dest, const DiagnosticChunk& chunk)
{
	const uint16_t length = recordHeaderSize + chunk.textLength;
	write16(dest, length);
	write32(dest + 2, chunk.moduleId);
	dest[6] = chunk.level;
	dest[7] = chunk.flags;
	dest[8] = chunk.partIndex;
	write16(dest + 9, chunk.sourceLine);
	write64(dest + 11, chunk.uptimeUs);
	write64(dest + 19, chunk.unixUs);
	memcpy(dest + recordHeaderSize, chunk.text, chunk.textLength);
	return length;
}

// Codifica value como uint16 little-endian.
void DiagnosticStore::write16(uint8_t* dest, uint16_t value)
{
	dest[0] = uint8_t(value);
	dest[1] = uint8_t(value >> 8);
}

// Codifica value como uint32 little-endian.
void DiagnosticStore::write32(uint8_t* dest, uint32_t value)
{
	for (uint8_t i = 0; i < 4; ++i) dest[i] = uint8_t(value >> (8 * i));
}

// Codifica value como uint64 little-endian.
void DiagnosticStore::write64(uint8_t* dest, uint64_t value)
{
	for (uint8_t i = 0; i < 8; ++i) dest[i] = uint8_t(value >> (8 * i));
}

// Lee uint16 little-endian desde data.
uint16_t DiagnosticStore::read16(const uint8_t* data)
{
	return uint16_t(data[0]) | (uint16_t(data[1]) << 8);
}

// Lee uint32 little-endian desde data.
uint32_t DiagnosticStore::read32(const uint8_t* data)
{
	uint32_t value = 0;
	for (uint8_t i = 0; i < 4; ++i) value |= uint32_t(data[i]) << (8 * i);
	return value;
}

// Lee ocho bytes little-endian desde data.
uint64_t DiagnosticStore::read64(const uint8_t* data)
{
	uint64_t value = 0;
	for (uint8_t i = 0; i < 8; ++i) value |= uint64_t(data[i]) << (8 * i);
	return value;
}

// Actualiza CRC32 IEEE con un tramo adicional de bytes.
uint32_t DiagnosticStore::updateCrc(uint32_t crc, const uint8_t* data, size_t length)
{
	for (size_t index = 0; index < length; ++index) {
		crc ^= data[index];
		for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320U : 0);
	}
	return crc;
}
