const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'src/meter.cpp'), 'utf8');
const first = source.indexOf('void compressWaveBuffer12(');
const last = source.indexOf('float sumVoltages(', first);
if (first < 0 || last < 0) throw new Error('No se encontro el compresor');
// Solo se quita la medicion de tiempo y se inicializan auxiliares para evaluar en constexpr.
const compressor = source.slice(first, last)
    .replace('void compressWaveBuffer12(', 'constexpr void compressWaveBuffer12(')
    .replace('uint32_t time = micros();', '')
    .replace('time = micros() - time;', '')
    .replace('int32_t val1, val2, k = 0, bytes = 0;', 'int32_t val1 = 0, val2 = 0, k = 0, bytes = 0;')
    .replace('uint8_t a, b, c;', 'uint8_t a = 0, b = 0, c = 0;')
    // Evita el rechazo constexpr de GCC 8 al propagar el signo antes de la mascara.
    .replace('val2 & 0x000F', 'uint32_t(val2) & 0x000F');
const header = path.join(root, 'src/event_test_pattern.h').replaceAll('\\', '/');
const test = `
#include "${header}"
${compressor}
// Comprueba los 7 canales y los 6 bloques usando el compresor real; sin parametros.
constexpr bool checkRamp() {
    int32_t raw[128 * 7] = {};
    uint8_t frame[8064] = {};
    for (uint32_t block = 0; block < 6; block++) {
        for (uint32_t part = 0; part < 6; part++) {
            for (uint32_t sample = 0; sample < 128; sample++) {
                int32_t value = eventTestRampValue(block * 768 + part * 128 + sample);
                for (uint32_t channel = 0; channel < 7; channel++) raw[sample * 7 + channel] = value * 65536;
            }
            compressWaveBuffer12(raw, frame, part);
        }
        for (uint32_t valueIndex = 0; valueIndex < 768 * 7; valueIndex++) {
            uint32_t byte = (valueIndex / 2) * 3;
            int32_t decoded = valueIndex % 2 == 0 ?
                frame[byte] | ((frame[byte + 1] & 15) << 8) :
                (frame[byte + 1] >> 4) | (frame[byte + 2] << 4);
            if (decoded >= 2048) decoded -= 4096;
            uint32_t sample = block * 768 + valueIndex / 7;
            int32_t expected = sample <= 2046 ? sample + 1 : 4093 - (int32_t)sample;
            if (decoded != expected) return false;
        }
    }
    return true;
}
static_assert(checkRamp(), "Rampa continua en 4608 muestras por 7 canales");
static_assert(eventTestRampValue(0) == 1, "Inicio");
static_assert(eventTestRampValue(2046) == 2047, "Maximo");
static_assert(eventTestRampValue(2047) == 2046, "Descenso");
static_assert(eventTestRampValue(4607) == -514, "Ultima muestra");
static_assert(eventTestRampValue(6141) == -2048, "Minimo del ciclo extendido");
static_assert(eventTestRampValue(8190) == 1, "Reinicio del ciclo");
`;
const output = path.join(root, '.pio/build/esp32doit-devkit-v1/event_ramp_test.cpp');
fs.writeFileSync(output, test);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: 32256 valores int12 verificados con el compresor real, sin reinicios por bloque.');
