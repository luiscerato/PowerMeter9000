const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({console, ArrayBuffer, Uint8Array, Float64Array, DataView,
    document: {getElementById: () => null}});
for (const file of ['scope.js', 'scope_fft_vendor.js', 'scope_fft.js']) vm.runInContext(fs.readFileSync('data/' + file, 'utf8'), context);
const scope = vm.runInContext('Scope', context), fft = vm.runInContext('ScopeFft', context);
scope.view = 'quality';
scope.shouldReconnect = true;
scope.qualityGeneration = 7;
scope.Data.waves.forEach((wave, index) => { wave.visible = index === 1; });

// Construye un fragmento con fundamental y tercer armonico conocidos; part/id/mask/generation describen la captura.
function packet(part, id = 10, mask = 2, generation = 7) {
    const selected = Array.from({length: 7}, (_, i) => i).filter(i => mask & 1 << i);
    const bytes = new Uint8Array(52 + 768 * selected.length), view = new DataView(bytes.buffer);
    bytes.set([83, 70, 3, 0]); view.setUint32(4, id, true);
    bytes.set([part, 4, mask, 0], 8);
    view.setUint16(12, 512, true); view.setUint16(14, 125, true);
    view.setUint16(16, part ? 0xffff : 256, true);
    view.setUint32(20, 1000000 + part * 64000, true);
    view.setFloat32(28, 0.2, true); view.setFloat32(32, 0.002, true);
    [36, 40, 44].forEach(offset => view.setFloat32(offset, 50, true));
    view.setUint32(48, generation, true);
    for (let point = 0; point < 512; ++point) for (let slot = 0; slot < selected.length; ++slot) {
        const phase = 2 * Math.PI * 50 * (point + part * 512) / 8000;
        const raw = Math.trunc(Math.SQRT2 * (200 * Math.sin(phase) + 20 * Math.sin(3 * phase)) / 0.2) & 4095;
        const bit = (point * selected.length + slot) * 12, offset = 52 + Math.floor(bit / 8);
        const value = raw << (bit % 8);
        bytes[offset] |= value & 255; bytes[offset + 1] |= value >>> 8;
    }
    return bytes;
}
for (let part = 0; part < 3; ++part) assert.equal(fft.receive(packet(part)), true);
assert.equal(fft.channels.length, 0, 'no calcular FFT con ventana parcial');
assert.equal(fft.receive(packet(3)), true);
assert.equal(fft.channels[1].samples.length, 2048);
const analysis = fft.channels[1].analysis;
assert.equal(analysis.cycles, 10);
assert.ok(Math.abs(analysis.frequency - 50) < 0.01);
assert.ok(Math.abs(analysis.resolution - 5) < 0.01);
assert.ok(Math.abs(analysis.points[10].y - 200) < 0.3);
assert.ok(Math.abs(analysis.points[30].y - 20) < 0.2);
for (const hz of [49.7, 50.3, 59.8, 60.1]) {
    const samples = Float64Array.from({length: 2048}, (_, i) => Math.SQRT2 * 230 * Math.sin(2 * Math.PI * hz * i / 8000));
    const result = fft.analyze(samples, 8000, hz);
    assert.equal(result.cycles, hz < 55 ? 10 : 12);
    assert.ok(Math.abs(result.points[result.cycles].y - 230) < 0.3);
}
fft.receive(packet(0, 11)); fft.receive(packet(2, 11));
assert.equal(fft.pending, null, 'descartar fragmentos faltantes');
fft.receive(packet(0, 12));
const changed = packet(1, 12); new DataView(changed.buffer).setFloat32(28, 0.1, true);
fft.receive(changed);
assert.equal(fft.pending, null, 'no mezclar factores');
fft.receive(packet(0, 13));
const shifted = packet(1, 13); new DataView(shifted.buffer).setUint32(20, 1064001, true);
fft.receive(shifted);
assert.equal(fft.pending, null, 'no aceptar huecos temporales');
fft.receive(packet(0, 14));
const cancel = new Uint8Array([83, 70, 3, 1, 14, 0, 0, 0, 3, 0]);
assert.equal(fft.receive(cancel), true);
assert.equal(fft.pending, null);
assert.match(fft.captureNote, /cola/);
fft.receive(packet(0, 15)); fft.pending.startedAt -= 4000; fft.expireCapture();
assert.equal(fft.pending, null, 'el timeout cubre una cancelacion perdida');
assert.equal(fft.receive(packet(0, 16, 2, 6)), true);
assert.equal(fft.pending, null, 'ignorar una revision vieja');
assert.equal(fft.receive(packet(0).slice(0, 100)), false);
for (let part = 0; part < 4; ++part) {
    const largeRange = packet(part, 18);
    new DataView(largeRange.buffer).setFloat32(28, 0.38, true);
    assert.equal(fft.receive(largeRange), true, 'aceptar capturas de mas de 500 V pico');
}
assert.ok(Math.abs(fft.channels[1].step - 0.38) < 1e-7);
const excessive = packet(0, 19); new DataView(excessive.buffer).setFloat32(28, 0.5, true);
assert.equal(fft.receive(excessive), false, 'rechazar ganancia fuera del limite ampliado');
scope.view = 'scope';
let previews = 0;
scope.showQualityFrame = (waves, meta) => {
    ++previews; assert.equal(waves[1].length, 512); assert.equal(meta.trigger, 256);
};
for (let part = 0; part < 4; ++part) fft.receive(packet(part, 17));
assert.equal(previews, 1, 'osciloscopio consume solo el primer fragmento');
assert.equal(fft.pending, null, 'osciloscopio no reserva la ventana completa');
scope.applyQualityConfig('quality_cfg,1,400,0.5,2,8');
assert.equal(scope.qualityLocked, true);
assert.equal(scope.qualityGeneration, 8);
const gain = scope.Data.Scale.Voltage;
scope.setOptions({voltDiv: 1});
assert.equal(scope.Data.Scale.Voltage, gain, 'bloquear cambios del osciloscopio durante prioridad');
scope.applyQualityConfig('quality_cfg,0,400,0.5,2,9');
assert.equal(scope.qualityLocked, false);
console.log('PASS: SF v3, 200 ms coherentes, fragmentos completos, cancelacion, revision y primer tramo temporal.');
