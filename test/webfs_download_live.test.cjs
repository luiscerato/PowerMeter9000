const assert = require('node:assert/strict');
const fs = require('node:fs');
const {createHash} = require('node:crypto');
const base = process.env.METER_URL || 'http://192.168.1.90';

// Calcula la firma de bytes para comparar exactamente el recurso recibido y el archivo fuente.
function hash(bytes) { return createHash('sha256').update(bytes).digest('hex'); }

// Descarga path y comprueba estado, longitud y contenido; usa un plazo acotado por solicitud.
async function download(path) {
    const response = await fetch(base + '/' + path, {signal: AbortSignal.timeout(30000)});
    assert.equal(response.status, 200, path);
    const received = Buffer.from(await response.arrayBuffer());
    const expected = fs.readFileSync('data/' + path);
    assert.equal(received.length, expected.length, path + ' length');
    assert.equal(hash(received), hash(expected), path + ' content');
}

// Interrumpe una descarga grande para comprobar que la sesion y su RAM se pueden reutilizar.
async function abortDownload() {
    const controller = new AbortController();
    const response = await fetch(base + '/lib/canvasjs.min.js', {signal: controller.signal});
    assert.equal(response.status, 200);
    const reader = response.body.getReader();
    await reader.read();
    controller.abort();
    await reader.cancel().catch(() => {});
    await new Promise(resolve => setTimeout(resolve, 150));
}

// Prueba cancelaciones repetidas y la carga completa con bytes reales; no modifica la placa.
async function run() {
    for (let index = 0; index < 8; ++index) {
        await abortDownload();
        await download('lib/jquery.simplegauge.css');
    }
    const html = fs.readFileSync('data/index.html', 'utf8');
    const styles = JSON.parse(html.match(/const bootStyles = (\[[^;]+\])/)[1]);
    const scripts = JSON.parse(html.match(/const bootScripts = (\[[^;]+\])/)[1]);
    for (const path of [...styles, ...scripts]) await download(path);
    console.log('PASS: ocho cancelaciones recuperadas y todos los assets completos, con SHA-256 correcto.');
}

run().catch(error => { console.error(error); process.exitCode = 1; });
