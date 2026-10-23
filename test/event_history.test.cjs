const assert = require('node:assert/strict');
const test = require('node:test');
const {buildEventHistory} = require('../data/event_history.js');
const base = Date.UTC(2026, 9, 21);

// Fabrica una transicion de prueba; subject, previous/current, duration y extra definen su contenido.
function record(subject, previous, current, duration = 0, extra = {}) {
    return {offsetMs: 0, previousDurationMs: duration, root: true, states: {subject, previous, current, ...extra}};
}

// Fabrica una captura en base+offset; id y records identifican sus transiciones y extra sus avisos.
function capture(id, offset, records, extra = {}) {
    return {id, timestamp: base + offset, records, ...extra};
}

// Reconstruye el periodo de prueba y devuelve solo anomalias; captures contiene los resumenes.
function episodes(captures) {
    return buildEventHistory(captures, base, base + 60000).intervals.filter(item => item.point || item.state > 1);
}

test('une inicio y retorno de capturas descargadas de mas reciente a mas antigua', () => {
    const result = episodes([capture(2, 31000, [record(0, 4, 1, 30000)]), capture(1, 1000, [record(0, 1, 4, 500)])]);
    assert.equal(result.length, 1);
    assert.equal(result[0].start, base + 1000);
    assert.equal(result[0].end, base + 31000);
    assert.deepEqual(result[0].ids, [2, 1]);
    assert.equal(result[0].endKnown, true);
});

test('reconstruye el inicio por duracion aunque no se haya descargado su captura', () => {
    const [item] = episodes([capture(2, 10000, [record(4, 3, 1, 20000)])]);
    assert.equal(item.start, base - 10000);
    assert.equal(item.end, base + 10000);
    assert.equal(item.startKnown, true);
});

test('mantiene separadas red, fases, neutro y corriente', () => {
    const result = episodes([capture(1, 1000, [record(4, 1, 3), record(0, 1, 4), record(3, 1, 3), record(0, 1, 5, 0, {isCurrent: true})])]);
    assert.deepEqual(result.map(item => item.lane).sort(), ['0:A', '0:V', '3:V', '4:V']);
    assert.ok(result.every(item => !item.endKnown));
});

test('bajo voltaje que muta a corte conserva ambos tramos sin duplicar el inicio', () => {
    const result = episodes([capture(1, 1000, [record(0, 1, 2)]), capture(2, 3000, [record(0, 2, 4, 2000)]), capture(3, 9000, [record(0, 4, 1, 6000)])]);
    assert.equal(result.length, 2);
    assert.deepEqual(result.map(item => [item.state, item.end - item.start]), [[4, 6000], [2, 2000]]);
});

test('conserva los puntuales y evita contarlos dos veces en capturas superpuestas', () => {
    const point = record(1, 0, 0, 50, {isPoint: true, pointType: 1});
    const result = episodes([capture(1, 1000, [point]), capture(2, 1000, [point])]);
    assert.equal(result.length, 1);
    assert.equal(result[0].end - result[0].start, 50);
    assert.deepEqual(result[0].ids, [2, 1]);
});

test('un reinicio no permite unir un estado anterior con uno posterior', () => {
    const result = episodes([capture(1, 1000, [record(0, 1, 4)]), capture(2, 5000, [record(4, 0, 1)], {startup: true}), capture(3, 10000, [record(0, 4, 1, 9000)])]);
    assert.equal(result.length, 2);
    assert.ok(result.some(item => !item.startKnown));
    assert.ok(result.some(item => !item.endKnown));
});

test('una secuencia incoherente queda parcial en lugar de inventar continuidad', () => {
    const result = episodes([capture(1, 1000, [record(0, 1, 4)]), capture(2, 15000, [record(0, 2, 1, 3000)])]);
    assert.equal(result.length, 2);
    assert.ok(result.some(item => item.state === 4 && !item.endKnown && item.uncertain));
});

test('excluye pruebas y horas estimadas, y conserva los avisos de CRC y registros perdidos', () => {
    const result = buildEventHistory([capture(1, 1000, [record(0, 1, 4)], {estimated: true}),
        capture(2, 2000, [record(0, 1, 4)], {synthetic: true}), capture(3, 3000, [record(0, 1, 4)], {partial: true})], base, base + 60000);
    assert.equal(result.excluded, 2);
    const abnormal = result.intervals.filter(item => item.state > 1);
    assert.equal(abnormal.length, 1);
    assert.equal(abnormal[0].uncertain, true);
});

test('prolonga el estado normal tras el retorno aunque no haya otro evento', () => {
    const result = buildEventHistory([capture(1, 1000, [record(0, 4, 1, 1000)])], base, base + 60000);
    const normal = result.intervals.find(item => item.state === 1);
    assert.equal(normal.start, base + 1000);
    assert.equal(normal.end, base + 60000);
    assert.equal(normal.endKnown, false);
    assert.equal(normal.uncertain, false);
});

test('une la normalidad entre transiciones sin duplicarla', () => {
    const result = buildEventHistory([capture(1, 1000, [record(0, 4, 1)]),
        capture(2, 10000, [record(0, 1, 2, 9000)])], base, base + 60000);
    const normal = result.intervals.filter(item => item.state === 1);
    assert.equal(normal.length, 1);
    assert.equal(normal[0].end, base + 10000);
    assert.equal(normal[0].endKnown, true);
    assert.deepEqual(normal[0].ids, [2, 1]);
});

test('no extiende normalidad ni averias a traves de un reinicio sin evidencia', () => {
    for (const state of [1, 4]) {
        const result = buildEventHistory([capture(1, 1000, [record(0, 0, state)]),
            capture(2, 5000, [record(4, 0, 1)], {startup: true})], base, base + 60000);
        const phase = result.intervals.find(item => item.lane === '0:V');
        assert.equal(phase.end, base + 5000);
        assert.equal(phase.uncertain, true);
    }
});

test('un DIP aislado no demuestra normalidad del resto del periodo', () => {
    const result = buildEventHistory([capture(1, 1000, [record(0, 0, 0, 50, {isPoint: true, pointType: 1})])], base, base + 60000);
    assert.equal(result.intervals.length, 1);
    assert.equal(result.intervals[0].point, 1);
});
