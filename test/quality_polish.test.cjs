const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
let nextTimer = 0;
const timers = new Map(), requests = [];
const context = vm.createContext({console, AbortController, document: {getElementById: () => null},
    board: {IP: 'http://meter'},
    setTimeout: (callback, delay) => { timers.set(++nextTimer, {callback, delay}); return nextTimer; },
    clearTimeout: id => timers.delete(id),
    fetch: (_, options) => new Promise(resolve => requests.push({resolve, signal: options.signal}))});
for (const file of ['scope.js', 'scope_fft_vendor.js', 'scope_fft.js'])
    vm.runInContext(fs.readFileSync('data/' + file, 'utf8'), context);
const scope = vm.runInContext('Scope', context), fft = vm.runInContext('ScopeFft', context), quality = vm.runInContext('powerQuality', context);
quality.renderMeter = quality.renderAnalysis = () => {};

for (const frequency of [49.7, 50, 50.3, 59.8, 60.1]) {
    const samples = Float64Array.from({length: 2048}, (_, index) => {
        const phase = 2 * Math.PI * frequency * index / 8000 + 0.7;
        return 15 + Math.SQRT2 * (230 * Math.sin(phase) + 23 * Math.sin(3 * phase) + 11.5 * Math.sin(5 * phase));
    });
    const analysis = fft.analyze(samples, 8000, frequency);
    const result = quality.summarize({samples, analysis, step: 0.01}, 'V');
    const expected = Math.sqrt(230 ** 2 + 23 ** 2 + 11.5 ** 2);
    assert.ok(Math.abs(result.rms - expected) < 0.3, 'RMS usa ciclos completos, no los 256 ms sin recortar');
    assert.equal(result.rms, analysis.rms);
    assert.ok(Math.abs(analysis.windowMs - 1000 * analysis.cycles / frequency) < 0.02);
    assert.ok(Math.abs(result.thd - Math.sqrt(100 + 25)) < 0.1);
    const spectralRms = Math.sqrt(analysis.points.reduce((sum, point) => sum + point.y ** 2, 0));
    assert.ok(Math.abs(spectralRms - result.rms) < 0.02, 'energia espectral y RMS de la misma ventana coinciden');
}
fft.coherent = false;
const constant = fft.analyze(Float64Array.from({length: 2048}, () => 10), 8000);
assert.equal(constant.windowMs, 256); assert.equal(constant.rms, 0);
assert.equal(quality.summarize({samples: new Float64Array(2048), analysis: constant}, 'V').valid, false);
fft.coherent = true;

scope.qualityGeneration = 7;
fft.channels = [{samples: [1], analysis: {}}]; fft.pending = {generation: 7};
scope.applyQualityConfig('quality_cfg,1,400,0.5,2,8');
assert.equal(fft.channels.length, 1, 'el espectro anterior permanece mientras se actualiza el rango');
assert.match(fft.captureNote, /Captura anterior/);
assert.equal(fft.pending, null);
fft.channels = [{samples: [1], analysis: {}}];
const cachedChannels = fft.channels;
fft.pending = {generation: 8};
scope.applyQualityConfig('quality_cfg,1,400,0.5,1,9');
assert.equal(fft.channels, cachedChannels, 'cambiar solo canales conserva las capturas');
assert.equal(fft.pending, null, 'la ventana parcial anterior nunca se mezcla');
scope.applyQualityConfig('quality_cfg,1,300,0.5,1,10');
assert.equal(fft.channels, cachedChannels, 'un rango distinto conserva resultados fisicos ya escalados');
assert.match(fft.captureNote, /nuevo rango/);

// Cambia de vista con HTTP pendiente y termina respuestas fuera de orden; sin parametros.
async function checkViewChanges() {
    quality.active = true; scope.shouldReconnect = true;
    const oldPoll = quality.pollMeter();
    quality.setActive(true, true);
    const newPoll = quality.pollMeter(), controller = quality.controller;
    assert.equal(requests.length, 2);
    assert.equal(requests[0].signal.aborted, true);
    requests[0].resolve({ok: true, json: async () => ({freq: 12})});
    await oldPoll;
    assert.equal(quality.controller, controller, 'el finally viejo no libera la consulta de la vista nueva');
    assert.equal([...timers.values()].filter(timer => timer.delay === 2000).length, 0, 'el finally viejo no agenda otro polling');
    requests[1].resolve({ok: true, json: async () => ({freq: 50})});
    await new Promise(setImmediate);
    assert.equal(requests.length, 3, 'THD se consulta despues de completar la referencia RMS');
    requests[2].resolve({ok: true, json: async () => ({voltageTHD: {r: 2}, currentTHD: {r: 5}})});
    await newPoll;
    assert.equal(quality.getMeter().freq, 50);
    assert.equal(quality.getMeter().voltageTHD.r, 2);
    assert.equal(quality.controller, null);
    assert.equal([...timers.values()].filter(timer => timer.delay === 2000).length, 1);
    quality.setActive(false, false);
    console.log('PASS: RMS y FFT de la misma ventana, energia espectral, cambios de ajustes y consultas antiguas sin interferir.');
}
checkViewChanges().catch(error => { console.error(error); process.exitCode = 1; });
