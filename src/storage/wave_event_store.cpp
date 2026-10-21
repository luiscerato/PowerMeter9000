#include "wave_event_store.h"
#include <Preferences.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include "../diagnostic_logger.h"
#include <algorithm>
#include <stdlib.h>

using namespace waveEventFormat;
static waveEventStore* mountedWaveStore = nullptr;

// Copia length bytes alineados de source a dest usando solo palabras: IRAM no admite accesos de bytes ni float.
static void copyWaveWords(void* dest, const void* source, size_t length) {
    auto* output = static_cast<volatile uint32_t*>(dest);
    const auto* input = static_cast<const volatile uint32_t*>(source);
    for (size_t index = 0; index < length / 4; ++index) output[index] = input[index];
}

// Reserva size bytes para Wave en el heap de palabras, preservando DRAM para red y FAST.
static void* allocateWaveWords(size_t size) {
    constexpr uint32_t caps = MALLOC_CAP_EXEC | MALLOC_CAP_32BIT;
    if (heap_caps_get_largest_free_block(caps) < size) return nullptr;
    return heap_caps_malloc(size, caps);
}

// Monta la particion y reserva paquetes individuales, reduciendo prehistoria si falta heap.
bool waveEventStore::begin() {
    NandPartitionInfo info;
    partition.getInfo(info);
    if (!info.enabled || !info.blockCount || info.blockCount > 320 || task) return false;
    blockCount = info.blockCount;
    commands = xQueueCreateStatic(16, sizeof(command), commandData, &commandControl);
    ioQueue = xQueueCreateStatic(1, sizeof(NandIoResult), ioData, &ioControl);
    readQueue = xQueueCreateStatic(1, sizeof(NandIoResult), readData, &readControl);
    readMutex = xSemaphoreCreateMutex();
    if (!commands || !ioQueue || !readQueue || !readMutex) return false;
    acquisition = static_cast<slice*>(allocateWaveWords(3 * sizeof(slice)));
    if (!acquisition) return false;
    Preferences prefs;
    if (prefs.begin("wave", true)) { metrics.requestedPreMs = prefs.getUShort("preMs", 192); prefs.end(); }
    if (metrics.requestedPreMs < 48 || metrics.requestedPreMs > 288 || metrics.requestedPreMs % 48)
        metrics.requestedPreMs = 192;
    for (uint16_t block = 0; block < blockCount; ++block) {
        if (!partition.isUsableBlock(block)) continue;
        packetHeader header;
        if (!read(block, 0, &header, sizeof(header), readQueue)) { blockState[block] = 3; ++metrics.failedWrites; continue; }
        if (!validHeader(header)) continue;
        ids[block] = header.waveId;
        if (int32_t(header.waveId - metrics.nextId) >= 0) {
            metrics.nextId = header.waveId + 1;
            writeCursor = uint16_t((block + 1) % blockCount);
        }
    }
    for (uint8_t wanted = metrics.requestedPreMs / 48 + 2; slotCount < wanted; ++slotCount) {
        slots[slotCount].words = static_cast<uint32_t*>(allocateWaveWords(packetBytes));
        if (!slots[slotCount].words) break;
    }
    if (slotCount < 3) {
        for (auto& item : slots) { free(item.words); item.words = nullptr; }
        slotCount = 0;
        free(acquisition); acquisition = nullptr;
        return false;
    }
    preSlots = slotCount - 2;
    metrics.actualPreMs = preSlots * 48;
    metrics.reservedBytes = slotCount * packetBytes + 3 * sizeof(slice);
    metrics.wordHeapBytes = metrics.reservedBytes;
    metrics.enabled = true;
    paused = false;
    mountedWaveStore = this;
    if (xTaskCreate(taskEntry, "waveStore", 4096, this, 2, &task) != pdPASS) {
        mountedWaveStore = nullptr; paused = true; metrics.enabled = false;
        for (auto& item : slots) { free(item.words); item = slot{}; }
        free(acquisition); acquisition = nullptr;
        slotCount = 0;
        return false;
    }
    DIAG_INFO(DiagnosticModule::storage, "Wave previo=%u/%ums wordHeap=%lu free8=%u largest8=%u",
        metrics.actualPreMs, metrics.requestedPreMs, (unsigned long)metrics.wordHeapBytes,
        heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return true;
}

// Publica payload de 16 ms con numero de secuencia; la seccion critica solo copia 1344 bytes.
void waveEventStore::publish(const uint8_t* payload, uint64_t firstUs, uint16_t flags) {
    if (paused || !payload) return;
    portENTER_CRITICAL(&mutex);
    slice& dest = acquisition[published % 3];
    dest.firstUs = firstUs; dest.sequence = published + 1; dest.flags = flags;
    copyWaveWords(dest.payload, payload, sliceBytes);
    ++published;
    portEXIT_CRITICAL(&mutex);
    xTaskNotifyGive(task);
}

// Encola el primer cruce firstUs sin esperar a confirmacion ni bloquear MeterTask.
void waveEventStore::trigger(uint64_t firstUs) {
    if (paused) return;
    nvsHoldUntilMs = millis() + 2000U;
    command item; item.startUs = firstUs;
    if (xQueueSend(commands, &item, 0) != pdTRUE) ++triggerDrops;
    else xTaskNotifyGive(task);
}

// Encola la vinculacion durable; sus tiempos provienen del mismo reloj que la adquisicion.
void waveEventStore::confirm(uint32_t eventId, uint64_t startUs, uint64_t endUs, uint64_t rootUs, uint64_t unixUs) {
    if (paused || !eventId) return;
    command item; item.eventId = eventId; item.startUs = startUs; item.endUs = endUs;
    item.rootUs = rootUs; item.unixUs = unixUs;
    if (xQueueSend(commands, &item, 0) != pdTRUE) ++triggerDrops;
    else xTaskNotifyGive(task);
}

// Copia la instantanea ya publicada por el guardador a dest.
void waveEventStore::getMetrics(waveStoreMetrics& dest) {
    portENTER_CRITICAL(&mutex); dest = snapshot; portEXIT_CRITICAL(&mutex);
    if (paused) dest.enabled = false;
}

// Incluye comandos aun no atendidos por el guardador; al vencer hold limpia solo ese mismo plazo.
bool waveEventStore::isAcquiring() {
    if (paused) return false;
    uint32_t hold = nvsHoldUntilMs.load();
    if (hold && int32_t(millis() - hold) >= 0) { nvsHoldUntilMs.compare_exchange_strong(hold, 0); hold = nvsHoldUntilMs.load(); }
    return acquisitionActive || hold != 0;
}

// Valida y persiste preMs; la reserva actual permanece intacta hasta reiniciar.
bool waveEventStore::savePreMs(uint16_t preMs) {
    if (preMs < 48 || preMs > 288 || preMs % 48) return false;
    Preferences prefs;
    if (!prefs.begin("wave", false)) return false;
    bool saved = prefs.putUShort("preMs", preMs) == sizeof(uint16_t); prefs.end();
    return saved;
}

// Espera queue sin liberar anticipadamente el puntero que sigue usando NandIoService.
bool waveEventStore::waitIo(QueueHandle_t queue) {
    NandIoResult result;
    while (xQueueReceive(queue, &result, pdMS_TO_TICKS(100)) != pdTRUE) vTaskDelay(1);
    return result.success;
}

// Lee un encabezado pequeno de block/page a data usando una cola exclusiva.
bool waveEventStore::read(uint16_t block, uint8_t page, void* data, size_t length, QueueHandle_t queue) {
    return partition.submitReadPage(block, page, 0, static_cast<uint8_t*>(data), length,
        StoragePriority::low, queue) && waitIo(queue);
}

// Inicia la tarea usando parameter como instancia del guardador.
void waveEventStore::taskEntry(void* parameter) { static_cast<waveEventStore*>(parameter)->run(); }

// Obtiene espacio sin tocar el paquete actual ni paquetes congelados para escritura.
int8_t waveEventStore::freeSlot() {
    int8_t oldest = -1;
    for (uint8_t index = 0; index < slotCount; ++index) {
        if (index == currentSlot || slots[index].queued) continue;
        if (!slots[index].slices) return index;
        if (oldest < 0 || slots[index].order < slots[oldest].order) oldest = index;
    }
    return oldest;
}

// Congela prehistoria y el paquete parcial al primer cruce triggerUs; no borra destinos aqui.
void waveEventStore::startCapture(uint64_t triggerUs) {
    if (active >= 0) return;
    int8_t reference = -1;
    for (uint8_t i = 0; i < 8; ++i) if (!pending[i].used) { reference = i; break; }
    uint16_t block = UINT16_MAX;
    portENTER_CRITICAL(&mutex);
    for (uint16_t offset = 0; offset < blockCount; ++offset) {
        uint16_t candidate = (writeCursor + offset) % blockCount;
        if (blockState[candidate] == 1) { block = candidate; blockState[block] = 2; break; }
    }
    portEXIT_CRITICAL(&mutex);
    if (reference < 0 || block == UINT16_MAX) {
        if (block != UINT16_MAX) blockState[block] = 1;
        ++metrics.droppedTriggers; return;
    }
    active = reference;
    acquisitionActive = true;
    pendingCapture& capture = pending[active]; capture = pendingCapture{};
    capture.used = true; capture.block = block;
    capture.commit.magic = commitMagic; capture.commit.waveId = metrics.nextId++;
    if (!metrics.nextId) ++metrics.nextId;
    capture.commit.triggerUs = triggerUs; capture.commit.formatVersion = version;
    portENTER_CRITICAL(&mutex); ids[block] = capture.commit.waveId; portEXIT_CRITICAL(&mutex);
    writeCursor = (block + 1) % blockCount;
    packetIndex = 0; ++metrics.captures;
    // Congela solo paquetes completos anteriores al parcial, conservando orden cronologico.
    for (uint8_t i = 0; i < slotCount; ++i) if (i != currentSlot && slots[i].slices == 3) slots[i].queued = true;
}

// Copia value al paquete NAND, con tiempos individuales para no ocultar interrupciones.
void waveEventStore::appendSlice(const slice& value) {
	if (value.flags & acquisitionGap) ++metrics.gaps;
	if (slots[currentSlot].slices == 3) {
		int8_t next = freeSlot();
		if (next < 0) { ++metrics.gaps; return; }
		currentSlot = next; slots[currentSlot].slices = 0;
	}
    slot& item = slots[currentSlot];
    if (!item.slices) {
        item.header = packetHeader{};
        auto& header = item.header;
        header.magic = packetMagic; header.firstUs = value.firstUs; header.firstSequence = value.sequence;
        header.voltageStep = voltageScale; header.currentStep = currentScale;
        header.sampleRate = 8000; header.sampleCount = 384; header.formatVersion = version; header.channelMask = 0x7f;
        item.crcState = ~0U;
        item.order = ++slotOrder;
    }
    auto* header = &item.header;
    if (item.slices) header->sliceOffsetsUs[item.slices - 1] = int32_t(value.firstUs - header->firstUs);
    if (value.sequence != header->firstSequence + item.slices) header->flags |= acquisitionGap;
    header->flags |= value.flags;
    copyWaveWords(item.words + sizeof(packetHeader) / 4 + item.slices * (sliceBytes / 4), value.payload, sliceBytes);
    item.crcState = updateCrc32(item.crcState, value.payload, sliceBytes);
    header->sliceMask |= 1U << item.slices;
    if (++item.slices != 3) return;
    if (active >= 0) item.queued = true;
    int8_t next = freeSlot();
    if (next < 0) { ++metrics.gaps; return; }
    currentSlot = next; slots[currentSlot].slices = 0;
    // En reposo conserva exactamente N paquetes completos mas el parcial.
    uint8_t complete = 0;
    for (uint8_t i = 0; i < slotCount; ++i) if (slots[i].slices == 3) ++complete;
    if (active < 0 && complete > preSlots) {
        int8_t oldest = freeSlot();
        if (oldest >= 0 && slots[oldest].slices == 3) slots[oldest].slices = 0;
    }
}

// Escribe ambas paginas de index manteniendo el mismo buffer hasta sus dos finalizaciones.
void waveEventStore::writeSlot(uint8_t index, uint8_t* pageBuffer) {
    if (active < 0) return;
    slot& item = slots[index]; pendingCapture& capture = pending[active];
    auto* header = &item.header;
    header->waveId = capture.commit.waveId; header->packetIndex = packetIndex;
    header->triggerUs = capture.commit.triggerUs;
    if (!packetIndex) {
        capture.firstUs = header->firstUs;
        if (header->firstUs > header->triggerUs || header->triggerUs - header->firstUs < uint64_t(preSlots) * packetUs)
            header->flags |= prehistoryMissing;
    }
    header->dataCrc = ~item.crcState;
    header->headerCrc = 0; header->headerCrc = crc32(header, sizeof(*header));
    copyWaveWords(item.words, header, sizeof(*header));
    for (uint8_t page = 0; page < 2; ++page) {
        // El driver SPI necesita bytes: reutiliza la pagina DRAM y espera antes de volver a tocarla.
        copyWaveWords(pageBuffer, item.words + page * 512, 2048);
        if (!partition.submitProgramPage(capture.block, packetIndex * 2 + page, pageBuffer, 2048,
            StoragePriority::high, ioQueue) || !waitIo(ioQueue)) { capture.failed = true; ++metrics.failedWrites; break; }
    }
    capture.commit.endUs = header->firstUs + header->sliceOffsetsUs[1] + sliceUs;
    item.queued = false; item.slices = 0;
    ++packetIndex;
    if (capture.failed || packetIndex == dataPackets) {
        capture.complete = true; capture.commit.packetCount = packetIndex;
        capture.deadlineUs = uint64_t(esp_timer_get_time()) + 6000000ULL;
        for (auto& value : slots) value.queued = false;
        active = -1;
        acquisitionActive = false;
    }
}

// Confirma con dos paginas nuevas identicas; sin enlace deja la captura huerfana para la proxima vuelta.
void waveEventStore::sealPending(uint64_t nowUs, uint8_t* pageBuffer) {
    for (auto& capture : pending) {
        if (!capture.used || !capture.complete || nowUs < capture.deadlineUs) continue;
        bool committed = false;
        if (!capture.failed && capture.commit.linkCount) {
            capture.commit.crc = crc32(&capture.commit, offsetof(commitHeader, crc));
            memset(pageBuffer, 0xff, 2048); memcpy(pageBuffer, &capture.commit, sizeof(capture.commit));
            committed = true;
            for (uint8_t page = 62; page < 64; ++page)
                if (!partition.submitProgramPage(capture.block, page, pageBuffer, 2048, StoragePriority::high, ioQueue) ||
                    !waitIo(ioQueue)) { committed = false; ++metrics.failedWrites; break; }
        }
        if (committed) ++metrics.committed; else ++metrics.orphans;
        portENTER_CRITICAL(&mutex); blockState[capture.block] = 0; portEXIT_CRITICAL(&mutex);
        capture.used = false;
    }
}

// Mantiene cuatro bloques ya borrados; no inicia borrados mientras captura ni sobre una descarga.
void waveEventStore::reserveBlocks() {
    if (active >= 0 || paused) return;
    uint16_t erased = 0, candidate = UINT16_MAX;
    portENTER_CRITICAL(&mutex);
    for (uint16_t i = 0; i < blockCount; ++i) if (blockState[i] == 1) ++erased;
    if (erased < 4) for (uint16_t offset = 0; offset < blockCount; ++offset) {
        const uint16_t block = (writeCursor + offset) % blockCount;
        if (blockState[block] == 0 && pinnedBlock != block && partition.isUsableBlock(block)) {
            candidate = block; blockState[block] = 2; ids[block] = 0; break;
        }
    }
    portEXIT_CRITICAL(&mutex);
    metrics.erasedBlocks = erased;
    if (candidate == UINT16_MAX) return;
    const bool erasedOk = partition.submitEraseBlock(candidate, StoragePriority::low, ioQueue) && waitIo(ioQueue);
    portENTER_CRITICAL(&mutex); blockState[candidate] = erasedOk ? 1 : 3; portEXIT_CRITICAL(&mutex);
    if (!erasedOk) ++metrics.failedWrites;
}

// Atiende todas las muestras disponibles antes de cada escritura; las perdidas siempre incrementan gaps.
void waveEventStore::run() {
    // Recepcion y escritura son secuenciales: comparten espacio de pila, sin otra pagina global de 2 KiB.
    workerBuffer buffer;
    uint32_t stackCheckMs = 0;
    for (;;) {
        if (paused) { workerPaused = true; vTaskDelay(1); continue; }
        workerPaused = false;
        command item;
        while (xQueueReceive(commands, &item, 0) == pdTRUE) {
            if (!item.eventId) { startCapture(item.startUs); continue; }
            for (auto& capture : pending) {
                if (!capture.used || capture.commit.triggerUs < item.startUs || capture.commit.triggerUs > item.endUs) continue;
                bool duplicate = false;
                for (uint8_t i = 0; i < capture.commit.linkCount; ++i) if (capture.commit.links[i].eventId == item.eventId) duplicate = true;
                if (duplicate) continue;
                if (capture.commit.linkCount == 4) { ++metrics.droppedTriggers; continue; }
                capture.commit.links[capture.commit.linkCount++] = eventLink{item.eventId, item.rootUs, item.unixUs};
            }
        }
        for (uint8_t count = 0; count < 3; ++count) {
            bool available = false;
            portENTER_CRITICAL(&mutex);
            const uint32_t backlog = published - consumed;
            if (backlog > 3) { metrics.gaps += backlog - 3; consumed = published - 3; }
            if (published != consumed) { copyWaveWords(&buffer.sample, &acquisition[consumed % 3], sizeof(slice)); ++consumed; available = true; }
            portEXIT_CRITICAL(&mutex);
            if (!available) break;
            appendSlice(buffer.sample);
        }
        int8_t queued = -1;
        for (uint8_t i = 0; i < slotCount; ++i) if (slots[i].queued &&
            (queued < 0 || slots[i].order < slots[queued].order)) queued = i;
        if (queued >= 0) writeSlot(queued, buffer.page);
        sealPending(esp_timer_get_time(), buffer.page);
        reserveBlocks();
        metrics.pendingLinks = 0;
        for (const auto& capture : pending) if (capture.used) ++metrics.pendingLinks;
        metrics.capturing = active >= 0; metrics.droppedTriggers += triggerDrops.exchange(0);
        if (uint32_t(millis() - stackCheckMs) >= 1000) {
            metrics.stackFreeBytes = uxTaskGetStackHighWaterMark(nullptr); stackCheckMs = millis();
        }
        portENTER_CRITICAL(&mutex);
        metrics.publishedSlices = published; snapshot = metrics;
        portEXIT_CRITICAL(&mutex);
        if (queued < 0) ulTaskNotifyTake(pdTRUE, 1);
    }
}

// Pausa el escritor sin destruir buffers pendientes; una descarga fijada impide el formato.
bool waveEventStore::prepareFormat() {
    if (!task) return true;
    paused = true;
    const uint32_t started = millis();
    while (!workerPaused && millis() - started < 2000) vTaskDelay(1);
    if (!workerPaused || !readMutex || xSemaphoreTake(readMutex, pdMS_TO_TICKS(2000)) != pdTRUE) { paused = false; return false; }
    const bool ready = pinnedBlock == UINT16_MAX;
    xSemaphoreGive(readMutex);
    if (!ready) paused = false;
    return ready;
}

// Reinicia el anillo despues del borrado; ante fallo mantiene el guardador detenido.
void waveEventStore::finishFormat(bool success) {
    if (!success) { metrics.enabled = false; return; }
    memset(ids, 0, sizeof(ids)); memset(blockState, 1, sizeof(blockState));
    for (auto& capture : pending) capture = pendingCapture{};
    for (auto& item : slots) { item.slices = 0; item.queued = false; }
    if (commands) xQueueReset(commands);
    consumed = published; active = -1; currentSlot = 0; writeCursor = 0;
    acquisitionActive = false; nvsHoldUntilMs = 0;
    // Los IDs no se reciclan al formatear: evita asociar una captura Wave anterior al FAST nuevo.
    if (task) { metrics.enabled = true; paused = false; }
}

// Busca enlaces validados de eventId; cursor permite paginar sin un catalogo grande en RAM.
bool waveEventStore::find(uint32_t eventId, uint64_t unixUs, uint16_t& cursor, uint16_t& block, commitHeader& commit) {
    if (paused || !readMutex || xSemaphoreTake(readMutex, 0) != pdTRUE) return false;
    bool found = false;
    while (cursor < blockCount && !paused) {
        block = cursor++;
        portENTER_CRITICAL(&mutex);
        const bool used = ids[block] && blockState[block] == 0 && pinnedBlock == UINT16_MAX;
        if (used) pinnedBlock = block;
        portEXIT_CRITICAL(&mutex);
        if (!used) continue;
        commitHeader second;
        const bool valid = read(block, 62, &commit, sizeof(commit), readQueue) && validCommit(commit) &&
            read(block, 63, &second, sizeof(second), readQueue) && !memcmp(&commit, &second, sizeof(commit));
        portENTER_CRITICAL(&mutex); pinnedBlock = UINT16_MAX; portEXIT_CRITICAL(&mutex);
        if (!valid) continue;
        for (uint8_t i = 0; i < commit.linkCount; ++i)
            if (commit.links[i].eventId == eventId && commit.links[i].unixUs == unixUs) found = true;
        if (found) break;
    }
    xSemaphoreGive(readMutex);
    return found;
}

// Fija block/id de forma atomica contra el borrado circular; sin esperar una lectura.
bool waveEventStore::pin(uint16_t block, uint32_t id) {
    if (paused) return false;
    portENTER_CRITICAL(&mutex);
    bool ready = block < blockCount && ids[block] == id && blockState[block] == 0 && pinnedBlock == UINT16_MAX;
    if (ready) pinnedBlock = block;
    portEXIT_CRITICAL(&mutex);
    return ready;
}

// Libera el bloque que la descarga ya no necesita.
void waveEventStore::unpin() { portENTER_CRITICAL(&mutex); pinnedBlock = UINT16_MAX; portEXIT_CRITICAL(&mutex); }

// Publica payload/firstUs/flags solamente si el guardador pudo montar su reserva.
void waveCapturePublish(const uint8_t* payload, uint64_t firstUs, uint16_t flags) {
    if (mountedWaveStore) mountedWaveStore->publish(payload, firstUs, flags);
}
// Solicita captura en firstUs sin acoplar MeterTask al administrador de almacenamiento.
void waveCaptureTrigger(uint64_t firstUs) { if (mountedWaveStore) mountedWaveStore->trigger(firstUs); }
// Permite al loop principal diferir NVS sin reservar memoria ni esperar una cola.
bool waveCaptureBusy() { return mountedWaveStore && mountedWaveStore->isAcquiring(); }
// Enlaza un FAST durable y su referencia temporal con las capturas provisionales que le corresponden.
void waveCaptureConfirm(uint32_t eventId, uint64_t startUs, uint64_t endUs, uint64_t rootUs, uint64_t unixUs) {
    if (mountedWaveStore) mountedWaveStore->confirm(eventId, startUs, endUs, rootUs, unixUs);
}
