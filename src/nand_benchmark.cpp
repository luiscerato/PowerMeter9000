#include "nand_benchmark.h"
#include "w25n02kv.h"
#include <esp_crc.h>
#include <new>

extern W25N02Kv eventFlash;

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
	uint8_t maxReadStatus = 0;
	char reason[32] = "";
};

static BenchmarkStatus benchmarkStatus;
static portMUX_TYPE benchmarkMutex = portMUX_INITIALIZER_UNLOCKED;
static uint8_t badBlockMap[W25N02Kv::blockCount / 8];

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
	for (uint32_t i = 0; i < W25N02Kv::pageDataSize; ++i) {
		value = value * 1664525U + 1013904223U;
		data[i] = uint8_t(value >> 24);
	}
}

// Compara una pagina leida con el patron esperado.
// data: pagina leida; page: direccion fisica usada como semilla.
static bool matchesPattern(const uint8_t* data, uint32_t page)
{
	uint32_t value = page * 2654435761U + 0xA5C39E17U;
	for (uint32_t i = 0; i < W25N02Kv::pageDataSize; ++i) {
		value = value * 1664525U + 1013904223U;
		if (data[i] != uint8_t(value >> 24)) return false;
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

	uint8_t* pageData = new (std::nothrow) uint8_t[W25N02Kv::pageDataSize];
	if (!pageData) {
		finishBenchmark(BenchmarkPhase::failed, "no_memory", startedMs);
		vTaskDelete(nullptr);
		return;
	}
	memset(badBlockMap, 0, sizeof(badBlockMap));
	eventFlash.setClockHz(uint32_t(config.speedMHz) * 1000000U);
	uint8_t protection = 0, configuration = 0;
	eventFlash.beginWriteSession(protection, configuration);

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
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.goodBlocks = goodBlocks;
	benchmarkStatus.badBlocks = badBlocks;
	benchmarkStatus.totalPages = uint32_t(goodBlocks) * W25N02Kv::pagesPerBlock;
	taskEXIT_CRITICAL(&benchmarkMutex);

	phaseStarted = millis();
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		if (isBadBlock(block)) continue;
		uint8_t status = 0;
		updateProgress(BenchmarkPhase::erase, block, 0);
		if (!eventFlash.eraseBlock(block, status)) {
			finishBenchmark(BenchmarkPhase::failed, "erase_failed", startedMs); goto cleanup;
		}
		if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
		vTaskDelay(1);
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.eraseMs = millis() - phaseStarted;
	taskEXIT_CRITICAL(&benchmarkMutex);

	phaseStarted = millis();
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		if (isBadBlock(block)) continue;
		for (uint16_t pageInBlock = 0; pageInBlock < W25N02Kv::pagesPerBlock; ++pageInBlock) {
			const uint32_t page = uint32_t(block) * W25N02Kv::pagesPerBlock + pageInBlock;
			uint8_t status = 0;
			fillPattern(pageData, page);
			updateProgress(BenchmarkPhase::write, block, pagesDone);
			if (!eventFlash.programPage(page, pageData, W25N02Kv::pageDataSize, status)) {
				finishBenchmark(BenchmarkPhase::failed, "program_failed", startedMs); goto cleanup;
			}
			writeCrc = esp_crc32_le(writeCrc, pageData, W25N02Kv::pageDataSize);
			++pagesDone;
			if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
		}
		taskYIELD();
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.writeMs = millis() - phaseStarted;
	benchmarkStatus.bytesWritten = pagesDone * W25N02Kv::pageDataSize;
	benchmarkStatus.writeCrc32 = writeCrc;
	taskEXIT_CRITICAL(&benchmarkMutex);

	phaseStarted = millis();
	pagesDone = 0;
	for (uint16_t offset = 0; offset < config.blockCount; ++offset) {
		const uint16_t block = config.startBlock + offset;
		if (isBadBlock(block)) continue;
		for (uint16_t pageInBlock = 0; pageInBlock < W25N02Kv::pagesPerBlock; ++pageInBlock) {
			const uint32_t page = uint32_t(block) * W25N02Kv::pagesPerBlock + pageInBlock;
			uint8_t status = 0;
			updateProgress(BenchmarkPhase::read, block, pagesDone);
			if (!eventFlash.readPage(page, pageData, W25N02Kv::pageDataSize, status)) {
				finishBenchmark(BenchmarkPhase::failed, "read_failed", startedMs); goto cleanup;
			}
			if (!matchesPattern(pageData, page)) {
				finishBenchmark(BenchmarkPhase::failed, "data_mismatch", startedMs); goto cleanup;
			}
			readCrc = esp_crc32_le(readCrc, pageData, W25N02Kv::pageDataSize);
			++pagesDone;
			taskENTER_CRITICAL(&benchmarkMutex);
			if ((status & 0x70) > benchmarkStatus.maxReadStatus) benchmarkStatus.maxReadStatus = status & 0x70;
			taskEXIT_CRITICAL(&benchmarkMutex);
			if (shouldCancel()) { finishBenchmark(BenchmarkPhase::cancelled, "cancelled", startedMs); goto cleanup; }
		}
		taskYIELD();
	}
	taskENTER_CRITICAL(&benchmarkMutex);
	benchmarkStatus.readMs = millis() - phaseStarted;
	benchmarkStatus.bytesRead = pagesDone * W25N02Kv::pageDataSize;
	benchmarkStatus.readCrc32 = readCrc;
	taskEXIT_CRITICAL(&benchmarkMutex);
	finishBenchmark(writeCrc == readCrc ? BenchmarkPhase::complete : BenchmarkPhase::failed,
		writeCrc == readCrc ? "" : "crc_mismatch", startedMs);

cleanup:
	eventFlash.endWriteSession(protection, configuration);
	eventFlash.setClockHz(20000000);
	delete[] pageData;
	vTaskDelete(nullptr);
}

bool nandBenchmarkStart(NandBenchmarkMode mode, uint8_t speedMHz)
{
	if (speedMHz != 20 && speedMHz != 40 && speedMHz != 80) return false;
	taskENTER_CRITICAL(&benchmarkMutex);
	if (benchmarkStatus.running) {
		taskEXIT_CRITICAL(&benchmarkMutex);
		return false;
	}
	benchmarkStatus = BenchmarkStatus{};
	benchmarkStatus.running = true;
	benchmarkStatus.mode = mode;
	benchmarkStatus.speedMHz = speedMHz;
	benchmarkStatus.startBlock = mode == NandBenchmarkMode::quick ? 2038 : 0;
	benchmarkStatus.blockCount = mode == NandBenchmarkMode::quick ? 8 : W25N02Kv::blockCount;
	benchmarkStatus.phase = BenchmarkPhase::scan;
	taskEXIT_CRITICAL(&benchmarkMutex);

	if (xTaskCreatePinnedToCore(benchmarkTask, "nandBenchmark", 4096, nullptr, 1, nullptr, PRO_CPU_NUM) != pdPASS) {
		finishBenchmark(BenchmarkPhase::failed, "task_create_failed", millis());
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

void nandBenchmarkGetJson(String& json)
{
	BenchmarkStatus status;
	taskENTER_CRITICAL(&benchmarkMutex);
	status = benchmarkStatus;
	taskEXIT_CRITICAL(&benchmarkMutex);
	const float writeMbps = status.writeMs ? (status.bytesWritten * 8.0f) / (status.writeMs * 1000.0f) : 0;
	const float readMbps = status.readMs ? (status.bytesRead * 8.0f) / (status.readMs * 1000.0f) : 0;
	char buffer[768];
	snprintf(buffer, sizeof(buffer), "{\"schemaVersion\":1,\"running\":%s,\"success\":%s,\"mode\":\"%s\","
		"\"phase\":\"%s\",\"speedMHz\":%u,\"startBlock\":%u,\"blockCount\":%u,\"currentBlock\":%u,"
		"\"currentPage\":%u,\"totalPages\":%u,\"goodBlocks\":%u,\"badBlocks\":%u,"
		"\"eraseMs\":%u,\"writeMs\":%u,\"readMs\":%u,\"totalMs\":%u,"
		"\"bytesWritten\":%u,\"bytesRead\":%u,\"writeMbps\":%.3f,\"readMbps\":%.3f,"
		"\"writeCrc32\":\"%08X\",\"readCrc32\":\"%08X\",\"maxReadStatus\":\"%02X\",\"reason\":\"%s\"}",
		status.running ? "true" : "false", status.success ? "true" : "false",
		status.mode == NandBenchmarkMode::quick ? "quick" : "full", phaseName(status.phase), status.speedMHz,
		status.startBlock, status.blockCount, status.currentBlock, status.currentPage, status.totalPages,
		status.goodBlocks, status.badBlocks, status.eraseMs, status.writeMs, status.readMs, status.totalMs,
		status.bytesWritten, status.bytesRead, writeMbps, readMbps, status.writeCrc32, status.readCrc32,
		status.maxReadStatus, status.reason);
	json = buffer;
}
