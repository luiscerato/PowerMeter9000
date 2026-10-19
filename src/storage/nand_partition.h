#pragma once

#include <Arduino.h>
#include "nand_io_service.h"

struct NandPartitionInfo {
	uint16_t startBlock = 0;
	uint16_t blockCount = 0;
	uint16_t usableBlockCount = 0;
	uint32_t configuredBytes = 0;
	uint32_t usableBytes = 0;
	bool enabled = false;
};

struct NandPartitionFormatProgress {
	bool active = false;
	bool done = false;
	bool success = false;
	uint32_t bytesDone = 0;
	uint32_t bytesErased = 0;
	uint32_t totalBytes = 0;
	uint16_t blocksDone = 0;
	uint16_t blocksTotal = 0;
	uint16_t failedBlocks = 0;
	uint8_t percent = 0;
};

// Traduce bloques relativos de una particion a direcciones fisicas NAND.
class NandPartition {
public:
	static constexpr uint16_t invalidBlock = UINT16_MAX;

	// Construye una particion deshabilitada; sin parametros.
	NandPartition() = default;

	// Configura una vista de particion; service ejecuta I/O y usableBitmap indica bloques globales utilizables.
	void configure(NandIoService& service, const uint8_t* usableBitmap, uint16_t bitmapBlockCount,
		uint16_t startBlock, uint16_t blockCount);

	// Habilita o deshabilita accesos de la particion; enabled define el nuevo estado.
	void setEnabled(bool enabled);

	// Reserva la particion para un formateo iniciado en background; sin parametros.
	bool requestFormat();

	// Borra todos los bloques utilizables; debe ejecutarse en la tarea que formateara la particion.
	bool eraseAllBlocks();

	// Publica el resultado y reabre la particion solo si el formato termino correctamente.
	void finishFormat(bool success);

	// Cancela una reserva antes de borrar y conserva los datos existentes.
	void cancelFormat();

	// Copia avance y resultado del ultimo formateo; progress recibe la instantanea.
	void getFormatProgress(NandPartitionFormatProgress& progress) const;

	// Indica si la particion esta bloqueada por un formateo o fallo; sin parametros.
	bool isFormatting() const;

	// Indica si la particion esta habilitada y tiene configuracion valida; sin parametros.
	bool isEnabled() const;

	// Encola una lectura relativa; los parametros describen pagina, rango, buffer, prioridad y finalizacion.
	bool submitReadPage(uint16_t relativeBlock, uint8_t page, uint16_t column, uint8_t* data, size_t length,
		StoragePriority priority, QueueHandle_t completionQueue = nullptr, uint32_t requestId = 0, void* context = nullptr) const;

	// Encola una programacion relativa; los parametros describen pagina, buffer, prioridad y finalizacion.
	bool submitProgramPage(uint16_t relativeBlock, uint8_t page, uint8_t* data, size_t length,
		StoragePriority priority, QueueHandle_t completionQueue = nullptr, uint32_t requestId = 0, void* context = nullptr) const;

	// Encola el borrado de un bloque relativo; priority y completionQueue controlan ejecucion y resultado.
	bool submitEraseBlock(uint16_t relativeBlock, StoragePriority priority,
		QueueHandle_t completionQueue = nullptr, uint32_t requestId = 0, void* context = nullptr) const;

	// Indica si relativeBlock existe y esta marcado como utilizable; relativeBlock usa indices de particion.
	bool isUsableBlock(uint16_t relativeBlock) const;

	// Devuelve el siguiente bloque utilizable con vuelta circular o invalidBlock si no existe.
	uint16_t nextUsableBlock(uint16_t relativeBlock) const;

	// Devuelve el bloque utilizable anterior con vuelta circular o invalidBlock si no existe.
	uint16_t previousUsableBlock(uint16_t relativeBlock) const;

	// Construye el resumen de capacidad y estado; info recibe los valores calculados.
	void getInfo(NandPartitionInfo& info) const;

private:
	NandIoService* ioService = nullptr;
	const uint8_t* usableBlockBitmap = nullptr;
	uint16_t bitmapBlocks = 0;
	uint16_t firstPhysicalBlock = 0;
	uint16_t physicalBlockCount = 0;
	bool enabled = false;
	bool formatting = false;
	bool formatFailed = false;
	TaskHandle_t formatOwner = nullptr;
	mutable uint32_t pendingRequests = 0;
	uint32_t formatRequestId = 0;
	mutable portMUX_TYPE accessMutex = portMUX_INITIALIZER_UNLOCKED;
	portMUX_TYPE formatMutex = portMUX_INITIALIZER_UNLOCKED;
	NandPartitionFormatProgress formatProgress;
	QueueHandle_t formatQueue = nullptr;
	StaticQueue_t formatQueueControl;
	uint8_t formatQueueData[sizeof(NandIoResult)] = {};
	friend class NandIoService;

	// Convierte un bloque relativo a bloque fisico y valida limites; relativeBlock identifica la particion.
	bool translateBlock(uint16_t relativeBlock, uint16_t& physicalBlock) const;

	// Consulta el bitmap global para un bloque fisico; physicalBlock identifica la NAND completa.
	bool isPhysicalBlockUsable(uint16_t physicalBlock) const;

	// Construye y envia una solicitud generica; request recibe los campos ya validados.
	bool submitRequest(NandIoRequest& request) const;

	// Espera el resultado del borrado solicitado; requestId identifica la operacion.
	bool waitFormatResult(uint32_t requestId);

	// Informa al propietario que una operacion NAND aceptada termino.
	void completeRequest();
};
