const assert = require('node:assert/strict');
const test = require('node:test');
const {execFileSync} = require('node:child_process');
const {buildEventStatistics, eventStatisticsGranularity, eventStatisticsChartData, formatEventStatisticsDuration, eventStatisticsTimeInterval} = require('../data/event_statistics.js');
const hour = 3600000, day = hour * 24;
const base = new Date(2026, 9, 19).getTime();

// Crea un tramo de prueba; start/end son ms relativos, extra redefine entrada, calidad o tipo.
function interval(start, end, extra = {}) {
    return {subject: 4, state: 3, point: 0, current: false, start: base + start, end: base + end,
        startKnown: true, endKnown: true, uncertain: false, ...extra};
}

test('un corte de cinco horas reparte su duracion y cuenta un solo inicio', () => {
    const result = buildEventStatistics([interval(hour / 2, hour * 5.5)], base, base + hour * 7, 'hour');
    assert.deepEqual(result.rows.map(row => row.values.outage.durationMs), [hour / 2, hour, hour, hour, hour, hour / 2, 0]);
    assert.deepEqual(result.rows.map(row => row.values.outage.starts), [1, 0, 0, 0, 0, 0, 0]);
    assert.equal(result.summary[0].values.outage.durationMs, 5 * hour);
});

test('graficas conservan cantidades enteras y expresan duraciones como tiempo', () => {
    const result = buildEventStatistics([interval(0, hour)], base, base + hour, 'hour');
    const counts = eventStatisticsChartData(result, false), durations = eventStatisticsChartData(result, true);
    assert.equal(counts.length, 8); assert.equal(durations.length, 5);
    assert.equal(counts[0].dataPoints[0].y, 1); assert.equal(durations[0].dataPoints[0].y, 3600);
    assert.equal(durations[0].dataPoints[0].tooltipText, '1 h 0 min 0 s');
    assert.equal(eventStatisticsChartData(result, false, 'points').length, 3);
    assert.equal(eventStatisticsChartData(result, true, 'points').length, 0);
    assert.equal(eventStatisticsChartData(result, false, 'states').length, 5);
});

test('corte de 2010 ms se muestra en segundos y no como 0.034 minutos', () => {
    const result = buildEventStatistics([interval(0, 2010)], base, base + hour, 'hour');
    const point = eventStatisticsChartData(result, true)[0].dataPoints[0];
    assert.equal(point.y, 2.01);
    assert.equal(point.tooltipText, '2,01 s');
    assert.equal(formatEventStatisticsDuration(90000), '1 min 30 s');
    assert.equal(formatEventStatisticsDuration(3661000), '1 h 1 min 1 s');
    assert.equal(formatEventStatisticsDuration(10000), '10 s');
    assert.equal(formatEventStatisticsDuration(250), '250 ms');
    assert.equal(eventStatisticsTimeInterval(2.01), 0.5);
    assert.equal(eventStatisticsTimeInterval(3600), 900);
});

test('recorta al periodo y no cuenta el inicio de un corte que empezo antes', () => {
    const result = buildEventStatistics([interval(-hour, hour * 3)], base, base + hour * 2, 'hour');
    assert.equal(result.summary[0].values.outage.durationMs, 2 * hour);
    assert.equal(result.summary[0].values.outage.starts, 0);
});

test('reparte baja tension entre dias y alta tension entre semanas', () => {
    const daily = buildEventStatistics([interval(23 * hour, 25 * hour, {subject: 0, state: 2})], base, base + 2 * day, 'day', 0);
    assert.deepEqual(daily.rows.map(row => row.values.low.durationMs), [hour, hour]);
    const weekly = buildEventStatistics([interval(6 * day + 23 * hour, 7 * day + hour, {subject: 0, state: 3})], base, base + 14 * day, 'week', 0);
    assert.deepEqual(weekly.rows.map(row => row.values.high.durationMs), [hour, hour]);
    assert.deepEqual(weekly.rows.map(row => row.values.high.starts), [1, 0]);
});

test('inicios en el limite pertenecen al intervalo siguiente y el final del periodo es exclusivo', () => {
    const result = buildEventStatistics([interval(hour, hour * 1.5), interval(hour * 2, hour * 3)], base, base + hour * 2, 'hour');
    assert.deepEqual(result.rows.map(row => row.values.outage.starts), [0, 1]);
    assert.equal(result.summary[0].values.outage.starts, 1);
});

test('union de duraciones evita duplicados y nunca supera el tiempo de una hora', () => {
    const result = buildEventStatistics([interval(0, hour), interval(0, hour), interval(hour / 2, hour)], base, base + hour, 'hour');
    assert.equal(result.rows[0].values.outage.durationMs, hour);
    assert.equal(result.rows[0].values.outage.starts, 2);
});

test('red y fases no se suman entre si y corriente pertenece a su fase', () => {
    const result = buildEventStatistics([interval(0, hour), interval(0, hour, {subject: 0, state: 4}),
        interval(0, hour / 2, {subject: 0, state: 5, current: true})], base, base + hour, 'hour', 0);
    assert.equal(result.summary[0].values.outage.durationMs, hour);
    assert.equal(result.summary[1].values.outage.durationMs, hour);
    assert.equal(result.rows[0].values.outage.durationMs, hour);
    assert.equal(result.rows[0].values.current.durationMs, hour / 2);
});

test('todas las fases suma sus metricas sin incorporar los estados globales de red', () => {
    const result = buildEventStatistics([
        interval(0, hour, {subject: 0, state: 2}),
        interval(0, hour / 2, {subject: 1, state: 4}),
        interval(0, hour, {subject: 4, state: 3})
    ], base, base + hour, 'hour', [0, 1, 2, 3]);
    assert.deepEqual(result.summary.map(row => row.subject), [0, 1, 2, 3]);
    assert.equal(result.rows[0].values.low.durationMs, hour);
    assert.equal(result.rows[0].values.low.starts, 1);
    assert.equal(result.rows[0].values.outage.durationMs, hour / 2);
    assert.equal(result.rows[0].values.outage.starts, 1);
    assert.equal(result.summary.some(row => row.subject === 4), false);
});

test('tres fases cortadas nueve minutos cuentan nueve minutos combinados', () => {
    const items = [0, 1, 2].map(subject => interval(0, 9 * 60000, {subject, state: 4}));
    const result = buildEventStatistics(items, base, base + hour, 'hour', [0, 1, 2, 3]);
    assert.equal(result.rows[0].values.outage.durationMs, 9 * 60000);
    assert.equal(result.rows[0].values.outage.starts, 3);
    for (const row of result.summary.slice(0, 3)) assert.equal(row.values.outage.durationMs, 9 * 60000);
});

test('union entre fases conserva cortes separados y une los parcialmente solapados', () => {
    const items = [interval(0, 60000, {subject: 0, state: 4}),
        interval(30000, 90000, {subject: 1, state: 4}), interval(120000, 180000, {subject: 2, state: 4})];
    const result = buildEventStatistics(items, base, base + hour, 'hour', [0, 1, 2]);
    assert.equal(result.rows[0].values.outage.durationMs, 150000);
});

test('Red cuenta los puntuales de cada fase sin sumar sus estados al corte global', () => {
    const items = [interval(0, 60000, {subject: 4, state: 3}), interval(0, hour, {subject: 0, state: 4}),
        ...[0, 1, 2].map(subject => interval(1000, 1100, {subject, point: 1}))];
    const result = buildEventStatistics(items, base, base + hour, 'hour', [4]);
    assert.equal(result.rows[0].values.outage.durationMs, 60000);
    assert.equal(result.rows[0].values.outage.starts, 1);
    assert.equal(result.rows[0].values.dip.starts, 3);
});

test('cuenta dips, swells y picos solo en la hora de inicio aunque crucen al siguiente intervalo', () => {
    const items = [1, 2, 3].map(point => interval(hour - 10, hour + 20, {subject: 0, point}));
    const result = buildEventStatistics([...items, items[0]], base, base + 2 * hour, 'hour', 0);
    for (const type of ['dip', 'swell', 'pulse']) {
        assert.equal(result.rows[0].values[type].starts, 1);
        assert.equal(result.rows[1].values[type].starts, 0);
    }
});

test('tramos abiertos o inciertos no inventan duraciones; conservan inicios conocidos', () => {
    const result = buildEventStatistics([interval(0, 5 * hour, {endKnown: false}), interval(hour, 3 * hour, {startKnown: false})], base, base + 6 * hour, 'hour');
    assert.equal(result.summary[0].values.outage.durationMs, 0);
    assert.equal(result.summary[0].values.outage.starts, 1);
    assert.equal(result.summary[0].values.outage.partial, 2);
});

test('distingue neutro alto y red anormal de baja tension de fase', () => {
    const result = buildEventStatistics([interval(0, hour, {state: 2}), interval(0, hour, {subject: 3, state: 3})], base, base + hour);
    assert.equal(result.summary[0].values.abnormal.durationMs, hour);
    assert.equal(result.summary[0].values.low.durationMs, 0);
    assert.equal(result.summary[4].values.high.durationMs, hour);
});

test('automatico usa horas para dia, dias para semana y semanas para mes; permite eleccion manual', () => {
    assert.equal(eventStatisticsGranularity(base, base + day, 'auto'), 'hour');
    assert.equal(eventStatisticsGranularity(base, base + day * 7, 'auto'), 'day');
    assert.equal(eventStatisticsGranularity(base, base + day * 31, 'auto'), 'week');
    assert.equal(eventStatisticsGranularity(base, base + day * 31, 'hour'), 'hour');
    const month = buildEventStatistics([], new Date(2026, 9, 1).getTime(), new Date(2026, 10, 1).getTime());
    assert.equal(month.rows.length, 5);
    assert.equal(new Date(month.rows[1].start).getDay(), 1);
});

test('incluye horas vacias y limita tablas excesivas sin perder el resumen', () => {
    assert.equal(buildEventStatistics([], base, base + day, 'hour').rows.length, 24);
    const result = buildEventStatistics([interval(0, hour)], base, base + day * 100, 'hour');
    assert.equal(result.limited, true);
    assert.deepEqual(result.rows, []);
    assert.equal(result.summary[0].values.outage.durationMs, hour);
});

test('respeta dias locales de 23 y 25 horas por cambios de horario', () => {
    const script = `const {buildEventStatistics:b}=require('./data/event_statistics.js');
        const a=new Date(2026,2,8).getTime(), c=new Date(2026,2,9).getTime();
        const d=new Date(2026,10,1).getTime(), e=new Date(2026,10,2).getTime();
        console.log(JSON.stringify([b([],a,c,'hour').rows.length,b([],d,e,'hour').rows.length,b([],a,c,'day').rows[0].end-a]));`;
    const result = JSON.parse(execFileSync(process.execPath, ['-e', script], {cwd: process.cwd(), env: {...process.env, TZ: 'America/New_York'}, encoding: 'utf8'}));
    assert.deepEqual(result, [23, 25, 23 * hour]);
});
