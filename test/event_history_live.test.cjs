const assert = require('node:assert/strict');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [process.cwd(), runtime]}));

// Consulta el Historial real en baseUrl sin generar eventos ni cambiar ajustes; sin otros parametros.
async function run(baseUrl) {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try {
        const context = await browser.newContext();
        let previousIds = [], previousPartialIds = [];
        for (const width of [1280, 390]) {
            const page = await context.newPage(), errors = [];
            let fastDownloads = 0, waveDownloads = 0;
            const downloadedIds = [];
            page.on('request', request => {
                const url = new URL(request.url());
                if (url.pathname === '/grid-events/data') { ++fastDownloads; downloadedIds.push(Number(url.searchParams.get('eventId'))); }
                if (url.pathname === '/wave-events/data') ++waveDownloads;
            });
            await page.setViewportSize({width, height: 900});
            page.on('pageerror', error => errors.push(error.message));
            await page.goto(baseUrl + '/#eventHistory', {waitUntil: 'domcontentloaded', timeout: 30000});
            await page.locator('#appRoot').waitFor({state: 'visible', timeout: 60000});
            await page.waitForFunction(() => eventHistory.loaded && !eventHistory.controller, {}, {timeout: 120000});
            const result = await page.evaluate(() => ({status: document.getElementById('eventHistoryStatus').textContent,
                captures: eventHistory.captures.length, ids: eventHistory.captures.map(item => item.id),
                partialIds: eventHistory.captures.filter(item => item.partial).map(item => item.id),
                failures: eventHistory.failures, summary: document.getElementById('eventHistorySummary').textContent,
                joined: eventHistory.result.intervals.filter(item => item.state > 1 && item.startKnown && item.endKnown && item.ids.length > 1).length,
                rawSamples: eventHistory.captures.some(item => item.samples), overflow: document.documentElement.scrollWidth > innerWidth}));
            assert.match(result.status, /Analisis completo/);
            assert.ok(result.captures > 0); assert.deepEqual(result.failures, []);
            assert.equal(result.rawSamples, false); assert.equal(result.overflow, false);
            if(result.partialIds.length) {
                await page.locator('#eventHistoryWarning').waitFor({state:'visible'});
                await page.locator('#eventHistoryWarning .btn-close').click();
                await page.evaluate(()=>renderEventHistory());
                assert.equal(await page.locator('#eventHistoryWarning').isVisible(),false);
                assert.equal(await page.locator('#eventHistoryWarning').count(),1);
            }
            assert.equal(await page.evaluate(() => validCaptureCacheScope(eventHistory.cacheScope)), true);
            if (width === 390) assert.ok(downloadedIds.every(id => previousPartialIds.includes(id) || !previousIds.includes(id)), 'solo vuelve a descargar parciales o nuevas capturas');
            previousIds = result.ids; previousPartialIds = result.partialIds;
            assert.ok(result.joined > 0);
            assert.equal(await page.locator('#eventStatisticsSummary tr').count(), 5);
            assert.ok(await page.locator('#eventStatisticsRows tr').count() >= 7);
            const statistics = await page.evaluate(() => {
                const {from, to} = eventHistory.range;
                const result = buildEventStatistics(eventHistory.result.intervals, from, to, 'day', 4);
                return {total: result.summary[0].values.outage.durationMs,
                    daily: result.rows.reduce((sum, row) => sum + row.values.outage.durationMs, 0)};
            });
            assert.ok(statistics.total > 0);
            assert.ok(Math.abs(statistics.total - statistics.daily) < 1);
            await page.locator('#eventHistoryStats-tab').click();
            await page.waitForFunction(() => eventStatisticsCharts.length === 2);
            assert.ok(await page.locator('#eventStatisticsCountChart canvas').count() > 0);
            await page.screenshot({path: 'tmp/event-statistics-live-' + width + '.png', fullPage: true});
            await page.locator('#eventHistoryEvents-tab').click();
            await page.locator('#eventHistoryRows td:nth-child(4) button').first().click();
            await page.waitForFunction(() => !eventHistoryPreview.loading && eventHistoryPreview.event && eventHistoryPreview.charts.length >= 1, {}, {timeout: 60000});
            const preview = await page.locator('#eventHistoryPreviewWaveStatus').textContent();
            assert.doesNotMatch(preview, /No se pudo/);
            assert.equal(await page.locator('#eventHistoryPreviewWarnings').textContent(), '');
            const combined = await page.evaluate(() => ({ids:eventHistoryPreview.events.map(event => event.id),
                waves:eventHistoryPreview.waves.map(wave => wave.id), markers:eventHistoryPreview.markers.length}));
            const beforeFilters = fastDownloads + waveDownloads;
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases R"]').click();
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases S"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['is','vs']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases R"]').click();
            await page.locator('#eventHistoryPreviewSignals [aria-label="Voltaje S"]').click();
            await page.locator('#eventHistoryPreviewSignals [aria-label="Corriente S"]').click();
            await page.locator('#eventHistoryPreviewSignals [aria-label="Corriente S"]').click();
            assert.deepEqual(await page.evaluate(() => eventHistoryPreview.charts.map(chart => chart.options.data.filter(series => series.visible).map(series => series.signalKey).sort())),
                [['ir','vr','vs'],['ir','vr','vs']]);
            assert.equal(fastDownloads + waveDownloads, beforeFilters);
            await page.screenshot({path: 'tmp/event-preview-live-' + width + '.png', fullPage: true});
            await page.locator('#eventHistoryPreviewExpand').click();
            assert.equal(await page.locator('#eventHistoryPreviewExpand').getAttribute('aria-pressed'), 'true');
            await page.screenshot({path: 'tmp/event-preview-fullscreen-live-' + width + '.png'});
            await page.keyboard.press('Escape');
            assert.equal(await page.locator('#eventHistoryPreview').isVisible(), true);
            assert.equal(await page.locator('#eventHistoryPreviewExpand').getAttribute('aria-pressed'), 'false');
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state: 'hidden'});
            const fastBefore = fastDownloads, waveBefore = waveDownloads;
            await page.locator('#eventHistoryRows td:nth-child(4) button').first().click();
            await page.waitForFunction(() => !eventHistoryPreview.loading && eventHistoryPreview.event && eventHistoryPreview.charts.length >= 1, {}, {timeout: 60000});
            assert.equal(fastDownloads, fastBefore, 'reabrir no descarga FAST');
            assert.equal(waveDownloads, waveBefore, 'reabrir no descarga Wave');
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state: 'hidden'});
            await page.screenshot({path: 'tmp/event-history-live-' + width + '.png', fullPage: true});
            assert.deepEqual(errors, []);
            console.log('PASS live ' + width + ': ' + JSON.stringify({...result, combined, fastDownloads, waveDownloads}));
            await page.close();
        }
    } finally { await browser.close(); }
}

run(process.argv[2] || 'http://192.168.1.90').catch(error => { console.error(error); process.exitCode = 1; });
