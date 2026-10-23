#pragma once
#include "nand_partition.h"
#include "wave_event_format.h"
#include <atomic>

struct waveStoreMetrics {
    uint32_t captures = 0, committed = 0, orphans = 0, gaps = 0, droppedTriggers = 0, failedWrites = 0;
    uint32_t publishedSlices = 0, reservedBytes = 0, nextId = 1, pendingLinks = 0, wordHeapBytes = 0;
    uint16_t requestedPreMs = 192, actualPreMs = 0, erasedBlocks = 0;
    uint16_t stackFreeBytes = 0;
    uint16_t occupiedSlots = 0, totalSlots = 0, storedCaptures = 0;
    bool enabled = false, capturing = false;
};

enum class waveIndexStatus : uint8_t { empty, pending, ready, busy, unavailable, failed };
struct waveIndexEntry {
    uint64_t triggerUs = 0, endUs = 0;
    uint32_t id = 0;
    uint16_t block = 0;
    uint8_t page = 0, version = 0;
};
struct waveIndexSnapshot { waveIndexEntry entries[8]; uint8_t count = 0; };

// Captura provisional Wave con buffers permanentes y un unico escritor fuera de MeterTask.
class waveEventStore {
public:
    // Asocia partition con este guardador; no reserva RAM ni inicia tareas todavia.
    explicit waveEventStore(NandPartition& partition) : partition(partition) {}
    // Recupera identificadores y reserva el historial; sin parametros, se llama una vez al arrancar.
    bool begin();
    // Publica payload alineado a 4 bytes (16 ms); firstUs/flags indican tiempo/calidad. Nunca espera NAND.
    void publish(const uint8_t* payload, uint64_t firstUs, uint16_t flags);
    // Solicita captura en firstUs monotono; varios cruces simultaneos comparten la captura activa.
    void trigger(uint64_t firstUs);
    // Vincula un FAST durable; startUs/endUs son su ventana y rootUs/unixUs su referencia temporal.
    void confirm(uint32_t eventId, uint64_t startUs, uint64_t endUs, uint64_t rootUs, uint64_t unixUs, uint8_t eventMask = 1);
    // Copia contadores y reserva efectiva a dest; no consulta NAND.
    void getMetrics(waveStoreMetrics& dest);
    // Indica captura activa o disparo reciente para diferir escrituras NVS; sin parametros.
    bool isAcquiring();
    // Aplica enabled y pointMask desde el servicio unificado, sin acceder a NVS.
    void applyConfig(bool enabled, uint8_t pointMask);
    // Pausa y espera operaciones propias antes del borrado; false si hay una descarga activa.
    bool prepareFormat();
    // Reabre despues del borrado; success indica si todos los bloques se borraron correctamente.
    void finishFormat(bool success);
    // Solicita eventId/unixUs sin esperar NAND; dest solo se completa cuando devuelve ready.
    waveIndexStatus lookup(uint32_t eventId, uint64_t unixUs, waveIndexSnapshot& dest);
    // Protege bloque/pagina/id/version contra borrado durante una descarga.
    bool pin(uint16_t block, uint8_t firstPage, uint32_t id, uint8_t formatVersion);
    // Libera la proteccion de descarga; sin parametros.
    void unpin();
private:
    struct slice { uint64_t firstUs; uint32_t sequence, flags; uint32_t payload[336]; };
    union workerBuffer { slice sample; alignas(4) uint8_t page[2048]; };
    struct slot {
        uint32_t* words = nullptr;
        alignas(4) waveEventFormat::pageHeader header = {};
        uint32_t crcState = ~0U;
        uint16_t dataBytes = 0;
        uint32_t previousSequence = 0;
        uint8_t slices = 0;
        uint64_t order = 0;
        bool queued = false;
    };
    static_assert(sizeof(slice) % 4 == 0, "Wave slices require word-aligned copies");
    struct pendingCapture {
        bool used = false, complete = false, failed = false;
        uint16_t block = 0, slotIndex = 0;
        uint8_t firstPage = 0;
        uint64_t firstUs = 0, deadlineUs = 0;
        waveEventFormat::commitHeader commit = {};
    };
    struct command {
        uint32_t eventId = 0;
        uint64_t startUs = 0, endUs = 0, rootUs = 0, unixUs = 0;
    };
    NandPartition& partition;
    TaskHandle_t task = nullptr;
    portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;
    slice* acquisition = nullptr;
    uint32_t published = 0, consumed = 0;
    slot slots[288 / (waveEventFormat::pageUs / 1000) + 3];
    uint8_t slotCount = 0, preSlots = 0, currentSlot = 0, packetIndex = 0;
    uint64_t slotOrder = 0;
    uint32_t ids[640] = {};
    uint8_t slotState[640] = {}, slotVersion[640] = {};
    uint8_t committedSlots[80] = {};
    uint8_t blockState[320] = {}; // 0: requiere borrar; 1: borrado; 2: reservado; 3: fallo de borrado.
    uint16_t blockCount = 0, writeCursor = 0, pinnedBlock = UINT16_MAX;
    pendingCapture pending[8];
    int8_t active = -1;
    waveStoreMetrics metrics;
    waveStoreMetrics snapshot;
    struct indexQuery {
        waveIndexSnapshot result;
        uint64_t unixUs = 0;
        uint32_t eventId = 0, completedMs = 0;
        uint16_t cursor = 0;
        bool delivered = false;
        waveIndexStatus status = waveIndexStatus::empty;
    };
    indexQuery indexQueries[2];
    std::atomic<uint32_t> triggerDrops{0};
    std::atomic<uint32_t> nvsHoldUntilMs{0};
    std::atomic<bool> acquisitionActive{false};
    std::atomic<bool> saveEnabled{true};
    std::atomic<uint8_t> wavePointMask{7};
    std::atomic<bool> paused{true}, workerPaused{true};
    QueueHandle_t commands = nullptr, ioQueue = nullptr, readQueue = nullptr;
    StaticQueue_t commandControl, ioControl, readControl;
    uint8_t commandData[16 * sizeof(command)], ioData[sizeof(NandIoResult)], readData[sizeof(NandIoResult)];
    // Ejecuta el guardador recibido como parameter desde FreeRTOS.
    static void taskEntry(void* parameter);
    // Atiende comandos, muestras, escrituras y reserva de bloques; sin parametros.
    void run();
    // Espera exclusivamente queue luego de un envio aceptado; conserva el buffer hasta completar.
    bool waitIo(QueueHandle_t queue);
    // Lee length bytes iniciales de block/page hacia data; queue identifica la finalizacion exclusiva.
    bool read(uint16_t block, uint8_t page, void* data, size_t length, QueueHandle_t queue);
    // Abre una captura provisional en triggerUs si hay destino borrado y referencia libre.
    void startCapture(uint64_t triggerUs);
    // Agrega value al paquete actual; detecta saltos de secuencia sin inventar muestras.
    void appendSlice(const slice& value);
    // Escribe index usando pageBuffer (2048 bytes DRAM) y devuelve el slot al terminar.
    void writeSlot(uint8_t index, uint8_t* pageBuffer);
    // Obtiene un slot libre o el historial mas viejo; nunca devuelve datos en escritura.
    int8_t freeSlot();
    // Completa cierres vencidos; nowUs es el reloj y pageBuffer la pagina DRAM reutilizable.
    void sealPending(uint64_t nowUs, uint8_t* pageBuffer);
    // Vincula item a las Wave pendientes cuyo tramo capturado coincide con la ventana FAST.
    void linkEvent(const command& item);
    // Borra como maximo un bloque y mantiene cuatro destinos preparados; sin parametros.
    void reserveBlocks();
    // Procesa una sola cabecera pendiente fuera del callback HTTP; sin parametros.
    void serviceIndex();
};

// Publica datos del ADE al guardador montado; payload/firstUs/flags describen 16 ms.
void waveCapturePublish(const uint8_t* payload, uint64_t firstUs, uint16_t flags);
// Inicia una captura candidata en firstUs; no espera ni reserva memoria.
void waveCaptureTrigger(uint64_t firstUs);
// Indica si la adquisicion Wave necesita evitar escrituras en la flash interna; sin parametros.
bool waveCaptureBusy();
// Vincula un FAST ya persistido con sus Wave; parametros identifican evento, ventana y relojes.
void waveCaptureConfirm(uint32_t eventId, uint64_t startUs, uint64_t endUs, uint64_t rootUs, uint64_t unixUs, uint8_t eventMask = 1);
