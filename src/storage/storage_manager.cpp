#include "storage_manager.h"
#include "../diagnostic_logger.h"
#include <string.h>
#include <Preferences.h>
#include <esp_system.h>

// Asocia el manager con el driver y crea el servicio de I/O que lo controlara.
StorageManager::StorageManager(WinbondSpiNand& nandDriver) : nand(nandDriver), nandService(nandDriver),
	historyStore(historyPartition), waveStore(wavePartition), gridEventStore(gridPartition), diagnosticStore(diagnosticPartition) {}

// Inicia el servicio y monta metadata existente sin escribir ni borrar la NAND.
bool StorageManager::begin(void (*onDiagnosticsReady)(DiagnosticStore&))
{
	if (!nand.geometry().blockCount) {
		mountState = StorageState::notDetected;
		return false;
	}
	if (!nandService.begin()) {
		mountState = StorageState::formatError;
		return false;
	}
	cacheMutex = xSemaphoreCreateMutexStatic(&cacheMutexStorage);
	updateEventCacheEpochs(false, false);
	metadataResultQueue = xQueueCreateStatic(1, sizeof(NandIoResult), metadataResultQueueData, &metadataResultQueueStorage);
	if (!metadataResultQueue) {
		mountState = StorageState::formatError;
		return false;
	}
	if (!loadMetadata()) {
		mountState = metadataReadFailed ? StorageState::formatError : StorageState::unformatted;
		return !metadataReadFailed;
	}
	if (activeMetadata.state == uint8_t(StorageState::formatting)) {
		mountState = StorageState::formatting;
		return true;
	}
	if (activeMetadata.state != uint8_t(StorageState::ready)) {
		mountState = StorageState::formatError;
		return false;
	}
	applyMetadata(activeMetadata);
	if (diagnosticStore.begin() && onDiagnosticsReady) onDiagnosticsReady(diagnosticStore);
	if (!historyStore.begin() || !gridEventStore.begin()) {
		mountState = StorageState::formatError;
		return false;
	}
	webFsStore.begin();
	mountState = StorageState::ready;
	return true;
}

// Devuelve el estado de montaje calculado durante begin.
StorageState StorageManager::state() const
{
	return mountState;
}

// Indica si las particiones quedaron habilitadas por metadata valida.
bool StorageManager::isReady() const
{
	return mountState == StorageState::ready;
}

// Copia los nonces sin esperar escrituras NVS; fast/wave se publican juntos bajo mutex.
bool StorageManager::getEventCacheEpochs(uint64_t& fast, uint64_t& wave) const
{
	if (!cacheMutex || xSemaphoreTake(cacheMutex, 0) != pdTRUE) return false;
	fast = fastCacheEpoch;
	wave = waveCacheEpoch;
	xSemaphoreGive(cacheMutex);
	return fast && wave;
}

// Persiste generaciones antes del borrado; resetFast/resetWave renuevan solo las particiones indicadas.
bool StorageManager::updateEventCacheEpochs(bool resetFast, bool resetWave)
{
	if (!cacheMutex || xSemaphoreTake(cacheMutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
	Preferences prefs;
	bool success = prefs.begin("event-cache", false);
	uint64_t fast = 0, wave = 0;
	if (success) {
		fast = prefs.getULong64("fastEpoch", 0);
		wave = prefs.getULong64("waveEpoch", 0);
		if (!fast || resetFast) {
			fast = (uint64_t(esp_random()) << 32) | esp_random();
			if (!fast) fast = 1;
			success = prefs.putULong64("fastEpoch", fast) == sizeof(fast);
		}
		if (success && (!wave || resetWave)) {
			wave = (uint64_t(esp_random()) << 32) | esp_random();
			if (!wave) wave = 1;
			success = prefs.putULong64("waveEpoch", wave) == sizeof(wave);
		}
		prefs.end();
	}
	fastCacheEpoch = success ? fast : 0;
	waveCacheEpoch = success ? wave : 0;
	xSemaphoreGive(cacheMutex);
	return success;
}

// Inicia el formateo destructivo cuando la NAND esta disponible y no hay otra tarea de formato.
bool StorageManager::startFormat()
{
	if ((mountState != StorageState::unformatted && mountState != StorageState::formatError &&
		mountState != StorageState::ready) || !nandService.isRunning() || !reserveFormatTask()) return false;
	StorageLayout layout;
	if (!buildDefaultLayout(layout)) {
		releaseFormatTask();
		return false;
	}
	if (!formatResultQueue)
		formatResultQueue = xQueueCreateStatic(1, sizeof(NandIoResult), formatResultQueueData, &formatResultQueueStorage);
	if (!formatResultQueue) {
		releaseFormatTask();
		return false;
	}
	diagnosticLogger.attachStore(nullptr);
	if (!diagnosticStore.stop()) {
		releaseFormatTask();
		return false;
	}

	historyPartition.setEnabled(false);
	wavePartition.setEnabled(false);
	gridPartition.setEnabled(false);
	diagnosticPartition.setEnabled(false);
	webBankAPartition.setEnabled(false);
	webBankBPartition.setEnabled(false);
	StorageFormatProgress progress;
	progress.active = true;
	progress.totalBlocks = nand.geometry().blockCount;
	setFormatProgress(progress);
	mountState = StorageState::formatting;
	formatTaskIsGlobal = true;
	if (xTaskCreatePinnedToCore(formatTaskEntry, "nandFormat", 2048, this, 1, &formatTaskHandle, tskNO_AFFINITY) != pdPASS) {
		formatTaskHandle = nullptr;
		formatTaskIsGlobal = false;
		releaseFormatTask();
		mountState = StorageState::unformatted;
		progress.active = false;
		progress.failed = true;
		setFormatProgress(progress);
		return false;
	}
	return true;
}

// Reserva una particion y lanza su borrado fisico sin bloquear el servidor web.
bool StorageManager::startPartitionFormat(StoragePartitionId id)
{
	if (mountState != StorageState::ready || !nandService.isRunning() || !reserveFormatTask()) return false;
	const bool webBank = id == StoragePartitionId::webFsA || id == StoragePartitionId::webFsB;
	const WebFsBank bank = id == StoragePartitionId::webFsA ? WebFsBank::a : WebFsBank::b;
	if (webBank && !webFsStore.preparePartitionFormat(bank)) {
		releaseFormatTask();
		return false;
	}
	NandPartition& target = partition(id);
	if (!target.requestFormat()) {
		if (webBank) webFsStore.finishPartitionFormat(bank, true);
		releaseFormatTask();
		return false;
	}
	formatTarget = id;
	formatTaskIsGlobal = false;
	if (xTaskCreatePinnedToCore(formatTaskEntry, "nandPartFmt", 4096, this, 1, &formatTaskHandle, tskNO_AFFINITY) != pdPASS) {
		formatTaskHandle = nullptr;
		formatTaskIsGlobal = false;
		target.cancelFormat();
		if (webBank) webFsStore.finishPartitionFormat(bank, true);
		releaseFormatTask();
		return false;
	}
	return true;
}

// Devuelve la particion solicitada para evitar exponer la configuracion interna.
NandPartition& StorageManager::partition(StoragePartitionId id)
{
	switch (id) {
	case StoragePartitionId::history: return historyPartition;
	case StoragePartitionId::waveEvents: return wavePartition;
	case StoragePartitionId::gridEvents: return gridPartition;
	case StoragePartitionId::diagnostics: return diagnosticPartition;
	case StoragePartitionId::webFsA: return webBankAPartition;
	case StoragePartitionId::webFsB: return webBankBPartition;
	}
	return diagnosticPartition;
}

// Devuelve el servicio de I/O comun a todas las particiones.
NandIoService& StorageManager::ioService()
{
	return nandService;
}

// Devuelve el escritor asociado a la particion de historico.
HistoryStore& StorageManager::history()
{
	return historyStore;
}

// Devuelve el Store que persiste incidentes de red y sus muestras Fast.
GridEventStore& StorageManager::gridEvents()
{
	return gridEventStore;
}

// Entrega el store que administra la particion de diagnostico.
DiagnosticStore& StorageManager::diagnostics()
{
	return diagnosticStore;
}

// Devuelve el Store WebFS que administra las imagenes A/B; sin parametros.
WebFsStore& StorageManager::webFs()
{
	return webFsStore;
}

// Construye una instantanea de capacidad, estado de particiones y metricas.
void StorageManager::getStatus(StorageManagerStatus& status) const
{
	status = StorageManagerStatus{};
	status.state = mountState;
	status.nandBlockCount = nand.geometry().blockCount;
	status.capacityBytes = nand.capacityBytes();
	status.metadataSequence = activeMetadata.sequence;
	historyPartition.getInfo(status.history);
	wavePartition.getInfo(status.waveEvents);
	gridPartition.getInfo(status.gridEvents);
	diagnosticPartition.getInfo(status.diagnostics);
	webBankAPartition.getInfo(status.webFsA);
	webBankBPartition.getInfo(status.webFsB);
	historyPartition.getFormatProgress(status.historyFormat);
	wavePartition.getFormatProgress(status.waveEventsFormat);
	gridPartition.getFormatProgress(status.gridEventsFormat);
	diagnosticPartition.getFormatProgress(status.diagnosticsFormat);
	webBankAPartition.getFormatProgress(status.webFsAFormat);
	webBankBPartition.getFormatProgress(status.webFsBFormat);
	historyStore.getRecoveryMetrics(status.historyRecovery);
	historyStore.getMetrics(status.historyStore);
	gridEventStore.getMetrics(status.gridStore);
	diagnosticStore.getMetrics(status.diagnosticStore);
	webFsStore.getInfo(status.webFs);
	portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&formatMutex));
	status.format = formatProgress;
	portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&formatMutex));
	nandService.getMetrics(status.ioMetrics);
}

// Propone el mapa fijo de la W25N02 instalada en este Power Meter.
bool StorageManager::buildDefaultLayout(StorageLayout& layout) const
{
	layout = StorageLayout{};
	const uint16_t blocks = nand.geometry().blockCount;
	if (blocks == 2048) {
		layout.history = StoragePartitionLayout{16, 1536};
		layout.waveEvents = StoragePartitionLayout{1552, 320};
		layout.gridEvents = StoragePartitionLayout{1872, 48};
		layout.diagnostics = StoragePartitionLayout{1920, 128};
		return true;
	}
	return false;
}

// Deriva la tarea FreeRTOS al manager y libera su contexto al completar el formateo.
void StorageManager::formatTaskEntry(void* parameter)
{
	static_cast<StorageManager*>(parameter)->formatTaskLoop();
	vTaskDelete(nullptr);
}

// Escanea bloques, borra la NAND y publica las dos copias de metadata iniciales.
void StorageManager::formatTaskLoop()
{
	if (!formatTaskIsGlobal) {
		partitionFormatTaskLoop();
		formatTaskHandle = nullptr;
		releaseFormatTask();
		return;
	}
	StorageLayout layout;
	bool success = updateEventCacheEpochs(true, true) && buildDefaultLayout(layout) && scanFormatBlocks() && eraseFormatBlocks();
	if (success) success = writeFormatMetadata(layout);
	if (success) {
		activeMetadata = MetadataHeader{};
		if (!loadMetadata()) success = false;
		else {
			applyMetadata(activeMetadata);
			if (!historyStore.begin() || !gridEventStore.begin()) success = false;
			webFsStore.begin();
			diagnosticStore.begin();
		}
	}
	finishFormat(success);
}

// Borra una particion, reconstruye su store y la reactiva solo si todo quedo consistente.
void StorageManager::partitionFormatTaskLoop()
{
	NandPartition& target = partition(formatTarget);
	bool success = false;
	bool canErase = true;
	if (formatTarget == StoragePartitionId::history) historyStore.prepareFormat();
	else if (formatTarget == StoragePartitionId::gridEvents) gridEventStore.prepareFormat();
	else if (formatTarget == StoragePartitionId::waveEvents) canErase = waveStore.prepareFormat();
	else if (formatTarget == StoragePartitionId::diagnostics) canErase = diagnosticLogger.prepareStoreFormat();
	if (!canErase) {
		target.cancelFormat();
		return;
	}

	const bool cacheReady = (formatTarget != StoragePartitionId::gridEvents && formatTarget != StoragePartitionId::waveEvents) ||
		updateEventCacheEpochs(formatTarget == StoragePartitionId::gridEvents, formatTarget == StoragePartitionId::waveEvents);
	const bool erased = cacheReady && (formatTarget == StoragePartitionId::diagnostics
		? diagnosticStore.erasePartition() : target.eraseAllBlocks());
	if (erased) {
		switch (formatTarget) {
		case StoragePartitionId::history:
			success = historyStore.begin();
			target.finishFormat(success);
			historyStore.finishFormat(success);
			return;
		case StoragePartitionId::gridEvents:
			success = gridEventStore.begin();
			target.finishFormat(success);
			gridEventStore.finishFormat(success);
			return;
		case StoragePartitionId::diagnostics:
			success = diagnosticLogger.finishStoreFormat(true);
			target.finishFormat(success);
			return;
		case StoragePartitionId::webFsA:
		case StoragePartitionId::webFsB: {
			const WebFsBank bank = formatTarget == StoragePartitionId::webFsA ? WebFsBank::a : WebFsBank::b;
			webFsStore.finishPartitionFormat(bank, true);
			target.finishFormat(true);
			return;
		}
		case StoragePartitionId::waveEvents:
			target.finishFormat(true);
			waveStore.finishFormat(true);
			return;
		}
	}

	if (formatTarget == StoragePartitionId::diagnostics) diagnosticLogger.finishStoreFormat(false);
	if (formatTarget == StoragePartitionId::webFsA || formatTarget == StoragePartitionId::webFsB) {
		const WebFsBank bank = formatTarget == StoragePartitionId::webFsA ? WebFsBank::a : WebFsBank::b;
		webFsStore.finishPartitionFormat(bank, false);
	}
	target.finishFormat(false);
	if (formatTarget == StoragePartitionId::history) historyStore.finishFormat(false);
	if (formatTarget == StoragePartitionId::gridEvents) gridEventStore.finishFormat(false);
	if (formatTarget == StoragePartitionId::waveEvents) waveStore.finishFormat(false);
}

// Lee los marcadores de fabrica de toda la NAND y crea el bitmap de bloques utilizables.
bool StorageManager::scanFormatBlocks()
{
	memset(usableBlockBitmap, 0, sizeof(usableBlockBitmap));
	StorageFormatProgress progress;
	portENTER_CRITICAL(&formatMutex);
	progress = formatProgress;
	portEXIT_CRITICAL(&formatMutex);

	for (uint16_t block = 0; block < nand.geometry().blockCount; ++block) {
		uint8_t marker = 0;
		NandIoRequest request;
		request.operation = NandOperation::readBadBlockMarker;
		request.priority = StoragePriority::low;
		request.physicalBlock = block;
		request.data = &marker;
		request.length = 1;
		const bool readable = submitFormatRequest(request);
		const bool usable = readable && marker == 0xFF;
		setUsableBlock(block, usable);
		if (usable) ++progress.usableBlocks;
		else ++progress.badBlocks;
		if (!readable) ++progress.failedBlocks;
		progress.currentBlock = block;
		progress.scannedBlocks = block + 1;
		setFormatProgress(progress);
	}
	return hasUsableMetadataBlocks();
}

// Borra cada bloque habilitado y descarta los que fallen durante la operacion.
bool StorageManager::eraseFormatBlocks()
{
	StorageFormatProgress progress;
	portENTER_CRITICAL(&formatMutex);
	progress = formatProgress;
	portEXIT_CRITICAL(&formatMutex);

	for (uint16_t block = 0; block < nand.geometry().blockCount; ++block) {
		if (!isUsableBlock(block)) continue;
		NandIoRequest request;
		request.operation = NandOperation::eraseBlock;
		request.priority = StoragePriority::low;
		request.physicalBlock = block;
		if (submitFormatRequest(request)) ++progress.erasedBlocks;
		else {
			setUsableBlock(block, false);
			--progress.usableBlocks;
			++progress.badBlocks;
			++progress.failedBlocks;
		}
		progress.currentBlock = block;
		setFormatProgress(progress);
	}
	return hasUsableMetadataBlocks();
}

// Construye las copias de metadata con secuencias consecutivas y las relee para validarlas.
bool StorageManager::writeFormatMetadata(const StorageLayout& layout)
{
	MetadataHeader header;
	header.magic = metadataMagic;
	header.version = metadataVersion;
	header.state = uint8_t(StorageState::ready);
	header.headerSize = sizeof(MetadataHeader);
	header.nandBlockCount = nand.geometry().blockCount;
	header.pageDataSize = nand.geometry().pageDataSize;
	header.layout = layout;
	header.bitmapBytes = (nand.geometry().blockCount + 7) / 8;
	uint8_t copiesWritten = 0;
	for (uint16_t block = 0; block < metadataReservedBlocks; ++block) {
		if (!isUsableBlock(block)) continue;
		header.sequence = uint32_t(copiesWritten) + 1;
		if (writeMetadataCopy(block, header)) {
			if (++copiesWritten == 2) return true;
			continue;
		}
		setUsableBlock(block, false);
		StorageFormatProgress progress;
		portENTER_CRITICAL(&formatMutex);
		progress = formatProgress;
		portEXIT_CRITICAL(&formatMutex);
		--progress.usableBlocks;
		++progress.badBlocks;
		++progress.failedBlocks;
		setFormatProgress(progress);
	}
	return false;
}

// Programa una metadata completa y confirma su CRC mediante una lectura posterior.
bool StorageManager::writeMetadataCopy(uint16_t block, const MetadataHeader& sourceHeader)
{
	MetadataHeader header = sourceHeader;
	header.crc32 = 0;
	memset(metadataPageBuffer, 0xFF, sizeof(metadataPageBuffer));
	memcpy(metadataPageBuffer, &header, sizeof(header));
	memcpy(metadataPageBuffer + sizeof(header), usableBlockBitmap, header.bitmapBytes);
	header.crc32 = calculateMetadataCrc(header.bitmapBytes);
	memcpy(metadataPageBuffer, &header, sizeof(header));

	NandIoRequest request;
	request.operation = NandOperation::programPage;
	request.priority = StoragePriority::low;
	request.physicalBlock = block;
	request.data = metadataPageBuffer;
	request.length = nand.geometry().pageDataSize;
	if (!submitFormatRequest(request) || !readMetadataPage(block)) return false;
	MetadataHeader verified;
	return validateMetadata(verified) && verified.sequence == header.sequence;
}

// Encola una operacion de formateo y espera su resultado fuera de MeterTask.
bool StorageManager::submitFormatRequest(NandIoRequest& request)
{
	NandIoResult staleResult;
	while (xQueueReceive(formatResultQueue, &staleResult, 0) == pdPASS) {}
	request.requestId = ++formatRequestId;
	request.completionQueue = formatResultQueue;
	if (!nandService.submit(request)) return false;
	NandIoResult result;
	return waitFormatResult(request.requestId, result) && result.success;
}

// Espera la finalizacion asociada al identificador solicitado, descartando resultados vencidos.
bool StorageManager::waitFormatResult(uint32_t requestId, NandIoResult& result)
{
	const TickType_t timeout = pdMS_TO_TICKS(2000);
	const TickType_t started = xTaskGetTickCount();
	TickType_t remaining = timeout;
	while (xQueueReceive(formatResultQueue, &result, remaining) == pdPASS) {
		if (result.requestId == requestId) return true;
		const TickType_t elapsed = xTaskGetTickCount() - started;
		if (elapsed >= timeout) break;
		remaining = timeout - elapsed;
	}
	return false;
}

// Guarda una instantanea del progreso bajo una seccion critica corta.
void StorageManager::setFormatProgress(const StorageFormatProgress& progress)
{
	portENTER_CRITICAL(&formatMutex);
	formatProgress = progress;
	portEXIT_CRITICAL(&formatMutex);
}

// Cierra el formateo, habilitando las particiones solo despues de validar metadata redundante.
void StorageManager::finishFormat(bool success)
{
	StorageFormatProgress progress;
	portENTER_CRITICAL(&formatMutex);
	progress = formatProgress;
	progress.active = false;
	progress.completed = success;
	progress.failed = !success;
	formatProgress = progress;
	portEXIT_CRITICAL(&formatMutex);
	mountState = success ? StorageState::ready : StorageState::formatError;
	formatTaskHandle = nullptr;
	formatTaskIsGlobal = false;
	releaseFormatTask();
}

// Reserva bajo una seccion critica la tarea unica de formato NAND.
bool StorageManager::reserveFormatTask()
{
	portENTER_CRITICAL(&formatMutex);
	const bool available = !formatTaskReserved;
	if (available) formatTaskReserved = true;
	portEXIT_CRITICAL(&formatMutex);
	return available;
}

// Libera la reserva de formato bajo una seccion critica corta.
void StorageManager::releaseFormatTask()
{
	portENTER_CRITICAL(&formatMutex);
	formatTaskReserved = false;
	portEXIT_CRITICAL(&formatMutex);
}

// Actualiza un bit del bitmap global para reflejar si el bloque se puede seguir usando.
void StorageManager::setUsableBlock(uint16_t block, bool usable)
{
	if (block >= nand.geometry().blockCount) return;
	const uint16_t byteIndex = block / 8;
	const uint8_t bit = uint8_t(1U << (block % 8));
	if (usable) usableBlockBitmap[byteIndex] |= bit;
	else usableBlockBitmap[byteIndex] &= uint8_t(~bit);
}

// Consulta el bitmap global sin acceder a la NAND fisica.
bool StorageManager::isUsableBlock(uint16_t block) const
{
	return block < nand.geometry().blockCount && (usableBlockBitmap[block / 8] & uint8_t(1U << (block % 8)));
}

// Cuenta los bloques reservados que sobrevivieron el escaneo y el borrado.
bool StorageManager::hasUsableMetadataBlocks() const
{
	uint8_t usableBlocks = 0;
	for (uint16_t block = 0; block < metadataReservedBlocks; ++block) {
		if (isUsableBlock(block) && ++usableBlocks == 2) return true;
	}
	return false;
}

// Calcula el CRC de metadata omitiendo los cuatro bytes donde se almacena el propio CRC.
uint32_t StorageManager::calculateMetadataCrc(uint16_t bitmapBytes) const
{
	const size_t headerBytes = sizeof(MetadataHeader) - sizeof(uint32_t);
	uint8_t crcBuffer[sizeof(MetadataHeader) + WinbondSpiNand::maxBlockCount / 8];
	memcpy(crcBuffer, metadataPageBuffer, headerBytes);
	memcpy(crcBuffer + headerBytes, metadataPageBuffer + sizeof(MetadataHeader), bitmapBytes);
	return calculateCrc32(crcBuffer, headerBytes + bitmapBytes);
}

// Lee ambos bloques de metadata y conserva la cabecera valida de mayor secuencia.
bool StorageManager::loadMetadata()
{
	MetadataHeader selected;
	bool found = false;
	metadataReadFailed = false;
	for (uint16_t block = 0; block < metadataReservedBlocks; ++block) {
		if (!readMetadataPage(block)) {
			metadataReadFailed = true;
			continue;
		}
		MetadataHeader candidate;
		if (!validateMetadata(candidate)) continue;
		if (!found || isNewerSequence(candidate.sequence, selected.sequence)) {
			selected = candidate;
			memcpy(usableBlockBitmap, metadataPageBuffer + sizeof(MetadataHeader), candidate.bitmapBytes);
			found = true;
		}
	}
	if (found) activeMetadata = selected;
	return found;
}

// Encola y espera la lectura de una pagina de metadata durante el arranque.
bool StorageManager::readMetadataPage(uint16_t block)
{
	NandIoResult staleResult;
	while (xQueueReceive(metadataResultQueue, &staleResult, 0) == pdPASS) {}
	NandIoRequest request;
	request.operation = NandOperation::readPage;
	request.requestId = uint32_t(block) + 1;
	request.priority = StoragePriority::medium;
	request.physicalBlock = block;
	request.page = metadataPage;
	request.data = metadataPageBuffer;
	request.length = nand.geometry().pageDataSize;
	request.completionQueue = metadataResultQueue;
	if (!nandService.submit(request)) return false;
	const TickType_t timeout = pdMS_TO_TICKS(250);
	const TickType_t started = xTaskGetTickCount();
	TickType_t remaining = timeout;
	NandIoResult result;
	while (xQueueReceive(metadataResultQueue, &result, remaining) == pdPASS) {
		if (result.requestId == request.requestId) return result.success;
		const TickType_t elapsed = xTaskGetTickCount() - started;
		if (elapsed >= timeout) break;
		remaining = timeout - elapsed;
	}
	return false;
}

// Valida la cabecera y su bitmap asociado mediante geometria, formato y CRC32.
bool StorageManager::validateMetadata(MetadataHeader& header) const
{
	if (isErased(metadataPageBuffer, sizeof(MetadataHeader))) return false;
	memcpy(&header, metadataPageBuffer, sizeof(header));
	if (header.magic != metadataMagic || header.version != metadataVersion ||
		header.headerSize != sizeof(MetadataHeader) || header.nandBlockCount != nand.geometry().blockCount ||
		header.pageDataSize != nand.geometry().pageDataSize ||
		header.bitmapBytes != (nand.geometry().blockCount + 7) / 8 ||
		header.bitmapBytes > sizeof(usableBlockBitmap) || !isValidLayout(header.layout)) return false;
	return calculateMetadataCrc(header.bitmapBytes) == header.crc32;
}

// Configura stores y subregiones WebFS dentro de diagnostico sin cambiar la metadata V1.
void StorageManager::applyMetadata(const MetadataHeader& header)
{
	const uint16_t blocks = nand.geometry().blockCount;
	historyPartition.configure(nandService, usableBlockBitmap, blocks, header.layout.history.startBlock, header.layout.history.blockCount);
	wavePartition.configure(nandService, usableBlockBitmap, blocks, header.layout.waveEvents.startBlock, header.layout.waveEvents.blockCount);
	gridPartition.configure(nandService, usableBlockBitmap, blocks, header.layout.gridEvents.startBlock, header.layout.gridEvents.blockCount);
	const uint16_t webBlocks = header.layout.diagnostics.blockCount >= 128 ? 24 : 0;
	const uint16_t diagnosticsBlocks = header.layout.diagnostics.blockCount >= 128 ? 80 : header.layout.diagnostics.blockCount;
	diagnosticPartition.configure(nandService, usableBlockBitmap, blocks, header.layout.diagnostics.startBlock, diagnosticsBlocks);
	webBankAPartition.configure(nandService, usableBlockBitmap, blocks,
		header.layout.diagnostics.startBlock + diagnosticsBlocks, webBlocks);
	webBankBPartition.configure(nandService, usableBlockBitmap, blocks,
		header.layout.diagnostics.startBlock + diagnosticsBlocks + webBlocks, webBlocks);
	historyPartition.setEnabled(true);
	wavePartition.setEnabled(true);
	gridPartition.setEnabled(true);
	diagnosticPartition.setEnabled(true);
	webBankAPartition.setEnabled(webBlocks != 0);
	webBankBPartition.setEnabled(webBlocks != 0);
	webFsStore.configure(webBankAPartition, webBankBPartition);
}

// Verifica que todas las regiones entren en NAND, no se solapen y no ocupen metadata.
bool StorageManager::isValidLayout(const StorageLayout& layout) const
{
	const StoragePartitionLayout regions[] = {
		layout.history, layout.waveEvents, layout.gridEvents, layout.diagnostics
	};
	const uint16_t totalBlocks = nand.geometry().blockCount;
	for (uint8_t index = 0; index < 4; ++index) {
		const StoragePartitionLayout& region = regions[index];
		if (!region.blockCount || region.startBlock < metadataReservedBlocks || region.startBlock >= totalBlocks ||
			uint32_t(region.startBlock) + region.blockCount > totalBlocks) return false;
		for (uint8_t other = 0; other < index; ++other) {
			const StoragePartitionLayout& previous = regions[other];
			const bool overlaps = region.startBlock < uint32_t(previous.startBlock) + previous.blockCount &&
				previous.startBlock < uint32_t(region.startBlock) + region.blockCount;
			if (overlaps) return false;
		}
	}
	return true;
}

// Indica si todos los bytes del rango conservan el valor borrado de NAND.
bool StorageManager::isErased(const uint8_t* data, size_t length)
{
	for (size_t index = 0; index < length; ++index) {
		if (data[index] != 0xFF) return false;
	}
	return true;
}

// Calcula CRC32 IEEE para proteger cabecera y bitmap de metadata.
uint32_t StorageManager::calculateCrc32(const uint8_t* data, size_t length)
{
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t index = 0; index < length; ++index) {
		crc ^= data[index];
		for (uint8_t bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320U & uint32_t(-int32_t(crc & 1U)));
	}
	return ~crc;
}

// Compara secuencias uint32 considerando el desborde modular.
bool StorageManager::isNewerSequence(uint32_t candidate, uint32_t current)
{
	return int32_t(candidate - current) > 0;
}
