const assert = require('node:assert/strict');
const base = process.env.METER_URL || 'http://192.168.1.90';

// Comprueba cache y revalidacion en la placa indicada por METER_URL; sin parametros.
async function run() {
    for (const path of ['/lib/jquery-3.6.4.min.js?v=vendor-1', '/lib/canvasjs.min.js?v=vendor-1']) {
        const first = await fetch(base + path, {signal: AbortSignal.timeout(30000)});
        assert.equal(first.status, 200);
        assert.equal(first.headers.get('cache-control'), 'public, max-age=31536000, immutable');
        const etag = first.headers.get('etag');
        assert.match(etag, /^(W\/)?"[0-9a-f]+"$/);
        await first.arrayBuffer();
        const second = await fetch(base + path, {headers: {'If-None-Match': etag}, signal: AbortSignal.timeout(10000)});
        assert.equal(second.status, 304);
        assert.equal((await second.arrayBuffer()).byteLength, 0);
    }
    for (const path of ['/', '/index.js', '/style.css']) {
        const response = await fetch(base + path, {signal: AbortSignal.timeout(30000)});
        assert.equal(response.status, 200);
        assert.equal(response.headers.get('cache-control'), 'private, no-cache, must-revalidate, no-transform');
        const etag = response.headers.get('etag');
        assert.match(etag, /^(W\/)?"[0-9a-f]+"$/);
        const body = await response.text();
        if (path === '/') assert.match(body, /const bootLibraryBuild = "vendor-1"/);
        const cached = await fetch(base + path, {headers: {'If-None-Match': etag}, signal: AbortSignal.timeout(10000)});
        assert.equal(cached.status, 304, path);
        assert.equal(cached.headers.get('etag'), etag);
        assert.equal((await cached.arrayBuffer()).byteLength, 0);
        const weak = await fetch(base + path, {headers: {'If-None-Match': etag.startsWith('W/') ? etag : 'W/' + etag}, signal: AbortSignal.timeout(10000)});
        assert.equal(weak.status, 304, path + ' weak ETag');
        await weak.arrayBuffer();
        const changed = await fetch(base + path, {headers: {'If-None-Match': '"outdated"'}, signal: AbortSignal.timeout(30000)});
        assert.equal(changed.status, 200, path);
        assert.equal(await changed.text(), body, path);
    }
    console.log('PASS: librerias cacheables; HTML y codigo propio revalidan con 304 o se descargan si cambia el ETag.');
}

run().catch(error => { console.error(error); process.exitCode = 1; });
