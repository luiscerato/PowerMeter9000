const assert = require('node:assert/strict');
const {codec, gridFixture} = require('./grid_fixture.cjs');
const {mergeEventHistorySamples} = require('../data/event_history_preview.js');

const bytes = gridFixture(), view = new DataView(bytes.buffer);
view.setInt32(22, 0, true);
for (const [offset, timestamp] of [[234, 65500], [250, 109], [2202, 165], [4250, 280]])
    view.setUint16(offset, timestamp, true);
view.setUint16(28, codec.gridCrc16(bytes, 0, 28), true);
view.setUint16(266, codec.gridCrc16(bytes, 30, 236), true);
for (const page of [2048, 4096])
    view.setUint16(page + 8, codec.gridCrc16(bytes, page, 170, page + 8), true);
const event = codec.decodeGridEvent(bytes.buffer);
assert.equal(event.partial, false);
assert.equal(event.timingEstimated, false);
const expected = [0, 14.5, 20.1, 31.6];
event.samples.forEach((sample, index) => assert.ok(Math.abs(sample.relativeMs - expected[index]) < 1e-9));
const merged = mergeEventHistorySamples([{timestamp: event.timestamp, samples: event.samples}], event.timestamp, 30);
assert.deepEqual(merged.map(sample => sample.relativeMs), expected);
console.log('PASS: FAST conserva tiempos originales, overflow de 100 us y limites de pagina sin forzar una grilla de 10 ms.');

const visual = codec.gridFastDisplaySamples(event);
assert.deepEqual(Array.from(visual, sample => sample.relativeMs), [0, 10, 20, 30]);
assert.equal(event.samples[1].relativeMs, 14.5, 'no modifica los datos decodificados ni la cache');
event.snapshot.frequency[0] = 60;
const sixty = codec.gridFastDisplaySamples(event);
sixty.filter(sample => Number.isFinite(sample.vr)).forEach(sample =>
    assert.ok(Math.abs(sample.relativeMs / (1000 / 120) - Math.round(sample.relativeMs / (1000 / 120))) < 1e-9));
event.snapshot = null;
assert.equal(codec.gridFastDisplaySamples(event)[1].relativeMs, 10);

const wave = codec.gridRegularSamples([
    {relativeMs: -192.032, vr: 1}, {relativeMs: -191.907, vr: 2},
    {relativeMs: -191.774, vr: 3}, {relativeMs: -191.649, vr: 4},
    {relativeMs: -191.524, vr: null}, {relativeMs: -140.01, vr: 5},
    {relativeMs: -139.88, vr: 6}
], 0.125);
assert.equal(wave[0].relativeMs, -192.032, 'respeta la referencia de sincronismo');
assert.equal(wave[4].vr, null, 'conserva el corte de la linea');
assert.equal(wave[5].relativeMs, -140.01, 'un nuevo tramo conserva su referencia');
for (const [a, b] of [[0, 1], [1, 2], [2, 3], [5, 6]])
    assert.ok(Math.abs(wave[b].relativeMs - wave[a].relativeMs - 0.125) < 1e-9);
const lost = codec.gridRegularSamples([{relativeMs: 0, vr: 1}, {relativeMs: 50, vr: 2}, {relativeMs: 64, vr: 3}], 10);
assert.deepEqual(Array.from(lost, sample => sample.relativeMs), [0, 50, 60]);
assert.equal(codec.gridRegularSamples([], 10).length, 0);
console.log('PASS: visor nominal FAST 50/60 Hz y Wave 8 kHz, referencias compartidas, huecos y datos originales intactos.');

const drift = {snapshot: {frequency: [50]}, states: {}, transitions: [{offsetMs: 2483, states: {}}],
    samples: Array.from({length: 400}, (_, index) => ({relativeMs: -1155.12 + index * 9.85, vr: index}))};
drift.samples[117].relativeMs = -0.02;
drift.samples[369].relativeMs = 2483.58;
const original = JSON.stringify(drift), aligned = codec.gridFastDisplaySamples(drift);
assert.ok(aligned.every(sample => Number.isFinite(sample.vr)),'cambiar de ancla no crea una muestra ausente');
assert.equal(aligned.find(sample => sample.vr === 117).relativeMs, 0);
assert.equal(aligned.find(sample => sample.vr === 369).relativeMs, 2483);
assert.equal(JSON.stringify(drift), original);
for (const anchor of [0, 2483]) {
    const nearby = aligned.filter(sample => Number.isFinite(sample.vr) && Math.abs(sample.relativeMs - anchor) < 100);
    nearby.forEach(sample => assert.ok(Math.abs((sample.relativeMs - anchor) / 10 - Math.round((sample.relativeMs - anchor) / 10)) < 1e-9));
}
for (let index = 1; index < aligned.length; ++index)
    assert.ok(aligned[index].relativeMs >= aligned[index - 1].relativeMs, 'el eje no retrocede al resincronizar');

const duplicate = {states: {}, samples: [{relativeMs: 0, vr: 100}, {relativeMs: 8, vr: 110}, {relativeMs: 10.1, vr: 120}, {relativeMs: 20, vr: 130}]};
const chosen = codec.gridFastDisplaySamples(duplicate).filter(sample => Number.isFinite(sample.vr));
assert.deepEqual(Array.from(chosen, sample => [sample.relativeMs, sample.vr]), [[0, 100], [10, 120], [20, 130]]);
assert.equal(duplicate.samples.length, 4, 'ninguna lectura se borra del original');
const gap = codec.gridFastDisplaySamples({states: {}, samples: [{relativeMs: 0, vr: 100}, {relativeMs: 10, vr: null}, {relativeMs: 20, vr: 120}]});
assert.ok(gap.some(sample => !Number.isFinite(sample.vr)), 'no conecta un separador explicito');
const neutralMissing = codec.gridFastDisplaySamples({states:{isPoint:true},previousDurationMs:170,samples:Array.from({length:40},(_,index)=>({relativeMs:-100+index*10,vr:230,vs:231,vt:229,vn:index===18?null:2}))});
assert.ok(neutralMissing.every(sample=>Number.isFinite(sample.vr)),'un dato BL ausente no corta los canales ADE');
assert.equal(neutralMissing.filter(sample=>sample.vn===null).length,1,'solo el neutro conserva el dato ausente');
const startupGap = codec.gridFastDisplaySamples({states:{},samples:[{relativeMs:-10,vr:230},{relativeMs:0,vr:230},{relativeMs:330,vr:230},{relativeMs:340,vr:230}]});
assert.ok(startupGap.some(sample=>!Number.isFinite(sample.vr)),'no oculta un salto real de 330 ms');
console.log('PASS: cada transicion vuelve a su referencia, grillas nominales sin deriva acumulada ni retrocesos.');
