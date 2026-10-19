#pragma once

#include <Arduino.h>
#include "nand_partition.h"

// Produce una descarga secuencial NAND sin bloquear el callback HTTP.
class NandReadSession {
public:
	// Prepara una lectura desde el inicio de partition; totalBytes define la longitud publicada.
	bool begin(NandPartition& partition, size_t totalBytes);

	// Prepara una lectura contigua desde block/page; pageCount define las paginas publicadas.
	bool begin(NandPartition& partition, uint16_t block, uint8_t page, uint32_t pageCount);

	// Copia el siguiente fragmento HTTP o devuelve RESPONSE_TRY_AGAIN mientras NAND precarga datos.
	size_t copyChunk(uint8_t* dest, size_t maxLen, size_t index);

	// Indica si una descarga sigue usando los buffers compartidos; sin parametros.
	bool isActive() const;

private:
	static constexpr uint8_t bufferCount = 2;
	static constexpr size_t pageSize = WinbondSpiNand::pageDataSize;
	NandPartition* source = nullptr;
	uint8_t buffers[bufferCount][pageSize] = {};
	uint8_t currentBuffer = bufferCount;
	size_t currentLength = 0;
	size_t currentOffset = 0;
	size_t total = 0;
	volatile size_t produced = 0;
	volatile bool active = false;
	volatile bool producerFailed = false;
	volatile uint32_t lastConsumerMs = 0;
	uint16_t firstBlock = 0;
	uint8_t firstPage = 0;
	QueueHandle_t freeQueue = nullptr;
	QueueHandle_t readyQueue = nullptr;
	QueueHandle_t completionQueue = nullptr;
	StaticQueue_t freeQueueStorage, readyQueueStorage, completionQueueStorage;
	uint8_t freeQueueData[bufferCount], readyQueueData[bufferCount];
	uint8_t completionQueueData[sizeof(NandIoResult)];
	TaskHandle_t taskHandle = nullptr;

	// Ejecuta la precarga secuencial en una tarea FreeRTOS; parameter identifica la sesion.
	static void readTaskEntry(void* parameter);

	// Lee paginas NAND y las deja disponibles para el callback HTTP; sin parametros.
	void readTaskLoop();
};
