#include "memory_diagnostics.h"
#include "diagnostic_logger.h"
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <rom/ets_sys.h>
#include <cstring>

struct memoryFailure {
    size_t requested = 0;
    uint32_t caps = 0;
    size_t freeBytes = 0;
    size_t largestBlock = 0;
    size_t free8Bit = 0;
    size_t largest8Bit = 0;
    size_t minimum8Bit = 0;
    int64_t uptimeUs = 0;
    int core = 0;
    char task[configMAX_TASK_NAME_LEN] = {};
    char function[32] = {};
};

static portMUX_TYPE memoryMutex = portMUX_INITIALIZER_UNLOCKED;
static memoryFailure lastFailure;
static uint32_t failureCount = 0;

// Captura el entorno sin asignar memoria; size/caps/function describen la reserva fallida.
static void memoryAllocationFailed(size_t size, uint32_t caps, const char* function)
{
    // El SDK prueba PSRAM antes de usar RAM interna; sin ese chip no es un fallo final.
    if ((caps & MALLOC_CAP_SPIRAM) && heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) return;
    memoryFailure failure;
    failure.requested = size;
    failure.caps = caps;
    failure.freeBytes = heap_caps_get_free_size(caps);
    failure.largestBlock = heap_caps_get_largest_free_block(caps);
    failure.free8Bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    failure.largest8Bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    failure.minimum8Bit = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    failure.uptimeUs = esp_timer_get_time();
    failure.core = xPortGetCoreID();
    strncpy(failure.task, pcTaskGetName(nullptr), sizeof(failure.task) - 1);
    strncpy(failure.function, function ? function : "?", sizeof(failure.function) - 1);
    portENTER_CRITICAL(&memoryMutex);
    lastFailure = failure;
    ++failureCount;
    portEXIT_CRITICAL(&memoryMutex);
}

// Instala el callback temprano para registrar fallos incluso durante la inicializacion.
void memoryDiagnosticsBegin()
{
    if (heap_caps_register_failed_alloc_callback(memoryAllocationFailed) != ESP_OK)
        ets_printf("[memory] allocation callback unavailable\n");
}

// Copia el ultimo fallo al logger sin usarlo desde el callback de asignacion.
void memoryDiagnosticsPoll()
{
    static uint32_t reported = 0;
    static int64_t nextReportUs = 0;
    const int64_t nowUs = esp_timer_get_time();
    if (nowUs < nextReportUs) return;
    memoryFailure failure;
    portENTER_CRITICAL(&memoryMutex);
    const uint32_t count = failureCount;
    if (count != reported) failure = lastFailure;
    portEXIT_CRITICAL(&memoryMutex);
    if (count == reported) return;
    reported = count;
    nextReportUs = nowUs + 1000000;
    // La salida se hace fuera del callback: algunos fallos ocurren con interrupciones restringidas.
    ets_printf("[alloc-fail] count=%u us=%llu size=%u caps=0x%x free=%u largest=%u free8=%u largest8=%u min8=%u task=%s core=%d fn=%s\n",
        unsigned(count), static_cast<unsigned long long>(failure.uptimeUs), unsigned(failure.requested),
        unsigned(failure.caps), unsigned(failure.freeBytes), unsigned(failure.largestBlock),
        unsigned(failure.free8Bit), unsigned(failure.largest8Bit), unsigned(failure.minimum8Bit),
        failure.task, failure.core, failure.function);
    DIAG_ERROR(DiagnosticModule::main,
        "alloc-fail #%lu us=%llu size=%u caps=%x free=%u largest=%u free8=%u largest8=%u min8=%u task=%s core=%d fn=%s",
        static_cast<unsigned long>(count), static_cast<unsigned long long>(failure.uptimeUs),
        unsigned(failure.requested), unsigned(failure.caps), unsigned(failure.freeBytes),
        unsigned(failure.largestBlock), unsigned(failure.free8Bit), unsigned(failure.largest8Bit),
        unsigned(failure.minimum8Bit), failure.task, failure.core, failure.function);
}

// Informa el estado y revisa integridad; context nombra la operacion y size sus bytes previstos.
void memoryReportState(const char* context, size_t size)
{
    const unsigned freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const unsigned largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const bool intact = heap_caps_check_integrity_all(false);
    ets_printf("[memory] %s size=%u free8=%u largest8=%u intact=%d\n",
        context, unsigned(size), freeBytes, largest, int(intact));
    DIAG_WARN(DiagnosticModule::main, "%s size=%u free8=%u largest8=%u intact=%d",
        context, unsigned(size), freeBytes, largest, int(intact));
}

// Comprueba bytes libres y contiguos; size es la reserva prevista y margin el minimo restante.
bool memoryHasRoom(size_t size, size_t margin)
{
    const size_t freeBytes = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    return freeBytes >= margin && size <= freeBytes - margin &&
        size <= heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}
