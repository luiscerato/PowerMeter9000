const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [process.cwd(), runtime]}));

// Prueba persistencia real del navegador, aislamiento y fallos; no recibe parametros ni consulta el ESP.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try {
        const context = await browser.newContext(), page = await context.newPage();
        await page.context().route('http://cache.test/**', route => route.fulfill({contentType: 'text/html', body: '<!doctype html><title>Cache test</title>'}));
        await page.goto('http://cache.test/');
        const source = fs.readFileSync('data/capture_cache.js', 'utf8');
        const setup = `
            window.scope = {ready:true, deviceId:'aabbccddeeff', fastEpoch:'123456789abcdef0', waveEpoch:'fedcba9876543210'};
            window.calls = 0; window.partial = false;
            window.fetchEventHistory = async (path, signal, binary) => {
                if (signal.aborted) throw new DOMException('Cancelado','AbortError');
                if (!binary) return scope;
                ++calls;
                return {identity:scope.deviceId+':'+scope.fastEpoch, buffer:new Uint8Array([partial ? 2 : 1, 3, 4]).buffer};
            };
            window.readTest = (key, signal = new AbortController().signal) => cachedCapture(key, scope.deviceId+':'+scope.fastEpoch,
                '/binary', signal, buffer => {
                    const value = new Uint8Array(buffer)[0];
                    if (!value) throw Error('CRC invalido');
                    return {partial:value === 2};
                }, decoded => !decoded.partial);
        `;
        await page.addScriptTag({content: source}); await page.addScriptTag({content: setup});
        await page.evaluate(async () => {
            window.key = captureCacheKey(scope, 'fast', [4, 1, '1000.0']);
            await readTest(key); await readTest(key);
        });
        assert.equal(await page.evaluate(() => calls), 1);
        await page.reload();
        await page.addScriptTag({content: source}); await page.addScriptTag({content: setup});
        await page.evaluate(async () => {
            window.key = captureCacheKey(scope, 'fast', [4, 1, '1000.0']);
            await readTest(key);
        });
        assert.equal(await page.evaluate(() => calls), 0, 'sobrevive una recarga sin volver a descargar');
        const result = await page.evaluate(async () => {
            const firstKey = key;
            scope.deviceId = '112233445566'; key = captureCacheKey(scope, 'fast', [4, 1, '1000.0']); await readTest(key);
            const secondKey = key;
            scope.fastEpoch = '1111111111111111'; key = captureCacheKey(scope, 'fast', [4, 1, '1000.0']); await readTest(key);
            const fastKey = key, waveKey = captureCacheKey(scope, 'wave', [scope.fastEpoch, 1]);
            scope.waveEpoch = '2222222222222222';
            const waveChanged = waveKey !== captureCacheKey(scope, 'wave', [scope.fastEpoch, 1]);
            const fastSame = fastKey === captureCacheKey(scope, 'fast', [4, 1, '1000.0']);
            await writeCaptureBuffer(key, new Uint8Array([0]).buffer); await readTest(key);
            partial = true; await readTest('partial'); await readTest('partial'); partial = false;
            const incomplete = await readCaptureBuffer('partial');
            const beforeAbort = calls, controller = new AbortController(); controller.abort();
            let aborted = false;
            try { await readTest('abort', controller.signal); } catch (error) { aborted = error.name === 'AbortError'; }
            captureCacheState.limit = 6; captureCacheState.maxEntries = 2;
            await writeCaptureBuffer('lru1', new Uint8Array(3).buffer);
            await writeCaptureBuffer('lru2', new Uint8Array(3).buffer);
            await writeCaptureBuffer('lru3', new Uint8Array(3).buffer);
            const evicted = await readCaptureBuffer('lru1'), newest = await readCaptureBuffer('lru3');
            return {separate:firstKey !== secondKey && secondKey !== fastKey, waveChanged, fastSame, incomplete,
                aborted, abortCalls:calls-beforeAbort, calls, evicted, newest:!!newest};
        });
        assert.equal(result.separate, true); assert.equal(result.waveChanged, true); assert.equal(result.fastSame, true);
        assert.equal(result.incomplete, null); assert.equal(result.aborted, true); assert.equal(result.abortCalls, 0);
        assert.equal(result.calls, 5); assert.equal(result.evicted, null); assert.equal(result.newest, true);
        const fallback = await page.context().newPage(); await fallback.goto('http://cache.test/');
        await fallback.evaluate(() => Object.defineProperty(window, 'indexedDB', {value: undefined}));
        await fallback.addScriptTag({content: source}); await fallback.addScriptTag({content: setup});
        assert.equal(await fallback.evaluate(async () => { await readTest('ram'); await readTest('ram'); return calls; }), 1);
        assert.equal(await fallback.evaluate(async () => { await readTest(null); await readTest(null); return calls; }), 3);
        await fallback.close(); await page.close();
        console.log('PASS: IndexedDB persistente, aislamiento, CRC, parciales, cancelacion, LRU y respaldo RAM');
    } finally { await browser.close(); }
}

run().catch(error => { console.error(error); process.exitCode = 1; });
