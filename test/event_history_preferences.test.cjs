const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const test = require('node:test');

// Prepara controles y almacenamiento locales; saved contiene las preferencias anteriores.
function context(saved) {
    const controls = {}, storage = new Map(saved ? [['powerMeter.eventHistory.v1', JSON.stringify(saved)]] : []);
    const scope = vm.createContext({Date, Set, JSON, Number,
        localStorage: {getItem: key => storage.get(key), setItem: (key, value) => storage.set(key, value)},
        $: key => ({val(value) { if (arguments.length) { controls[key] = value; return this; } return controls[key]; }}),
        eventHistory: {periodUnit: 'week', lane: 'all', types: new Set(['dip'])}, eventStatisticsSubjects: new Set([4]),
        eventHistoryTypes: [{key: 'dip'}, {key: 'outage'}], syncEventHistoryDates() {},
        eventHistoryInputTime: date => date.toISOString(), updateEventHistoryTypeControls() {}, updateEventStatisticsSubjectControls() {}});
    vm.runInContext(fs.readFileSync('data/event_history_period.js', 'utf8'), scope);
    return {scope, controls, storage};
}

test('Hoy se recupera relativo al dia actual y los filtros se conservan', () => {
    const {scope, controls, storage} = context({preset: 'today', from: '2020-01-01', lane: '1:V',
        types: ['dip', 'invalid'], subjects: [1, 2], grouping: 'hour'});
    vm.runInContext('restoreEventHistoryPreferences(); saveEventHistoryPreferences();', scope);
    assert.equal(controls['#eventHistoryPreset'], 'today');
    assert.equal(new Date(controls['#eventHistoryFrom']).getDate(), new Date().getDate());
    const saved = JSON.parse(storage.get('powerMeter.eventHistory.v1'));
    assert.deepEqual(saved.types, ['dip']); assert.deepEqual(saved.subjects, [1, 2]);
    assert.equal(saved.lane, '1:V'); assert.equal(saved.grouping, 'hour');
});

test('un periodo manual conserva fechas y el almacenamiento corrupto no impide cargar', () => {
    const {scope, controls, storage} = context({preset: '', from: '2026-11-01T12:30:00Z', to: '2026-11-02T12:30:00Z', unit: 'custom'});
    vm.runInContext('restoreEventHistoryPreferences()', scope);
    assert.equal(Date.parse(controls['#eventHistoryFrom']), Date.parse('2026-11-01T12:30:00Z'));
    storage.set('powerMeter.eventHistory.v1', '{');
    assert.doesNotThrow(() => vm.runInContext('restoreEventHistoryPreferences()', scope));
});
