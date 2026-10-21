const fs = require('node:fs');
const vm = require('node:vm');
const codec = vm.createContext({Uint8Array, DataView});
vm.runInContext(fs.readFileSync('data/grid_events_ui.js', 'utf8'), codec);

// Construye tres paginas del formato unico; sin parametros, mezcla OC, un estado y DIP.
function gridFixture() {
    const bytes = new Uint8Array(6144), v = new DataView(bytes.buffer);
    v.setUint8(0, 4); v.setUint32(1, 77, true); v.setUint32(5, 1792414800, true);
    v.setUint16(11, 512 | 3 << 10, true); v.setUint8(13, 32 | 128);
    v.setUint32(14, 15, true); v.setUint16(18, 100, true); v.setUint16(20, 12000, true);
    v.setInt32(22, -10000, true); v.setUint8(26, 2); v.setUint8(27, 3);
    for (const at of [30, 32, 34]) v.setUint16(at, 1900, true);
    for (const at of [36, 38, 40]) v.setUint16(at, 2500, true);
    for (const [at, value] of [[42,200],[44,50],[46,25],[48,25],[50,370],[52,500],[54,100],
        [60,250],[62,20],[64,10],[66,25],[68,113],[70,100],[74,585]]) v.setUint16(at, value, true);
    v.setUint8(56,5); v.setUint8(57,7); v.setUint8(58,1); v.setUint8(59,15); v.setUint8(72,5);
    v.setFloat32(170,50.01,true);
    // Empaqueta ocho valores de 14 bits; at/time indican posicion y reloj de la muestra.
    const sample = (at, time) => {
        v.setUint16(at,time,true); let packed = 0n;
        [8800,8800,8800,100,100,100,300,800].forEach((raw,index) => packed |= BigInt(raw) << BigInt(index * 14));
        for (let index = 0; index < 14; ++index) bytes[at + 2 + index] = Number(packed >> BigInt(index * 8) & 255n);
    };
    sample(234,65486); sample(250,50);
    v.setUint16(266,codec.gridCrc16(bytes,30,236),true); v.setUint16(28,codec.gridCrc16(bytes,0,28),true);
    for (let page = 1; page <= 2; ++page) {
        const at = page * 2048;
        v.setUint32(at,77,true); v.setUint8(at+4,page); v.setUint8(at+5,3);
        v.setUint8(at+6,1); v.setUint8(at+7,1);
        v.setInt16(at+10,page === 1 ? 200 : -20,true); v.setUint32(at+12,page === 1 ? 100 : 8,true);
        v.setUint16(at+16,page === 1 ? 1 | 1 << 3 | 4 << 6 : 512 | 1 << 10 | 32768,true);
        v.setUint16(at+18,23000,true); v.setUint16(at+20,page === 1 ? 0 : 17000,true);
        sample(at+154,page === 1 ? 150 : 250);
        v.setUint16(at+8,codec.gridCrc16(bytes,at,170,at+8),true);
    }
    return bytes;
}
module.exports = {codec, gridFixture};
