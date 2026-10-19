#pragma once

#include "diagnostic_store.h"

class DiagnosticLogger;

// Conserva un snapshot y transmite sus bloques NAND por paginas.
class DiagnosticReadSession {
public:
	static constexpr uint16_t responseHeaderSize = 20;

	// Captura el bloque actual; logger protege el escritor y partition suministra las lecturas.
	bool beginLatest(DiagnosticLogger& logger, DiagnosticStore& store, NandPartition& partition);

	// Abre el bloque anterior a beforeBlock; token identifica el snapshot y done indica fin del anillo.
	bool beginPrevious(DiagnosticStore& store, uint32_t token, uint16_t beforeBlock, bool& done);

	// Copia bytes listos al callback HTTP; destination, capacity y offset describen el tramo solicitado.
	size_t copyChunk(uint8_t* destination, size_t capacity, size_t offset);

	// Indica si no hay una respuesta HTTP o tarea de lectura en curso.
	bool isAvailable() const;

	// Indica si token corresponde al snapshot conservado.
	bool matches(uint32_t token) const;

	// Indica si una sesion inactiva vencio y puede reemplazarse.
	bool isExpired() const;

	// Descarta el snapshot cuando no hay una transferencia o lectura activa.
	void invalidate();

	// Devuelve los bytes publicados por la respuesta actual.
	size_t contentLength() const;

private:
	static constexpr uint8_t bufferCount = 2;
	static constexpr size_t pageSize = WinbondSpiNand::pageDataSize;
	DiagnosticBinarySnapshot* snapshot = nullptr;
	NandPartition* source = nullptr;
	uint8_t* buffers[bufferCount] = {};
	uint8_t responseHeader[responseHeaderSize] = {};
	uint32_t sessionToken = 0;
	uint32_t lastUsedMs = 0;
	uint16_t responseBlock = 0;
	uint16_t blockIndex = 0;
	uint8_t ramPageCount = 0;
	uint8_t nandPageCount = 0;
	uint8_t currentBuffer = bufferCount;
	size_t currentOffset = 0;
	size_t totalBytes = 0;
	volatile uint8_t producedPages = 0;
	volatile bool active = false;
	volatile bool taskRunning = false;
	volatile uint32_t lastConsumerMs = 0;
	QueueHandle_t freeQueue = nullptr;
	QueueHandle_t readyQueue = nullptr;
	QueueHandle_t completionQueue = nullptr;
	StaticQueue_t freeControl, readyControl, completionControl;
	uint8_t freeData[bufferCount] = {};
	uint8_t readyData[bufferCount] = {};
	uint8_t completionData[sizeof(NandIoResult)] = {};

	// Abre una respuesta con ramCount paginas copiadas y nandCount paginas del bloque fisico.
	bool beginBlock(NandPartition& partition, uint16_t block, uint8_t ramCount, uint8_t nandCount);

	// Libera buffers de una sesion vencida antes de reutilizarla.
	void releaseBuffers();

	// Entra a la tarea de lectura; parameter apunta a esta sesion.
	static void taskEntry(void* parameter);

	// Precarga primero RAM y despues NAND desde la pagina mas nueva hacia la cabecera del bloque.
	void taskLoop();
};
