const assert = require('node:assert/strict');
const base = process.env.METER_URL || 'http://192.168.1.90';

// Espera ms sin bloquear el proceso ni mantener una conexion al medidor.
function delay(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }

// Consulta path con conexion corta y plazo acotado; devuelve la respuesta HTTP.
function get(path) {
    return fetch(base + path, {headers: {Connection: 'close'}, signal: AbortSignal.timeout(30000)});
}

// Lee el heap total informado por la placa sin crear una sesion de descarga.
async function freeHeap() {
    const response = await get('/status');
    assert.equal(response.status, 200);
    return (await response.json()).free;
}

// Comprueba que una transferencia devuelva la RAM a before, tolerando buffers transitorios de red.
async function recovered(before, label) {
    await delay(1000);
    const after = await freeHeap();
    console.log(label, 'heap before=', before, 'after=', after);
    assert.ok(after >= before - 4096, label + ' retained transfer buffers');
}

// Descarga un bloque binario de logs desde path y devuelve su cabecera de cursor.
async function logs(path = '/debug/binary') {
    const response = await get(path);
    if (response.status === 204) return null;
    assert.equal(response.status, 200);
    const bytes = Buffer.from(await response.arrayBuffer());
    assert.equal(bytes.toString('ascii', 0, 4), 'DLOG');
    assert.equal((bytes.length - 20) % 2048, 0);
    assert.equal(bytes.length, 20 + (bytes[5] + bytes[6]) * 2048);
    return {token: bytes.readUInt32LE(8), block: bytes.readUInt16LE(12)};
}

// Interrumpe path despues de su cabecera binaria; devuelve esos bytes para conservar el cursor nuevo.
async function abort(path) {
    const controller = new AbortController();
    try {
        const response = await fetch(base + path, {headers: {Connection: 'close'},
            signal: AbortSignal.any([controller.signal, AbortSignal.timeout(30000)])});
        assert.equal(response.status, 200);
        const reader = response.body.getReader();
        let header = Buffer.alloc(0);
        while (header.length < 20) {
            const chunk = await reader.read();
            if (chunk.done) break;
            header = Buffer.concat([header, Buffer.from(chunk.value)]);
        }
        await reader.cancel();
        return header;
    } finally {
        controller.abort();
    }
}

// Verifica finalizacion, cancelaciones y continuidad sin snapshot; opcionalmente espera el TTL real.
async function run() {
    for (let index = 0; index < 3; ++index) {
        const before = await freeHeap();
        const response = await get('/history/data?last=5');
        assert.equal(response.status, 200);
        const bytes = await response.arrayBuffer();
        assert.ok(bytes.byteLength && bytes.byteLength % 2048 === 0);
        await recovered(before, 'history complete ' + index);
        await abort('/history/data?last=3600');
        await recovered(before, 'history abort ' + index);
    }
    const before = await freeHeap();
    const cursor = await logs();
    await recovered(before, 'logs complete');
    const previous = await logs(`/debug/binary?session=${cursor.token}&before=${cursor.block}`);
    await recovered(before, 'logs continue without snapshot');
    const current = previous || await logs();
    const interrupted = await abort(`/debug/binary?refresh=${current.token}`);
    assert.equal(interrupted.toString('ascii', 0, 4), 'DLOG');
    const refreshToken = interrupted.readUInt32LE(8);
    await recovered(before, 'logs abort');
    if (process.env.METER_CHECK_TTL === '1') {
        const fresh = await logs(`/debug/binary?refresh=${refreshToken}`);
        console.log('Waiting for the real 120-second cursor TTL...');
        await delay(122000);
        const expired = await get(`/debug/binary?session=${fresh.token}&before=${fresh.block}`);
        assert.equal(expired.status, 429);
        await expired.arrayBuffer();
        const renewed = await logs();
        assert.notEqual(renewed.token, fresh.token);
        await recovered(before, 'logs expired and renewed');
    }
    console.log('PASS: lectores completados/cancelados recuperan RAM y el cursor permite continuar sin snapshot.');
}

run().catch(error => { console.error(error); process.exitCode = 1; });
