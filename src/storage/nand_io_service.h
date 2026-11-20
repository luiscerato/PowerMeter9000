#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "winbond_spi_nand.h"

enum class NandOperation : uint8_t {
	readPage,
	programPage,
	eraseBlock,
	readBadBlockMarker
};

enum class StoragePriority : uint8_t {
	high,
	medium,
	low
};

enum class NandIoError : uint8_t {
	none,
	notRunning,
	queueFull,
	invalidRequest,
	timeout,
	nandFailure,
	completionQueueFull
};

class NandPartition;

struct NandIoRequest {
	NandOperation operation = NandOperation::readPage;
	StoragePriority priority = StoragePriority::low;
	uint16_t physicalBlock = 0;
	uint8_t page = 0;
	uint16_t column = 0;
	uint8_t* data = nullptr;
	size_t length = 0;
	uint32_t requestId = 0;
	QueueHandle_t completionQueue = nullptr;
	void* context = nullptr;
	NandPartition* partition = nullptr;
};

struct NandIoResult {
	uint32_t requestId = 0;
	void* context = nullptr;
	bool success = false;
	NandIoError error = NandIoError::none;
	uint8_t nandStatus = 0;
	size_t transferredBytes = 0;
	uint32_t elapsedUs = 0;
};

struct NandIoMetrics {
	uint32_t accepted[3] = {};
	uint32_t rejected[3] = {};
	uint32_t completed[4] = {};
	uint32_t failed[4] = {};
	uint32_t timeouts[4] = {};
	uint32_t bytesRead = 0;
	uint32_t bytesWritten = 0;
	uint32_t maxQueueDepth[3] = {};
	uint32_t maxElapsedUs[4] = {};
	uint64_t totalElapsedUs[4] = {};
	uint32_t lastElapsedUs[4] = {};
	uint32_t lastTransferredBytes[4] = {};
	uint32_t recentWindowStartedUs = 0;
	uint32_t recentReadBytes = 0;
	uint32_t recentWrittenBytes = 0;
	uint16_t stackFreeWords = 0;
	NandOperation lastOperation = NandOperation::readPage;
	NandIoError lastError = NandIoError::none;
};

// Serializa todas las operaciones fisicas de una NAND mediante colas por prioridad.
class NandIoService {
public:
	static constexpr uint8_t highQueueLength = 16;
	static constexpr uint8_t mediumQueueLength = 8;
	static constexpr uint8_t lowQueueLength = 4;

	// Asocia el servicio con el driver NAND ya inicializado; nandDriver permanece bajo control externo.
	explicit NandIoService(WinbondSpiNand& nandDriver);

	// Crea las colas y la tarea de servicio; taskPriority, stackWords y core definen la tarea FreeRTOS.
	bool begin(UBaseType_t taskPriority = 2, uint16_t stackWords = 3072, BaseType_t core = tskNO_AFFINITY);

	// Encola una operacion sin esperar su ejecucion; request conserva su buffer hasta la finalizacion.
	bool submit(const NandIoRequest& request);

	// Indica si la tarea de I/O esta lista para recibir solicitudes; sin parametros.
	bool isRunning() const;

	// Copia las metricas acumuladas; metrics recibe la instantanea protegida.
	void getMetrics(NandIoMetrics& metrics) const;

private:
	WinbondSpiNand& nand;
	QueueHandle_t highQueue = nullptr;
	QueueHandle_t mediumQueue = nullptr;
	QueueHandle_t lowQueue = nullptr;
	StaticQueue_t highQueueStorage;
	StaticQueue_t mediumQueueStorage;
	StaticQueue_t lowQueueStorage;
	uint8_t highQueueData[highQueueLength * sizeof(NandIoRequest)];
	uint8_t mediumQueueData[mediumQueueLength * sizeof(NandIoRequest)];
	uint8_t lowQueueData[lowQueueLength * sizeof(NandIoRequest)];
	TaskHandle_t taskHandle = nullptr;
	bool running = false;
	bool writeSessionActive = false;
	uint8_t originalProtection = 0;
	uint8_t originalConfiguration = 0;
	portMUX_TYPE metricsMutex = portMUX_INITIALIZER_UNLOCKED;
	NandIoMetrics metrics;

	// Punto de entrada FreeRTOS que deriva al objeto que recibio la tarea.
	static void storageTaskEntry(void* parameter);

	// Ejecuta el ciclo de seleccion y procesamiento de solicitudes; sin parametros.
	void storageTaskLoop();

	// Obtiene la siguiente solicitud respetando alta, media y baja prioridad.
	bool dequeueRequest(NandIoRequest& request);

	// Ejecuta una solicitud y publica su resultado; request define la operacion fisica.
	void processRequest(const NandIoRequest& request);

	// Valida la direccion y el buffer antes de acceder al driver; request define la operacion.
	bool isValidRequest(const NandIoRequest& request) const;

	// Devuelve la cola correspondiente a la prioridad solicitada; priority selecciona la cola.
	QueueHandle_t queueForPriority(StoragePriority priority) const;

	// Actualiza las metricas de envio; priority identifica la cola y accepted indica el resultado.
	void recordSubmission(StoragePriority priority, bool accepted);

	// Actualiza las metricas de finalizacion; request y result describen la operacion completada.
	void recordCompletion(const NandIoRequest& request, const NandIoResult& result);

	// Habilita escritura NAND una sola vez antes de programar o borrar; sin parametros.
	void ensureWriteSession();
};
