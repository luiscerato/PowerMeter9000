#pragma once

#include "nand_partition.h"
#include "../diagnostic_logger.h"
#include <atomic>

struct DiagnosticStoreMetrics {
	bool ready = false;
	bool formatting = false;
	bool formatDone = false;
	bool formatSuccess = false;
	uint32_t writtenPages = 0;
	uint32_t failedWrites = 0;
	uint32_t recoveredPages = 0;
	uint32_t pageSequence = 0;
	uint32_t recordsThisRun = 0;
	uint32_t capacityBytes = 0;
	uint32_t usedBytes = 0;
	uint32_t usedPages = 0;
	uint16_t currentBlock = 0;
	uint8_t currentPage = 0;
	uint16_t activeBytes = 0;
	uint16_t usedBlocks = 0;
	uint16_t formatBlocksDone = 0;
	uint16_t formatBlocksTotal = 0;
	uint32_t formatBytesDone = 0;
	uint32_t formatBytesErased = 0;
	uint32_t formatBytesTotal = 0;
	uint8_t formatPercent = 0;
};

struct DiagnosticStoredChunk {
	DiagnosticChunk chunk;
	uint32_t pageSequence = 0;
	uint8_t recordIndex = 0;
};

struct DiagnosticBinarySnapshot {
	uint8_t ramPages[2][WinbondSpiNand::pageDataSize] = {};
	uint16_t block = 0;
	uint8_t lastCommittedPage = 0;
	uint8_t ramPageCount = 0;
	uint32_t recentSequence = 0;
};

class DiagnosticStore {
public:
	// Asocia el store con la particion diagnostica, que conserva su propiedad.
	explicit DiagnosticStore(NandPartition& partition);

	// Recupera el anillo existente o prepara un primer bloque; sin parametros.
	bool begin();

	// Detiene escrituras nuevas y espera cualquier pagina en vuelo antes de formatear.
	bool stop();

	// Detiene el logger antes del borrado; debe llamarse con storeAccess tomado.
	bool prepareFormat();

	// Borra los bloques utilizables y actualiza el progreso por bloque.
	bool erasePartition();

	// Abre un anillo vacio despues del borrado; debe llamarse con storeAccess tomado.
	bool completeFormat();

	// Marca el formateo fallido y conserva el store detenido.
	void abortFormat();

	// Indica si el logger esta ejecutando el borrado solicitado.
	bool isFormatting() const;

	// Agrega chunk a la pagina RAM, sin exponer el formato NAND.
	bool append(const DiagnosticChunk& chunk);

	// Procesa confirmaciones NAND pendientes; sin parametros.
	void poll();

	// Cierra una pagina no vacia cuando vence su plazo; sin parametros.
	void flushIfDue();

	// Indica si el anillo NAND ya puede recibir registros; sin parametros.
	bool isReady() const;

	// Copia el estado actual en metrics.
	void getMetrics(DiagnosticStoreMetrics& metrics) const;

	// Congela las paginas RAM y el limite NAND; snapshot recibe la copia mientras el logger protege el escritor.
	bool captureSnapshot(DiagnosticBinarySnapshot& snapshot);

	// Busca el bloque escrito anterior a fromBlock sin cruzar anchorBlock; previous recibe invalidBlock al terminar.
	bool findPreviousSnapshotBlock(uint16_t fromBlock, uint16_t anchorBlock, uint16_t& previous);

	// Lee registros NAND hacia atras; cursor identifica la pagina y el byte anterior exclusivo.
	bool readPrevious(DiagnosticStoredChunk* output, uint8_t capacity, bool hasCursor,
		uint16_t cursorBlock, uint8_t cursorPage, uint16_t cursorOffset, bool hasAnchor,
		uint16_t anchorBlock, uint8_t anchorPage, uint8_t& count, uint16_t& nextBlock,
		uint8_t& nextPage, uint16_t& nextOffset, uint16_t& nextAnchorBlock,
		uint8_t& nextAnchorPage, bool& done);

private:
	static constexpr uint32_t blockMagic = 0x474F4C44U;
	static constexpr uint32_t pageMagic = 0x47415044U;
	static constexpr uint8_t formatVersion = 1;
	static constexpr uint16_t pageHeaderSize = 32;
	static constexpr uint16_t recordHeaderSize = 27;
	static constexpr uint16_t pageSize = WinbondSpiNand::pageDataSize;
	static constexpr uint16_t maxRecordsPerPage = (pageSize - pageHeaderSize) / recordHeaderSize;
	static constexpr uint8_t noBuffer = 0xFF;
	NandPartition& partition;
	uint8_t* buffers[2] = {};
	uint8_t scanBuffer[256] = {};
	uint8_t* viewerBuffer = nullptr;
	uint8_t activeBuffer = 0;
	uint8_t pendingBuffer = noBuffer;
	uint16_t currentBlock = 0;
	uint8_t currentPage = 1;
	uint16_t usedBytes = 0;
	uint16_t recordCount = 0;
	uint32_t blockSequence = 0;
	uint32_t pageSequence = 0;
	uint32_t bootId = 0;
	uint32_t viewerRequestId = 0;
	uint16_t formatFirstBlock = NandPartition::invalidBlock;
	bool viewerReadPending = false;
	uint64_t firstUnixMs = 0;
	uint64_t pageOpenedMs = 0;
	std::atomic<bool> ready{false};
	std::atomic<bool> formatting{false};
	QueueHandle_t completionQueue = nullptr;
	QueueHandle_t viewerQueue = nullptr;
	StaticQueue_t completionControl;
	StaticQueue_t viewerQueueControl;
	uint8_t completionData[2 * sizeof(NandIoResult)] = {};
	uint8_t viewerQueueData[sizeof(NandIoResult)] = {};
	StaticSemaphore_t viewerMutexControl;
	SemaphoreHandle_t viewerMutex = nullptr;
	mutable portMUX_TYPE metricsMutex = portMUX_INITIALIZER_UNLOCKED;
	DiagnosticStoreMetrics metrics;

	// Lee una pagina y espera el resultado; block, page, data y length describen el rango.
	bool readPage(uint16_t block, uint8_t page, uint8_t* data, size_t length);

	// Lee una pagina para el visor sin compartir la cola del escritor.
	bool readViewerPage(uint16_t block, uint8_t page, size_t length);

	// Busca un bloque anterior con cabecera valida; block recibe el bloque encontrado.
	bool findPreviousBlock(uint16_t fromBlock, uint16_t& block);

	// Retrocede una pagina valida; block y page reciben el destino.
	bool previousPage(uint16_t& block, uint8_t& page);

	// Espera la finalizacion de requestId y procesa cualquier escritura anterior.
	bool waitResult(uint32_t requestId);

	// Escanea cabeceras y calcula la siguiente pagina disponible.
	bool recover();

	// Borra block y escribe su cabecera de nueva secuencia.
	bool beginBlock(uint16_t block);

	// Finaliza y envia la pagina RAM actual si tiene registros.
	bool flushPage();

	// Espera a que el buffer programado vuelva a estar disponible.
	bool waitPending();

	// Procesa una confirmacion de programacion; result describe el resultado.
	void processResult(const NandIoResult& result);

	// Limpia el buffer activo para ensamblar una nueva pagina.
	void clearActive();

	// Codifica chunk en dest y devuelve los bytes escritos.
	static uint16_t encodeRecord(uint8_t* dest, const DiagnosticChunk& chunk);

	// Escribe value en dos bytes little-endian desde dest.
	static void write16(uint8_t* dest, uint16_t value);

	// Escribe value en cuatro bytes little-endian desde dest.
	static void write32(uint8_t* dest, uint32_t value);

	// Escribe value en ocho bytes little-endian desde dest.
	static void write64(uint8_t* dest, uint64_t value);

	// Lee dos bytes little-endian desde data.
	static uint16_t read16(const uint8_t* data);

	// Lee cuatro bytes little-endian desde data.
	static uint32_t read32(const uint8_t* data);

	// Lee ocho bytes little-endian desde data.
	static uint64_t read64(const uint8_t* data);

	// Calcula CRC32 encadenable sobre data y length; crc es el estado anterior.
	static uint32_t updateCrc(uint32_t crc, const uint8_t* data, size_t length);
};
