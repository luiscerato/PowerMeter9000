#pragma once

#include "history_format.h"
#include "nand_partition.h"
#include <atomic>

struct HistoryRecoveryMetrics {
	uint32_t headerReads = 0;
	uint32_t validHeaders = 0;
	uint32_t elapsedUs = 0;
	bool historyFound = false;
};

struct HistoryStoreMetrics {
	uint32_t acceptedSamples = 0;
	uint32_t rejectedSamples = 0;
	uint32_t writtenPages = 0;
	uint32_t failedPages = 0;
	uint32_t verificationFailures = 0;
	uint16_t activeSamples = 0;
	uint16_t currentBlock = 0;
	uint8_t currentPage = 0;
	bool writePending = false;
	bool hasCommittedPages = false;
	uint32_t oldestTimestamp = 0;
	uint32_t newestTimestamp = 0;
	uint32_t latestSequence = 0;
	uint16_t preparedBlock = NandPartition::invalidBlock;
	bool erasePending = false;
	bool eraseReady = false;
	bool writeBlockReady = true;
};

struct HistoryReadPlan {
	uint16_t firstBlock = 0;
	uint8_t firstPage = 0;
	uint32_t firstSequence = 0;
	uint32_t storedPageCount = 0;
	uint32_t physicalPageCount = 0;
	uint32_t sampleCount = 0;
	uint16_t activeSampleCount = 0;
	uint32_t fromTimestamp = 0;
	uint32_t toTimestamp = UINT32_MAX;
	uint32_t searchElapsedUs = 0;
	uint32_t searchHeaderReads = 0;
	bool includesActivePage = false;
};

enum class HistoryReadPlanStatus : uint8_t {
	ready,
	noData,
	rangeTooLarge,
	ioError
};

// Almacena paginas historicas completas usando doble buffer y la cola NAND compartida.
class HistoryStore {
public:
	// Asocia el Store con la particion historica ya montada; partition permanece bajo control externo.
	explicit HistoryStore(NandPartition& partition);

	// Prepara los buffers y la cola de finalizacion; sin parametros.
	bool begin();

	// Pausa lectores y escritor y espera las llamadas ya iniciadas antes de borrar.
	void prepareFormat();

	// Reanuda el Store solo despues de que el borrado y el montaje terminaron bien.
	void finishFormat(bool success);

	// Agrega una muestra ya empaquetada; timestamp, groupConfig y samplePeriod describen la pagina destino.
	bool addSample(uint32_t timestamp, uint32_t groupConfig, uint16_t samplePeriod, const uint8_t* sample, size_t sampleSize);

	// Fuerza la escritura de la pagina parcial actual; sin parametros.
	bool flush();

	// Libera buffers terminados e informa si una escritura fallo; sin parametros.
	void poll();

	// Copia las metricas de la ultima reconstruccion de cursor; metrics recibe la instantanea.
	void getRecoveryMetrics(HistoryRecoveryMetrics& metrics) const;

	// Copia contadores del escritor y su cursor actual; metrics recibe la instantanea.
	void getMetrics(HistoryStoreMetrics& metrics) const;

	// Actualiza el timestamp de la pagina RAM activa antes de calcular sus CRC.
	bool updateActiveTimestamp(uint32_t timestamp);

	// Devuelve el timestamp de la ultima pagina valida recuperada al iniciar.
	uint32_t lastStoredTimestamp() const;

	// Construye un plan inmutable de paginas confirmadas dentro de un intervalo Unix; snapshot recibe la pagina RAM opcional.
	HistoryReadPlanStatus buildReadPlan(uint32_t fromTimestamp, uint32_t toTimestamp, HistoryReadPlan& plan,
		uint8_t* activeSnapshot, size_t snapshotSize);

	// Copia el rango temporal conocido por el Store; oldest y newest se expresan en segundos Unix.
	void getTimestampRange(uint32_t& oldestTimestamp, uint32_t& newestTimestamp) const;

private:
	NandPartition& partition;
	std::atomic<bool> formatPaused{false};
	std::atomic<uint16_t> activeCalls{0};
	uint8_t buffers[2][HistoryFormat::pageSize] = {};
	bool pending[2] = {};
	uint8_t activeBuffer = 0;
	uint16_t block = 0;
	uint8_t page = 0;
	uint32_t sequence = 0;
	size_t activeSampleSize = 0;
	QueueHandle_t completionQueue = nullptr;
	StaticQueue_t completionQueueStorage;
	uint8_t completionQueueData[2 * sizeof(NandIoResult)] = {};
	uint8_t headerBuffer[HistoryFormat::headerSize] = {};
	uint8_t planHeaderBuffer[HistoryFormat::headerSize] = {};
	StaticQueue_t planQueueStorage;
	uint8_t planQueueData[sizeof(NandIoResult)] = {};
	QueueHandle_t planQueue = nullptr;
	SemaphoreHandle_t planMutex = nullptr;
	StaticQueue_t maintenanceQueueStorage;
	uint8_t maintenanceQueueData[sizeof(NandIoResult)] = {};
	QueueHandle_t maintenanceQueue = nullptr;
	HistoryRecoveryMetrics recoveryMetrics;
	HistoryStoreMetrics storeMetrics;
	uint32_t latestStoredTimestamp = 0;
	uint32_t oldestStoredTimestamp = 0;
	uint16_t latestStoredBlock = 0;
	uint8_t latestStoredPage = 0;
	uint32_t latestStoredSequence = 0;
	bool hasLatestStoredPage = false;
	uint16_t preparedBlock = NandPartition::invalidBlock;
	bool preparedErasePending = false;
	bool preparedEraseReady = false;
	bool writeBlockReady = true;
	struct PendingWrite {
		uint16_t block = 0;
		uint8_t page = 0;
		uint32_t sequence = 0;
		uint32_t lastTimestamp = 0;
		bool verificationPending = false;
	};
	PendingWrite pendingWrites[2];
	static constexpr uint32_t verificationRequestFlag = 0x80000000UL;

	// Inicia una pagina RAM nueva con timestamp, configuracion y periodo definidos.
	void startPage(uint32_t timestamp, uint32_t groupConfig, uint16_t samplePeriod, size_t sampleSize);

	// Finaliza CRC y encola la pagina activa para programacion NAND.
	bool submitPage();

	// Recalcula los CRC de la pagina RAM activa para que pueda copiarse como instantanea valida.
	void refreshActiveCrc();

	// Encola una relectura de la pagina programada; buffer identifica el buffer que conserva la pagina.
	bool submitVerification(uint8_t buffer);

	// Confirma estructura y CRC de una pagina completa; data contiene los 2048 bytes leidos de NAND.
	static bool isPageValid(const uint8_t* data);

	// Confirma una pagina verificada y actualiza el cursor temporal; buffer identifica su escritura pendiente.
	void commitVerifiedPage(uint8_t buffer);

	// Avanza al siguiente destino de pagina, recorriendo bloques utilizables.
	bool advanceCursor();

	// Programa el borrado anticipado del bloque que seguira al bloque actual.
	bool prepareNextBlock();

	// Borra el bloque cursor recuperado antes de reutilizarlo tras una vuelta completa.
	bool prepareCurrentBlock();

	// Recoge la confirmacion del borrado anticipado; sin parametros.
	void pollMaintenance();

	// Indica si el bloque siguiente esta borrado y listo para recibir paginas.
	bool isNextBlockPrepared() const;

	// Reconstruye cursor y secuencia desde paginas validas ya guardadas.
	bool recoverCursor();

	// Busca el primer bloque retenido usando solo cabeceras y la secuencia continua.
	bool recoverOldestTimestamp();

	// Lee y valida solo la cabecera de una pagina; valid y erased describen el resultado.
	bool readHeader(uint16_t relativeBlock, uint8_t page, HistoryFormat::PageHeader& header, bool& valid, bool& erased);

	// Lee una cabecera para planificar una descarga sin consumir resultados de las escrituras activas.
	bool readPlanHeader(uint16_t relativeBlock, uint8_t page, HistoryFormat::PageHeader& header, bool& valid, bool& erased);

	// Retrocede una pagina utilizable dentro de la particion; block y page se actualizan en sitio.
	bool previousPage(uint16_t& relativeBlock, uint8_t& page) const;

	// Retrocede count paginas sin leer NAND; block y page reciben la posicion final.
	bool previousPages(uint16_t& relativeBlock, uint8_t& page, uint32_t count) const;

	// Localiza por saltos la pagina anterior a timestamp; block, page, sequence y headerReads reciben el resultado.
	bool findPageAtOrBefore(uint32_t timestamp, uint16_t& relativeBlock, uint8_t& page, uint32_t& pageSequence,
		uint32_t& headerReads);

	// Indica si una pagina contiene al menos una muestra dentro de un intervalo Unix inclusivo.
	static bool pageIntersectsRange(const HistoryFormat::PageHeader& header, uint32_t fromTimestamp, uint32_t toTimestamp);

	// Compara secuencias de 24 bits contemplando el desborde modular.
	static bool isNewerSequence(uint32_t candidate, uint32_t current);

	// Decodifica tres bytes little-endian como entero de 24 bits.
	static uint32_t readUint24(const uint8_t* source);

	// Escribe los 24 bits bajos de value en dest en orden little-endian.
	static void writeUint24(uint8_t* dest, uint32_t value);
};
