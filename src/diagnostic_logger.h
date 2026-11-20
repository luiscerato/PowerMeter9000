#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/stream_buffer.h>
#include <freertos/task.h>
#include <stdarg.h>

enum class DiagnosticLevel : uint8_t { none, error, warn, info, verbose, debug };

namespace DiagnosticModule {
constexpr uint32_t logger = 1000;
constexpr uint32_t main = 1001;
constexpr uint32_t storage = 1002;
constexpr uint32_t wifi = 1003;
constexpr uint32_t mqtt = 1004;
constexpr uint32_t tcp = 1005;
constexpr uint32_t health = 1006;
constexpr uint8_t count = 7;
}

namespace DiagnosticFlags {
constexpr uint8_t hasMore = 0x01;
constexpr uint8_t truncated = 0x02;
constexpr uint8_t utcValid = 0x04;
}

struct DiagnosticChunk {
	uint64_t uptimeUs = 0;
	uint64_t unixUs = 0;
	uint32_t moduleId = 0;
	uint16_t sourceLine = 0;
	uint8_t level = 0;
	uint8_t flags = 0;
	uint8_t partIndex = 0;
	uint8_t textLength = 0;
	char text[80] = {};
};
static_assert(sizeof(DiagnosticChunk) <= 120, "Diagnostic queue slot exceeds RAM budget");

struct DiagnosticRecentChunk {
	uint32_t sequence = 0;
	DiagnosticChunk chunk;
};

struct DiagnosticLoggerMetrics {
	uint32_t acceptedChunks = 0;
	uint32_t lostChunks = 0;
	uint32_t incompleteMessages = 0;
	uint32_t truncatedMessages = 0;
	uint32_t queueFull = 0;
	uint32_t nandDrops = 0;
	uint32_t serialDrops = 0;
	uint8_t queueHighWater = 0;
	uint8_t queueUsed = 0;
	uint16_t stackFreeWords = 0;
};

struct DiagnosticLoggerConfig {
	DiagnosticLevel globalLevel = DiagnosticLevel::info;
	bool serialEnabled = true;
	bool nandEnabled = true;
	uint8_t moduleLevels[DiagnosticModule::count] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
};

class DiagnosticStore;
struct DiagnosticBinarySnapshot;

class DiagnosticLogger {
public:
	static constexpr uint8_t queueLength = 32;
	static constexpr uint8_t textPerChunk = 80;
	static constexpr uint8_t maxParts = 4;
	// La NAND conserva el historial completo; RAM solo necesita una ventana reciente.
	static constexpr uint8_t recentChunkCount = 16;
	static constexpr uint16_t taskStackWords = 3072;

	// Crea la cola y carga opciones; sin parametros.
	bool begin();

	// Inicia el consumidor; store recibe los registros persistentes o puede ser nullptr.
	bool start(DiagnosticStore* store);

	// Conecta almacenamiento despues del formateo; store debe permanecer valido.
	void attachStore(DiagnosticStore* store);

	// Indica si hay un escritor NAND asociado; sin parametros.
	bool hasStore() const;

	// Prepara el logger y su Store para el formateo coordinado por StorageManager.
	bool prepareStoreFormat();

	// Finaliza o aborta el formato del Store y reactiva el procesamiento de logs.
	bool finishStoreFormat(bool success);

	// Indica si el formateo del registro esta en curso.
	bool isFormatRunning() const;

	// Captura los buffers NAND del logger y su posicion persistida; snapshot recibe una copia estable.
	bool captureStoreSnapshot(DiagnosticBinarySnapshot& snapshot);

	// Indica si level esta habilitado para moduleId.
	bool isEnabled(uint32_t moduleId, DiagnosticLevel level) const;

	// Formatea y envia fragmentos; moduleId, level, sourceLine y format describen el mensaje.
	void log(uint32_t moduleId, DiagnosticLevel level, uint16_t sourceLine, const char* format, ...);

	// Encola moduleId/level/sourceLine/format esperando como maximo timeoutMs; no llamar desde ISR ni MeterTask.
	bool logWait(uint32_t moduleId, DiagnosticLevel level, uint16_t sourceLine, uint32_t timeoutMs,
		const char* format, ...);

	// Envia al UART solo los bytes disponibles; llamar desde loop.
	void pollSerial();

	// Copia contadores actuales en metrics.
	void getMetrics(DiagnosticLoggerMetrics& metrics) const;

	// Devuelve palabras libres minimas de la pila del consumidor; sin parametros.
	uint16_t getStackFreeWords() const;

	// Copia un tramo por after o before; devuelve el rango reciente y si el cursor quedo atras.
	uint8_t getRecentChunks(uint32_t after, uint32_t before, DiagnosticRecentChunk* output, uint8_t capacity,
		uint32_t& oldest, uint32_t& newest, bool& gap) const;

	// Copia niveles y destinos activos en config.
	void getConfig(DiagnosticLoggerConfig& config) const;

	// Cambia y persiste el umbral general; level define el nuevo umbral.
	bool setGlobalLevel(DiagnosticLevel level);

	// Cambia y persiste un umbral particular; moduleId identifica un modulo conocido.
	bool setModuleLevel(uint32_t moduleId, DiagnosticLevel level);

	// Quita la excepcion persistida de moduleId para heredar el nivel global.
	bool clearModuleLevel(uint32_t moduleId);

	// Cambia y persiste los destinos; serialEnabled y nandEnabled seleccionan cada salida.
	bool setOutputs(bool serialEnabled, bool nandEnabled);

	// Entrega el ID y nombre del modulo en index para la cabecera NAND.
	static bool getModuleInfo(uint8_t index, uint32_t& moduleId, const char*& name);

	// Devuelve la cantidad de modulos compilados; sin parametros.
	static uint8_t getModuleCount();

private:
	static constexpr uint16_t serialBufferSize = 512;
	static constexpr uint8_t moduleCount = DiagnosticModule::count;
	static constexpr uint8_t inheritLevel = 0xFF;
	static constexpr size_t queueDataBytes = queueLength * sizeof(DiagnosticChunk);
	StaticQueue_t queueControl;
	alignas(4) uint8_t queueData[queueDataBytes] = {};
	QueueHandle_t queue = nullptr;
	StaticStreamBuffer_t serialControl;
	alignas(4) uint8_t serialData[serialBufferSize + 1] = {};
	StreamBufferHandle_t serialBuffer = nullptr;
	TaskHandle_t task = nullptr;
	volatile bool formatTaskRunning = false;
	StaticSemaphore_t storeAccessControl;
	SemaphoreHandle_t storeAccess = nullptr;
	DiagnosticStore* store = nullptr;
	uint8_t globalLevel = uint8_t(DiagnosticLevel::info);
	uint8_t moduleLevels[moduleCount] = { inheritLevel, inheritLevel, inheritLevel, inheritLevel,
		inheritLevel, inheritLevel, inheritLevel };
	bool serialOutput = true;
	bool nandOutput = true;
	mutable portMUX_TYPE stateMutex = portMUX_INITIALIZER_UNLOCKED;
	DiagnosticLoggerMetrics metrics;
	uint32_t pendingLostChunks = 0;
	uint32_t pendingFullEvents = 0;
	DiagnosticRecentChunk* recentChunks = nullptr;
	uint32_t recentSequence = 0;
	mutable portMUX_TYPE recentMutex = portMUX_INITIALIZER_UNLOCKED;

	// Devuelve la posicion de moduleId o -1 si no esta en la tabla.
	static int8_t moduleIndex(uint32_t moduleId);

	// Ejecuta el consumidor FreeRTOS usando el objeto recibido en parameter.
	static void taskEntry(void* parameter);

	// Procesa fragmentos y sintetiza avisos de perdida; sin parametros.
	void taskLoop();

	// Entrega chunk a los destinos sin volver a entrar en la cola.
	void emit(const DiagnosticChunk& chunk);

	// Guarda el fragmento emitido en el anillo RAM reciente; chunk contiene el registro.
	void recordRecentChunk(const DiagnosticChunk& chunk);

	// Agrega una linea al buffer serie si cabe completa; chunk contiene el texto.
	void enqueueSerial(const DiagnosticChunk& chunk);

	// Crea un aviso agregado al recuperarse la cola; sin parametros.
	void emitLossNotice();

	// Acumula count fragmentos perdidos sin producir otro log.
	void recordQueueLoss(uint32_t count);

	// Formatea y encola un mensaje usando waitTicks por fragmento; args contiene sus parametros.
	bool enqueueMessage(uint32_t moduleId, DiagnosticLevel level, uint16_t sourceLine,
		const char* format, va_list args, TickType_t waitTicks);
};

extern DiagnosticLogger diagnosticLogger;

#define DIAG_LOG(moduleId, logLevel, ...) do { \
	const uint32_t diagnosticModuleId = (moduleId); \
	if (diagnosticLogger.isEnabled(diagnosticModuleId, (logLevel))) \
		diagnosticLogger.log(diagnosticModuleId, (logLevel), __LINE__, __VA_ARGS__); \
} while (false)

#define DIAG_LOG_WAIT(moduleId, logLevel, timeoutMs, ...) do { \
	const uint32_t diagnosticModuleId = (moduleId); \
	if (diagnosticLogger.isEnabled(diagnosticModuleId, (logLevel))) \
		diagnosticLogger.logWait(diagnosticModuleId, (logLevel), __LINE__, (timeoutMs), __VA_ARGS__); \
} while (false)

#define DIAG_ERROR(moduleId, ...) DIAG_LOG(moduleId, DiagnosticLevel::error, __VA_ARGS__)
#define DIAG_WARN(moduleId, ...) DIAG_LOG(moduleId, DiagnosticLevel::warn, __VA_ARGS__)
#define DIAG_INFO(moduleId, ...) DIAG_LOG(moduleId, DiagnosticLevel::info, __VA_ARGS__)
#define DIAG_VERBOSE(moduleId, ...) DIAG_LOG(moduleId, DiagnosticLevel::verbose, __VA_ARGS__)
#define DIAG_DEBUG(moduleId, ...) DIAG_LOG(moduleId, DiagnosticLevel::debug, __VA_ARGS__)
