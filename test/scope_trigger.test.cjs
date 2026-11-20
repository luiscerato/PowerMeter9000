const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

class FakeChart {
    // Conserva las opciones del grafico; container es el destino y options su configuracion.
    constructor(container, options) { this.options = options; }
    // Permite dibujar sin DOM ni CanvasJS real; sin parametros.
    render() {}
}
const context = vm.createContext({console, ArrayBuffer, Uint8Array, DataView,
    document: {getElementById: () => null}, CanvasJS: {Chart: FakeChart}});
vm.runInContext(fs.readFileSync('data/chart_tooltips.js', 'utf8') + fs.readFileSync('data/scope.js', 'utf8') + '\nglobalThis.scope = Scope;', context);
const scope = context.scope;
scope.initGraph('scope');
scope.updateMetrics = () => {};
scope.Data.Trigger.Channel = 1;
scope.Data.Trigger.Value = 5;
assert.equal(scope.options.animationEnabled, false);

// Arma una trama real de 200 puntos; offset mueve el cruce respecto de la marca y falling selecciona el flanco.
function makeFrame(offset, falling = false) {
    const points = 200;
    const buffer = new ArrayBuffer(24 + points * 7 * 12 / 8);
    const bytes = new Uint8Array(buffer), view = new DataView(buffer);
    bytes.set([83, 67, 1, 1]);
    view.setUint16(4, points, true); view.setUint16(6, 100, true); view.setUint16(8, 500, true);
    for (let point = 0; point < points; ++point) {
        const value = ((falling ? 100 - point : point - 100) * 20 + offset) & 4095;
        const start = (point * 7 + 1) * 12;
        for (let bit = 0; bit < 12; ++bit) {
            if (value & (1 << bit)) bytes[24 + ((start + bit) >> 3)] |= 1 << ((start + bit) & 7);
        }
    }
    return buffer;
}

// Comprueba el cruce dibujado en x=0 y ambos extremos visibles; expected es su posicion fraccional en la trama.
function verifyCentered(expected) {
    assert.ok(Math.abs(scope.Data.frameTriggerIndex - expected) < 1e-9);
    const points = scope.options.data.find(series => series.name === 'Voltaje R').dataPoints;
    const index = Math.floor(expected), a = points[index], b = points[index + 1];
    const crossing = a.x + (5 - a.y) * (b.x - a.x) / (b.y - a.y);
    assert.ok(Math.abs(crossing) < 1e-9, 'el umbral debe cruzarse exactamente en x=0');
    assert.equal(scope.options.axisX.minimum + scope.options.axisX.maximum, 0);
    assert.equal(scope.options.axisX.minimum, -50);
    assert.equal(scope.options.axisX.maximum, 50);
    assert.equal(scope.options.axisX.interval, 10);
}

for (const offset of [-14, -5, 3, 12]) {
    scope.Data.Trigger.Edge = 'rising';
    scope.newData(makeFrame(offset));
    verifyCentered(100 + (5 - offset) / 20);
    scope.Data.Trigger.Edge = 'falling';
    scope.newData(makeFrame(offset, true));
    verifyCentered(100 + (offset - 5) / 20);
}
const automatic = makeFrame(0);
new DataView(automatic).setUint16(6, 65535, true);
scope.newData(automatic);
assert.equal(scope.Data.frameTriggerIndex, -1, 'una captura automatica no debe inventar un disparo');
const invalid = makeFrame(0);
new DataView(invalid).setUint16(6, 200, true);
assert.equal(scope.decodeFrame(new Uint8Array(invalid)), null);
console.log('PASS: disparos ascendentes y descendentes centrados entre puntos, eje simetrico y live sin animacion.');
