const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const vm = require('node:vm');
const {mergeEventHistorySamples, groupEventHistoryPreviewSections} = require('../data/event_history_preview.js');

test('agrupa hasta 15 segundos exactos y separa una pausa de cinco minutos', () => {
    const segment = (start, end) => ({timestamp:1000, samples:[{relativeMs:start, vr:220}, {relativeMs:end, vr:220}]});
    assert.deepEqual(groupEventHistoryPreviewSections([segment(0,1000),segment(16000,17000),segment(300000,301000)],1000),
        [{start:0,end:17000},{start:300000,end:301000}]);
    assert.equal(groupEventHistoryPreviewSections([segment(0,1000),segment(16000.1,17000)],1000).length,2);
});

test('ventanas solapadas no crean botones extra y segmentos sin datos se ignoran', () => {
    const segments = [{timestamp:1000,samples:[{relativeMs:0,vr:220},{relativeMs:1000,vr:220}]},
        {timestamp:1000,samples:[{relativeMs:500,vr:220},{relativeMs:1200,vr:220}]},
        {timestamp:1000,samples:[{relativeMs:500000,vr:null}]}];
    assert.deepEqual(groupEventHistoryPreviewSections(segments,1000),[{start:0,end:1200}]);
    assert.deepEqual(groupEventHistoryPreviewSections([],1000),[]);
});

test('une FAST separados en tiempo real sin conectar el intervalo sin muestras', () => {
    const origin = 1700000000000;
    const result = mergeEventHistorySamples([
        {timestamp: origin + 30000, samples: [{relativeMs: -10, vr: 220}, {relativeMs: 0, vr: 221}]},
        {timestamp: origin, samples: [{relativeMs: -10, vr: 0}, {relativeMs: 0, vr: 1}]}
    ], origin, 30);
    assert.deepEqual(result.map(point => point.relativeMs), [-10, 0, 14995, 29990, 30000]);
    assert.equal(result[2].vr, undefined); assert.equal(result[3].vr, 220);
});

test('Wave conserva 125 us, elimina solapamientos y no reemplaza datos validos por null', () => {
    const result = mergeEventHistorySamples([
        {timestamp: 1000, samples: [{relativeMs: 0, vr: 10}, {relativeMs: 0.125, vr: 20}]},
        {timestamp: 1000, samples: [{relativeMs: 0.125, vr: null}, {relativeMs: 0.25, vr: 30}]},
        {timestamp: 1001, samples: [{relativeMs: 0, vr: 40}]}
    ], 1000, 0.25);
    assert.deepEqual(result.map(point => point.relativeMs), [0, 0.125, 0.25, 0.625, 1]);
    assert.equal(result[1].vr, 20); assert.equal(result[3].vr, undefined);
});

test('mantiene NaN como ausencia de datos y soporta segmentos vacios', () => {
    const result = mergeEventHistorySamples([{timestamp: 1000, samples: []},
        {timestamp: 1000, samples: [{relativeMs: 0, vn: NaN}]}], 1000, 30);
    assert.equal(result[0].vn, null);
    assert.deepEqual(mergeEventHistorySamples([], 1000, 30), []);
});

test('reune marcas de todos los sujetos y deduplica registros compartidos', () => {
    const context = vm.createContext({gridEventRecords: event => event.records});
    vm.runInContext(fs.readFileSync('data/event_history_preview.js', 'utf8'), context);
    const state = {subject: 0, previous: 1, current: 4};
    const events = [{timestamp: 1000, records: [{offsetMs: 0, states: state}]},
        {timestamp: 2000, records: [{offsetMs: -1000, states: state},
            {offsetMs: 0, states: {subject: 1, isPoint: true, pointType: 1}, previousDurationMs: 50}]}];
    const markers = context.collectEventHistoryMarkers(events, 1000);
    assert.equal(markers.length, 2); assert.equal(markers[0].at, 0);
    assert.equal(markers[1].at, 1000); assert.equal(markers[1].durationMs, 50);
});
