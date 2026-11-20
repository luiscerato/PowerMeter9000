const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync('data/chart_tooltips.js', 'utf8') + ';globalThis.tooltip = chartTooltip;', context);
const tooltip = context.tooltip;

// Crea una entrada CanvasJS; name/y describen la señal, extra permite simular series ocultas o unidades distintas.
function entry(name, y, extra = {}) {
    return {dataPoint: {x: 2008, y}, dataSeries: {name, color: '#d9534f', ...extra}};
}
const event = {entries: [entry('R', 231.1), entry('S oculta', 232, {visible: false}), entry('N sin dato', null), entry('T', 0)]};
const options = tooltip.options({title: 'FAST RMS #87', unit: 'V', markers: [
    {at: 2008, label: 'Red: corte → normal'}, {at: 0, label: 'Inicio lejano'}]});
assert.equal(options.shared, true); assert.equal(options.animationEnabled, false);
const html = options.contentFormatter(event);
assert.match(html, /FAST RMS #87.*2\.008,00 ms/);
assert.match(html, /231,10 V/); assert.match(html, /0,00 V/);
assert.match(html, /Red: corte → normal/);
assert.doesNotMatch(html, /S oculta|N sin dato|Inicio lejano/);
assert.equal(tooltip.time({entries: []}), '');
assert.equal(tooltip.number(null), '—'); assert.equal(tooltip.number(NaN), '—');
const mixed = tooltip.time({entries: [entry('Voltaje', 230, {signalUnit: 'V'}), entry('Corriente', 0.2, {signalUnit: 'A'})]});
assert.match(mixed, /230,00 V/); assert.match(mixed, /0,200 A/);
const unsafe = tooltip.wrap('<img src=x>', [{name: '<script>', value: '<svg>', color: 'red;position:fixed'}], ['<iframe>']);
assert.doesNotMatch(unsafe, /<img|<script|<svg|<iframe|position:fixed/);
assert.match(unsafe, /&lt;img/);
const state = entry('Wi-Fi', 7); state.dataPoint.tooltipText = 'Sí';
assert.match(tooltip.time({entries: [state]}, {mode: 'date'}), />Sí<\/strong>/);
assert.doesNotMatch(tooltip.time({entries: [state]}), /7,00/);
assert.match(tooltip.wrap('Muchos', Array.from({length: 7}, (_, index) => ({name: 'R' + index, value: '1 V'}))), /isMany/);
console.log('PASS: tooltips compartidos, visibles, cero valido, unidades, marcas cercanas y texto escapado.');
