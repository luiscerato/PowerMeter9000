const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const vm = require('node:vm');
const {eventHistoryCalendarRange, shiftEventHistoryPeriod} = require('../data/event_history_period.js');
const {eventHistoryPreviewSource} = require('../data/event_history_preview.js');

test('hoy y ayer incluyen desde medianoche hasta 23:59:59', () => {
    const now = new Date(2026, 9, 21, 15, 20), today = eventHistoryCalendarRange('today', now), yesterday = eventHistoryCalendarRange('yesterday', now);
    assert.equal(today.from, new Date(2026, 9, 21).getTime());
    assert.equal(today.to, new Date(2026, 9, 21, 23, 59, 59).getTime());
    assert.equal(yesterday.from, new Date(2026, 9, 20).getTime());
    assert.equal(yesterday.to + 1000, today.from);
});

test('semanas empiezan lunes y terminan domingo, y mes/anio cubren el calendario', () => {
    const now = new Date(2026, 9, 21);
    const week = eventHistoryCalendarRange('thisWeek', now), previous = eventHistoryCalendarRange('lastWeek', now);
    assert.equal(new Date(week.from).getDate(), 19); assert.equal(new Date(week.to).getDate(), 25);
    assert.equal(previous.to + 1000, week.from);
    assert.equal(eventHistoryCalendarRange('lastMonth', now).from, new Date(2026, 8, 1).getTime());
    assert.equal(eventHistoryCalendarRange('thisMonth', now).to, new Date(2026, 10, 1).getTime() - 1000);
    assert.equal(eventHistoryCalendarRange('thisYear', now).to, new Date(2027, 0, 1).getTime() - 1000);
});

test('mover meses no pierde febrero ni acumula desplazamiento, incluido anio bisiesto', () => {
    const january = eventHistoryCalendarRange('thisMonth', new Date(2024, 0, 31));
    const february = shiftEventHistoryPeriod(january, 1), march = shiftEventHistoryPeriod(february, 1);
    assert.equal(new Date(february.from).getMonth(), 1); assert.equal(new Date(february.to).getDate(), 29);
    assert.equal(march.from, new Date(2024, 2, 1).getTime());
    assert.deepEqual(shiftEventHistoryPeriod(february, -1), january);
});

test('un periodo personalizado se desplaza por su longitud inclusiva', () => {
    const custom = {from: 1000, to: 5000, unit: 'custom'};
    assert.deepEqual(shiftEventHistoryPeriod(custom, 1), {from: 6000, to: 10000, unit: 'custom'});
});

test('la vista previa elige inicio o retorno sin confundir corriente con tension', () => {
    const item = {ids: [2, 1], subject: 0, start: 1000, end: 9000, current: true};
    const captures = [{id: 1, timestamp: 1000, records: [{offsetMs: 0, states: {subject: 0, isCurrent: true}}]},
        {id: 2, timestamp: 1000, records: [{offsetMs: 0, states: {subject: 0}}, {offsetMs: 8000, states: {subject: 0, isCurrent: true}}]}];
    assert.equal(eventHistoryPreviewSource(item, captures, 'start'), 1);
    assert.equal(eventHistoryPreviewSource(item, captures, 'end'), 2);
});

test('historial y vista previa comparten una sola descarga y cancelan las consultas en cola', async () => {
    let active = 0, peak = 0, calls = 0;
    const context = vm.createContext({Promise, AbortController, DOMException, setTimeout, clearTimeout, board: {IP: 'http://meter.test'},
        fetch: async () => {
            ++calls; ++active; peak = Math.max(peak, active);
            return {ok: true, json: async () => { await new Promise(resolve => setTimeout(resolve, 10)); --active; return {}; }};
        }});
    vm.runInContext(fs.readFileSync('data/event_history.js', 'utf8'), context);
    const controller = new AbortController(), cancelled = new AbortController();
    const first = context.fetchEventHistory('/first', controller.signal), second = context.fetchEventHistory('/preview', controller.signal);
    const third = context.fetchEventHistory('/cancelled', cancelled.signal); cancelled.abort();
    await Promise.all([first, second, assert.rejects(third, {name: 'AbortError'})]);
    assert.equal(calls, 2); assert.equal(peak, 1); assert.equal(active, 0);
});

test('volver a Historial consulta novedades, sin duplicar una carga activa, y reanuda una cancelada', () => {
    const context = vm.createContext({AbortController});
    vm.runInContext(fs.readFileSync('data/event_history.js', 'utf8'), context);
    vm.runInContext(`
        let calls = 0;
        loadEventHistory = () => { ++calls; eventHistory.controller = new AbortController(); };
        closeEventHistoryPreview = () => {};
        eventHistory.loaded = true;
        setEventHistoryActive(true);
        setEventHistoryActive(true);
        if (calls !== 1) throw Error('duplico la carga activa');
        setEventHistoryActive(false);
        setEventHistoryActive(true);
        if (calls !== 2) throw Error('no reanudo la cancelada');
        eventHistory.controller = null;
        setEventHistoryActive(true);
        if (calls !== 3) throw Error('no refresco el analisis finalizado');
    `,context);
});
