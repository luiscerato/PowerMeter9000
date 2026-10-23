#include "wave_event_store.h"
#include "../event_settings.h"
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

// Monta la particion y reserva paginas compactas, reduciendo prehistoria si falta heap.
bool waveEventStore::begin() {
    NandPartitionInfo info;
    partition.getInfo(info);
    if (!info.enabled || !info.blockCount || info.blockCount > 320 || task) return false;
    blockCount = info.blockCount;
    commands = xQueueCreateStatic(16, sizeof(command), commandData, &commandControl);
    ioQueue = xQueueCreateStatic(1, sizeof(NandIoResult), ioData, &ioControl);
    readQueue = xQueueCreateStatic(1, sizeof(NandIoResult), readData, &readControl);
    if (!commands || !ioQueue || !readQueue) return false;
    acquisition = static_cast<slice*>(allocateWaveWords(3 * sizeof(slice)));
    if (!acquisition) return false;
    eventSettingsSnapshot settings; eventSettingsGet(settings);
    metrics.requestedPreMs = settings.config.preMs;
    applyConfig(settings.config.waveEnabled, settings.config.waveMask);
    if (metrics.requestedPreMs < 48 || metrics.requestedPreMs > 288 || metrics.requestedPreMs % 48)
        metrics.requestedPreMs = 192;
    uint32_t newestId = 0;
    uint16_t newestSlot = 0;
    for (uint16_t block = 0; block < blockCount; ++block) {
        if (!partition.isUsableBlock(block)) continue;
        uint8_t raw[64];
        if (!read(block, 0, raw, sizeof(raw), readQueue)) { blockState[block] = 3; ++metrics.failedWrites; continue; }
        packetHeader legacy;
        memcpy(&legacy, raw, sizeof(legacy));
        if (validLegacyHeader(legacy)) {
            ids[block * 2] = legacy.waveId;
            slotState[block * 2] = slotState[block * 2 + 1] = 2;
            slotVersion[block * 2] = legacyVersion;
            commitHeader footer, duplicate;
            if (read(block, 62, &footer, sizeof(footer), readQueue) && validCommit(footer) && footer.waveId == legacy.waveId &&
                read(block, 63, &duplicate, sizeof(duplicate), readQueue) && !memcmp(&footer, &duplicate, sizeof(footer)))
                committedSlots[(block * 2) / 8] |= 1U << ((block * 2) % 8);
            blockState[block] = 0;
            if (!newestId || int32_t(legacy.waveId - newestId) > 0) { newestId = legacy.waveId; newestSlot = block * 2; }
            continue;
        }
        for (uint8_t half = 0; half < 2; ++half) {
            const uint8_t firstPage = half * 32;
            if (half && !read(block, firstPage, raw, sizeof(pageHeader), readQueue)) {
                blockState[block] = 3; ++metrics.failedWrites; break;
            }
            pageHeader page;
            memcpy(&page, raw, sizeof(page));
            const uint16_t slotIndex = block * 2 + half;
            if (validPageHeader(page) && page.pageIndex == 0) {
                ids[slotIndex] = page.waveId; slotState[slotIndex] = 2; slotVersion[slotIndex] = version;
                commitHeader footer;
                if (read(block, firstPage + 31, &footer, sizeof(footer), readQueue) && validCommit(footer) &&
                    footer.waveId == page.waveId && footer.formatVersion == version)
                    committedSlots[slotIndex / 8] |= 1U << (slotIndex % 8);
                if (!newestId || int32_t(page.waveId - newestId) > 0) { newestId = page.waveId; newestSlot = slotIndex; }
            } else {
                bool programmed = false;
                for (uint8_t index = 0; index < sizeof(page); ++index) if (raw[index] != 0xff) programmed = true;
                if (programmed) slotState[slotIndex] = 2;
            }
        }
        if (blockState[block] != 3) blockState[block] = slotState[block * 2] == 2 && slotState[block * 2 + 1] == 2 ? 0 : 1;
    }
    if (newestId) {
        metrics.nextId = newestId + 1;
        if (!metrics.nextId) metrics.nextId = 1;
        writeCursor = (newestSlot + 1) % (blockCount * 2);
    }
    // Conserva una pagina extra porque el disparo puede caer en cualquier punto de la pagina actual.
    const uint8_t wanted = metrics.requestedPreMs / (pageUs / 1000) + 3;
    for (; slotCount < wanted && slotCount < sizeof(slots) / sizeof(slots[0]); ++slotCount) {
        slots[slotCount].words = static_cast<uint32_t*>(allocateWaveWords(pagePayloadBytes));
        if (!slots[slotCount].words) break;
    }
    if (slotCount < 4) {
        for (auto& item : slots) { free(item.words); item.words = nullptr; }
        slotCount = 0;
        free(acquisition); acquisition = nullptr;
        return false;
    }
    preSlots = slotCount - 2;
    // Informa el minimo garantizado, no la capacidad total del anillo.
    metrics.actualPreMs = (preSlots - 1) * 24;
    metrics.reservedBytes = slotCount * pagePayloadBytes + 3 * sizeof(slice);
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
    if (paused || !saveEnabled) return;
    nvsHoldUntilMs = millis() + 2000U;
    command item; item.startUs = firstUs;
    if (xQueueSend(commands, &item, 0) != pdTRUE) ++triggerDrops;
    else xTaskNotifyGive(task);
}

// Encola la vinculacion durable; sus tiempos provienen del mismo reloj que la adquisicion.
void waveEventStore::confirm(uint32_t eventId, uint64_t startUs, uint64_t endUs, uint64_t rootUs, uint64_t unixUs, uint8_t eventMask) {
    if (paused || !eventId || !(eventMask & (1U | (wavePointMask.load() << 1)))) return;
    command item; item.eventId = eventId; item.startUs = startUs; item.endUs = endUs;
    item.rootUs = rootUs; item.unixUs = unixUs;
    if (xQueueSend(commands, &item, 0) != pdTRUE) ++triggerDrops;
    else xTaskNotifyGive(task);
}

// Copia la instantanea ya publicada por el guardador a dest.
void waveEventStore::getMetrics(waveStoreMetrics& dest) {
    portENTER_CRITICAL(&mutex);
    dest = snapshot;
    dest.totalSlots = blockCount * 2;
    dest.occupiedSlots = 0;
    dest.storedCaptures = 0;
    for (uint16_t index = 0; index < dest.totalSlots; ++index) {
        if (slotState[index]) ++dest.occupiedSlots;
        if (committedSlots[index / 8] & (1U << (index % 8))) ++dest.storedCaptures;
    }
    portEXIT_CRITICAL(&mutex);
    if (paused) dest.enabled = false;
}

// Incluye comandos aun no atendidos por el guardador; al vencer hold limpia solo ese mismo plazo.
bool waveEventStore::isAcquiring() {
    if (paused) return false;
    uint32_t hold = nvsHoldUntilMs.load();
    if (hold && int32_t(millis() - hold) >= 0) { nvsHoldUntilMs.compare_exchange_strong(hold, 0); hold = nvsHoldUntilMs.load(); }
    return acquisitionActive || hold != 0;
}

// Cambia solo los controles RAM; enabled y pointMask provienen de una configuracion confirmada.
void waveEventStore::applyConfig(bool enabled, uint8_t pointMask) { saveEnabled = enabled; wavePointMask = pointMask; }

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
        if (!slots[index].dataBytes) return index;
        if (oldest < 0 || slots[index].order < slots[oldest].order) oldest = index;
    }
    return oldest;
}

// Congela prehistoria y el paquete parcial al primer cruce triggerUs; no borra destinos aqui.
void waveEventStore::startCapture(uint64_t triggerUs) {
    if (active >= 0) return;
    int8_t reference = -1;
    for (uint8_t i = 0; i < 8; ++i) if (!pending[i].used) { reference = i; break; }
    uint16_t slotIndex = UINT16_MAX;
    portENTER_CRITICAL(&mutex);
    for (uint16_t offset = 0; offset < blockCount * 2; ++offset) {
        const uint16_t candidate = (writeCursor + offset) % (blockCount * 2);
        if (slotState[candidate] == 0 && partition.isUsableBlock(candidate / 2) && blockState[candidate / 2] != 2 && blockState[candidate / 2] != 3) {
            slotIndex = candidate; slotState[slotIndex] = 1; break;
        }
    }
    portEXIT_CRITICAL(&mutex);
    if (reference < 0 || slotIndex == UINT16_MAX) {
        if (slotIndex != UINT16_MAX) slotState[slotIndex] = 0;
        ++metrics.droppedTriggers; return;
    }
    active = reference;
    acquisitionActive = true;
    pendingCapture& capture = pending[active]; capture = pendingCapture{};
    capture.used = true; capture.slotIndex = slotIndex; capture.block = slotIndex / 2;
    capture.firstPage = (slotIndex & 1) ? 32 : 0;
    capture.commit.magic = commitMagic; capture.commit.waveId = metrics.nextId++;
    if (!metrics.nextId) ++metrics.nextId;
    capture.commit.triggerUs = triggerUs; capture.commit.formatVersion = version;
    portENTER_CRITICAL(&mutex);
    ids[slotIndex] = capture.commit.waveId; slotVersion[slotIndex] = version;
    portEXIT_CRITICAL(&mutex);
    writeCursor = (slotIndex + 1) % (blockCount * 2);
    packetIndex = 0; ++metrics.captures;
    // Congela paginas completas anteriores, conservando la pagina parcial para terminarla al llegar muestras.
    for (uint8_t i = 0; i < slotCount; ++i) if (i != currentSlot && slots[i].dataBytes == pagePayloadBytes) slots[i].queued = true;
}

// Divide cada tramo de 16 ms entre paginas de 24 ms y conserva sus tiempos exactos.
void waveEventStore::appendSlice(const slice& value) {
    if (value.flags & acquisitionGap) ++metrics.gaps;
    size_t sourceOffset = 0;
    while (sourceOffset < sliceBytes) {
        slot& item = slots[currentSlot];
        if (!item.dataBytes) {
            item.header = pageHeader{};
            item.header.magic = pageMagic; item.header.firstUs = value.firstUs + (sourceOffset ? 8000 : 0);
            item.header.firstSequence = uint16_t((value.sequence - 1) * 128 + (sourceOffset ? 64 : 0));
            item.header.formatVersion = version; item.header.sampleCount = samplesPerPage;
            item.crcState = ~0U; item.order = ++slotOrder; item.previousSequence = value.sequence; item.slices = 1;
        } else if (!sourceOffset && value.sequence != item.previousSequence + 1) {
            item.header.flags |= acquisitionGap;
            ++metrics.gaps;
        }
        item.header.flags |= value.flags;
        item.previousSequence = value.sequence;
        const size_t count = min(size_t(sliceBytes) - sourceOffset, size_t(pagePayloadBytes - item.dataBytes));
        copyWaveWords(item.words + item.dataBytes / 4, value.payload + sourceOffset / 4, count);
        item.crcState = updateCrc32(item.crcState, value.payload + sourceOffset / 4, count);
        item.dataBytes += count; sourceOffset += count;
        if (item.dataBytes != pagePayloadBytes) continue;
        if (active >= 0) item.queued = true;
        const int8_t next = freeSlot();
        if (next < 0) { ++metrics.gaps; return; }
        currentSlot = next; slots[currentSlot].dataBytes = 0; slots[currentSlot].slices = 0;
        if (active < 0) {
            uint8_t complete = 0;
            for (uint8_t index = 0; index < slotCount; ++index)
                if (index != currentSlot && slots[index].dataBytes == pagePayloadBytes) ++complete;
            while (complete > preSlots) {
                int8_t oldest = -1;
                for (uint8_t index = 0; index < slotCount; ++index)
                    if (index != currentSlot && slots[index].dataBytes == pagePayloadBytes &&
                        (oldest < 0 || slots[index].order < slots[oldest].order)) oldest = index;
                if (oldest < 0) break;
                slots[oldest].dataBytes = 0; slots[oldest].slices = 0; --complete;
            }
        }
    }
}

// Escribe una pagina de 2 KiB y cierra la captura al completar sus 31 paginas de datos.
void waveEventStore::writeSlot(uint8_t index, uint8_t* pageBuffer) {
    if (active < 0) return;
    slot& item = slots[index]; pendingCapture& capture = pending[active];
    auto* header = &item.header;
    header->waveId = capture.commit.waveId; header->pageIndex = packetIndex;
    if (!packetIndex) {
        capture.firstUs = header->firstUs;
        const uint64_t expectedPreUs = uint64_t(metrics.actualPreMs) * 1000;
        if (header->firstUs > capture.commit.triggerUs || capture.commit.triggerUs - header->firstUs < expectedPreUs)
            header->flags |= prehistoryMissing;
    }
    header->dataCrc = ~item.crcState; header->headerCrc = 0;
    header->headerCrc = crc32(header, sizeof(*header));
    memset(pageBuffer, 0xff, pageBytes);
    copyWaveWords(pageBuffer, header, sizeof(*header));
    copyWaveWords(pageBuffer + sizeof(*header), item.words, pagePayloadBytes);
    if (!partition.submitProgramPage(capture.block, capture.firstPage + packetIndex, pageBuffer, pageBytes,
        StoragePriority::high, ioQueue) || !waitIo(ioQueue)) { capture.failed = true; ++metrics.failedWrites; }
    capture.commit.endUs = header->firstUs + (samplesPerPage - 1) * 125ULL;
    item.queued = false; item.dataBytes = 0; item.slices = 0;
    ++packetIndex;
    if (capture.failed || packetIndex == dataPages) {
        capture.complete = true; capture.commit.packetCount = packetIndex;
        capture.deadlineUs = uint64_t(esp_timer_get_time()) + 6000000ULL;
        for (auto& value : slots) value.queued = false;
        active = -1;
        acquisitionActive = false;
    }
}

// Confirma en una pagina final; sin enlace deja la captura huerfana para la proxima vuelta.
void waveEventStore::sealPending(uint64_t nowUs, uint8_t* pageBuffer) {
    for (auto& capture : pending) {
        if (!capture.used || !capture.complete || nowUs < capture.deadlineUs) continue;
        bool committed = false;
        if (!capture.failed && capture.commit.linkCount) {
            capture.commit.crc = crc32(&capture.commit, offsetof(commitHeader, crc));
            memset(pageBuffer, 0xff, 2048); memcpy(pageBuffer, &capture.commit, sizeof(capture.commit));
            committed = partition.submitProgramPage(capture.block, capture.firstPage + 31, pageBuffer, 2048,
                StoragePriority::high, ioQueue) && waitIo(ioQueue);
            if (!committed) ++metrics.failedWrites;
        }
        if (committed) ++metrics.committed; else ++metrics.orphans;
        portENTER_CRITICAL(&mutex);
        if (committed) committedSlots[capture.slotIndex / 8] |= 1U << (capture.slotIndex % 8);
        slotState[capture.slotIndex] = 2;
        blockState[capture.block] = slotState[capture.block * 2] == 2 && slotState[capture.block * 2 + 1] == 2 ? 0 : 1;
        portEXIT_CRITICAL(&mutex);
        capture.used = false;
    }
}

// Mantiene cuatro bloques ya borrados; no inicia borrados mientras captura ni sobre una descarga.
void waveEventStore::reserveBlocks() {
    if (active >= 0 || paused) return;
    uint16_t erased = 0, candidate = UINT16_MAX;
    portENTER_CRITICAL(&mutex);
    for (uint16_t i = 0; i < blockCount; ++i)
        if (blockState[i] == 1 && !slotState[i * 2] && !slotState[i * 2 + 1]) ++erased;
    if (erased < 4) for (uint16_t offset = 0; offset < blockCount; ++offset) {
        const uint16_t block = (writeCursor / 2 + offset) % blockCount;
        if (blockState[block] == 0 && slotState[block * 2] == 2 && slotState[block * 2 + 1] == 2 &&
            pinnedBlock != block && partition.isUsableBlock(block)) {
            candidate = block; blockState[block] = 2; break;
        }
    }
    portEXIT_CRITICAL(&mutex);
    metrics.erasedBlocks = erased;
    if (candidate == UINT16_MAX) return;
    const bool erasedOk = partition.submitEraseBlock(candidate, StoragePriority::low, ioQueue) && waitIo(ioQueue);
    portENTER_CRITICAL(&mutex);
    blockState[candidate] = erasedOk ? 1 : 3;
    if (erasedOk) {
        for (uint8_t half = 0; half < 2; ++half) {
            const uint16_t index = candidate * 2 + half;
            ids[index] = 0; slotState[index] = 0; slotVersion[index] = 0;
            committedSlots[index / 8] &= ~(1U << (index % 8));
        }
    }
    portEXIT_CRITICAL(&mutex);
    if (!erasedOk) ++metrics.failedWrites;
}

// Vincula item por solapamiento temporal: el retorno puede disparar Wave antes de alcanzar el estado normal.
void waveEventStore::linkEvent(const command& item) {
    if (!item.eventId || item.endUs < item.startUs) return;
    for (auto& capture : pending) {
        if (!capture.used || capture.commit.endUs < item.startUs || capture.commit.triggerUs > item.endUs) continue;
        bool duplicate = false;
        for (uint8_t i = 0; i < capture.commit.linkCount; ++i) if (capture.commit.links[i].eventId == item.eventId) duplicate = true;
        if (duplicate) continue;
        if (capture.commit.linkCount == 4) { ++metrics.droppedTriggers; continue; }
        capture.commit.links[capture.commit.linkCount++] = eventLink{item.eventId, item.rootUs, item.unixUs};
    }
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
            linkEvent(item);
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
        if (queued < 0 && active < 0) serviceIndex();
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
    if (!workerPaused) { paused = false; return false; }
    portENTER_CRITICAL(&mutex);
    const bool ready = pinnedBlock == UINT16_MAX;
    portEXIT_CRITICAL(&mutex);
    if (!ready) paused = false;
    return ready;
}

// Reinicia el anillo despues del borrado; ante fallo mantiene el guardador detenido.
void waveEventStore::finishFormat(bool success) {
    portENTER_CRITICAL(&mutex);
    for (auto& query : indexQueries) query = indexQuery{};
    portEXIT_CRITICAL(&mutex);
    if (!success) { metrics.enabled = false; return; }
    memset(ids, 0, sizeof(ids)); memset(slotState, 0, sizeof(slotState)); memset(slotVersion, 0, sizeof(slotVersion));
    memset(committedSlots, 0, sizeof(committedSlots));
    memset(blockState, 1, sizeof(blockState));
    for (auto& capture : pending) capture = pendingCapture{};
    for (auto& item : slots) { item.dataBytes = 0; item.slices = 0; item.queued = false; }
    if (commands) xQueueReset(commands);
    consumed = published; active = -1; currentSlot = 0; writeCursor = 0;
    acquisitionActive = false; nvsHoldUntilMs = 0;
    // Los IDs no se reciclan al formatear: evita asociar una captura Wave anterior al FAST nuevo.
    if (task) { metrics.enabled = true; paused = false; }
}

// Obtiene o inicia la consulta eventId/unixUs; dest recibe una instantanea acotada sin leer NAND.
waveIndexStatus waveEventStore::lookup(uint32_t eventId, uint64_t unixUs, waveIndexSnapshot& dest) {
    if (paused || !task) return waveIndexStatus::unavailable;
    const uint32_t now = millis();
    waveIndexStatus status = waveIndexStatus::busy;
    portENTER_CRITICAL(&mutex);
    indexQuery* available = nullptr;
    for (auto& query : indexQueries) {
        const bool expired = query.status != waveIndexStatus::pending &&
            uint32_t(now - query.completedMs) >= (query.delivered ? 2000U : 10000U);
        if (query.eventId == eventId && query.unixUs == unixUs && !expired) {
            status = query.status;
            if (status == waveIndexStatus::ready) dest = query.result;
            if (status != waveIndexStatus::pending) query.delivered = true;
            portEXIT_CRITICAL(&mutex);
            return status;
        }
        if (query.status == waveIndexStatus::empty || expired || query.delivered) available = &query;
    }
    if (available) {
        *available = indexQuery{}; available->eventId = eventId; available->unixUs = unixUs;
        available->status = status = waveIndexStatus::pending;
    }
    portEXIT_CRITICAL(&mutex);
    if (status == waveIndexStatus::pending) xTaskNotifyGive(task);
    return status;
}

// Busca una cabecera por turno en la tarea Wave; no usa el pin reservado a las descargas.
void waveEventStore::serviceIndex() {
    indexQuery* query = nullptr;
    uint16_t slotIndex = 0;
    portENTER_CRITICAL(&mutex);
    for (auto& item : indexQueries) if (item.status == waveIndexStatus::pending) { query = &item; break; }
    if (query) {
        bool selected = false;
        while (query->cursor < blockCount * 2) {
            slotIndex = query->cursor++;
            if (committedSlots[slotIndex / 8] & (1U << (slotIndex % 8))) { selected = true; break; }
        }
        if (!selected) {
            query->completedMs = millis(); query->status = waveIndexStatus::ready; query = nullptr;
        }
    }
    portEXIT_CRITICAL(&mutex);
    if (!query) return;
    const uint16_t block = slotIndex / 2;
    const uint8_t firstPage = (slotIndex & 1) ? 32 : 0, format = slotVersion[slotIndex];
    const uint8_t footer = format == legacyVersion ? 62 : firstPage + 31;
    commitHeader commit{}, duplicate{};
    bool valid = read(block, footer, &commit, sizeof(commit), ioQueue) && validCommit(commit) &&
        commit.waveId == ids[slotIndex] && commit.formatVersion == format;
    if (valid && format == legacyVersion)
        valid = read(block, footer + 1, &duplicate, sizeof(duplicate), ioQueue) && !memcmp(&commit, &duplicate, sizeof(commit));
    bool matches = false;
    if (valid) for (uint8_t i = 0; i < commit.linkCount; ++i)
        if (commit.links[i].eventId == query->eventId && commit.links[i].unixUs == query->unixUs) matches = true;
    portENTER_CRITICAL(&mutex);
    if (matches) {
        auto& entry = query->result.entries[query->result.count++];
        entry.id = commit.waveId; entry.block = block; entry.page = firstPage; entry.version = format;
        entry.triggerUs = commit.triggerUs; entry.endUs = commit.endUs;
    }
    if (!valid || query->cursor == blockCount * 2 || query->result.count == 8) {
        query->completedMs = millis(); query->status = valid ? waveIndexStatus::ready : waveIndexStatus::failed;
    }
    portEXIT_CRITICAL(&mutex);
}

// Fija bloque, media captura e identificador contra el borrado circular.
bool waveEventStore::pin(uint16_t block, uint8_t firstPage, uint32_t id, uint8_t formatVersion) {
    if (paused || block >= blockCount || (firstPage != 0 && firstPage != 32)) return false;
    portENTER_CRITICAL(&mutex);
    const uint8_t half = firstPage == 32 ? 1 : 0;
    const uint16_t slotIndex = block * 2 + half;
    bool ready = ids[slotIndex] == id &&
        slotVersion[slotIndex] == formatVersion && slotState[slotIndex] == 2 &&
        blockState[block] != 2 && blockState[block] != 3 && pinnedBlock == UINT16_MAX;
    if (ready && formatVersion == legacyVersion) ready = firstPage == 0;
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
void waveCaptureConfirm(uint32_t eventId, uint64_t startUs, uint64_t endUs, uint64_t rootUs, uint64_t unixUs, uint8_t eventMask) {
    if (mountedWaveStore) mountedWaveStore->confirm(eventId, startUs, endUs, rootUs, unixUs, eventMask);
}
