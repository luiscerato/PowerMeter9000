const assert = require('node:assert/strict');
const test = require('node:test');
const {eventHistorySignalDefinitions, toggleEventHistorySignals} = require('../data/event_history_preview.js');

test('FAST ofrece ocho senales y Wave no inventa voltaje neutro-tierra', () => {
    assert.equal(eventHistorySignalDefinitions().length, 8);
    const keys = eventHistorySignalDefinitions(true).map(signal => signal[1]);
    assert.equal(keys.length, 7); assert.equal(keys.includes('vn'), false); assert.equal(keys.includes('inn'), true);
});

test('accesos rapidos reemplazan la seleccion y repetirlos no apaga nada', () => {
    let selected = toggleEventHistorySignals(new Set(['vn','inn']), ['vr','ir'], true);
    assert.deepEqual([...selected], ['vr','ir']);
    selected = toggleEventHistorySignals(selected, ['vs','is'], true);
    assert.deepEqual([...selected], ['vs','is']);
    selected = toggleEventHistorySignals(selected, ['vs','is'], true);
    assert.deepEqual([...selected], ['vs','is']);
    selected = toggleEventHistorySignals(selected, ['vr','vs','vt','vn'], true);
    assert.deepEqual([...selected], ['vr','vs','vt','vn']);
    selected = toggleEventHistorySignals(selected, ['ir','is','it','inn'], true);
    assert.deepEqual([...selected], ['ir','is','it','inn']);
});

test('despues del acceso R se agregan senales individuales de S sin perder las de R', () => {
    let selected = toggleEventHistorySignals(new Set(), ['vr','ir'], true);
    selected = toggleEventHistorySignals(selected, ['vs']);
    selected = toggleEventHistorySignals(selected, ['is']);
    assert.deepEqual([...selected].sort(), ['ir','is','vr','vs']);
    selected = toggleEventHistorySignals(selected, ['is']);
    assert.deepEqual([...selected].sort(), ['ir','vr','vs']);
});

test('un grupo mixto se completa primero y al repetir se apaga sin afectar al otro', () => {
    const original = new Set(['vr','ir']);
    let selected = toggleEventHistorySignals(original, ['vr','vs','vt','vn']);
    assert.equal(selected.size, 5); assert.equal(original.size, 2);
    selected = toggleEventHistorySignals(selected, ['vr','vs','vt','vn']);
    assert.deepEqual([...selected], ['ir']);
    selected = toggleEventHistorySignals(selected, ['ir']);
    assert.equal(selected.size, 0);
});
