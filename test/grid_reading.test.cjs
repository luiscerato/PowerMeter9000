const assert = require('node:assert/strict');
const {codec} = require('./grid_fixture.cjs');

// Construye un registro de estado; subject/previous/current definen la transicion y offsetMs su tiempo.
function record(subject, previous, current, offsetMs, previousDurationMs = 0, currentMagnitude = false) {
    return {offsetMs, previousDurationMs, states: codec.decodeGridStates(subject | previous << 3 | current << 6 | (currentMagnitude ? 1024 : 0)), previousVoltage: 0, currentVoltage: 220};
}

// Convierte registros en una captura minima; records conserva cabecera primero y transiciones despues.
function capture(records) {
    return {...records[0], id: 11, transitions: records.slice(1)};
}

const event = capture([
    record(0, 1, 4, 0), record(1, 1, 4, 0), record(2, 1, 4, 0), record(4, 1, 3, 0),
    record(0, 4, 1, 3518, 3520), record(1, 4, 1, 3518, 3520), record(2, 4, 1, 3518, 3520), record(4, 3, 1, 3518, 3520)
]);
const groups = codec.buildGridOverview(event);
assert.equal(groups.length, 4);
for (const group of groups) {
    assert.equal(group.returns.length, 1);
    assert.equal(group.returns[0].previousDurationMs, 3520);
    assert.equal(group.discontinuous, false);
}
const open = codec.buildGridOverview(capture([record(0, 1, 4, 0)]))[0];
assert.equal(open.returns.length, 0);
assert.equal(open.states[0].states.current, 4);
const returnOnly = codec.buildGridOverview(capture([record(0, 4, 1, 0, 99000)]))[0];
assert.equal(returnOnly.states.length, 1);
assert.equal(returnOnly.returns[0].previousDurationMs, 99000);
assert.equal(returnOnly.states[0].states.previous, 4);
const missing = codec.buildGridOverview(capture([record(0, 1, 4, 0), record(0, 2, 1, 1000)]))[0];
assert.equal(missing.discontinuous, true);
const mixed = codec.buildGridOverview(capture([record(3, 1, 3, 0), record(3, 1, 5, 0, 0, true)]));
assert.equal(mixed.length, 2);
assert.equal(mixed[0].name, 'Neutro-tierra');
assert.equal(mixed[1].name, 'Neutro');
const point = {...record(0, 0, 0, -20, 150), states: codec.decodeGridStates(512 | 1 << 10)};
const withPoint = codec.buildGridOverview(capture([record(0, 1, 4, 0), point]))[0];
assert.equal(withPoint.states.length, 1);
assert.equal(withPoint.points.length, 1);
assert.equal(withPoint.returns.length, 0);
const records = codec.gridEventRecords(event), markers = codec.gridRecordMarkers(records, 6, -1100, 4700);
assert.equal(markers.length, 2);
assert.equal(markers[0].label, '#1\u20134');
assert.equal(markers[1].label, '#6');
assert.equal(markers[1].thickness, 2);
assert.equal(codec.gridRecordMarkers(codec.gridEventRecords(capture([point, record(1, 1, 4, 100)])), null, -10, 20).length, 1);
assert.equal(event.transitions[0].number, undefined);
assert.deepEqual({...codec.gridSelectionRange(codec.gridEventRecords(event),-1100,4700)},{minimum:-50,maximum:3568});
assert.deepEqual({...codec.gridSelectionRange([point],-1100,4700)},{minimum:-70,maximum:180});
assert.deepEqual({...codec.gridSelectionRange([record(0,1,4,4600)],-1100,4700)},{minimum:4550,maximum:4650});
assert.equal(codec.gridSelectionRange([record(0,1,4,6000)],-1100,4700),null);
assert.equal(codec.gridSelectionRange([],0,1000),null);
assert.equal(codec.gridSelectionRange([point],10,10),null);
const paired = codec.gridEventRecords(event);
assert.deepEqual({...codec.gridSelectionRange([paired[0]],-1100,4700,paired)},{minimum:-50,maximum:3568});
assert.deepEqual({...codec.gridSelectionRange([paired[4]],-1100,4700,paired)},{minimum:-50,maximum:3568});
assert.deepEqual({...codec.gridSelectionRange([record(0,4,1,0,99000)],-1100,4700)},{minimum:-1100,maximum:50});
const evolving = codec.gridEventRecords(capture([record(0,1,2,0),record(0,2,4,2000),record(0,4,1,3500)]));
assert.deepEqual({...codec.gridSelectionRange([evolving[0]],-1100,4700,evolving)},{minimum:-50,maximum:2050});
assert.deepEqual({...codec.gridSelectionRange([evolving[1]],-1100,4700,evolving)},{minimum:1950,maximum:3550});
console.log('PASS: resumen por sujeto/magnitud, retornos guardados, secuencias incompletas, puntuales y marcas simultaneas.');
