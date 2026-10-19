#include "nand_io_service.h"
#include "nand_partition.h"

// Asocia el servicio de I/O con el driver NAND que ejecutara las operaciones fisicas.
NandIoService::NandIoService(WinbondSpiNand& nandDriver) : nand(nandDriver) {}

// Crea las colas estaticas y arranca la tarea de I/O sin modificar registros NAND.
bool NandIoService::begin(UBaseType_t taskPriority, uint16_t stackWords, BaseType_t core)
{
	if (running || !nand.geometry().blockCount) return false;
	highQueue = xQueueCreateStatic(highQueueLength, sizeof(NandIoRequest), highQueueData, &highQueueStorage);
	mediumQueue = xQueueCreateStatic(mediumQueueLength, sizeof(NandIoRequest), mediumQueueData, &mediumQueueStorage);
	lowQueue = xQueueCreateStatic(lowQueueLength, sizeof(NandIoRequest), lowQueueData, &lowQueueStorage);
	if (!highQueue || !mediumQueue || !lowQueue) return false;

	running = true;
	if (xTaskCreatePinnedToCore(storageTaskEntry, "nandIo", stackWords, this, taskPriority, &taskHandle, core) != pdPASS) {
		running = false;
		taskHandle = nullptr;
		return false;
	}
	return true;
}

// Encola una solicitud y despierta la tarea de almacenamiento sin bloquear al solicitante.
bool NandIoService::submit(const NandIoRequest& request)
{
	if (!running || !taskHandle) {
		recordSubmission(request.priority, false);
		return false;
	}
	if (!isValidRequest(request)) {
		recordSubmission(request.priority, false);
		return false;
	}
	QueueHandle_t queue = queueForPriority(request.priority);
	if (!queue || xQueueSend(queue, &request, 0) != pdPASS) {
		recordSubmission(request.priority, false);
		return false;
	}
	recordSubmission(request.priority, true);
	xTaskNotifyGive(taskHandle);
	return true;
}

// Indica si la tarea NAND ya puede aceptar operaciones encoladas.
bool NandIoService::isRunning() const
{
	return running;
}

// Copia las metricas acumuladas bajo una seccion critica breve.
void NandIoService::getMetrics(NandIoMetrics& output) const
{
	portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&metricsMutex));
	output = metrics;
	portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&metricsMutex));
}

// Entra al ciclo del objeto recibido en parameter y elimina la tarea al finalizar.
void NandIoService::storageTaskEntry(void* parameter)
{
	static_cast<NandIoService*>(parameter)->storageTaskLoop();
	vTaskDelete(nullptr);
}

// Espera solicitudes y las procesa en orden estricto de prioridad.
void NandIoService::storageTaskLoop()
{
	while (running) {
		NandIoRequest request;
		if (dequeueRequest(request)) processRequest(request);
		else ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
	}
}

// Busca una solicitud pendiente empezando por la cola de mayor prioridad.
bool NandIoService::dequeueRequest(NandIoRequest& request)
{
	return xQueueReceive(highQueue, &request, 0) == pdPASS ||
		xQueueReceive(mediumQueue, &request, 0) == pdPASS ||
		xQueueReceive(lowQueue, &request, 0) == pdPASS;
}

// Ejecuta una operacion fisica y entrega el resultado en la cola indicada.
void NandIoService::processRequest(const NandIoRequest& request)
{
	NandIoResult result;
	result.requestId = request.requestId;
	result.context = request.context;
	const uint32_t startedUs = micros();
	const uint32_t absolutePage = uint32_t(request.physicalBlock) * nand.geometry().pagesPerBlock + request.page;
	if (request.operation == NandOperation::programPage || request.operation == NandOperation::eraseBlock)
		ensureWriteSession();

	switch (request.operation) {
	case NandOperation::readPage:
		result.success = nand.readPage(absolutePage, request.column, request.data, request.length, result.nandStatus);
		break;
	case NandOperation::programPage:
		result.success = nand.programPage(absolutePage, request.data, request.length, result.nandStatus);
		break;
	case NandOperation::eraseBlock:
		result.success = nand.eraseBlock(request.physicalBlock, result.nandStatus);
		break;
	case NandOperation::readBadBlockMarker:
		result.success = nand.readBadBlockMarker(request.physicalBlock, request.data[0], result.nandStatus);
		break;
	}

	result.elapsedUs = micros() - startedUs;
	result.transferredBytes = result.success && request.operation != NandOperation::eraseBlock ? request.length : 0;
	if (!result.success) result.error = (result.nandStatus & 0x01) ? NandIoError::timeout : NandIoError::nandFailure;
	if (request.completionQueue && xQueueSend(request.completionQueue, &result, 0) != pdPASS) {
		result.error = NandIoError::completionQueueFull;
	}
	recordCompletion(request, result);
	if (request.partition) request.partition->completeRequest();
}

// Verifica limites de bloque, pagina, columna, longitud y buffers de una solicitud.
bool NandIoService::isValidRequest(const NandIoRequest& request) const
{
	const WinbondSpiNandGeometry& geometry = nand.geometry();
	if (request.physicalBlock >= geometry.blockCount || request.page >= geometry.pagesPerBlock) return false;
	if (request.operation == NandOperation::eraseBlock) return true;
	if (!request.data || !request.length) return false;
	if (request.operation == NandOperation::readBadBlockMarker) return request.length >= 1;
	if (request.operation == NandOperation::programPage) return request.column == 0 && request.length <= geometry.pageDataSize;
	return uint32_t(request.column) + request.length <= geometry.pageDataSize;
}

// Devuelve la cola correspondiente a la prioridad elegida.
QueueHandle_t NandIoService::queueForPriority(StoragePriority priority) const
{
	switch (priority) {
	case StoragePriority::high: return highQueue;
	case StoragePriority::medium: return mediumQueue;
	default: return lowQueue;
	}
}

// Registra aceptacion o rechazo y la profundidad maxima de la cola usada.
void NandIoService::recordSubmission(StoragePriority priority, bool accepted)
{
	const uint8_t index = uint8_t(priority);
	portENTER_CRITICAL(&metricsMutex);
	if (accepted) {
		++metrics.accepted[index];
		const UBaseType_t depth = uxQueueMessagesWaiting(queueForPriority(priority));
		if (depth > metrics.maxQueueDepth[index]) metrics.maxQueueDepth[index] = depth;
	}
	else ++metrics.rejected[index];
	portEXIT_CRITICAL(&metricsMutex);
}

// Registra tiempos, bytes y errores de una operacion finalizada.
void NandIoService::recordCompletion(const NandIoRequest& request, const NandIoResult& result)
{
	const uint8_t index = uint8_t(request.operation);
	portENTER_CRITICAL(&metricsMutex);
	metrics.lastOperation = request.operation;
	metrics.lastError = result.error;
	if (result.success) {
		++metrics.completed[index];
		const uint32_t nowUs = micros();
		if (!metrics.recentWindowStartedUs || uint32_t(nowUs - metrics.recentWindowStartedUs) >= 1000000U) {
			metrics.recentWindowStartedUs = nowUs;
			metrics.recentReadBytes = 0;
			metrics.recentWrittenBytes = 0;
		}
		if (request.operation == NandOperation::readPage) {
			metrics.bytesRead += result.transferredBytes;
			metrics.recentReadBytes += result.transferredBytes;
		}
		if (request.operation == NandOperation::programPage) {
			metrics.bytesWritten += result.transferredBytes;
			metrics.recentWrittenBytes += result.transferredBytes;
		}
	}
	else {
		++metrics.failed[index];
		if (result.error == NandIoError::timeout) ++metrics.timeouts[index];
	}
	metrics.totalElapsedUs[index] += result.elapsedUs;
	if (result.elapsedUs > metrics.maxElapsedUs[index]) metrics.maxElapsedUs[index] = result.elapsedUs;
	metrics.lastElapsedUs[index] = result.elapsedUs;
	metrics.lastTransferredBytes[index] = result.transferredBytes;
	portEXIT_CRITICAL(&metricsMutex);
}

// Habilita programacion y lectura por cache solo cuando aparece la primera escritura real.
void NandIoService::ensureWriteSession()
{
	if (writeSessionActive) return;
	nand.beginWriteSession(originalProtection, originalConfiguration);
	writeSessionActive = true;
}
