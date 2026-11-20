#include "cpu_usage.h"
#include <esp_attr.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_freertos_hooks.h>

static constexpr uint32_t cpuWindowSamples = 2U * configTICK_RATE_HZ;

struct cpuSampleWindow {
    uint32_t samples = 0, busy = 0;

    // Cuenta active y publica result al completar dos segundos de ticks; no realiza reservas.
    bool IRAM_ATTR record(bool active, int16_t& result) {
        ++samples;
        if (active) ++busy;
        if (samples < cpuWindowSamples) return false;
        result = int16_t((busy * 1000U + cpuWindowSamples / 2U) / cpuWindowSamples);
        samples = busy = 0;
        return true;
    }
};

static cpuSampleWindow cpuWindows[2];
static TaskHandle_t cpuIdleTasks[2] = {};
static int16_t cpuLoads[2] = {-1, -1};
static portMUX_TYPE cpuUsageMux = portMUX_INITIALIZER_UNLOCKED;

// Muestrea la tarea interrumpida por el tick local; sin parametros, solo contadores en RAM/IRAM.
static void IRAM_ATTR cpuUsageTick() {
    const uint32_t core = xPortGetCoreID();
    int16_t result;
    if (!cpuWindows[core].record(xTaskGetCurrentTaskHandle() != cpuIdleTasks[core], result)) return;
    portENTER_CRITICAL_ISR(&cpuUsageMux);
    cpuLoads[core] = result;
    portEXIT_CRITICAL_ISR(&cpuUsageMux);
}

// Inicia callbacks en los nucleos disponibles; sin parametros, un fallo deja su resultado en -1.
void cpuUsageBegin() {
    static bool started = false;
    if (started) return;
    started = true;
    for (uint32_t core = 0; core < portNUM_PROCESSORS && core < 2; ++core) {
        cpuIdleTasks[core] = xTaskGetIdleTaskHandleForCPU(core);
        if (cpuIdleTasks[core]) esp_register_freertos_tick_hook_for_cpu(cpuUsageTick, core);
    }
}

// Lee la ultima ventana completa sin reiniciar contadores; values recibe C0 y C1 en decimas porcentuales.
void cpuUsageRead(int16_t values[2]) {
    portENTER_CRITICAL(&cpuUsageMux);
    values[0] = cpuLoads[0]; values[1] = cpuLoads[1];
    portEXIT_CRITICAL(&cpuUsageMux);
}
