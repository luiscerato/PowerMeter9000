const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const host = process.argv[2];
const streams = Number(process.argv[3] || 3);
assert.ok([1, 2, 3].includes(streams), 'tipo de vista 1 temporal, 2 espectro, 3 ambos');
if (!host) throw Error('Uso: node test/scope_fft_live.cjs 192.168.1.90');
const context = vm.createContext({console, ArrayBuffer, Uint8Array, Float64Array, DataView,
    document: {getElementById: () => null}});
for (const file of ['scope.js', 'scope_fft_vendor.js', 'scope_fft.js'])
    vm.runInContext(fs.readFileSync('data/' + file, 'utf8'), context);
vm.runInContext('globalThis.scope = Scope; globalThis.fft = ScopeFft;', context);
context.scope.drawWaves = () => {};
context.scope.updateMetrics = () => {};
context.scope.bytes = 0;
context.scope.view = streams & 2 ? 'quality' : 'scope';
context.scope.shouldReconnect = true;
context.scope.showQualityFrame = () => {};
const calculateSpectrum = context.fft.updateSpectrum;
context.fft.updateSpectrum = () => {}; // El transporte se mide sin el costo artificial de FFT dentro de node:vm.
const socket = new WebSocket('ws://' + host + '/ws');
socket.binaryType = 'arraybuffer';
let heartbeat;
socket.onopen = () => {
    socket.send('scope_view,' + streams);
    heartbeat = setInterval(() => socket.send('scope_view,' + streams), 1000);
};
let scopeFrames = 0, fftFrames = 0, completeFrames = 0, cancelled = 0, lastUs = 0n, enabled, original, changed = false;
const reasons = {};
const requestedMask = process.argv[4] ? Number(process.argv[4]) : null;
const requestedVoltage = process.argv[5] ? Number(process.argv[5]) : null;
const requestedCurrent = process.argv[6] ? Number(process.argv[6]) : null;
const timer = setTimeout(() => {
    if (changed) socket.send(`quality_set,${original[1]},${original[2]},${original[3]}`);
    setTimeout(() => socket.close(), 300);
}, 7000);

// Valida paquetes reales sin cambiar configuracion; event es un mensaje WebSocket de la placa.
socket.onmessage = event => {
    if (typeof event.data === 'string') {
        for (const line of event.data.split('\n')) {
        if (line.startsWith('scope_fft_on,')) enabled = Number(line.slice(13));
        if (line.startsWith('quality_cfg,')) {
            const config = line.split(',').slice(1).map(Number);
            if (!original) original = config;
            if (config[0] && requestedMask !== null && !changed) {
                changed = true;
                socket.send(`quality_set,${requestedVoltage || config[1]},${requestedCurrent || config[2]},${requestedMask}`);
            }
        }
        }
        context.scope.newData(event.data);
        return;
    }
    const data = new Uint8Array(event.data);
    if (data[1] === 70) {
        const previous = context.fft.channels.slice();
        assert.equal(context.fft.receive(data), true);
        if (data[3] === 1) { ++cancelled; reasons[data[8]] = (reasons[data[8]] || 0) + 1; return; }
        assert.equal(data[2], 3);
        assert.ok(data.length <= 5428, 'fragmento maximo de 5376 + 52 bytes');
        const view = new DataView(event.data), firstUs = view.getBigUint64(20, true);
        assert.ok(firstUs > lastUs);
        lastUs = firstUs;
        ++fftFrames;
        if (data[8] === 0) ++scopeFrames;
        if (data[8] === 3 && context.fft.channels.some((record, index) => record !== previous[index])) ++completeFrames;
    } else {
        assert.ok(context.scope.decodeFrame(data));
        ++scopeFrames;
    }
};
socket.onclose = () => {
    clearTimeout(timer);
    clearInterval(heartbeat);
    calculateSpectrum();
    const channels = context.fft.channels.map((record, index) => record?.analysis && ({index,
        bins: record.analysis.points.length, referenceHz: record.referenceHz, frequency: record.analysis.frequency,
        step: record.step, peak: record.analysis.points.reduce((peak, point) => point.y > peak.y ? point : peak, {x: 0, y: 0})})).filter(Boolean);
    console.log(JSON.stringify({scopeFrames, fftFrames, completeFrames, cancelled, reasons, enabled, channels}));
    if (streams & 1) assert.ok(scopeFrames >= 10, 'el osciloscopio debe seguir recibiendo');
    if (streams & 2) {
        assert.ok(completeFrames >= 8, 'el espectro debe recibir ventanas completas');
        assert.ok(channels.length > 0 && channels.every(channel => channel.bins >= 700 && channel.bins <= 1025));
    }
};
socket.onerror = event => { console.error(event.message); process.exitCode = 1; };
