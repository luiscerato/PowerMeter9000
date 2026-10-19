#include "meter_timing.h"
#include <freertos/FreeRTOS.h>

struct meterTimingMetric {
    uint64_t totalUs = 0;
    uint32_t n = 0, maxUs = 0, over3 = 0, over10 = 0;
};
struct meterTimingSlow {
    uint32_t atMs = 0, elapsedUs = 0;
    meterTimingPart part = meterTimingPart::wait;
};
static const char* const timingNames[] = {"wait", "wakeLate", "cycle", "irq0", "irqDelay",
    "wave", "waveSpi", "waveScale", "wavePack", "waveStore", "waveWeb", "fast",
    "slowRms", "thd", "energy", "calibration", "bl", "commit", "events", "commands", "irq1", "grid", "gridNvs"};
static constexpr size_t timingCount = size_t(meterTimingPart::count);
static_assert(sizeof(timingNames) / sizeof(*timingNames) == timingCount, "Timing names mismatch");
static meterTimingMetric timingMetrics[timingCount];
static meterTimingSlow timingSlow[8];
static uint8_t timingSlowNext = 0, timingSlowCount = 0;
static portMUX_TYPE timingMutex = portMUX_INITIALIZER_UNLOCKED;

// Registra una duracion en RAM; part es el bloque y elapsedUs incluye suspensiones dentro del mismo.
void meterTimingRecord(meterTimingPart part, uint32_t elapsedUs)
{
    const size_t index = size_t(part);
    if (index >= timingCount) return;
    const uint32_t atMs = millis();
    portENTER_CRITICAL(&timingMutex);
    meterTimingMetric& metric = timingMetrics[index];
    ++metric.n;
    metric.totalUs += elapsedUs;
    if (elapsedUs > metric.maxUs) metric.maxUs = elapsedUs;
    if (elapsedUs >= 3000) ++metric.over3;
    if (elapsedUs >= 10000) ++metric.over10;
    const bool parent = part == meterTimingPart::cycle || part == meterTimingPart::irq0 ||
        part == meterTimingPart::wave || part == meterTimingPart::commit || part == meterTimingPart::grid;
    if (elapsedUs >= 5000 && !parent && part != meterTimingPart::wait && part != meterTimingPart::irqDelay) {
        meterTimingSlow& slow = timingSlow[timingSlowNext];
        slow.atMs = atMs; slow.elapsedUs = elapsedUs; slow.part = part;
        timingSlowNext = (timingSlowNext + 1) % 8;
        if (timingSlowCount < 8) ++timingSlowCount;
    }
    portEXIT_CRITICAL(&timingMutex);
}

// Devuelve una instantanea consistente; json recibe tiempos en us y marcas de uptime en ms.
void meterTimingGetJson(String& json)
{
    meterTimingMetric metrics[timingCount];
    meterTimingSlow slow[8];
    uint8_t next, count;
    portENTER_CRITICAL(&timingMutex);
    memcpy(metrics, timingMetrics, sizeof(metrics));
    memcpy(slow, timingSlow, sizeof(slow));
    next = timingSlowNext; count = timingSlowCount;
    portEXIT_CRITICAL(&timingMutex);
    json.reserve(2400);
    json = "{\"unit\":\"us\",\"uptime\":" + String(millis()) + ",\"parts\":{";
    bool separator = false;
    char item[144];
    for (size_t index = 0; index < timingCount; ++index) {
        const meterTimingMetric& metric = metrics[index];
        if (!metric.n) continue;
        snprintf(item, sizeof(item), "%s\"%s\":{\"n\":%lu,\"avg\":%lu,\"max\":%lu,\"gt3\":%lu,\"gt10\":%lu}",
            separator ? "," : "", timingNames[index], (unsigned long)metric.n,
            (unsigned long)(metric.totalUs / metric.n), (unsigned long)metric.maxUs,
            (unsigned long)metric.over3, (unsigned long)metric.over10);
        json += item; separator = true;
    }
    json += "},\"slow\":[";
    for (uint8_t index = 0; index < count; ++index) {
        const meterTimingSlow& entry = slow[(next + 8 - count + index) % 8];
        snprintf(item, sizeof(item), "%s{\"part\":\"%s\",\"at\":%lu,\"us\":%lu}", index ? "," : "",
            timingNames[size_t(entry.part)], (unsigned long)entry.atMs, (unsigned long)entry.elapsedUs);
        json += item;
    }
    json += "]}";
}
