#pragma once

#include <Arduino.h>
#include <atomic>
#include "history_store.h"

struct HistoryReadTiming {
	uint64_t totalUs = 0;
	uint32_t count = 0, maxUs = 0;
	uint32_t over1ms = 0, over2ms = 0, over5ms = 0, over10ms = 0;
};

struct HistoryReadTimings {
	HistoryReadTiming nand, prepare, queue;
};

struct HistoryReadSessionMetrics {
	uint32_t emptyBufferCalls = 0;
	uint32_t emptyBufferEpisodes = 0;
	uint64_t totalBufferWaitUs = 0;
	uint32_t lastBufferWaitUs = 0;
	uint32_t maxBufferWaitUs = 0;
};

// Transmite paginas historicas congeladas; HTTP usa RAM y como maximo 20 ms de espera de cola por llamada.
class HistoryReadSession {
public:
	// Reserva buffers dinamicos y comienza a precargar las paginas descriptas por plan.
	bool begin(NandPartition& partition, const HistoryReadPlan& plan, const uint8_t* activeSnapshot);

	// Copia destination/maxLength/offset para la respuesta id; devuelve RESPONSE_TRY_AGAIN si NAND aun prepara datos.
	size_t copyChunk(uint8_t* destination, size_t maxLength, size_t offset, uint32_t id);

	// Identifica la respuesta actual; sin parametros, cambia al iniciar cada descarga.
	uint32_t getId() const;

	// Cancela la respuesta id; la tarea libera sus paginas cuando termina cualquier lectura NAND pendiente.
	void cancel(uint32_t id);

	// Indica si la sesion termino y puede reutilizarse para otra descarga.
	bool isAvailable();

	// Copia metricas acumuladas desde el arranque; output recibe la instantanea protegida.
	static void getMetrics(HistoryReadSessionMetrics& output);

	// Copia tiempos globales de lectura, preparacion y espera de cola; output recibe la instantanea.
	static void getTimings(HistoryReadTimings& output);

private:
	static constexpr uint8_t bufferCount = 3;
	static constexpr size_t pageSize = HistoryFormat::pageSize;
	NandPartition* source = nullptr;
	uint8_t* buffers[bufferCount] = {};
	uint8_t* snapshot = nullptr;
	uint8_t currentBuffer = bufferCount;
	size_t currentLength = 0;
	size_t currentOffset = 0;
	size_t totalBytes = 0;
	volatile size_t producedBytes = 0;
	std::atomic<bool> active{false}, producerFailed{false};
	uint32_t sessionId = 0;
	volatile uint32_t lastConsumerMs = 0;
	bool waitingForBuffer = false;
	uint32_t bufferWaitStartedUs = 0;
	uint16_t nextBlock = 0;
	uint8_t nextPage = 0;
	uint32_t nextSequence = 0;
	uint32_t remainingPhysicalPages = 0;
	uint32_t totalPhysicalPages = 0;
	uint32_t fromTimestamp = 0;
	uint32_t toTimestamp = UINT32_MAX;
	bool snapshotPending = false;
	HistoryReadSessionMetrics sessionMetrics;
	QueueHandle_t freeQueue = nullptr;
	QueueHandle_t readyQueue = nullptr;
	QueueHandle_t completionQueue = nullptr;
	StaticQueue_t freeQueueStorage, readyQueueStorage, completionQueueStorage;
	uint8_t freeQueueData[bufferCount] = {};
	uint8_t readyQueueData[bufferCount] = {};
	uint8_t completionQueueData[sizeof(NandIoResult)] = {};
	TaskHandle_t taskHandle = nullptr;

	// Ejecuta el productor FreeRTOS; parameter identifica la sesion a atender.
	static void readTaskEntry(void* parameter);

	// Lee paginas NAND y las deja disponibles para el callback HTTP; sin parametros.
	void readTaskLoop();

	// Libera los buffers que existen solo mientras la sesion esta activa; sin parametros.
	void releaseBuffers();

	// Avanza a la pagina utilizable siguiente; sin parametros.
	bool advancePage();

	// Comprueba cabecera y CRC de una pagina NAND, sin interpretar aun el intervalo solicitado.
	bool isValidPage(const uint8_t* data) const;

	// Indica si la pagina valida corresponde al intervalo Unix congelado para esta descarga.
	bool pageIntersectsRange(const uint8_t* data) const;
};
