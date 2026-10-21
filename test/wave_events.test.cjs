const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({Uint8Array, DataView});
vm.runInContext(fs.readFileSync('data/wave_events_ui.js', 'utf8'), context);

// Construye los 31 paquetes y las dos paginas de cierre; sin parametros, con valores signed conocidos.
function waveFixture() {
    const bytes = new Uint8Array(131072), view = new DataView(bytes.buffer);
    const uint64 = (at, value) => { view.setUint32(at, value % 4294967296, true); view.setUint32(at + 4, Math.floor(value / 4294967296), true); };
    for (let packet = 0; packet < 31; ++packet) {
        const at = packet * 4096;
        view.setUint32(at, 0x31564157, true); view.setUint32(at + 4, 7, true);
        uint64(at + 8, 808000 + packet * 48000); uint64(at + 24, 1000000);
        view.setInt32(at + 16,16000,true); view.setInt32(at + 20,32000,true);
        view.setUint32(at + 32,packet * 3 + 1,true);
        view.setFloat32(at + 36,.25,true); view.setFloat32(at + 40,.1,true);
        view.setUint16(at + 52,8000,true); view.setUint16(at + 54,384,true);
        bytes.set([1,packet,7,127],at + 56);
        const values = [-2048,2047,-1,0,1,1200,-1200];
        for (let index = 0; index < 2688; index += 2) {
            const a = values[index % 7] & 4095, b = values[(index + 1) % 7] & 4095, offset = at + 64 + index / 2 * 3;
            bytes[offset] = a & 255; bytes[offset + 1] = a >> 8 | (b & 15) << 4; bytes[offset + 2] = b >> 4;
        }
        view.setUint32(at + 44,context.waveCrc32(bytes,at + 64,4032),true);
        view.setUint32(at + 48,context.waveCrc32(bytes,at,64,at + 48),true);
    }
    const footer = 126976;
    view.setUint32(footer,0x31435657,true); view.setUint32(footer + 4,7,true);
    uint64(footer + 8,1000000); uint64(footer + 16,2296000);
    bytes.set([1,31,1,0],footer + 24);
    view.setUint32(footer + 28,77,true); uint64(footer + 32,1000000); uint64(footer + 40,1792414800000000);
    view.setUint32(footer + 108,context.waveCrc32(bytes,footer,108),true);
    bytes.copyWithin(footer + 2048,footer,footer + 112);
    return bytes;
}

const bytes = waveFixture(), wave = context.decodeWaveEvent(bytes.buffer,77);
assert.equal(context.waveCrc32(new TextEncoder().encode('123456789'),0,9),0xcbf43926);
assert.equal(wave.samples.length,11904); assert.equal(wave.invalidPackets,0);
assert.equal(wave.samples[0].relativeMs,-192); assert.equal(wave.samples.at(-1).relativeMs,1295.875);
assert.equal(wave.samples[0].vr,511.75); assert.equal(wave.samples[0].vs,0); assert.equal(wave.samples[0].vt,300);
assert.ok(Math.abs(wave.samples[0].ir + 204.8) < .00001); assert.ok(wave.samples[0].inn < 0);
assert.throws(() => context.decodeWaveEvent(bytes.buffer,78),/no pertenece/);
const bad = bytes.slice(); bad[4096 + 100] ^= 1;
const partial = context.decodeWaveEvent(bad.buffer,77);
assert.equal(partial.invalidPackets,1); assert.equal(partial.samples.filter(point => point.vr === null).length,1);
const orphan = bytes.slice(); orphan.fill(255,126976);
assert.throws(() => context.decodeWaveEvent(orphan.buffer,77),/cierre valido/);
const torn = bytes.slice(); torn[129024] ^= 1;
assert.throws(() => context.decodeWaveEvent(torn.buffer,77),/incompleto/);
const header = bytes.slice(); header[56] = 2;
assert.equal(context.decodeWaveEvent(header.buffer,77).invalidPackets,1);
console.log('PASS: Wave 128 KiB, 31x48 ms, signed12, escalas, CRC32, huecos, cierre atomico y enlace FAST.');
module.exports = {waveFixture};
