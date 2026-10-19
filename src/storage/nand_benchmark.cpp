#include "nand_benchmark.h"
#include "winbond_spi_nand.h"
#include "storage_manager.h"
#include <esp_crc.h>
#include <new>
#include <atomic>

extern WinbondSpiNand eventFlash;
extern StorageManager storageManager;

enum class BenchmarkPhase : uint8_t {
	idle,
	scan,
	erase,
	write,
	read,
	complete,
	failed,
	cancelled
};

struct BenchmarkStatus {
	bool running = false;
	bool success = false;
	bool cancelRequested = false;
	NandBenchmarkMode mode = NandBenchmarkMode::quick;
	BenchmarkPhase phase = BenchmarkPhase::idle;
	uint8_t speedMHz = 0;
	uint16_t startBlock = 0;
	uint16_t blockCount = 0;
	uint16_t currentBlock = 0;
	uint16_t goodBlocks = 0;
	uint16_t badBlocks = 0;
	uint32_t currentPage = 0;
	uint32_t totalPages = 0;
	uint32_t bytesWritten = 0;
	uint32_t bytesRead = 0;
	uint32_t eraseMs = 0;
	uint32_t writeMs = 0;
	uint32_t readMs = 0;
	uint32_t totalMs = 0;
	uint32_t writeCrc32 = 0;
	uint32_t readCrc32 = 0;
	uint64_t writeOperationUs = 0;
	uint64_t writeProcessUs = 0;
	uint64_t writeYieldUs = 0;
	uint64_t writeBusWaitUs = 0;
	uint64_t writeSpiActiveUs = 0;
	uint64_t writeReadyWaitUs = 0;
	uint64_t readOperationUs = 0;
	uint64_t readProcessUs = 0;
	uint64_t readYieldUs = 0;
	uint64_t readBusWaitUs = 0;
	uint64_t readSpiActiveUs = 0;
	uint64_t readReadyWaitUs = 0;
	uint32_t mismatchPages = 0;
	uint32_t firstMismatchPage = 0;
	uint16_t firstMismatchByte = 0;
	uint8_t expectedByte = 0;
	uint8_t actualByte = 0;
	uint8_t firstMismatchStatus = 0;
	uint8_t maxReadStatus = 0;
	char reason[32] = "";
};

static BenchmarkStatus benchmarkStatus;
static portMUX_TYPE benchmarkMutex = portMUX_INITIALIZER_UNLOCKED;
static uint8_t badBlockMap[WinbondSpiNand::maxBlockCount / 8];
static std::atomic<bool> nandDiagnosticActive(false);

// Devuelve el nombre JSON de phase.
// phase: etapa interna del benchmark.
static const char* phaseName(BenchmarkPhase phase)
{
	switch (phase) {
	case BenchmarkPhase::scan: return "scan";
	case BenchmarkPhase::erase: return "erase";
	case BenchmarkPhase::write: return "write";
	case BenchmarkPhase::read: return "read";
	case BenchmarkPhase::complete: return "complete";
	case BenchmarkPhase::failed: return "failed";
	case BenchmarkPhase::cancelled: return "cancelled";
	default: return "idle";
	}
}

// Devuelve el nombre JSON del modo de prueba.
// mode: modo configurado para el benchmark.
static const char* modeName(NandBenchmarkMode mode)
{
	switch (mode) {
	case NandBenchmarkMode::full: return "full";
	case NandBenchmarkMode::verify: return "verify";
	default: return "quick";
	}
}

// Indica si block fue marcado como defectuoso durante el escaneo.
// block: indice fisico NAND.
static bool isBadBlock(uint16_t block)
{
	return badBlockMap[block >> 3] & (1U << (block & 7));
}

// Guarda el estado defectuoso de block en el mapa compacto.
// block: indice fisico NAND.
static void markBadBlock(uint16_t block)
{
	badBlockMap[block >> 3] |= 1U << (block & 7);
}

// Genera un patron reproducible diferente para cada pagina.
// data: pagina destino; page: direccion fisica usada como semilla.
static void fillPattern(uint8_t* data, uint32_t page)
{
	uint32_t value = page * 2654435761U + 0xA5C39E17U;
	for (uint32_t i = 0; i < WinbondSpiNand::pageDataSize; ++i) {
		value = value * 1664525U + 1013904223U;
		data[i] = uint8_t(value >> 24);
	}
}

// Compara una pagina y devuelve el primer byte diferente.
// data y page identifican la pagina; mismatchByte, expected y actual reciben el detalle del error.
static bool matchesPattern(const uint8_t* data, uint32_t page, uint16_t& mismatchByte,
	uint8_t& expected, uint8_t& actual)
{
	uint32_t value = page * 2654435761U + 0xA5C39E17U;
	for (uint32_t i = 0; i < WinbondSpiNand::pageDataSize; ++i) {
		value = value * 1664525U + 1013904223U;
		expected = uint8_t(value >> 24);
		if (data[i] != expected) {
			mismatchByte = uint16_t(i);
			actual = data[i];
			return false;
		}
	}
	return true;
}

// Actualiza los campos de progreso visibles por HTTP.
// phase, block y page describen la posicion actual.
static void updateProgress(BenchmarkPhase phase, uint16_t block, uint32_t page)
{
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.phase = phase;
	benchmarkStatus.currentBlock = block;
	benchmarkStatus.currentPage = page;
	taskEXIT_CRITICAL(&benchmarkMutex);
}

// Indica si se solicito cancelar la tarea; sin parametros.
static bool shouldCancel()
{
	taskENTER_CRITICAL(&benchmarkMutex);
	const bool cancel = benchmarkStatus.cancelRequested;
	taskEXIT_CRITICAL(&benchmarkMutex);
	return cancel;
}

// Finaliza el benchmark y publica su resultado.
// phase y reason describen la causa final; startedMs permite calcular la duracion.
static void finishBenchmark(BenchmarkPhase phase, const char* reason, uint32_t startedMs)
{
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.phase = phase;
	benchmarkStatus.running = false;
	benchmarkStatus.success = phase == BenchmarkPhase::complete;
	benchmarkStatus.totalMs = millis() - startedMs;
	strlcpy(benchmarkStatus.reason, reason ? reason : "", sizeof(benchmarkStatus.reason));
	taskEXIT_CRITICAL(&benchmarkMutex);
}

// Ejecuta escaneo, borrado, escritura y lectura desde una tarea de baja prioridad.
// parameter no se utiliza; la configuracion esta en benchmarkStatus.
static void benchmarkTask(void* parameter)
{
	(void)parameter;
	BenchmarkStatus config;
	taskENTER_CRITICAL(&benchmarkMutex);
	config = benchmarkStatus;
	taskEXIT_CRITICAL(&benchmarkMutex);
	const uint32_t startedMs = millis();
	uint32_t phaseStarted = 0;
	uint32_t writeCrc = 0;
	uint32_t readCrc = 0;
	uint32_t pagesDone = 0;
	uint32_t mismatchPages = 0;
	WinbondSpiNandTiming writeTiming;
	WinbondSpiNandTiming readTiming;
	uint64_t writeOperationUs = 0, writeProcessUs = 0, writeYieldUs = 0;
	uint64_t readOperationUs = 0, readProcessUs = 0, readYieldUs = 0;

	uint8_t* pageData = new (std::nothrow) uint8_t[WinbondSpiNand::pageDataSize];
	if (!pageData) {
		finishBenchmark(BenchmarkPhase::failed, "no_memory", startedMs);
		nandDiagnosticRelease();
		vTaskDelete(nullptr);
		return;
	}
	memset(badBlockMap, 0, sizeof(badBlockMap));
	eventFlash.setClockHz(uint32_t(config.speedMHz) * 1000000U);
	uint8_t protection = 0, configuration = 0;
	const bool writeSession = config.mode != NandBenchmarkMode::verify;
	if (writeSession) eventFlash.beginWriteSession(protection, configuration);

	uint16_t goodBlocks = 0, badBlocks = 0;
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		uint8_t marker = 0, status = 0;
		updateProgress(BenchmarkPhase::scan, block, 0);
		if (!eventFlash.readBadBlockMarker(block, marker, status)) {
			finishBenchmark(BenchmarkPhase::failed, "scan_failed", startedMs); goto cleanup;
		}
		if (marker == 0xFF) ++goodBlocks;
		else { markBadBlock(block); ++badBlocks; }
		if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
		vTaskDelay(1);
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.goodBlocks = goodBlocks;
	benchmarkStatus.badBlocks = badBlocks;
	benchmarkStatus.totalPages = uint32_t(goodBlocks) * eventFlash.geometry().pagesPerBlock;
	taskEXIT_CRITICAL(&benchmarkMutex);
	if (!goodBlocks) {
		finishBenchmark(BenchmarkPhase::failed, "no_good_blocks", startedMs);
		goto cleanup;
	}
	if (config.mode == NandBenchmarkMode::verify) goto readPhase;

	phaseStarted = millis();
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		if (isBadBlock(block)) continue;
		uint8_t status = 0;
		updateProgress(BenchmarkPhase::erase, block, 0);
		if (!eventFlash.eraseBlock(block, status)) {
			markBadBlock(block);
			--goodBlocks;
			++badBlocks;
			continue;
		}
		if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
		vTaskDelay(1);
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.eraseMs = millis() - phaseStarted;
	benchmarkStatus.goodBlocks = goodBlocks;
	benchmarkStatus.badBlocks = badBlocks;
	benchmarkStatus.totalPages = uint32_t(goodBlocks) * eventFlash.geometry().pagesPerBlock;
	taskEXIT_CRITICAL(&benchmarkMutex);
	if (!goodBlocks) {
		finishBenchmark(BenchmarkPhase::failed, "no_erasable_blocks", startedMs);
		goto cleanup;
	}

	phaseStarted = millis();
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		if (isBadBlock(block)) continue;
		for (uint16_t pageInBlock = 0; pageInBlock < WinbondSpiNand::pagesPerBlock; ++pageInBlock) {
			const uint32_t page = uint32_t(block) * WinbondSpiNand::pagesPerBlock + pageInBlock;
			uint8_t status = 0;
			uint32_t measuredUs = micros();
			fillPattern(pageData, page);
			writeProcessUs += micros() - measuredUs;
			updateProgress(BenchmarkPhase::write, block, pagesDone);
			measuredUs = micros();
			if (!eventFlash.programPage(page, pageData, WinbondSpiNand::pageDataSize, status, &writeTiming)) {
				finishBenchmark(BenchmarkPhase::failed, "program_failed", startedMs); goto cleanup;
			}
			writeOperationUs += micros() - measuredUs;
			measuredUs = micros();
			writeCrc = esp_crc32_le(writeCrc, pageData, WinbondSpiNand::pageDataSize);
			writeProcessUs += micros() - measuredUs;
			++pagesDone;
			if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
			if ((pagesDone & 7U) == 0) {
				measuredUs = micros();
				vTaskDelay(1);
				writeYieldUs += micros() - measuredUs;
			}
		}
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.writeMs = millis() - phaseStarted;
	benchmarkStatus.bytesWritten = pagesDone * WinbondSpiNand::pageDataSize;
	benchmarkStatus.writeCrc32 = writeCrc;
	benchmarkStatus.writeOperationUs = writeOperationUs;
	benchmarkStatus.writeProcessUs = writeProcessUs;
	benchmarkStatus.writeYieldUs = writeYieldUs;
	benchmarkStatus.writeBusWaitUs = writeTiming.busWaitUs;
	benchmarkStatus.writeSpiActiveUs = writeTiming.spiActiveUs;
	benchmarkStatus.writeReadyWaitUs = writeTiming.readyWaitUs;
	taskEXIT_CRITICAL(&benchmarkMutex);

	readPhase:
	phaseStarted = millis();
	pagesDone = 0;
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		if (isBadBlock(block)) continue;
		for (uint16_t pageInBlock = 0; pageInBlock < WinbondSpiNand::pagesPerBlock; ++pageInBlock) {
			const uint32_t page = uint32_t(block) * WinbondSpiNand::pagesPerBlock + pageInBlock;
			uint8_t status = 0;
			uint32_t measuredUs = micros();
			if (!eventFlash.readPage(page, pageData, WinbondSpiNand::pageDataSize, status, &readTiming)) {
				finishBenchmark(BenchmarkPhase::failed, "read_failed", startedMs); goto cleanup;
			}
			readOperationUs += micros() - measuredUs;
			measuredUs = micros();
			uint16_t mismatchByte = 0;
			uint8_t expected = 0, actual = 0;
			if (!matchesPattern(pageData, page, mismatchByte, expected, actual)) {
				++mismatchPages;
				taskENTER_CRITICAL(&benchmarkMutex);
				benchmarkStatus.mismatchPages = mismatchPages;
				if (mismatchPages == 1) {
					benchmarkStatus.firstMismatchPage = page;
					benchmarkStatus.firstMismatchByte = mismatchByte;
					benchmarkStatus.expectedByte = expected;
					benchmarkStatus.actualByte = actual;
					benchmarkStatus.firstMismatchStatus = status;
				}
				taskEXIT_CRITICAL(&benchmarkMutex);
				if (config.mode != NandBenchmarkMode::verify) {
					finishBenchmark(BenchmarkPhase::failed, "data_mismatch", startedMs); goto cleanup;
				}
			}
			readCrc = esp_crc32_le(readCrc, pageData, WinbondSpiNand::pageDataSize);
			readProcessUs += micros() - measuredUs;
			++pagesDone;
			updateProgress(BenchmarkPhase::read, block, pagesDone);
			taskENTER_CRITICAL(&benchmarkMutex);
			const uint8_t eccStatus = status & eventFlash.geometry().eccStatusMask;
			if (eccStatus > benchmarkStatus.maxReadStatus) benchmarkStatus.maxReadStatus = eccStatus;
			taskEXIT_CRITICAL(&benchmarkMutex);
			if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
			// Una pausa cada ocho paginas alimenta al idle task sin sumar un tick a cada transferencia.
			if ((pagesDone & 7U) == 0) {
				measuredUs = micros();
				vTaskDelay(1);
				readYieldUs += micros() - measuredUs;
			}
		}
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.readMs = millis() - phaseStarted;
	benchmarkStatus.bytesRead = pagesDone * WinbondSpiNand::pageDataSize;
	benchmarkStatus.writeCrc32 = writeCrc;
	benchmarkStatus.readCrc32 = readCrc;
	benchmarkStatus.readOperationUs = readOperationUs;
	benchmarkStatus.readProcessUs = readProcessUs;
	benchmarkStatus.readYieldUs = readYieldUs;
	benchmarkStatus.readBusWaitUs = readTiming.busWaitUs;
	benchmarkStatus.readSpiActiveUs = readTiming.spiActiveUs;
	benchmarkStatus.readReadyWaitUs = readTiming.readyWaitUs;
	taskEXIT_CRITICAL(&benchmarkMutex);
	if (config.mode == NandBenchmarkMode::verify) {
		finishBenchmark(mismatchPages == 0 ? BenchmarkPhase::complete : BenchmarkPhase::failed,
			mismatchPages == 0 ? "" : "data_mismatch", startedMs);
	}
	else {
		finishBenchmark(writeCrc == readCrc ? BenchmarkPhase::complete : BenchmarkPhase::failed,
			writeCrc == readCrc ? "" : "crc_mismatch", startedMs);
	}

cleanup:
	if (writeSession) eventFlash.endWriteSession(protection, configuration);
	eventFlash.setClockHz(30000000);
	delete[] pageData;
	nandDiagnosticRelease();
	vTaskDelete(nullptr);
}

bool nandBenchmarkStart(NandBenchmarkMode mode, uint8_t speedMHz, uint16_t startBlock, uint16_t blockCount)
{
	if (storageManager.state() != StorageState::unformatted) return false;
	if (speedMHz != 20 && speedMHz != 25 && speedMHz != 30 && speedMHz != 35 && speedMHz != 40) return false;
	if (!nandDiagnosticTryAcquire()) return false;
	const uint16_t detectedBlocks = eventFlash.geometry().blockCount;
	if (!detectedBlocks) {
		nandDiagnosticRelease();
		return false;
	}
	const uint16_t defaultStart = mode == NandBenchmarkMode::quick ? detectedBlocks - 10 :
		mode == NandBenchmarkMode::verify ? 4 : 0;
	if (blockCount == 0) {
		startBlock = defaultStart;
		blockCount = mode == NandBenchmarkMode::quick ? 8 : detectedBlocks - startBlock;
	}
	if (startBlock >= detectedBlocks || blockCount > detectedBlocks - startBlock) {
		nandDiagnosticRelease();
		return false;
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	if (benchmarkStatus.running) {
		taskEXIT_CRITICAL(&benchmarkMutex);
		nandDiagnosticRelease();
		return false;
	}
	benchmarkStatus = BenchmarkStatus{};
	benchmarkStatus.running = true;
	benchmarkStatus.mode = mode;
	benchmarkStatus.speedMHz = speedMHz;
	benchmarkStatus.startBlock = startBlock;
	benchmarkStatus.blockCount = blockCount;
	benchmarkStatus.phase = BenchmarkPhase::scan;
	taskEXIT_CRITICAL(&benchmarkMutex);

	if (xTaskCreatePinnedToCore(benchmarkTask, "nandBenchmark", 4096, nullptr, 1, nullptr, PRO_CPU_NUM) != pdPASS) {
		finishBenchmark(BenchmarkPhase::failed, "task_create_failed", millis());
		nandDiagnosticRelease();
		return false;
	}
	return true;
}

bool nandBenchmarkCancel()
{
	taskENTER_CRITICAL(&benchmarkMutex);
	const bool running = benchmarkStatus.running;
	benchmarkStatus.cancelRequested = running;
	taskEXIT_CRITICAL(&benchmarkMutex);
	return running;
}

// Reserva el acceso exclusivo usado por pruebas destructivas de diagnostico.
bool nandDiagnosticTryAcquire()
{
	bool expected = false;
	return nandDiagnosticActive.compare_exchange_strong(expected, true);
}

// Libera el acceso exclusivo usado por pruebas destructivas de diagnostico.
void nandDiagnosticRelease()
{
	nandDiagnosticActive = false;
}

void nandBenchmarkGetJson(String& json)
{
	BenchmarkStatus status;
	taskENTER_CRITICAL(&benchmarkMutex);
	status = benchmarkStatus;
	taskEXIT_CRITICAL(&benchmarkMutex);
	const float writeMbps = status.writeMs ? (status.bytesWritten * 8.0f) / (status.writeMs * 1000.0f) : 0;
	const float readMbps = status.readMs ? (status.bytesRead * 8.0f) / (status.readMs * 1000.0f) : 0;
	const uint64_t writeNoContentionUs = status.writeOperationUs > status.writeBusWaitUs ?
		status.writeOperationUs - status.writeBusWaitUs : 0;
	const uint64_t readNoContentionUs = status.readOperationUs > status.readBusWaitUs ?
		status.readOperationUs - status.readBusWaitUs : 0;
	const float writeNoContentionMbps = writeNoContentionUs ? status.bytesWritten * 8.0f / writeNoContentionUs : 0;
	const float readNoContentionMbps = readNoContentionUs ? status.bytesRead * 8.0f / readNoContentionUs : 0;
	const float writeSpiMbps = status.writeSpiActiveUs ? status.bytesWritten * 8.0f / status.writeSpiActiveUs : 0;
	const float readSpiMbps = status.readSpiActiveUs ? status.bytesRead * 8.0f / status.readSpiActiveUs : 0;
	char* buffer = new (std::nothrow) char[1536];
	if (!buffer) {
		json = "{\"schemaVersion\":1,\"reason\":\"no_memory\"}";
		return;
	}
	snprintf(buffer, 1536, "{\"schemaVersion\":1,\"model\":\"%s\",\"capacityBytes\":%u,"
		"\"running\":%s,\"success\":%s,\"mode\":\"%s\"," 
		"\"phase\":\"%s\",\"speedMHz\":%u,\"startBlock\":%u,\"blockCount\":%u,\"currentBlock\":%u,"
		"\"currentPage\":%u,\"totalPages\":%u,\"goodBlocks\":%u,\"badBlocks\":%u,"
		"\"eraseMs\":%u,\"writeMs\":%u,\"readMs\":%u,\"totalMs\":%u,"
		"\"bytesWritten\":%u,\"bytesRead\":%u,\"writeMbps\":%.3f,\"readMbps\":%.3f,"
		"\"writeTiming\":{\"operationUs\":%llu,\"busWaitUs\":%llu,\"spiActiveUs\":%llu,"
		"\"readyWaitUs\":%llu,\"processUs\":%llu,\"yieldUs\":%llu,"
		"\"noContentionMbps\":%.3f,\"spiMbps\":%.3f},"
		"\"readTiming\":{\"operationUs\":%llu,\"busWaitUs\":%llu,\"spiActiveUs\":%llu,"
		"\"readyWaitUs\":%llu,\"processUs\":%llu,\"yieldUs\":%llu,"
		"\"noContentionMbps\":%.3f,\"spiMbps\":%.3f},"
		"\"writeCrc32\":\"%08X\",\"readCrc32\":\"%08X\",\"maxReadStatus\":\"%02X\","
		"\"mismatchPages\":%u,\"firstMismatchPage\":%u,\"firstMismatchBlock\":%u,"
		"\"firstMismatchByte\":%u,\"expectedByte\":\"%02X\",\"actualByte\":\"%02X\","
		"\"firstMismatchStatus\":\"%02X\",\"reason\":\"%s\"}",
		eventFlash.modelName(), eventFlash.capacityBytes(),
		status.running ? "true" : "false", status.success ? "true" : "false",
		modeName(status.mode), phaseName(status.phase), status.speedMHz,
		status.startBlock, status.blockCount, status.currentBlock, status.currentPage, status.totalPages,
		status.goodBlocks, status.badBlocks, status.eraseMs, status.writeMs, status.readMs, status.totalMs,
		status.bytesWritten, status.bytesRead, writeMbps, readMbps,
		(unsigned long long)status.writeOperationUs, (unsigned long long)status.writeBusWaitUs,
		(unsigned long long)status.writeSpiActiveUs, (unsigned long long)status.writeReadyWaitUs,
		(unsigned long long)status.writeProcessUs, (unsigned long long)status.writeYieldUs,
		writeNoContentionMbps, writeSpiMbps,
		(unsigned long long)status.readOperationUs, (unsigned long long)status.readBusWaitUs,
		(unsigned long long)status.readSpiActiveUs, (unsigned long long)status.readReadyWaitUs,
		(unsigned long long)status.readProcessUs, (unsigned long long)status.readYieldUs,
		readNoContentionMbps, readSpiMbps,
		status.writeCrc32, status.readCrc32,
		status.maxReadStatus, status.mismatchPages, status.firstMismatchPage,
		status.firstMismatchPage / WinbondSpiNand::pagesPerBlock, status.firstMismatchByte,
		status.expectedByte, status.actualByte, status.firstMismatchStatus, status.reason);
	json = buffer;
	delete[] buffer;
}
