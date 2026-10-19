#include "nand_partition.h"
#include <freertos/task.h>

// Configura la region fisica y el bitmap global que usara la particion.
void NandPartition::configure(NandIoService& service, const uint8_t* usableBitmap, uint16_t bitmapBlockCount,
	uint16_t startBlock, uint16_t blockCount)
{
	ioService = &service;
	usableBlockBitmap = usableBitmap;
	bitmapBlocks = bitmapBlockCount;
	firstPhysicalBlock = startBlock;
	physicalBlockCount = blockCount;
	enabled = false;
	formatting = false;
	formatFailed = false;
	formatOwner = nullptr;
	pendingRequests = 0;
	formatProgress = NandPartitionFormatProgress{};
}

// Habilita o deshabilita la particion configurada.
void NandPartition::setEnabled(bool value)
{
	enabled = value && ioService && usableBlockBitmap && physicalBlockCount;
}

// Bloquea nuevos accesos y publica el tamano util que debera borrar la tarea de formato.
bool NandPartition::requestFormat()
{
	if (!enabled || !ioService || !ioService->isRunning()) return false;
	NandPartitionInfo info;
	getInfo(info);
	portENTER_CRITICAL(&accessMutex);
	if (formatting) {
		portEXIT_CRITICAL(&accessMutex);
		return false;
	}
	formatting = true;
	formatFailed = false;
	formatOwner = nullptr;
	portEXIT_CRITICAL(&accessMutex);
	portENTER_CRITICAL(&formatMutex);
	formatProgress = NandPartitionFormatProgress{};
	formatProgress.active = true;
	formatProgress.totalBytes = info.usableBytes;
	formatProgress.blocksTotal = info.usableBlockCount;
	portEXIT_CRITICAL(&formatMutex);
	if (info.usableBlockCount) return true;
	finishFormat(false);
	return false;
}

// Espera las operaciones aceptadas y borra cada bloque utilizable de esta particion.
bool NandPartition::eraseAllBlocks()
{
	portENTER_CRITICAL(&accessMutex);
	if (!formatting || formatOwner) {
		portEXIT_CRITICAL(&accessMutex);
		return false;
	}
	formatOwner = xTaskGetCurrentTaskHandle();
	portEXIT_CRITICAL(&accessMutex);
	const TickType_t waitStarted = xTaskGetTickCount();
	while (true) {
		portENTER_CRITICAL(&accessMutex);
		const uint32_t pending = pendingRequests;
		portEXIT_CRITICAL(&accessMutex);
		if (!pending) break;
		if (xTaskGetTickCount() - waitStarted > pdMS_TO_TICKS(10000)) return false;
		vTaskDelay(1);
	}
	if (!formatQueue)
		formatQueue = xQueueCreateStatic(1, sizeof(NandIoResult), formatQueueData, &formatQueueControl);
	if (!formatQueue) return false;
	bool success = true;
	const uint32_t blockBytes = WinbondSpiNand::blockDataSize;
	for (uint16_t offset = 0; offset < physicalBlockCount; ++offset) {
		const uint16_t block = uint16_t(physicalBlockCount - 1 - offset);
		if (!isUsableBlock(block)) continue;
		NandIoResult stale;
		while (xQueueReceive(formatQueue, &stale, 0) == pdPASS) {}
		if (++formatRequestId == 0) ++formatRequestId;
		bool erased = false;
		for (uint16_t retry = 0; retry < 1000 && !erased; ++retry) {
			if (submitEraseBlock(block, StoragePriority::low, formatQueue, formatRequestId)) {
				erased = waitFormatResult(formatRequestId);
				break;
			}
			vTaskDelay(1);
		}
		portENTER_CRITICAL(&formatMutex);
		++formatProgress.blocksDone;
		formatProgress.bytesDone += blockBytes;
		if (erased) formatProgress.bytesErased += blockBytes;
		else {
			++formatProgress.failedBlocks;
			success = false;
		}
		formatProgress.percent = formatProgress.totalBytes
			? uint8_t(min(uint64_t(100), uint64_t(formatProgress.bytesDone) * 100 / formatProgress.totalBytes)) : 0;
		portEXIT_CRITICAL(&formatMutex);
	}
	return success;
}

// Cierra el formateo y mantiene la particion bloqueada si no pudo reconstruirse.
void NandPartition::finishFormat(bool success)
{
	portENTER_CRITICAL(&formatMutex);
	formatProgress.active = false;
	formatProgress.done = true;
	formatProgress.success = success;
	if (success) formatProgress.percent = 100;
	portEXIT_CRITICAL(&formatMutex);
	portENTER_CRITICAL(&accessMutex);
	formatFailed = !success;
	formatting = false;
	formatOwner = nullptr;
	portEXIT_CRITICAL(&accessMutex);
}

// Libera una reserva que fallo antes de comenzar a borrar la particion.
void NandPartition::cancelFormat()
{
	portENTER_CRITICAL(&formatMutex);
	formatProgress = NandPartitionFormatProgress{};
	portEXIT_CRITICAL(&formatMutex);
	portENTER_CRITICAL(&accessMutex);
	formatFailed = false;
	formatting = false;
	formatOwner = nullptr;
	portEXIT_CRITICAL(&accessMutex);
}

// Copia el avance y resultado del ultimo formateo.
void NandPartition::getFormatProgress(NandPartitionFormatProgress& progress) const
{
	portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&formatMutex));
	progress = formatProgress;
	portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&formatMutex));
}

// Indica si se esta formateando o si un fallo dejo la particion fuera de servicio.
bool NandPartition::isFormatting() const
{
	portENTER_CRITICAL(&accessMutex);
	const bool blocked = formatting || formatFailed;
	portEXIT_CRITICAL(&accessMutex);
	return blocked;
}

// Indica si la particion puede aceptar operaciones en este momento.
bool NandPartition::isEnabled() const
{
	return enabled && ioService && ioService->isRunning();
}

// Construye una solicitud de lectura usando indices relativos a esta particion.
bool NandPartition::submitReadPage(uint16_t relativeBlock, uint8_t page, uint16_t column, uint8_t* data,
	size_t length, StoragePriority priority, QueueHandle_t completionQueue, uint32_t requestId, void* context) const
{
	uint16_t physicalBlock = 0;
	if (!translateBlock(relativeBlock, physicalBlock)) return false;
	NandIoRequest request;
	request.operation = NandOperation::readPage;
	request.priority = priority;
	request.physicalBlock = physicalBlock;
	request.page = page;
	request.column = column;
	request.data = data;
	request.length = length;
	request.requestId = requestId;
	request.completionQueue = completionQueue;
	request.context = context;
	return submitRequest(request);
}

// Construye una solicitud de programacion usando indices relativos a esta particion.
bool NandPartition::submitProgramPage(uint16_t relativeBlock, uint8_t page, uint8_t* data, size_t length,
	StoragePriority priority, QueueHandle_t completionQueue, uint32_t requestId, void* context) const
{
	uint16_t physicalBlock = 0;
	if (!translateBlock(relativeBlock, physicalBlock)) return false;
	NandIoRequest request;
	request.operation = NandOperation::programPage;
	request.priority = priority;
	request.physicalBlock = physicalBlock;
	request.page = page;
	request.data = data;
	request.length = length;
	request.requestId = requestId;
	request.completionQueue = completionQueue;
	request.context = context;
	return submitRequest(request);
}

// Construye una solicitud de borrado para un bloque relativo utilizable.
bool NandPartition::submitEraseBlock(uint16_t relativeBlock, StoragePriority priority,
	QueueHandle_t completionQueue, uint32_t requestId, void* context) const
{
	uint16_t physicalBlock = 0;
	if (!translateBlock(relativeBlock, physicalBlock)) return false;
	NandIoRequest request;
	request.operation = NandOperation::eraseBlock;
	request.priority = priority;
	request.physicalBlock = physicalBlock;
	request.requestId = requestId;
	request.completionQueue = completionQueue;
	request.context = context;
	return submitRequest(request);
}

// Consulta si un bloque relativo existe y esta disponible para uso.
bool NandPartition::isUsableBlock(uint16_t relativeBlock) const
{
	return relativeBlock < physicalBlockCount && isPhysicalBlockUsable(firstPhysicalBlock + relativeBlock);
}

// Busca el siguiente bloque utilizable recorriendo esta particion de forma circular.
uint16_t NandPartition::nextUsableBlock(uint16_t relativeBlock) const
{
	if (!physicalBlockCount) return invalidBlock;
	const uint16_t start = relativeBlock < physicalBlockCount ? uint16_t((relativeBlock + 1) % physicalBlockCount) : 0;
	for (uint16_t offset = 0; offset < physicalBlockCount; ++offset) {
		const uint16_t candidate = uint16_t((start + offset) % physicalBlockCount);
		if (isUsableBlock(candidate)) return candidate;
	}
	return invalidBlock;
}

// Busca el bloque utilizable anterior recorriendo esta particion de forma circular.
uint16_t NandPartition::previousUsableBlock(uint16_t relativeBlock) const
{
	if (!physicalBlockCount) return invalidBlock;
	const uint16_t start = relativeBlock < physicalBlockCount ?
		uint16_t((relativeBlock + physicalBlockCount - 1) % physicalBlockCount) : uint16_t(physicalBlockCount - 1);
	for (uint16_t offset = 0; offset < physicalBlockCount; ++offset) {
		const uint16_t candidate = uint16_t((start + physicalBlockCount - offset) % physicalBlockCount);
		if (isUsableBlock(candidate)) return candidate;
	}
	return invalidBlock;
}

// Calcula el estado y la capacidad real descontando bloques defectuosos.
void NandPartition::getInfo(NandPartitionInfo& info) const
{
	info = NandPartitionInfo{};
	info.startBlock = firstPhysicalBlock;
	info.blockCount = physicalBlockCount;
	info.enabled = isEnabled() && !isFormatting();
	for (uint16_t block = 0; block < physicalBlockCount; ++block) {
		if (isUsableBlock(block)) ++info.usableBlockCount;
	}
	const uint32_t blockBytes = WinbondSpiNand::blockDataSize;
	info.configuredBytes = uint32_t(physicalBlockCount) * blockBytes;
	info.usableBytes = uint32_t(info.usableBlockCount) * blockBytes;
}

// Convierte un bloque relativo a fisico y rechaza bloques fuera de rango o defectuosos.
bool NandPartition::translateBlock(uint16_t relativeBlock, uint16_t& physicalBlock) const
{
	if (!enabled || !ioService || relativeBlock >= physicalBlockCount) return false;
	physicalBlock = firstPhysicalBlock + relativeBlock;
	return isPhysicalBlockUsable(physicalBlock);
}

// Consulta un bit del bitmap global donde uno significa bloque utilizable.
bool NandPartition::isPhysicalBlockUsable(uint16_t physicalBlock) const
{
	if (!usableBlockBitmap || physicalBlock >= bitmapBlocks) return false;
	return (usableBlockBitmap[physicalBlock >> 3] & uint8_t(1U << (physicalBlock & 7))) != 0;
}

// Envia una solicitud ya traducida al servicio de I/O compartido.
bool NandPartition::submitRequest(NandIoRequest& request) const
{
	portENTER_CRITICAL(&accessMutex);
	const bool allowed = enabled && ioService && ioService->isRunning() && !formatFailed &&
		(!formatting || formatOwner == xTaskGetCurrentTaskHandle());
	if (allowed) ++pendingRequests;
	portEXIT_CRITICAL(&accessMutex);
	if (!allowed) return false;
	request.partition = const_cast<NandPartition*>(this);
	if (ioService->submit(request)) return true;
	const_cast<NandPartition*>(this)->completeRequest();
	return false;
}

// Espera la respuesta del bloque borrado y valida solicitud y resultado.
bool NandPartition::waitFormatResult(uint32_t requestId)
{
	const TickType_t started = xTaskGetTickCount();
	TickType_t remaining = pdMS_TO_TICKS(15000);
	NandIoResult result;
	while (xQueueReceive(formatQueue, &result, remaining) == pdPASS) {
		if (result.requestId == requestId) return result.success;
		const TickType_t elapsed = xTaskGetTickCount() - started;
		if (elapsed >= pdMS_TO_TICKS(15000)) break;
		remaining = pdMS_TO_TICKS(15000) - elapsed;
	}
	return false;
}

// Reduce el conteo de operaciones aceptadas cuando NAND publica su resultado.
void NandPartition::completeRequest()
{
	portENTER_CRITICAL(&accessMutex);
	if (pendingRequests) --pendingRequests;
	portEXIT_CRITICAL(&accessMutex);
}
