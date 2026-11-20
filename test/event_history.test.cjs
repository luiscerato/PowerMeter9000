const assert = require('node:assert/strict');
const test = require('node:test');
const {buildEventHistory, filterEventHistoryIntervals} = require('../data/event_history.js');
const base = Date.UTC(2026, 9, 21);

test('selecciona tipos independientes sin confundir cortes de red, corriente y normalidad', () => {
    const source = [{subject: 0, point: 0, state: 1}, {subject: 0, point: 0, state: 2},
        {subject: 0, point: 0, state: 4}, {subject: 4, point: 0, state: 3},
        {subject: 0, point: 0, current: true, state: 5}, {subject: 3, point: 0, state: 3},
        {subject: 0, point: 1}, {subject: 1, point: 2}, {subject: 0, point: 3}];
    assert.deepEqual(filterEventHistoryIntervals(source, 'all', 'all', new Set(['dip', 'pulse'])), [source[6], source[8]]);
    assert.deepEqual(filterEventHistoryIntervals(source, 'all', 'all', new Set(['outage'])), [source[0], source[2], source[3]]);
    assert.deepEqual(filterEventHistoryIntervals(source, 'all', 'all', new Set(['high', 'current'])), [source[0], source[4], source[5]]);
    assert.deepEqual(filterEventHistoryIntervals(source, 'all', '1:V', new Set(['swell'])), [source[7]]);
    assert.deepEqual(filterEventHistoryIntervals(source, 'all', 'all', new Set()), []);
});

test('filtra estados y puntuales por sujeto sin separar corriente de tension ni mutar los tramos', () => {
    const source = [{subject: 0, lane: '0:V', point: 0}, {subject: 0, lane: '0:A', point: 3},
        {subject: 1, lane: '1:V', point: 1}, {subject: 4, lane: '4:V', point: 0}];
    assert.equal(filterEventHistoryIntervals(source, 'all', '0:V').length, 2);
    assert.deepEqual(filterEventHistoryIntervals(source, 'points', '0:V'), [source[1]]);
    assert.equal(filterEventHistoryIntervals(source, 'states').length, 2);
    assert.equal(filterEventHistoryIntervals(source, 'points').length, 2);
    assert.equal(source.length, 4);
});

test('Red muestra solo estados globales y excluye eventos propios de fase', () => {
    const source = [{subject: 4, point: 0, state: 2}, {subject: 4, point: 0, state: 3},
        {subject: 0, point: 1}, {subject: 0, point: 0, state: 2}];
    assert.deepEqual(filterEventHistoryIntervals(source, 'all', new Set(['4:V']), new Set(['outage', 'abnormal'])), source.slice(0, 2));
});

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

test('cierra sobrecorriente aunque el offset secundario invierta 0.3ms el retorno', () => {
    const result = episodes([capture(248, 20000.8, [record(0, 1, 5, 10100, {isCurrent:true})]),
        capture(249, 20000.5, [record(0, 5, 1, 14800, {isCurrent:true})])]);
    assert.equal(result.length, 1);
    assert.equal(result[0].state, 5);
    assert.equal(result[0].endKnown, true);
    assert.equal(result[0].end - result[0].start, 14800);
});

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

test('un reinicio no interrumpe el corte de otra entrada', () => {
    const result = episodes([capture(1, 1000, [record(0, 1, 4)]), capture(2, 5000, [record(4, 0, 1)], {startup: true}), capture(3, 10000, [record(0, 4, 1, 9000)])]);
    assert.equal(result.length, 1);
    assert.equal(result[0].start, base + 1000);
    assert.equal(result[0].end, base + 10000);
    assert.equal(result[0].uncertain, false);
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

test('conserva el ultimo estado de cada fase aunque reinicie la red', () => {
    for (const state of [1, 4]) {
        const result = buildEventHistory([capture(1, 1000, [record(0, 0, state)]),
            capture(2, 5000, [record(4, 0, 1)], {startup: true})], base, base + 60000);
        const phase = result.intervals.find(item => item.lane === '0:V');
        assert.equal(phase.end, base + 60000);
        assert.equal(phase.uncertain, false);
    }
});

test('une la normalidad de fase observada en registros secundarios de varios reinicios', () => {
    const result = buildEventHistory([capture(1, 1000, [record(0, 4, 1)]),
        capture(2, 5000, [record(4, 0, 1), {...record(0, 0, 1), root: false}], {startup: true}),
        capture(3, 10000, [record(4, 0, 1), {...record(0, 0, 1), root: false}], {startup: true})], base, base + 60000);
    const phases = result.intervals.filter(item => item.lane === '0:V');
    assert.equal(phases.length, 1);
    const normal = phases.find(item => item.state === 1);
    assert.equal(normal.start, base + 1000); assert.equal(normal.end, base + 60000);
    assert.deepEqual(normal.ids, [3, 2, 1]); assert.equal(normal.uncertain, false);
});

test('cinco arranques sin tension son un solo corte hasta el retorno, no cinco cortes', () => {
    const {buildEventStatistics} = require('../data/event_statistics.js');
    const hour = 3600000, end = 12 * hour;
    const captures = [capture(1, 0, [record(4, 1, 3)])];
    for (let id = 2; id <= 6; ++id) captures.push(capture(id, (id - 1) * hour, [record(4, 0, 3)], {startup: true}));
    captures.push(capture(7, end, [record(4, 3, 1, 2000)]));
    const result = buildEventHistory(captures.reverse(), base, base + end + hour);
    const outages = result.intervals.filter(item => item.subject === 4 && item.state === 3);
    assert.equal(outages.length, 1);
    assert.equal(outages[0].end - outages[0].start, end);
    assert.equal(outages[0].ids.length, 7);
    const stats = buildEventStatistics(result.intervals, base, base + end + hour, 'hour');
    assert.equal(stats.summary[0].values.outage.durationMs, end);
    assert.equal(stats.summary[0].values.outage.starts, 1);
    assert.equal(stats.summary[0].values.outage.partial, 0);
    assert.deepEqual(stats.rows.slice(0, 12).map(row => row.values.outage.durationMs), Array(12).fill(hour));
});

test('arranque ya sin tension cuenta desde la observacion, sin inventar el inicio ni otro corte', () => {
    const {buildEventStatistics} = require('../data/event_statistics.js');
    const result = buildEventHistory([capture(1, 1000, [record(4, 0, 3)], {startup: true}),
        capture(2, 10000, [record(4, 3, 1, 2000)])], base, base + 60000);
    const stats = buildEventStatistics(result.intervals, base, base + 60000);
    assert.equal(stats.summary[0].values.outage.durationMs, 9000);
    assert.equal(stats.summary[0].values.outage.starts, 0);
});

test('corte abierto suma hasta el limite del periodo y una mutacion separa los tiempos', () => {
    const {buildEventStatistics} = require('../data/event_statistics.js');
    const result = buildEventHistory([capture(1, 1000, [record(4, 1, 2)]),
        capture(2, 10000, [record(4, 2, 3, 1)])], base, base + 60000);
    const stats = buildEventStatistics(result.intervals, base + 5000, base + 30000);
    assert.equal(stats.summary[0].values.abnormal.durationMs, 5000);
    assert.equal(stats.summary[0].values.outage.durationMs, 20000);
});

test('un DIP aislado no demuestra normalidad del resto del periodo', () => {
    const result = buildEventHistory([capture(1, 1000, [record(0, 0, 0, 50, {isPoint: true, pointType: 1})])], base, base + 60000);
    assert.equal(result.intervals.length, 1);
    assert.equal(result.intervals[0].point, 1);
});
