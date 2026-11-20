const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const sent = [];
const context = vm.createContext({console, WebSocket: {OPEN: 1}, document: {getElementById: () => null},
    localStorage: {getItem: () => '{"current":0,"voltage":0}', setItem() {}}});
for (const file of ['scope.js', 'scope_fft.js']) vm.runInContext(fs.readFileSync('data/' + file, 'utf8'), context);
const scope = vm.runInContext('Scope', context), fft = vm.runInContext('ScopeFft', context);
const quality = vm.runInContext('powerQuality', context);
scope.view = 'quality'; scope.Data.configSynced = true;
scope.websocket = {readyState: 1, send: message => sent.push(message)};
fft.loadScales();
assert.equal(fft.currentDiv, 1); assert.equal(fft.voltageDiv, 100);
for (const division of [0.01, 0.1, 1, 2, 5, 10, 20, 50, 100]) {
    fft.setScale('current', division);
    const [command, voltage, current, mask] = sent.at(-1).split(',');
    assert.equal(command, 'quality_set'); assert.equal(Number(mask), 127);
    assert.ok(Math.abs(Number(current) - division * 5 * Math.SQRT2 * 1.1) < 1e-9);
    assert.ok(Number(current) >= 0.05 && Number(current) <= 1000);
    assert.ok(Math.abs(Number(voltage) - 100 * 5 * Math.SQRT2 * 1.1) < 1e-9);
}
assert.ok(Math.abs(quality.capturePeak(1) - 7.778174593) < 1e-8);
const count = sent.length;
fft.setScale('current', 0); fft.setScale('current', NaN);
assert.equal(sent.length, count, 'no publicar escalas invalidas');
scope.view = 'scope'; fft.setScale('current', 1);
assert.equal(sent.length, count, 'no modificar captura de Calidad desde otra vista');
scope.applyQualityConfig('quality_cfg,1,777.81746,777.81746,127,30');
assert.equal(scope.qualityGeneration, 30);
scope.drawWaves = () => {};
scope.applyRemoteConfig('scope_cfg,0.3797937,0.3797937,5,0,1,1,127');
assert.equal(scope.Data.configSynced, true);
assert.ok(scope.Data.waves[1].gain > 0.2442, 'aceptar el rango ampliado de Calidad');
console.log('PASS: captura automatica de cinco divisiones RMS, pico +10%, escalas validas y rango ampliado.');
