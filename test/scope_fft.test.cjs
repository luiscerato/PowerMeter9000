const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const elements = {scopeFftStatus: {}, scopeFftEnabled: {}};
const saved = new Map();
let expanded = false, focusRestored = 0;
elements.scopeFftExpand = {setAttribute(name, value) { this[name] = value; }};
const document = {getElementById: id => elements[id] || null, body: {style: {overflow: 'auto'}},
    querySelector: () => ({classList: {toggle: (_, value) => { expanded = value; }}}),
    activeElement: {focus: () => { ++focusRestored; }}};
const context = vm.createContext({console, ArrayBuffer, Uint8Array, Float64Array, DataView,
    document, requestAnimationFrame: callback => callback(),
    localStorage: {getItem: key => saved.get(key), setItem: (key, value) => saved.set(key, value)}});
vm.runInContext(fs.readFileSync('data/chart_tooltips.js', 'utf8') + fs.readFileSync('data/scope.js', 'utf8') + '\nglobalThis.scope = Scope;', context);
vm.runInContext(fs.readFileSync('data/scope_fft_vendor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('data/scope_fft.js', 'utf8') + '\nglobalThis.fft = ScopeFft;', context);
const fft = context.fft, scope = context.scope;
const quality = vm.runInContext('powerQuality', context);
scope.bytes = 0;
fft.chart = {options: {axisX: {}, axisY: {}, axisY2: {}}, render() {}};

// Genera senos de amplitud RMS conocida mas continua; tones es una lista [frecuencia, RMS], dc es el offset.
function signal(tones, dc = 0) {
    return Float64Array.from({length: 1024}, (_, i) => dc + tones.reduce((v, [hz, rms]) =>
        v + Math.SQRT2 * rms * Math.sin(2 * Math.PI * hz * i / 8000), 0));
}
const spectrum = fft.calculate(signal([[62.5, 230], [187.5, 23]], 15), 8000);
const harmonicSamples = signal([[50, 230], [150, 23], [250, 11.5]], 15);
const harmonicRecord = {samples: harmonicSamples, analysis: fft.analyze(harmonicSamples, 8000, 50), step: 0.1};
const summary = quality.summarize(harmonicRecord, 'V');
assert.equal(summary.valid, true);
assert.ok(Math.abs(summary.fundamental - 230) < 0.3);
assert.ok(Math.abs(summary.thd - Math.sqrt(100 + 25)) < 0.1, 'THD-F suma RMS de armonicos en cuadratura');
assert.equal(summary.dominant.order, 3);
assert.ok(Math.abs(summary.harmonics[2].percent - 10) < 0.1);
assert.equal(summary.harmonics.length, 40);
assert.equal(quality.summarize({...harmonicRecord, clipped: true}, 'V').valid, false);
const tinySamples = signal([[50, 0.0001]]);
assert.equal(quality.summarize({samples: tinySamples, analysis: fft.analyze(tinySamples, 8000, 50)}, 'A').valid, false);
assert.equal(quality.format(null), '—', 'un valor ausente no es cero');
assert.equal(quality.summarize({...harmonicRecord, analysis: {cycles: 0}}, 'V').valid, false);
assert.equal(spectrum.length, 513);
assert.ok(Math.abs(spectrum[8].y - 230) < 1e-8);
assert.ok(Math.abs(spectrum[24].y - 23) < 1e-8);
assert.equal(spectrum[512].x, 4000);
assert.ok(spectrum[0].y < 1e-8, 'el offset no debe aparecer como energia de alterna');
assert.ok(fft.calculate(signal([], 17), 8000).every(p => p.y < 1e-8));

for (const frequency of [49.7, 50, 50.3, 59.8, 60.1]) {
    const samples = signal([[frequency, 230], [frequency * 3, 23]], 15);
    const result = fft.analyze(samples, 8000, frequency + 0.1);
    assert.ok(result.cycles >= 6);
    assert.ok(Math.abs(result.frequency - frequency) < 0.002, 'estimar frecuencia sin fijarla a 50/60 Hz');
    assert.ok(Math.abs(result.points[result.cycles].y - 230) < 0.25);
    assert.ok(Math.abs(result.points[result.cycles * 3].y - 23) < 0.1);
    assert.ok(result.points.filter((_, bin) => bin !== result.cycles && bin !== result.cycles * 3)
        .every(point => point.y < 0.1), 'no crear picos vecinos al fundamental ni al armonico');
    assert.ok(result.points.at(-1).x <= 4000, 'remuestrear no amplia el ancho de banda adquirido');
    const estimated = fft.analyze(samples, 8000);
    assert.ok(Math.abs(estimated.frequency - frequency) < 0.002, 'poder estimar sin frecuencia ADE');
}
assert.equal(fft.analyze(signal([], 17), 8000, 50).cycles, 0);
for (const index of [0, 1, 500, 1022, 1023]) {
    const pulse = new Float64Array(1024); pulse[index] = 100;
    assert.equal(fft.analyze(pulse, 8000, 50).cycles, 0, 'un transitorio no debe forzarse a ciclos periodicos');
}
fft.coherent = false;
assert.equal(fft.analyze(signal([[50, 230]]), 8000, 50).points.length, 513);
fft.coherent = true;

// Arma una captura SF v2 intercalada; mask selecciona canales simultaneos y clipped marca los saturados.
function packet(mask = 127, clipped = 0) {
    const selected = Array.from({length: 7}, (_, index) => index).filter(index => mask & (1 << index));
    const bytes = new Uint8Array(36 + selected.length * 1536), view = new DataView(bytes.buffer);
    bytes.set([83, 70, 2, mask]);
    view.setUint16(4, 1024, true); bytes[6] = 125; bytes[7] = clipped;
    view.setFloat32(8, 0.25, true);
    view.setFloat32(12, 0.0025, true);
    for (let phase = 0; phase < 3; ++phase) view.setFloat32(24 + phase * 4, 62.5, true);
    for (let i = 0; i < 1024; ++i) {
        selected.forEach((channel, slot) => {
            const amplitude = channel % 2 ? 200 + channel : 2 + channel * 0.1;
            const gain = channel % 2 ? 0.25 : 0.0025;
            const raw = Math.trunc(Math.SQRT2 * amplitude * Math.sin(2 * Math.PI * 62.5 * i / 8000) / gain) & 4095;
            const bit = (i * selected.length + slot) * 12;
            for (let b = 0; b < 12; ++b) if (raw & (1 << b)) bytes[36 + ((bit + b) >> 3)] |= 1 << ((bit + b) & 7);
        });
    }
    return bytes;
}
const data = packet();
scope.newData(data.buffer);
assert.equal(scope.bytes, 10788);
assert.equal(fft.chart.options.data.length, 7);
fft.channels.forEach((record, index) => {
    const peak = record.analysis.points[record.analysis.cycles];
    assert.ok(Math.abs(peak.y - (index % 2 ? 200 + index : 2 + index * 0.1)) < (index % 2 ? 0.2 : 0.002));
    assert.ok(Math.abs(peak.x - 62.5) < 0.005);
    assert.equal(fft.chart.options.data[index].axisYType, index % 2 ? 'secondary' : 'primary');
    assert.equal(fft.chart.options.data[index].color, scope.Data.waves[index].color);
});
assert.equal(fft.chart.options.axisY.maximum, fft.currentDiv * 5);
assert.equal(fft.chart.options.axisY2.maximum, fft.voltageDiv * 5);
assert.equal(fft.chart.options.axisY.interval, fft.currentDiv);
assert.match(elements.scopeFftStatus.textContent, /1024/);
assert.equal(fft.receive(data.subarray(1)), false);
const invalid = packet(); invalid[6] = 250;
assert.equal(fft.receive(invalid), false, 'no interpretar muestras reducidas como si fueran de 8 kHz');
assert.equal(fft.receive(packet(127, 1)), true);
assert.match(elements.scopeFftStatus.textContent, /saturada/);
assert.match(fft.tooltip({entries: [{dataSeries: fft.chart.options.data[0],
    dataPoint: fft.channels[0].analysis.points[fft.channels[0].analysis.cycles]}]}), /H1.*A RMS.*THD-F.*Saturada/);
fft.chart.options.axisY.viewportMaximum = 2;
assert.equal(fft.receive(packet()), true);
assert.equal(fft.chart.options.axisY.viewportMaximum, 2, 'conservar zoom manual al actualizar');
fft.resetZoom();
assert.equal(fft.chart.options.axisY.viewportMaximum, null);
scope.Data.waves.forEach((wave, index) => { wave.visible = index === 0 || index === 2; });
fft.syncScope();
const series = fft.chart.options.data, firstSeries = series[0];
assert.equal(series.length, 7);
assert.equal(series.filter(item => item.visible).length, 2);
const previous = fft.channels;
assert.equal(fft.receive(packet(127)), true);
assert.equal(fft.channels, previous, 'descartar capturas de una seleccion anterior');
assert.equal(fft.receive(packet(5)), true);
assert.ok(fft.channels[1], 'recibir corrientes no elimina la tension guardada');
assert.equal(fft.chart.options.data.filter(item => item.visible).length, 2);
scope.drawWaves = () => fft.syncScope();
fft.toggleChannel({dataSeries: {name: scope.Data.waves[2].name}});
assert.equal(scope.Data.waves[2].visible, false);
assert.equal(fft.chart.options.data.filter(item => item.visible).length, 1);
scope.toggleWave(2);
assert.equal(fft.chart.options.data.filter(item => item.visible).length, 2);
assert.equal(fft.chart.options.data, series, 'la seleccion no reemplaza el array de series');
assert.equal(fft.chart.options.data[0], firstSeries, 'la seleccion reutiliza cada serie');
scope.Data.Scale.Current = 2;
fft.syncScope();
assert.equal(fft.chart.options.axisY.maximum, 5);
assert.equal(fft.chart.options.axisY.interval, 1);
scope.newData('scope_fft_on,0');
assert.equal(fft.channels.length, 0);
assert.equal(elements.scopeFftStatus.textContent, 'Desactivada');
assert.equal(fft.chart.options.data.filter(item => item.visible).length, 0);
scope.newData('scope_fft_on,1');
scope.Data.waves.forEach(wave => { wave.visible = false; });
fft.syncScope();
assert.equal(elements.scopeFftStatus.textContent, 'Sin canales seleccionados');
fft.setScale('current', 0.01);
assert.equal(fft.chart.options.axisY.maximum, 0.05);
assert.equal(scope.Data.Scale.Current, 2, 'una escala visual FFT no modifica la adquisicion');
fft.chart.options.axisY.viewportMaximum = 0.03;
scope.Data.Scale.Current = 5;
fft.syncScope();
assert.equal(fft.chart.options.axisY.maximum, 0.05);
assert.equal(fft.chart.options.axisY.viewportMaximum, 0.03, 'la escala FFT independiente conserva su zoom');
fft.setScale('current', -1);
assert.equal(fft.currentDiv, 0.01, 'rechazar escalas invalidas');
fft.setScale('current', 0.001);
assert.equal(fft.currentDiv, 0.01, 'rechazar escalas retiradas');
fft.setScale('voltage', 20);
assert.equal(fft.chart.options.axisY2.maximum, 100);
fft.currentDiv = fft.voltageDiv = 0;
fft.loadScales();
assert.equal(fft.currentDiv, 0.01);
assert.equal(fft.voltageDiv, 20);
fft.setScale('current', 0);
assert.equal(fft.chart.options.axisY.maximum, 0.05, 'cero ya no es una escala ni depende del osciloscopio');
fft.setExpanded(true);
assert.equal(expanded, true);
assert.equal(document.body.style.overflow, 'hidden');
assert.equal(elements.scopeFftExpand['aria-pressed'], 'true');
let prevented = false;
fft.onKeyDown({key: 'Escape', preventDefault: () => { prevented = true; }});
assert.equal(expanded, false);
assert.equal(document.body.style.overflow, 'auto');
assert.equal(prevented, true);
assert.equal(focusRestored, 1);
console.log('PASS: FFT multicanal, escalas independientes persistentes, zoom y ampliacion sin modificar la adquisicion.');
