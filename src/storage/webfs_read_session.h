#pragma once

#include <Arduino.h>
#include <atomic>
#include "webfs_store.h"

// Transmite un archivo WebFS sin construirlo completo en RAM.
class WebFsReadSession {
public:
	// Prepara una lectura del archivo indicado; store conserva la imagen y file describe el recurso.
	bool begin(WebFsStore& store, const WebFsFile& file);

	// Copia el siguiente fragmento HTTP o devuelve RESPONSE_TRY_AGAIN mientras NAND lee datos.
	size_t copyChunk(uint8_t* data, size_t maxLength, size_t offset, uint32_t id);

	// Identifica la descarga actual para distinguir callbacks de conexiones anteriores; sin parametros.
	uint32_t getId() const;

	// Cancela solamente la descarga id; el productor libera RAM al terminar cualquier I/O pendiente.
	void cancel(uint32_t id);

	// Indica si la sesion mantiene buffers o una tarea activa; sin parametros.
	bool isActive() const;

private:
	static constexpr uint8_t bufferCount = 2;
	WebFsStore* source = nullptr;
	WebFsBank bank = WebFsBank::none;
	uint8_t* buffers[bufferCount] = {};
	uint8_t currentBuffer = bufferCount;
	size_t currentOffset = 0;
	size_t currentLength = 0;
	uint32_t firstPage = 0;
	uint16_t firstColumn = 0;
	size_t totalBytes = 0;
	size_t producedBytes = 0;
	std::atomic<bool> active{false}, producerFailed{false};
	uint32_t sessionId = 0;
	QueueHandle_t freeQueue = nullptr;
	QueueHandle_t readyQueue = nullptr;
	QueueHandle_t completionQueue = nullptr;
	StaticQueue_t freeQueueStorage, readyQueueStorage, completionQueueStorage;
	uint8_t freeQueueData[bufferCount] = {};
	uint8_t readyQueueData[bufferCount] = {};
	uint8_t completionQueueData[sizeof(NandIoResult)] = {};
	TaskHandle_t taskHandle = nullptr;

	// Entra a la tarea que precarga paginas NAND; parameter identifica esta sesion.
	static void readTaskEntry(void* parameter);

	// Lee paginas de la imagen y las entrega al callback HTTP; sin parametros.
	void readTaskLoop();

	// Libera los buffers dinamicos retenidos por una descarga terminada; sin parametros.
	void releaseBuffers();
};
