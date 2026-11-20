const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const timers = new Map();
let timerId = 0, requests = 0, complete;
const context = vm.createContext({console, AbortController,
    document: {getElementById: () => null}, board: {IP: 'http://meter'},
    setTimeout: callback => { const id = ++timerId; timers.set(id, callback); return id; },
    clearTimeout: id => timers.delete(id),
    fetch: () => { ++requests; return new Promise(resolve => { complete = resolve; }); }});
vm.runInContext(fs.readFileSync('data/scope.js', 'utf8') + fs.readFileSync('data/scope_fft.js', 'utf8') +
    '\nglobalThis.quality = powerQuality; globalThis.scope = Scope;', context);
const quality = context.quality, scope = context.scope;
quality.renderMeter = quality.renderAnalysis = () => {};

// Prueba consultas serializadas, vigencia y pausa sin clientes; sin parametros.
async function verifyPolling() {
    quality.active = true;
    scope.shouldReconnect = true;
    const pending = quality.pollMeter();
    await quality.pollMeter();
    assert.equal(requests, 1, 'no superponer consultas de referencia');
    complete({ok: true, json: async () => ({vrms: {r: 230}, freq: 50})});
    await pending;
    assert.equal(quality.getMeter().vrms.r, 230);
    quality.meterAt -= 7000;
    assert.equal(quality.getMeter(), null, 'no mostrar referencias antiguas');
    scope.shouldReconnect = false;
    await quality.pollMeter();
    assert.equal(requests, 1, 'Detener suspende HTTP');
    quality.active = false;
    scope.shouldReconnect = true;
    await quality.pollMeter();
    assert.equal(requests, 1, 'salir de la vista suspende HTTP');
    const next = quality.pollMeter();
    await next;
    quality.active = quality.quality = true;
    const withThd = quality.pollMeter();
    complete({ok: true, json: async () => ({vrms: {r: 229}, freq: 50})});
    await new Promise(setImmediate);
    assert.equal(requests, 3, 'RMS y luego THD, sin consultas simultaneas');
    complete({ok: false, status: 503});
    await withThd;
    assert.equal(quality.getMeter().vrms.r, 229, 'un fallo THD no borra la referencia RMS');
    assert.equal(quality.getMeter().voltageTHD, undefined, 'no reutilizar una THD anterior');
    quality.active = false;
    console.log('PASS: referencia ADE secuencial, lectura vencida y pausa al detener o salir.');
}
verifyPolling().catch(error => { console.error(error); process.exitCode = 1; });
