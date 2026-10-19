const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const context = vm.createContext({Uint8Array, DataView});
vm.runInContext(fs.readFileSync(path.join(__dirname, '../data/grid_events_ui.js'), 'utf8'), context);

// Construye paginas al limite; neutral selecciona los ocho canales empaquetados o los siete originales.
function fixture(neutral) {
    const bytes = new Uint8Array(6144), view = new DataView(bytes.buffer);
    const stride = 16, counts = [113, 118, 118];
    view.setUint8(0, 1);
    view.setUint32(1, 42, true);
    view.setUint8(13, neutral ? 32 : 0);
    view.setUint8(26, counts[0]);
    view.setUint8(27, 3);
    let timestamp = 65000;
    counts.forEach((count, page) => {
        const start = page * 2048, offset = page ? 154 : 234;
        if (page) {
            view.setUint32(start, 42, true);
            view.setUint8(start + 4, page);
            view.setUint8(start + 5, 3);
            view.setUint8(start + 6, count);
        }
        for (let i = 0; i < count; ++i) {
            const at = start + offset + i * stride;
            view.setUint16(at, timestamp & 65535, true);
            timestamp += 10;
            if (neutral) {
                for (let channel = 0; channel < 8; ++channel) {
                    const raw = channel === 7 && !i ? 16383 : (channel + 1) * 100;
                    for (let bit = 0; bit < 14; ++bit) {
                        const position = channel * 14 + bit;
                        bytes[at + 2 + Math.floor(position / 8)] |= (raw >> bit & 1) << (position % 8);
                    }
                }
            } else for (let field = 1; field < 8; ++field) view.setUint16(at + field * 2, field * 100, true);
        }
        const end = offset + count * stride;
        if (page) view.setUint16(start + 8, context.gridCrc16(bytes, start, end, start + 8), true);
        else {
            view.setUint16(end, context.gridCrc16(bytes, 30, end - 30), true);
            view.setUint16(28, context.gridCrc16(bytes, 0, 28), true);
        }
    });
    return bytes;
}

for (const neutral of [false, true]) {
    const bytes = fixture(neutral), event = context.decodeGridEvent(bytes.buffer);
    assert.equal(event.samples.length, 349);
    assert.equal(event.partial, false);
    assert.equal(event.hasNeutral, neutral);
    assert.equal(event.samples[0].vn, null);
    assert.equal(event.samples[1].vn, neutral ? 20 : null);
    assert.equal(event.samples[1].vr, neutral ? 2.5 : 1);
    assert.equal(event.samples[1].inn, 7);
    assert.equal(event.samples[70].relativeMs, 700);
    bytes[2048 + 160] ^= 1;
    assert.equal(context.decodeGridEvent(bytes.buffer).invalidPages, 1);
}
const invalid = fixture(true);
invalid[26] = 114;
assert.throws(() => context.decodeGridEvent(invalid.buffer), /inválida/);
const retired = fixture(true);
retired[13] = 16;
assert.throws(() => context.decodeGridEvent(retired.buffer), /18 bytes no admitido/);
console.log('PASS: Grid original/14 bits, 349 muestras, neutro ausente, CRC, limites, tiempo y rechazo de 18 bytes.');

for (const filename of process.argv.slice(2)) {
    const bytes = fs.readFileSync(filename);
    const event = context.decodeGridEvent(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length));
    const voltages = event.samples.map(sample => sample.vn).filter(value => value !== null);
    console.log(JSON.stringify({file: filename, id: event.id, samples: event.samples.length,
        hasNeutral: event.hasNeutral, partial: event.partial, invalidPages: event.invalidPages,
        firstSample: event.samples[0],
        neutralSamples: voltages.length, neutralMin: voltages.length ? Math.min(...voltages) : null,
        neutralMax: voltages.length ? Math.max(...voltages) : null}));
}
