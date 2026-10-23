#pragma once

#include "diagnostic_store.h"
#include <atomic>

class DiagnosticLogger;

// Transmite bloques NAND con snapshot temporal y conserva solo el cursor entre respuestas.
class DiagnosticReadSession {
public:
	static constexpr uint16_t responseHeaderSize = 20;

	// Captura el bloque actual; logger protege el escritor y partition suministra las lecturas.
	bool beginLatest(DiagnosticLogger& logger, DiagnosticStore& store, NandPartition& partition);

	// Abre el bloque anterior a beforeBlock; token identifica el snapshot y done indica fin del anillo.
	bool beginPrevious(DiagnosticStore& store, uint32_t token, uint16_t beforeBlock, bool& done);

	// Copia bytes listos; destination/capacity/offset describen el tramo e id identifica la respuesta vigente.
	size_t copyChunk(uint8_t* destination, size_t capacity, size_t offset, uint32_t id);

	// Identifica la transferencia actual, sin cambiar el token del cursor; sin parametros.
	uint32_t getId() const;

	// Cancela solamente la transferencia id, sin liberar memoria que NAND todavia usa.
	void cancel(uint32_t id);

	// Indica si no hay una respuesta HTTP o tarea de lectura en curso.
	bool isAvailable() const;

	// Indica si token corresponde al snapshot conservado.
	bool matches(uint32_t token) const;

	// Indica si el cursor inactivo vencio y puede descartarse; sin parametros.
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
	uint32_t sessionId = 0, recentSequence = 0;
	std::atomic<uint32_t> lastUsedMs{0};
	uint16_t anchorBlock = 0;
	uint16_t responseBlock = 0;
	uint16_t blockIndex = 0;
	uint8_t ramPageCount = 0;
	uint8_t nandPageCount = 0;
	uint8_t currentBuffer = bufferCount;
	size_t currentOffset = 0;
	size_t totalBytes = 0;
	volatile uint8_t producedPages = 0;
	std::atomic<bool> active{false}, taskRunning{false}, producerFailed{false};
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

	// Libera paginas y snapshot de la respuesta terminada; conserva el cursor para continuar leyendo.
	void releaseTransferBuffers();

	// Entra a la tarea de lectura; parameter apunta a esta sesion.
	static void taskEntry(void* parameter);

	// Precarga primero RAM y despues NAND desde la pagina mas nueva hacia la cabecera del bloque.
	void taskLoop();
};
