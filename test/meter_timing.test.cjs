const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const header = fs.readFileSync('src/meter_timing.h', 'utf8');
const source = fs.readFileSync('src/meter_timing.cpp', 'utf8');
const enumeration = header.match(/enum class meterTimingPart[^]*?};/)[0];
const structures = source.slice(source.indexOf('struct meterTimingMetric'), source.indexOf('static const char*'));
const record = source.match(/void meterTimingRecord\([^]*?^}/m)[0]
    .replace('void meterTimingRecord', 'constexpr void meterTimingRecord')
    .replace(/^.*port(?:ENTER|EXIT)_CRITICAL.*$/gm, '');
const harness = `
#include <cstdint>
#include <cstddef>
${enumeration}
${structures}
struct Profiler {
    static constexpr size_t timingCount = size_t(meterTimingPart::count);
    meterTimingMetric timingMetrics[timingCount];
    meterTimingSlow timingSlow[8];
    uint8_t timingSlowNext = 0, timingSlowCount = 0;
    uint32_t clockMs = 123;
    // Devuelve la marca simulada del registro; sin parametros.
    constexpr uint32_t millis() const { return clockMs; }
    ${record}
};
// Comprueba acumuladores, limites y que las esperas normales no llenen la traza lenta; sin parametros.
constexpr bool metrics() {
    Profiler p{};
    p.meterTimingRecord(meterTimingPart::waveSpi, 2999);
    p.meterTimingRecord(meterTimingPart::waveSpi, 3000);
    p.meterTimingRecord(meterTimingPart::waveSpi, 10000);
    const auto& m = p.timingMetrics[size_t(meterTimingPart::waveSpi)];
    if (m.n != 3 || m.totalUs != 15999 || m.maxUs != 10000 || m.over3 != 2 || m.over10 != 1) return false;
    p.meterTimingRecord(meterTimingPart::wait, 20000);
    p.meterTimingRecord(meterTimingPart::count, 50000);
    return p.timingSlowCount == 1 && p.timingSlow[0].atMs == 123;
}
// Verifica anillo de ocho entradas y suma de 64 bits; sin parametros.
constexpr bool wrap() {
    Profiler p{};
    for (int i = 0; i < 10; ++i) { p.clockMs = i; p.meterTimingRecord(meterTimingPart::bl, 5000 + i); }
    if (p.timingSlowCount != 8 || p.timingSlowNext != 2 || p.timingSlow[2].atMs != 2) return false;
    p.meterTimingRecord(meterTimingPart::energy, UINT32_MAX);
    p.meterTimingRecord(meterTimingPart::energy, UINT32_MAX);
    return p.timingMetrics[size_t(meterTimingPart::energy)].totalUs == uint64_t(UINT32_MAX) * 2;
}
static_assert(metrics(), "Contadores y limites de tiempos");
static_assert(wrap(), "Anillo y total sin overflow de 32 bits");
`;
const output = path.join('.pio', 'build', 'meter-timing-test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: registro real de tiempos, umbrales, anillo, indices y acumulacion de 64 bits.');
