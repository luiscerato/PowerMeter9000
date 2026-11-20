#include "history_store.h"
#include "store_format_guard.h"

static constexpr uint32_t minimumHistoryTimestamp = 1704067200UL;

HistoryStore::HistoryStore(NandPartition& historyPartition) : partition(historyPartition) {}

// Pausa las operaciones externas y espera que terminen antes de reinicializar los cursores.
void HistoryStore::prepareFormat()
{
	formatPaused = true;
	while (activeCalls.load()) vTaskDelay(1);
}

// Reanuda las operaciones solo si el Store quedo montado correctamente.
void HistoryStore::finishFormat(bool success)
{
	if (success) formatPaused = false;
}

// Prepara la cola que recibe resultados de las dos paginas que pueden estar en vuelo.
bool HistoryStore::begin()
{
	if (!partition.isEnabled()) return false;
	memset(buffers, 0, sizeof(buffers));
	memset(headerBuffer, 0, sizeof(headerBuffer));
	pending[0] = pending[1] = false;
	activeBuffer = 0;
	block = 0;
	page = 0;
	sequence = 0;
	activeSampleSize = 0;
	latestStoredTimestamp = 0;
	oldestStoredTimestamp = 0;
	hasLatestStoredPage = false;
	preparedBlock = NandPartition::invalidBlock;
	preparedErasePending = false;
	preparedEraseReady = false;
	writeBlockReady = true;
	memset(pendingWrites, 0, sizeof(pendingWrites));
	storeMetrics = HistoryStoreMetrics{};
	if (!completionQueue)
		completionQueue = xQueueCreateStatic(2, sizeof(NandIoResult), completionQueueData, &completionQueueStorage);
	else xQueueReset(completionQueue);
	if (!planQueue)
		planQueue = xQueueCreateStatic(1, sizeof(NandIoResult), planQueueData, &planQueueStorage);
	else xQueueReset(planQueue);
	if (!planMutex) planMutex = xSemaphoreCreateMutex();
	if (!maintenanceQueue)
		maintenanceQueue = xQueueCreateStatic(1, sizeof(NandIoResult), maintenanceQueueData, &maintenanceQueueStorage);
	else xQueueReset(maintenanceQueue);
	if (!completionQueue || !planQueue || !planMutex || !maintenanceQueue || !recoverCursor()) return false;
	storeMetrics.mountReadyMs = millis();
	if (!writeBlockReady) {
		if (!prepareCurrentBlock()) return false;
		const uint32_t startedMs = millis();
		while (!writeBlockReady && preparedErasePending && uint32_t(millis() - startedMs) < 250U) {
			pollMaintenance();
			if (!writeBlockReady && preparedErasePending) vTaskDelay(1);
		}
		if (!writeBlockReady) return false;
	}
	storeMetrics.writeReadyMs = millis();
	return prepareNextBlock();
}

// Agrega una muestra y cierra la pagina si cambia el formato o no queda espacio.
bool HistoryStore::addSample(uint32_t timestamp, uint32_t groupConfig, uint16_t samplePeriod, const uint8_t* sample, size_t sampleSize)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return false;
	poll();
	if (!writeBlockReady && !preparedErasePending) prepareCurrentBlock();
	if (!sample || !sampleSize || sampleSize != HistoryFormat::sampleSize(groupConfig) || pending[activeBuffer] || !writeBlockReady) {
		++storeMetrics.rejectedSamples;
		return false;
	}
	HistoryFormat::PageHeader* header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	if (!header->sampleCount) startPage(timestamp, groupConfig, samplePeriod, sampleSize);
	header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	uint8_t config[3]; writeUint24(config, groupConfig);
	if (activeSampleSize != sampleSize || header->samplePeriod != samplePeriod || memcmp(header->groupConfig, config, 3) ||
		size_t(header->sampleCount + 1) * sampleSize > HistoryFormat::payloadSize) {
		if (!submitPage()) {
			++storeMetrics.rejectedSamples;
			return false;
		}
		startPage(timestamp, groupConfig, samplePeriod, sampleSize);
		header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	}
	memcpy(buffers[activeBuffer] + HistoryFormat::headerSize + size_t(header->sampleCount) * sampleSize, sample, sampleSize);
	++header->sampleCount;
	refreshActiveCrc();
	++storeMetrics.acceptedSamples;
	storeMetrics.activeSamples = header->sampleCount;
	return true;
}

// Cierra la pagina parcial actual para pruebas o cambios controlados de configuracion.
bool HistoryStore::flush()
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return false;
	poll();
	HistoryFormat::PageHeader* header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	return !header->sampleCount || submitPage();
}

// Recoge resultados y libera el buffer asociado a cada solicitud terminada.
void HistoryStore::poll()
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return;
	pollMaintenance();
	NandIoResult result;
	while (completionQueue && xQueueReceive(completionQueue, &result, 0) == pdPASS) {
		const uint8_t buffer = uint8_t(reinterpret_cast<uintptr_t>(result.context));
		if (buffer >= 2) continue;
		const bool verification = (result.requestId & verificationRequestFlag) != 0;
		if (verification) {
			pending[buffer] = false;
			pendingWrites[buffer].verificationPending = false;
			if (result.success && isPageValid(buffers[buffer])) commitVerifiedPage(buffer);
			else {
				++storeMetrics.failedPages;
				++storeMetrics.verificationFailures;
			}
		}
		else if (!result.success || !submitVerification(buffer)) {
			pending[buffer] = false;
			++storeMetrics.failedPages;
		}
	}
	storeMetrics.writePending = pending[0] || pending[1];
}

// Copia los contadores obtenidos durante la ultima busqueda del cursor historico.
void HistoryStore::getRecoveryMetrics(HistoryRecoveryMetrics& metrics) const
{
	metrics = recoveryMetrics;
}

// Copia el estado actual de la pagina RAM y de las escrituras en curso.
void HistoryStore::getMetrics(HistoryStoreMetrics& metrics) const
{
	metrics = storeMetrics;
	metrics.currentBlock = block;
	metrics.currentPage = page;
	metrics.writePending = pending[0] || pending[1];
	metrics.hasCommittedPages = hasLatestStoredPage;
	metrics.oldestTimestamp = oldestStoredTimestamp;
	metrics.newestTimestamp = latestStoredTimestamp;
	metrics.latestSequence = latestStoredSequence;
	metrics.preparedBlock = preparedBlock;
	metrics.erasePending = preparedErasePending;
	metrics.eraseReady = preparedEraseReady;
	metrics.writeBlockReady = writeBlockReady;
}

// Reemplaza el timestamp de la pagina aun residente en RAM.
bool HistoryStore::updateActiveTimestamp(uint32_t timestamp)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return false;
	if (pending[activeBuffer]) return false;
	auto* header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	if (!header->sampleCount) return false;
	header->timestamp = timestamp;
	refreshActiveCrc();
	return true;
}

// Devuelve el timestamp asociado a la pagina mas reciente encontrada al montar.
uint32_t HistoryStore::lastStoredTimestamp() const
{
	return latestStoredTimestamp;
}

// Copia los limites temporales actualmente conocidos; oldest puede ser cero tras una recuperacion incompleta.
void HistoryStore::getTimestampRange(uint32_t& oldestTimestamp, uint32_t& newestTimestamp) const
{
	oldestTimestamp = oldestStoredTimestamp;
	newestTimestamp = latestStoredTimestamp;
	const auto* header = reinterpret_cast<const HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	if (header->sampleCount) {
		const uint32_t activeEnd = header->timestamp + uint32_t(header->sampleCount - 1) * header->samplePeriod / 10U;
		if (activeEnd > newestTimestamp) newestTimestamp = activeEnd;
		if (!oldestTimestamp) oldestTimestamp = header->timestamp;
	}
}

// Inicializa header y payload borrado para una nueva pagina de muestras.
void HistoryStore::startPage(uint32_t timestamp, uint32_t groupConfig, uint16_t samplePeriod, size_t sampleSize)
{
	memset(buffers[activeBuffer], 0xFF, HistoryFormat::pageSize);
	auto* header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	header->timestamp = timestamp; writeUint24(header->sequence, sequence++); writeUint24(header->groupConfig, groupConfig);
	header->samplePeriod = samplePeriod; header->sampleCount = 0; header->formatVersion = HistoryFormat::version; header->pageFlags = 0;
	refreshActiveCrc();
	activeSampleSize = sampleSize;
	storeMetrics.activeSamples = 0;
}

// Finaliza la pagina activa, la encola y cambia al otro buffer disponible.
bool HistoryStore::submitPage()
{
	auto* header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	if (!header->sampleCount || pending[activeBuffer]) return false;
	if (page == WinbondSpiNand::pagesPerBlock - 1 && !isNextBlockPrepared()) {
		if (!preparedErasePending) prepareNextBlock();
		return false;
	}
	refreshActiveCrc();
	const uint32_t pageSequence = readUint24(header->sequence);
	const uint32_t lastTimestamp = header->timestamp + uint32_t(header->sampleCount - 1) * header->samplePeriod / 10U;
	if (!partition.submitProgramPage(block, page, buffers[activeBuffer], HistoryFormat::pageSize,
		StoragePriority::medium, completionQueue, pageSequence, reinterpret_cast<void*>(uintptr_t(activeBuffer)))) return false;
	pendingWrites[activeBuffer].block = block;
	pendingWrites[activeBuffer].page = page;
	pendingWrites[activeBuffer].sequence = pageSequence;
	pendingWrites[activeBuffer].lastTimestamp = lastTimestamp;
	pendingWrites[activeBuffer].verificationPending = false;
	pending[activeBuffer] = true;
	storeMetrics.writePending = true;
	const uint8_t nextBuffer = activeBuffer ^ 1U;
	if (pending[nextBuffer] || !advanceCursor()) return false;
	activeBuffer = nextBuffer; activeSampleSize = 0;
	return true;
}

// Recalcula los CRC de header y payload de la pagina RAM actualmente abierta.
void HistoryStore::refreshActiveCrc()
{
	auto* header = reinterpret_cast<HistoryFormat::PageHeader*>(buffers[activeBuffer]);
	const size_t payloadBytes = size_t(header->sampleCount) * activeSampleSize;
	header->payloadCrc16 = HistoryFormat::crc16(buffers[activeBuffer] + HistoryFormat::headerSize, payloadBytes);
	header->headerCrc16 = HistoryFormat::crc16(buffers[activeBuffer], 16);
}

// Encola una lectura completa para validar que la pagina NAND coincide con sus CRC almacenados.
bool HistoryStore::submitVerification(uint8_t buffer)
{
	if (buffer >= 2) return false;
	const PendingWrite& write = pendingWrites[buffer];
	if (!partition.submitReadPage(write.block, write.page, 0, buffers[buffer], HistoryFormat::pageSize,
		StoragePriority::medium, completionQueue, write.sequence | verificationRequestFlag,
		reinterpret_cast<void*>(uintptr_t(buffer)))) return false;
	pendingWrites[buffer].verificationPending = true;
	return true;
}

// Valida formato, limites y ambos CRC de una pagina completa antes de publicarla como historico.
bool HistoryStore::isPageValid(const uint8_t* data)
{
	if (!data) return false;
	const auto* header = reinterpret_cast<const HistoryFormat::PageHeader*>(data);
	const uint32_t groupConfig = readUint24(header->groupConfig);
	const size_t sampleSize = HistoryFormat::sampleSize(header->formatVersion, groupConfig);
	const size_t payloadBytes = size_t(header->sampleCount) * sampleSize;
	return sampleSize && header->sampleCount && header->samplePeriod &&
		HistoryFormat::isValidGroupConfig(header->formatVersion, groupConfig) && payloadBytes <= HistoryFormat::payloadSize &&
		header->headerCrc16 == HistoryFormat::crc16(data, 16) &&
		header->payloadCrc16 == HistoryFormat::crc16(data + HistoryFormat::headerSize, payloadBytes);
}

// Registra una pagina cuya relectura fue valida y actualiza los limites temporales conocidos.
void HistoryStore::commitVerifiedPage(uint8_t buffer)
{
	++storeMetrics.writtenPages;
	const PendingWrite& write = pendingWrites[buffer];
	if (!hasLatestStoredPage || isNewerSequence(write.sequence, latestStoredSequence)) {
		hasLatestStoredPage = true;
		latestStoredBlock = write.block;
		latestStoredPage = write.page;
		latestStoredSequence = write.sequence;
		latestStoredTimestamp = write.lastTimestamp;
		if (!oldestStoredTimestamp) oldestStoredTimestamp = latestStoredTimestamp;
	}
}

// Avanza pagina y luego bloque, usando el salto circular que entrega la particion.
bool HistoryStore::advanceCursor()
{
	if (++page < WinbondSpiNand::pagesPerBlock) return true;
	const uint16_t nextBlock = partition.nextUsableBlock(block);
	if (nextBlock == NandPartition::invalidBlock || nextBlock != preparedBlock || !preparedEraseReady) return false;
	block = nextBlock;
	page = 0;
	writeBlockReady = true;
	preparedBlock = NandPartition::invalidBlock;
	preparedEraseReady = false;
	return prepareNextBlock();
}

// Programa el borrado anticipado del bloque siguiente para que la rotacion no bloquee la captura.
bool HistoryStore::prepareNextBlock()
{
	if (preparedErasePending || preparedEraseReady) return true;
	const uint16_t nextBlock = partition.nextUsableBlock(block);
	if (nextBlock == NandPartition::invalidBlock || nextBlock == block) return false;
	NandIoResult staleResult;
	while (xQueueReceive(maintenanceQueue, &staleResult, 0) == pdPASS) {}
	if (!partition.submitEraseBlock(nextBlock, StoragePriority::low, maintenanceQueue, nextBlock)) return false;
	preparedBlock = nextBlock;
	preparedErasePending = true;
	return true;
}

// Borra el bloque recuperado que sera reutilizado inmediatamente despues de una vuelta circular.
bool HistoryStore::prepareCurrentBlock()
{
	if (preparedErasePending || preparedEraseReady) return preparedBlock == block;
	NandIoResult staleResult;
	while (xQueueReceive(maintenanceQueue, &staleResult, 0) == pdPASS) {}
	if (!partition.submitEraseBlock(block, StoragePriority::low, maintenanceQueue, block)) return false;
	preparedBlock = block;
	preparedErasePending = true;
	return true;
}

// Recoge el borrado anticipado y deja disponible el bloque solo despues de una confirmacion correcta.
void HistoryStore::pollMaintenance()
{
	NandIoResult result;
	while (maintenanceQueue && xQueueReceive(maintenanceQueue, &result, 0) == pdPASS) {
		if (result.requestId != preparedBlock) continue;
		preparedErasePending = false;
		preparedEraseReady = result.success;
		if (preparedBlock == block && !writeBlockReady) {
			writeBlockReady = result.success;
			preparedBlock = NandPartition::invalidBlock;
			preparedEraseReady = false;
			if (result.success) prepareNextBlock();
		}
		else if (!result.success) ++storeMetrics.failedPages;
	}
}

// Indica si el proximo bloque circular ya fue borrado por la tarea de I/O.
bool HistoryStore::isNextBlockPrepared() const
{
	return preparedEraseReady && preparedBlock == partition.nextUsableBlock(block);
}

// Escribe un entero de 24 bits en formato little-endian.
void HistoryStore::writeUint24(uint8_t* dest, uint32_t value)
{
	dest[0] = uint8_t(value); dest[1] = uint8_t(value >> 8); dest[2] = uint8_t(value >> 16);
}

// Reconstruye desde cabeceras: sondea cada 16 bloques, acota el tramo y revisa las paginas del bloque final.
bool HistoryStore::recoverCursor()
{
	recoveryMetrics = HistoryRecoveryMetrics{};
	latestStoredTimestamp = 0;
	oldestStoredTimestamp = 0;
	hasLatestStoredPage = false;
	const uint32_t startedUs = micros();
	NandPartitionInfo info; partition.getInfo(info);
	bool found = false; uint16_t candidateBlock = 0; uint32_t candidateSequence = 0;
	for (uint16_t relativeBlock = 0; relativeBlock < info.blockCount; relativeBlock += 16) {
		if (!partition.isUsableBlock(relativeBlock)) continue;
		HistoryFormat::PageHeader header; bool valid, erased;
		if (!readHeader(relativeBlock, 0, header, valid, erased)) return false;
		const uint32_t value = readUint24(header.sequence);
		if (valid && (!found || isNewerSequence(value, candidateSequence))) {
			found = true; candidateBlock = relativeBlock; candidateSequence = value;
		}
	}
	if (!found) { block = 0; page = 0; sequence = 0; recoveryMetrics.elapsedUs = micros() - startedUs; return true; }
	uint32_t candidateTimestamp = 0;
	uint16_t candidateSamples = 0;
	uint16_t candidatePeriod = 0;
	uint8_t candidatePage = 0;
	{
		HistoryFormat::PageHeader header; bool valid, erased;
		if (!readHeader(candidateBlock, 0, header, valid, erased)) return false;
		if (valid) {
			candidateTimestamp = header.timestamp;
			candidateSamples = header.sampleCount;
			candidatePeriod = header.samplePeriod;
		}
	}
	const uint16_t centerBlock = candidateBlock;
	for (int16_t offset = -15; offset <= 15; ++offset) {
		const uint16_t relativeBlock = uint16_t((int32_t(centerBlock) + offset + info.blockCount) % info.blockCount);
		if (!partition.isUsableBlock(relativeBlock)) continue;
		HistoryFormat::PageHeader header; bool valid, erased;
		if (!readHeader(relativeBlock, 0, header, valid, erased)) return false;
		const uint32_t value = readUint24(header.sequence);
		if (valid && isNewerSequence(value, candidateSequence)) {
			candidateBlock = relativeBlock; candidatePage = 0; candidateSequence = value; candidateTimestamp = header.timestamp;
			candidateSamples = header.sampleCount; candidatePeriod = header.samplePeriod;
		}
		if (!valid && !erased) {
			for (uint8_t pageIndex = 1; pageIndex < WinbondSpiNand::pagesPerBlock; ++pageIndex) {
				if (!readHeader(relativeBlock, pageIndex, header, valid, erased)) return false;
				const uint32_t pageSequence = readUint24(header.sequence);
				if (valid && isNewerSequence(pageSequence, candidateSequence)) {
					candidateBlock = relativeBlock; candidatePage = pageIndex; candidateSequence = pageSequence;
					candidateTimestamp = header.timestamp; candidateSamples = header.sampleCount; candidatePeriod = header.samplePeriod;
				}
			}
		}
	}
	for (uint8_t currentPage = 0; currentPage < WinbondSpiNand::pagesPerBlock; ++currentPage) {
		HistoryFormat::PageHeader header; bool valid, erased;
		if (!readHeader(candidateBlock, currentPage, header, valid, erased)) return false;
		if (valid && isNewerSequence(readUint24(header.sequence), candidateSequence)) {
			candidatePage = currentPage; candidateSequence = readUint24(header.sequence); candidateTimestamp = header.timestamp;
			candidateSamples = header.sampleCount; candidatePeriod = header.samplePeriod;
		}
	}
	block = candidateBlock; page = candidatePage; sequence = (candidateSequence + 1) & 0xFFFFFFU;
	latestStoredTimestamp = candidateTimestamp + uint32_t(candidateSamples ? candidateSamples - 1 : 0) * candidatePeriod / 10U;
	latestStoredBlock = candidateBlock;
	latestStoredPage = candidatePage;
	latestStoredSequence = candidateSequence;
	hasLatestStoredPage = true;
	if (!recoverOldestTimestamp()) return false;
	recoveryMetrics.historyFound = true; recoveryMetrics.elapsedUs = micros() - startedUs;
	if (++page < WinbondSpiNand::pagesPerBlock) return true;
	block = partition.nextUsableBlock(block);
	if (block == NandPartition::invalidBlock) return false;
	page = 0;
	writeBlockReady = false;
	return true;
}

// Retrocede por cabeceras de bloque y solo abre paginas extra si encuentra un bloque parcialmente valido.
bool HistoryStore::recoverOldestTimestamp()
{
	NandPartitionInfo info;
	partition.getInfo(info);
	if (!info.usableBlockCount) return false;
	uint16_t oldestBlock = latestStoredBlock;
	uint32_t oldestSequence = (latestStoredSequence - latestStoredPage) & 0xFFFFFFU;
	HistoryFormat::PageHeader header;
	bool valid = false, erased = false;
	if (!readHeader(oldestBlock, 0, header, valid, erased) || !valid || readUint24(header.sequence) != oldestSequence) {
		oldestStoredTimestamp = 0;
		return true;
	}
	oldestStoredTimestamp = header.timestamp >= minimumHistoryTimestamp ? header.timestamp : latestStoredTimestamp;
	uint16_t scanBlock = oldestBlock;
	for (uint16_t scanned = 1; scanned < info.usableBlockCount; ++scanned) {
		const uint16_t previousBlock = partition.previousUsableBlock(scanBlock);
		if (previousBlock == NandPartition::invalidBlock) return false;
		scanBlock = previousBlock;
		if (!readHeader(previousBlock, 0, header, valid, erased)) return false;
		if (erased) break;
		if (valid && isNewerSequence(oldestSequence, readUint24(header.sequence))) {
			oldestBlock = previousBlock;
			oldestSequence = readUint24(header.sequence);
			if (header.timestamp >= minimumHistoryTimestamp) oldestStoredTimestamp = header.timestamp;
			continue;
		}
		if (valid) continue;
		for (uint8_t pageIndex = 1; pageIndex < WinbondSpiNand::pagesPerBlock; ++pageIndex) {
			if (!readHeader(previousBlock, pageIndex, header, valid, erased)) return false;
			if (erased) break;
			const uint32_t pageSequence = readUint24(header.sequence);
			if (valid && isNewerSequence(oldestSequence, pageSequence)) {
				oldestBlock = previousBlock;
				oldestSequence = pageSequence;
				if (header.timestamp >= minimumHistoryTimestamp) oldestStoredTimestamp = header.timestamp;
			}
		}
	}
	return true;
}

// Lee los 20 bytes de header y valida su CRC sin traer el payload completo.
bool HistoryStore::readHeader(uint16_t relativeBlock, uint8_t pageIndex, HistoryFormat::PageHeader& header, bool& valid, bool& erased)
{
	++recoveryMetrics.headerReads;
	NandIoResult result;
	while (xQueueReceive(completionQueue, &result, 0) == pdPASS) {}
	if (!partition.submitReadPage(relativeBlock, pageIndex, 0, headerBuffer, sizeof(headerBuffer), StoragePriority::medium, completionQueue)) return false;
	if (xQueueReceive(completionQueue, &result, pdMS_TO_TICKS(250)) != pdPASS || !result.success) return false;
	memcpy(&header, headerBuffer, sizeof(header)); erased = true;
	for (uint8_t byte : headerBuffer) if (byte != 0xFF) { erased = false; break; }
	const uint32_t groupConfig = readUint24(header.groupConfig);
	const size_t sampleSize = HistoryFormat::sampleSize(header.formatVersion, groupConfig);
	valid = !erased && sampleSize && header.sampleCount && header.samplePeriod &&
		size_t(header.sampleCount) * sampleSize <= HistoryFormat::payloadSize &&
		header.headerCrc16 == HistoryFormat::crc16(headerBuffer, 16) &&
		HistoryFormat::isValidGroupConfig(header.formatVersion, groupConfig);
	if (valid) ++recoveryMetrics.validHeaders;
	return true;
}

// Lee una cabecera con una cola independiente para no consumir confirmaciones de escritura.
bool HistoryStore::readPlanHeader(uint16_t relativeBlock, uint8_t pageIndex, HistoryFormat::PageHeader& header, bool& valid, bool& erased)
{
	NandIoResult result;
	while (xQueueReceive(planQueue, &result, 0) == pdPASS) {}
	if (!partition.submitReadPage(relativeBlock, pageIndex, 0, planHeaderBuffer, sizeof(planHeaderBuffer),
		StoragePriority::low, planQueue)) return false;
	if (xQueueReceive(planQueue, &result, pdMS_TO_TICKS(500)) != pdPASS || !result.success) return false;
	memcpy(&header, planHeaderBuffer, sizeof(header));
	erased = true;
	for (uint8_t byte : planHeaderBuffer) if (byte != 0xFF) { erased = false; break; }
	const uint32_t groupConfig = readUint24(header.groupConfig);
	const size_t sampleSize = HistoryFormat::sampleSize(header.formatVersion, groupConfig);
	valid = sampleSize && header.sampleCount && header.samplePeriod &&
		header.headerCrc16 == HistoryFormat::crc16(planHeaderBuffer, 16) &&
		HistoryFormat::isValidGroupConfig(header.formatVersion, groupConfig) &&
		size_t(header.sampleCount) * sampleSize <= HistoryFormat::payloadSize;
	return true;
}

// Retrocede una pagina, saltando los bloques NAND que no pertenecen al espacio utilizable.
bool HistoryStore::previousPage(uint16_t& relativeBlock, uint8_t& pageIndex) const
{
	if (pageIndex) {
		--pageIndex;
		return true;
	}
	relativeBlock = partition.previousUsableBlock(relativeBlock);
	if (relativeBlock == NandPartition::invalidBlock) return false;
	pageIndex = WinbondSpiNand::pagesPerBlock - 1;
	return true;
}

// Retrocede count paginas dentro de la particion, saltando bloques no utilizables sin acceder a NAND.
bool HistoryStore::previousPages(uint16_t& relativeBlock, uint8_t& pageIndex, uint32_t count) const
{
	while (count--) if (!previousPage(relativeBlock, pageIndex)) return false;
	return true;
}

// Ubica por saltos la pagina mas reciente que comienza antes de timestamp y conserva su secuencia.
bool HistoryStore::findPageAtOrBefore(uint32_t timestamp, uint16_t& relativeBlock, uint8_t& pageIndex, uint32_t& pageSequence,
	uint32_t& headerReads)
{
	HistoryFormat::PageHeader header;
	bool valid = false, erased = false;
	++headerReads;
	if (!readPlanHeader(relativeBlock, pageIndex, header, valid, erased) || !valid || readUint24(header.sequence) != pageSequence)
		return false;
	if (header.timestamp <= timestamp) return true;
	NandPartitionInfo info;
	partition.getInfo(info);
	const uint32_t maximumDistance = uint32_t(info.usableBlockCount) * WinbondSpiNand::pagesPerBlock;
	const uint16_t steps[] = { 4096, 1024, 256, 64, 16, 1 };
	uint32_t distance = 0;
	for (uint16_t step : steps) {
		while (distance + step < maximumDistance) {
			uint16_t candidateBlock = relativeBlock;
			uint8_t candidatePage = pageIndex;
			if (!previousPages(candidateBlock, candidatePage, step)) return false;
			const uint32_t candidateSequence = (pageSequence - step) & 0xFFFFFFU;
		++headerReads;
		if (!readPlanHeader(candidateBlock, candidatePage, header, valid, erased)) return false;
		if (!valid) {
			if (step != 1) break;
			// Una pagina borrada marca el limite real del historial; una corrupta se salta como hueco.
			if (erased) return false;
			relativeBlock = candidateBlock;
				pageIndex = candidatePage;
				pageSequence = candidateSequence;
				distance += step;
				continue;
			}
			if (readUint24(header.sequence) != candidateSequence) return false;
			if (header.timestamp <= timestamp) {
				if (step == 1) {
					relativeBlock = candidateBlock;
					pageIndex = candidatePage;
					pageSequence = candidateSequence;
					return true;
				}
				break;
			}
			relativeBlock = candidateBlock;
			pageIndex = candidatePage;
			pageSequence = candidateSequence;
			distance += step;
		}
	}
	return false;
}

// Evalua si el intervalo temporal de una pagina se superpone con el rango solicitado.
bool HistoryStore::pageIntersectsRange(const HistoryFormat::PageHeader& header, uint32_t fromTimestamp, uint32_t toTimestamp)
{
	if (!header.sampleCount || !header.samplePeriod) return false;
	const uint32_t endTimestamp = header.timestamp + uint32_t(header.sampleCount - 1) * header.samplePeriod / 10U;
	return endTimestamp >= fromTimestamp && header.timestamp <= toTimestamp;
}

// Construye un plan de lectura inmutable y toma una copia valida de la pagina RAM si entra en el rango.
HistoryReadPlanStatus HistoryStore::buildReadPlan(uint32_t fromTimestamp, uint32_t toTimestamp, HistoryReadPlan& plan,
	uint8_t* activeSnapshot, size_t snapshotSize, uint32_t maxPages, uint32_t beforeSequence)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return HistoryReadPlanStatus::ioError;
	if (!activeSnapshot || snapshotSize < HistoryFormat::pageSize || fromTimestamp > toTimestamp || !planMutex ||
		!maxPages || maxPages > 4096) return HistoryReadPlanStatus::ioError;
	if (xSemaphoreTake(planMutex, pdMS_TO_TICKS(1000)) != pdPASS) return HistoryReadPlanStatus::ioError;
	plan = HistoryReadPlan{};
	plan.fromTimestamp = fromTimestamp;
	plan.toTimestamp = toTimestamp;
	poll();

	bool snapshotValid = false;
	for (uint8_t attempt = 0; attempt < 3 && !snapshotValid; ++attempt) {
		memcpy(activeSnapshot, buffers[activeBuffer], HistoryFormat::pageSize);
		const auto* header = reinterpret_cast<const HistoryFormat::PageHeader*>(activeSnapshot);
		const size_t sampleSize = HistoryFormat::sampleSize(header->formatVersion, readUint24(header->groupConfig));
		const size_t payloadBytes = size_t(header->sampleCount) * sampleSize;
		snapshotValid = sampleSize && header->sampleCount && header->samplePeriod &&
			payloadBytes <= HistoryFormat::payloadSize && header->headerCrc16 == HistoryFormat::crc16(activeSnapshot, 16) &&
			header->payloadCrc16 == HistoryFormat::crc16(activeSnapshot + HistoryFormat::headerSize, payloadBytes);
	}
	if (snapshotValid && maxPages == 4096 && beforeSequence == UINT32_MAX) {
		const auto* header = reinterpret_cast<const HistoryFormat::PageHeader*>(activeSnapshot);
		plan.includesActivePage = pageIntersectsRange(*header, fromTimestamp, toTimestamp);
		if (plan.includesActivePage) plan.activeSampleCount = header->sampleCount;
	}

	if (!hasLatestStoredPage) {
		xSemaphoreGive(planMutex);
		return plan.includesActivePage ? HistoryReadPlanStatus::ready : HistoryReadPlanStatus::noData;
	}

	uint16_t cursorBlock = latestStoredBlock;
	uint8_t cursorPage = latestStoredPage;
	uint32_t expectedSequence = latestStoredSequence;
	if (beforeSequence != UINT32_MAX) {
		const uint32_t distance = (latestStoredSequence - beforeSequence) & 0xFFFFFFU;
		NandPartitionInfo info;
		partition.getInfo(info);
		if (distance >= uint32_t(info.usableBlockCount) * WinbondSpiNand::pagesPerBlock ||
			!previousPages(cursorBlock, cursorPage, distance)) {
			xSemaphoreGive(planMutex);
			return HistoryReadPlanStatus::noData;
		}
		expectedSequence = beforeSequence;
	}
	const uint32_t searchStartedUs = micros();
	const bool foundTarget = findPageAtOrBefore(toTimestamp, cursorBlock, cursorPage, expectedSequence, plan.searchHeaderReads);
	plan.searchElapsedUs = micros() - searchStartedUs;
	if (!foundTarget) {
		xSemaphoreGive(planMutex);
		return HistoryReadPlanStatus::noData;
	}
	bool found = false;
	const uint32_t maxDownloadPages = maxPages;
	for (uint32_t scanned = 0; scanned < maxDownloadPages; ++scanned) {
		HistoryFormat::PageHeader header;
		bool valid = false;
		bool erased = false;
		if (!readPlanHeader(cursorBlock, cursorPage, header, valid, erased)) {
			xSemaphoreGive(planMutex);
			return HistoryReadPlanStatus::ioError;
		}
		if (valid && readUint24(header.sequence) != expectedSequence) {
			break;
		}
		if (valid) {
			const uint32_t endTimestamp = header.timestamp + uint32_t(header.sampleCount - 1) * header.samplePeriod / 10U;
			if (endTimestamp < fromTimestamp) break;
		}
		if (valid && pageIntersectsRange(header, fromTimestamp, toTimestamp)) {
			plan.firstBlock = cursorBlock;
			plan.firstPage = cursorPage;
			plan.firstSequence = expectedSequence;
			++plan.storedPageCount;
			plan.sampleCount += header.sampleCount;
			if (!found) plan.physicalPageCount = 1;
			else ++plan.physicalPageCount;
			found = true;
		} else if (found) {
			++plan.physicalPageCount;
		}
		if (!previousPage(cursorBlock, cursorPage)) {
			break;
		}
		expectedSequence = (expectedSequence - 1U) & 0xFFFFFFU;
	}
	if (found && plan.physicalPageCount >= maxDownloadPages && maxPages == 4096) {
		xSemaphoreGive(planMutex);
		return HistoryReadPlanStatus::rangeTooLarge;
	}
	if (!found && !plan.includesActivePage) {
		xSemaphoreGive(planMutex);
		return HistoryReadPlanStatus::noData;
	}
	if (!found && plan.includesActivePage) plan.storedPageCount = 0;
	xSemaphoreGive(planMutex);
	return HistoryReadPlanStatus::ready;
}

// Compara secuencias de 24 bits usando diferencia modular.
bool HistoryStore::isNewerSequence(uint32_t candidate, uint32_t current)
{
	const uint32_t difference = (candidate - current) & 0xFFFFFFU;
	return difference && difference < 0x800000U;
}

// Decodifica tres bytes little-endian como entero de 24 bits.
uint32_t HistoryStore::readUint24(const uint8_t* source)
{
	return uint32_t(source[0]) | uint32_t(source[1]) << 8 | uint32_t(source[2]) << 16;
}
