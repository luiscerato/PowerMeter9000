const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/cpu_usage.cpp', 'utf8');
const counter = source.slice(source.indexOf('struct cpuSampleWindow'), source.indexOf('static cpuSampleWindow'))
    .replace('bool IRAM_ATTR record', 'constexpr bool record');
const fixture = `#include <stdint.h>
constexpr uint32_t cpuWindowSamples = 2000;
${counter}
// Verifica idle, ocupacion parcial y total; percent indica el porcentaje esperado por ventana.
constexpr bool checkWindow(unsigned percent) {
    cpuSampleWindow counter;
    int16_t result = -1;
    for (unsigned i = 0; i < 2000; ++i) {
        const bool ready = counter.record(i % 100 < percent, result);
        if (ready != (i == 1999)) return false;
        if (i < 1999 && result != -1) return false;
    }
    return result == percent * 10 && counter.samples == 0 && counter.busy == 0;
}
// Verifica ventanas independientes y lectura estable; sin parametros.
constexpr bool consecutive() {
    cpuSampleWindow counter;
    int16_t result = -1;
    for (unsigned i = 0; i < 2000; ++i) counter.record(true, result);
    for (unsigned i = 0; i < 1999; ++i) {
        if (counter.record(false, result) || result != 1000) return false;
    }
    return counter.record(false, result) && result == 0;
}
static_assert(checkWindow(0), "Idle 100%, CPU 0%");
static_assert(checkWindow(25), "CPU 25%");
static_assert(checkWindow(50), "CPU 50%");
static_assert(checkWindow(100), "CPU 100%");
static_assert(consecutive(), "Resultados independientes cada dos segundos");
`;
const file = '.pio/build/cpu-usage-test.cpp';
fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, fixture);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', file], {stdio: 'inherit'});
assert.doesNotMatch(source, /\b(?:malloc|calloc|realloc|new|Serial|String|delay|vTaskDelay)\b/);
assert.match(source, /esp_register_freertos_tick_hook_for_cpu/);
const read = source.slice(source.indexOf('void cpuUsageRead'));
assert.doesNotMatch(read, /cpuWindows|record\(/, 'consultar status no altera la ventana');
const web = fs.readFileSync('src/webserver.cpp', 'utf8');
const status = web.slice(web.indexOf('void getStatus(String& str)'), web.indexOf('int32_t scanWifiGetJson'));
assert.match(status, /json.objBegin\("cpu"\)/);
assert.match(status, /json.objAppendNull\(key\)/);
assert.match(status, /cpu\[core\] \/ 10.0, 1/);
assert.match(status, /JsonWriter json\(data, sizeof\(data\)\)/);
console.log('PASS: CPU 0/25/50/100%, ventana de 2 s, resultados estables, sin heap, status con null y lectura no destructiva.');
