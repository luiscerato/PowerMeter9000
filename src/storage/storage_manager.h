#pragma once

#include <Arduino.h>
#include "history_store.h"
#include "grid_event_store.h"
#include "diagnostic_store.h"
#include "nand_partition.h"
#include "webfs_store.h"
#include "wave_event_store.h"

enum class StorageState : uint8_t {
	notDetected,
	unformatted,
	formatting,
	ready,
	formatError
};

enum class StoragePartitionId : uint8_t {
	history,
	waveEvents,
	gridEvents,
	diagnostics,
	webFsA,
	webFsB
};

struct StoragePartitionLayout {
	uint16_t startBlock;
	uint16_t blockCount;
};

struct StorageLayout {
	StoragePartitionLayout history;
	StoragePartitionLayout waveEvents;
	StoragePartitionLayout gridEvents;
	StoragePartitionLayout diagnostics;
};

static_assert(sizeof(StoragePartitionLayout) == 4, "Unexpected storage partition layout size");
static_assert(sizeof(StorageLayout) == 16, "Unexpected storage layout size");

struct StorageFormatProgress {
	bool active = false;
	bool completed = false;
	bool failed = false;
	uint16_t currentBlock = 0;
	uint16_t totalBlocks = 0;
	uint16_t scannedBlocks = 0;
	uint16_t erasedBlocks = 0;
	uint16_t usableBlocks = 0;
	uint16_t badBlocks = 0;
	uint16_t failedBlocks = 0;
};

struct StorageManagerStatus {
	StorageState state = StorageState::notDetected;
	uint16_t nandBlockCount = 0;
	uint32_t capacityBytes = 0;
	uint32_t metadataSequence = 0;
	NandPartitionInfo history;
	NandPartitionInfo waveEvents;
	NandPartitionInfo gridEvents;
	NandPartitionInfo diagnostics;
	NandPartitionInfo webFsA;
	NandPartitionInfo webFsB;
	NandPartitionFormatProgress historyFormat;
	NandPartitionFormatProgress waveEventsFormat;
	NandPartitionFormatProgress gridEventsFormat;
	NandPartitionFormatProgress diagnosticsFormat;
	NandPartitionFormatProgress webFsAFormat;
	NandPartitionFormatProgress webFsBFormat;
	StorageFormatProgress format;
	HistoryRecoveryMetrics historyRecovery;
	HistoryStoreMetrics historyStore;
	GridEventStoreMetrics gridStore;
	DiagnosticStoreMetrics diagnosticStore;
	WebFsInfo webFs;
	NandIoMetrics ioMetrics;
};

// Inicializa metadata, bitmap y particiones sobre una unica NAND fisica.
class StorageManager {
public:
	// Asocia el manager con el driver NAND que se detecta antes de montar metadata.
	explicit StorageManager(WinbondSpiNand& nandDriver);

	// Monta NAND y diagnostico antes que los otros stores; onDiagnosticsReady se llama al quedar listo el logger.
	bool begin(void (*onDiagnosticsReady)(DiagnosticStore&) = nullptr);

	// Devuelve el estado de montaje actual; sin parametros.
	StorageState state() const;

	// Indica si existe una metadata lista que habilita las particiones; sin parametros.
	bool isReady() const;

	// Inicia el formateo destructivo de la NAND cuando no hay otro formato en curso; sin parametros.
	bool startFormat();

	// Inicia el borrado fisico en background; id identifica la particion y excluye metadata.
	bool startPartitionFormat(StoragePartitionId id);

	// Devuelve la particion elegida; id identifica history, wave, grid o diagnostico.
	NandPartition& partition(StoragePartitionId id);

	// Devuelve el servicio que ejecuta operaciones fisicas; sin parametros.
	NandIoService& ioService();

	// Devuelve el escritor del historico para registrar muestras ya empaquetadas.
	HistoryStore& history();

	// Devuelve el Store de incidentes Grid ya asociado a su particion.
	GridEventStore& gridEvents();
	// Devuelve el guardador Wave asociado a su particion; sin parametros.
	waveEventStore& waveEvents() { return waveStore; }

	// Devuelve el escritor de logs de la particion diagnostica.
	DiagnosticStore& diagnostics();

	// Devuelve el Store de imagenes web A/B montadas sobre NAND.
	WebFsStore& webFs();

	// Copia estado, capacidades y metricas para diagnostico; status recibe la instantanea.
	void getStatus(StorageManagerStatus& status) const;

	// Construye el layout inicial para la geometria detectada; layout recibe las regiones propuestas.
	bool buildDefaultLayout(StorageLayout& layout) const;

private:
	static constexpr uint32_t metadataMagic = 0x54534D50U;
	static constexpr uint8_t metadataVersion = 1;
	static constexpr uint16_t metadataReservedBlocks = 16;
	static constexpr uint16_t metadataPage = 0;
	static constexpr uint16_t metadataPageSize = WinbondSpiNand::pageDataSize;

	struct __attribute__((packed)) MetadataHeader {
		uint32_t magic = 0;
		uint8_t version = 0;
		uint8_t state = 0;
		uint16_t headerSize = 0;
		uint16_t nandBlockCount = 0;
		uint16_t pageDataSize = 0;
		uint32_t sequence = 0;
		StorageLayout layout;
		uint16_t bitmapBytes = 0;
		uint16_t reserved = 0;
		uint32_t crc32 = 0;
	};
	static_assert(sizeof(MetadataHeader) == 40, "Unexpected storage metadata header size");

	WinbondSpiNand& nand;
	NandIoService nandService;
	NandPartition historyPartition;
	HistoryStore historyStore;
	NandPartition wavePartition;
	waveEventStore waveStore;
	NandPartition gridPartition;
	GridEventStore gridEventStore;
	NandPartition diagnosticPartition;
	DiagnosticStore diagnosticStore;
	NandPartition webBankAPartition;
	NandPartition webBankBPartition;
	WebFsStore webFsStore;
	StorageState mountState = StorageState::notDetected;
	MetadataHeader activeMetadata;
	uint8_t usableBlockBitmap[WinbondSpiNand::maxBlockCount / 8] = {};
	uint8_t metadataPageBuffer[metadataPageSize] = {};
	StaticQueue_t metadataResultQueueStorage;
	uint8_t metadataResultQueueData[sizeof(NandIoResult)] = {};
	QueueHandle_t metadataResultQueue = nullptr;
	bool metadataReadFailed = false;
	StaticQueue_t formatResultQueueStorage;
	uint8_t formatResultQueueData[sizeof(NandIoResult)] = {};
	QueueHandle_t formatResultQueue = nullptr;
	TaskHandle_t formatTaskHandle = nullptr;
	bool formatTaskReserved = false;
	bool formatTaskIsGlobal = false;
	StoragePartitionId formatTarget = StoragePartitionId::history;
	portMUX_TYPE formatMutex = portMUX_INITIALIZER_UNLOCKED;
	StorageFormatProgress formatProgress;
	uint32_t formatRequestId = 0;

	// Lee y valida los dos bloques de metadata; sin parametros.
	bool loadMetadata();

	// Lee una pagina de metadata esperando solo durante el arranque; block identifica el bloque fisico.
	bool readMetadataPage(uint16_t block);

	// Punto de entrada FreeRTOS que ejecuta el formateo solicitado sobre este manager.
	static void formatTaskEntry(void* parameter);

	// Escanea, borra y escribe metadata redundante sin bloquear la tarea de adquisicion.
	void formatTaskLoop();

	// Borra y vuelve a montar solamente la particion seleccionada.
	void partitionFormatTaskLoop();

	// Lee los marcadores de todos los bloques y actualiza el bitmap de bloques utilizables.
	bool scanFormatBlocks();

	// Borra cada bloque utilizable y excluye del bitmap los que fallen durante el borrado.
	bool eraseFormatBlocks();

	// Construye, escribe y verifica las dos copias iniciales de metadata; layout define las particiones.
	bool writeFormatMetadata(const StorageLayout& layout);

	// Programa y relee una copia de metadata; block identifica su bloque fisico y header su contenido.
	bool writeMetadataCopy(uint16_t block, const MetadataHeader& header);

	// Encola una operacion del formateo y espera exclusivamente su resultado; request define la operacion.
	bool submitFormatRequest(NandIoRequest& request);

	// Espera una finalizacion del formateo identificada por requestId; result recibe la respuesta NAND.
	bool waitFormatResult(uint32_t requestId, NandIoResult& result);

	// Actualiza el progreso visible por web; progress contiene la nueva instantanea.
	void setFormatProgress(const StorageFormatProgress& progress);

	// Finaliza el formateo y deja el estado listo o en error; success indica si se validaron las dos copias.
	void finishFormat(bool success);

	// Reserva la unica tarea de formato compartida; sin parametros.
	bool reserveFormatTask();

	// Libera la reserva de tarea cuando no se pudo iniciar o termino el trabajo.
	void releaseFormatTask();

	// Marca o limpia la disponibilidad de un bloque fisico dentro del bitmap; block y usable definen el cambio.
	void setUsableBlock(uint16_t block, bool usable);

	// Indica si el bitmap actual permite usar block; block identifica una direccion fisica NAND.
	bool isUsableBlock(uint16_t block) const;

	// Indica si existen al menos dos bloques utilizables dentro de la reserva de metadata.
	bool hasUsableMetadataBlocks() const;

	// Calcula el CRC de metadata excluyendo su propio campo; bitmapBytes indica cuantos bytes estan incluidos.
	uint32_t calculateMetadataCrc(uint16_t bitmapBytes) const;

	// Verifica formato, geometria y CRC de metadataPageBuffer; header recibe la copia validada.
	bool validateMetadata(MetadataHeader& header) const;

	// Configura particiones y bitmap desde la metadata elegida; header contiene la metadata valida.
	void applyMetadata(const MetadataHeader& header);

	// Verifica que las particiones no se solapen ni excedan la geometria; layout describe sus regiones.
	bool isValidLayout(const StorageLayout& layout) const;

	// Comprueba si una pagina parece estar borrada; data y length identifican el rango a evaluar.
	static bool isErased(const uint8_t* data, size_t length);

	// Calcula CRC32 IEEE de data; data y length delimitan los bytes protegidos.
	static uint32_t calculateCrc32(const uint8_t* data, size_t length);

	// Compara secuencias con desborde modular; candidate y current son numeros de secuencia uint32.
	static bool isNewerSequence(uint32_t candidate, uint32_t current);
};
