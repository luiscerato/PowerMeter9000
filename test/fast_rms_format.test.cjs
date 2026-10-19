const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const raw = [0, 16383, 1, 8192, 16382, 12345, 42, 16383];
const golden = new Uint8Array(16);
golden[0] = 0x34; golden[1] = 0x12;
raw.forEach((value, channel) => {
    for (let bit = 0; bit < 14; ++bit) {
        const position = channel * 14 + bit;
        golden[2 + Math.floor(position / 8)] |= (value >> bit & 1) << (position % 8);
    }
});
const harness = `
${fs.readFileSync(path.join(root, 'src/fast_rms_format.h'), 'utf8').replace('#pragma once', '')
    .replace(/^(    )(void|uint16_t|float) (setRaw|getRaw|setValue|getValue)\(/gm, '$1constexpr $2 $3(')}
// Valida escritura, sobreescritura y aislamiento de cada canal; sin parametros.
constexpr bool checkPacking() {
    fastRMSData_t sample;
    const uint16_t values[] = {${raw.join(',')}};
    const uint8_t expected[] = {${[...golden.slice(2)].join(',')}};
    sample.timeStamp = 0x1234;
    for (uint8_t channel = 0; channel < 8; ++channel) sample.setRaw(channel, values[channel]);
    for (uint8_t i = 0; i < 14; ++i) if (sample.channels[i] != expected[i]) return false;
    for (uint8_t channel = 0; channel < 8; ++channel) {
        for (uint16_t value : {uint16_t(0), uint16_t(1), uint16_t(255), uint16_t(8191), uint16_t(16382), uint16_t(16383)}) {
            sample.setRaw(channel, value);
            for (uint8_t other = 0; other < 8; ++other)
                if (sample.getRaw(other) != (other == channel ? value : values[other])) return false;
        }
        sample.setRaw(channel, values[channel]);
    }
    sample.setRaw(8, 0);
    return sample.timeStamp == 0x1234 && sample.getRaw(8) == 16383;
}
// Valida escalas, redondeo y valores ausentes/fuera de rango; sin parametros.
constexpr bool checkScaling() {
    fastRMSData_t sample;
    sample.setValue(0, 409.55f);
    sample.setValue(3, 163.82f);
    sample.setValue(7, 3.025f);
    if (sample.getRaw(0) != 16382 || sample.getRaw(3) != 16382 || sample.getRaw(7) != 121) return false;
    sample.setValue(0, 410);
    sample.setValue(3, 164);
    sample.setValue(4, -1);
    sample.setValue(5, std::numeric_limits<float>::infinity());
    sample.setValue(7, std::numeric_limits<float>::quiet_NaN());
    return sample.getRaw(0) == 16383 && sample.getRaw(3) == 16383 && sample.getRaw(4) == 16383 &&
        sample.getRaw(5) == 16383 && std::isnan(sample.getValue(7));
}
static_assert(checkPacking(), "14-bit packing corrupted adjacent channels");
static_assert(checkScaling(), "Invalid scaling or sentinel");
`;
const output = path.join(root, '.pio/build/fast_rms_format_test.cpp');
fs.writeFileSync(output, '#include <initializer_list>\n' + harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-Isrc', '-c', output, '-o', output + '.o'], {cwd: root, stdio: 'inherit'});
const context = vm.createContext({Uint8Array, DataView});
vm.runInContext(fs.readFileSync(path.join(root, 'data/grid_events_ui.js'), 'utf8'), context);
const decoded = context.decodeGridFastSample(new DataView(golden.buffer), 0, true);
assert.equal(decoded.timestamp, 0x1234);
assert.equal(decoded.vr, 0);
assert.equal(decoded.vs, null);
assert.equal(decoded.vt, 0.025);
assert.equal(decoded.ir, 81.92);
assert.equal(decoded.is, 163.82);
assert.equal(decoded.it, 123.45);
assert.equal(decoded.inn, 0.42);
assert.equal(decoded.vn, null);
console.log('PASS: encoder ESP32 y decoder web coinciden en 128 bits; limites, vecinos, redondeo y NaN.');
