const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");
const vm = require("node:vm");

const source = fs.readFileSync(path.join(__dirname, "../data/logs_ui.js"), "utf8");
const context = vm.createContext({TextDecoder, URLSearchParams});
vm.runInContext(source, context);
const parseDiagnosticBlock = vm.runInContext("parseDiagnosticBlock", context);

// Calcula el mismo CRC32 de las páginas NAND; bytes es el tramo a validar.
function crc32(bytes, start, length, previous = 0) {
    let crc = (previous ^ 0xffffffff) >>> 0;
    for (let index = 0; index < length; ++index) {
        crc ^= bytes[start + index];
        for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}

// Crea un bloque con una página de RAM y la cabecera física del bloque NAND.
function makeBlock(unit) {
    const bytes = new Uint8Array(20 + 2 * 2048);
    bytes.fill(0xff, 20);
    const view = new DataView(bytes.buffer);
    bytes.set(Buffer.from("DLOG"), 0);
    bytes[4] = 1;
    bytes[5] = 1;
    bytes[6] = 1;
    view.setUint32(8, 27, true);
    view.setUint16(12, 9, true);
    view.setUint32(16, 31, true);

    const page = 20;
    bytes.set(Buffer.from("DPAG"), page);
    bytes[page + 4] = 1;
    bytes[page + 5] = 32;
    view.setUint16(page + 6, 1, true);
    view.setUint32(page + 8, 42, true);
    view.setUint32(page + 12, 77, true);
    view.setUint16(page + 16, 29, true);
    view.setUint16(page + 18, unit, true);
    const record = page + 32;
    view.setUint16(record, 29, true);
    view.setUint32(record + 2, 1001, true);
    bytes[record + 6] = 3;
    bytes[record + 8] = 1;
    view.setUint16(record + 9, 123, true);
    const unixUs = 1790000000123456n;
    view.setBigUint64(record + 11, unit ? 1000001n : 1000n, true);
    view.setBigUint64(record + 19, unit ? unixUs : unixUs / 1000n, true);
    bytes.set(Buffer.from("ok"), record + 27);
    view.setUint32(page + 28, crc32(bytes, record, 29, crc32(bytes, page, 28)), true);

    const block = 20 + 2048;
    bytes.set(Buffer.from("DLOG"), block);
    bytes[block + 4] = 1;
    bytes[block + 5] = 1;
    view.setUint16(block + 6, 40, true);
    view.setUint32(block + 20, 1001, true);
    bytes.set(Buffer.from("main"), block + 24);
    bytes[block + 28] = 0;
    view.setUint32(block + 16, crc32(bytes, block + 20, 20, crc32(bytes, block, 16)), true);
    return bytes;
}

test("decodifica timestamps en microsegundos y el diccionario del bloque", () => {
    const bytes = makeBlock(1);
    const result = parseDiagnosticBlock(bytes.buffer);
    assert.equal(result.logs.length, 1);
    assert.equal(result.logs[0].t, 1790000000123456);
    assert.equal(result.logs[0].u, 1000001);
    assert.equal(result.logs[0].n, "main");
    assert.equal(result.logs[0].x, "ok");
    assert.equal(result.next.session, 27);
    assert.equal(result.next.before, 9);
});

test("convierte timestamps de páginas anteriores en milisegundos", () => {
    const result = parseDiagnosticBlock(makeBlock(0).buffer);
    assert.equal(result.logs[0].t, 1790000000123000);
    assert.equal(result.logs[0].u, 1000000);
});

test("descarta una página dañada sin leer fuera del payload", () => {
    const bytes = makeBlock(1);
    new DataView(bytes.buffer).setUint16(20 + 16, 65535, true);
    const result = parseDiagnosticBlock(bytes.buffer);
    assert.equal(result.logs.length, 0);
    assert.equal(result.invalidPages, 1);
});
