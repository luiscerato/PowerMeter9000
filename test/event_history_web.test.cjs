const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {codec, gridFixture} = require('./grid_fixture.cjs');
const {waveFixture} = require('./wave_events.test.cjs');
const vm = require('node:vm');
const waveCodec = vm.createContext({Uint8Array, DataView});
vm.runInContext(fs.readFileSync('data/wave_events_ui.js', 'utf8'), waveCodec);
const root = path.join(__dirname, '..');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [root, runtime]}));
const start = Math.floor(Date.now() / 1000) * 1000 - 3600000;

// Crea una captura real con CRC para id y at; previous/current y duration representan el estado de R.
function makeCapture(id, at, previous, current, duration) {
    const bytes = gridFixture(), view = new DataView(bytes.buffer);
    view.setUint32(1, id, true); view.setUint32(5, Math.floor(at / 1000), true);
    view.setUint8(13, 32); view.setUint16(11, previous << 3 | current << 6, true);
    view.setUint32(14, duration / 10, true);
    view.setUint16(28, codec.gridCrc16(bytes, 0, 28), true);
    for (const page of [1, 2]) {
        const at = page * 2048;
        view.setUint32(at, id, true); view.setUint8(at + 7, 0);
        view.setUint16(at + 8, codec.gridCrc16(bytes, at, 170, at + 8), true);
    }
    return Buffer.from(bytes);
}

// Crea una Wave compacta valida para eventId con cierre y CRC; usa muestras conocidas del fixture legado.
function makePreviewWave(eventId, waveId = 100 + eventId, offset = 0) {
    const old = waveFixture(), bytes = new Uint8Array(65536), view = new DataView(bytes.buffer);
    for (let packet = 0; packet < 31; ++packet) {
        const at = packet * 2048;
        view.setUint32(at, 0x32564157, true); view.setUint32(at + 4, waveId, true);
        view.setUint32(at + 8, 808000 + offset + packet * 24000, true);
        view.setUint8(at + 28, 2); view.setUint8(at + 29, packet); view.setUint16(at + 30, 192, true);
        bytes.set(old.subarray(packet * 4096 + 64, packet * 4096 + 64 + 2016), at + 32);
        view.setUint32(at + 20, waveCodec.waveCrc32(bytes, at + 32, 2016), true);
        view.setUint32(at + 24, waveCodec.waveCrc32(bytes, at, 32, at + 24), true);
    }
    const footer = 31 * 2048;
    bytes.set(old.subarray(31 * 4096, 31 * 4096 + 112), footer);
    view.setUint32(footer + 4, waveId, true); view.setUint8(footer + 24, 2); view.setUint32(footer + 28, eventId, true);
    view.setUint32(footer + 8, view.getUint32(footer + 8, true) + offset, true);
    view.setUint32(footer + 108, waveCodec.waveCrc32(bytes, footer, 108), true);
    return Buffer.from(bytes);
}

// Prueba carga progresiva, union, filtros, cancelacion y acceso a capturas en escritorio y movil; sin parametros.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try {
        for (const width of [1280, 390]) {
            const page = await browser.newPage({viewport: {width, height: 900}}), errors = [], order = [];
            let active = 0, peak = 0;
            const waveOrder = [];
            const fixtures = {1: makeCapture(1, start, 1, 4, 1000), 2: makeCapture(2, start + 30000, 4, 1, 30000)};
            page.on('pageerror', error => errors.push(error.message));
            await page.route('http://app.test/**', async route => {
                const url = new URL(route.request().url());
                if (url.pathname === '/grid-events') return route.fulfill({json: {eventCount: 2, oldestId: 1, newestId: 2}});
                if (url.pathname === '/grid-events/list') return route.fulfill({json: {events: [{i: 1, t: start}, {i: 2, t: start + 30000}]}});
                if (url.pathname === '/wave-events') {
                    const eventId = Number(url.searchParams.get('eventId'));
                    const ids = eventId === 1 ? [101, 201] : [101, 102];
                    return route.fulfill({json: {captures: ids.map(id => ({id, block: 1, page: 0, version: 2}))}});
                }
                if (url.pathname === '/wave-events/data') {
                    const id = Number(url.searchParams.get('waveId')); waveOrder.push(id);
                    ++active; peak = Math.max(peak, active);
                    await new Promise(resolve => setTimeout(resolve, 50)); --active;
                    return route.fulfill({body: id === 201 ? makePreviewWave(1, 201, 800000) : makePreviewWave(id - 100), contentType: 'application/octet-stream'}).catch(() => {});
                }
                if (url.pathname === '/grid-events/data') {
                    const id = Number(url.searchParams.get('eventId')); order.push(id);
                    ++active; peak = Math.max(peak, active);
                    await new Promise(resolve => setTimeout(resolve, 350));
                    --active;
                    return route.fulfill({body: fixtures[id], contentType: 'application/octet-stream'}).catch(() => {});
                }
                const file = path.join(root, 'data', url.pathname === '/' ? 'index.html' : url.pathname.slice(1));
                if (!fs.existsSync(file)) return route.fulfill({status: 404, body: ''});
                return route.fulfill({body: fs.readFileSync(file), contentType: {'.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css'}[path.extname(file)] || 'application/octet-stream'});
            });
            await page.goto('http://app.test/');
            await page.locator('#appRoot').waitFor({state: 'visible'});
            if (!await page.locator('#eventHistory-tab').isVisible()) await page.locator('.navbar-toggler').click();
            await page.locator('#eventHistory-tab').click();
            await page.waitForFunction(() => eventHistory.captures.length === 1 && eventHistory.controller !== null);
            assert.equal(await page.locator('#eventHistoryRows tr').count(), 1);
            await page.waitForFunction(() => !eventHistory.controller && eventHistory.captures.length === 2);
            assert.deepEqual(order, [2, 1]); assert.equal(peak, 1);
            assert.equal(await page.locator('#eventHistoryRows tr').count(), 1);
            assert.match(await page.locator('#eventHistoryRows').textContent(), /30.0 s/);
            await page.evaluate(() => {eventHistory.captures[0].partial=true;renderEventHistory();});
            await page.locator('#eventHistoryWarning').waitFor({state:'visible'});
            await page.locator('#eventHistoryWarning .btn-close').click();
            assert.equal(await page.locator('#eventHistoryWarning').count(),1,'cerrar no elimina el aviso del DOM');
            await page.evaluate(() => renderEventHistory());
            assert.equal(await page.locator('#eventHistoryWarning').isVisible(),false,'filtrar no repite el mismo aviso');
            await page.evaluate(() => {eventHistory.failures.push(99);renderEventHistory();});
            assert.equal(await page.locator('#eventHistoryWarning').isVisible(),true,'un problema distinto muestra su aviso');
            await page.evaluate(() => {eventHistory.captures[0].partial=false;eventHistory.failures=[];eventHistory.dismissedWarning=null;renderEventHistory();});
            assert.equal(await page.evaluate(() => eventHistory.captures.every(item => !item.samples)), true);
            assert.equal(await page.locator('#eventStatisticsSummary tr').count(), 4);
            assert.match(await page.locator('#eventStatisticsSummary tr').nth(0).textContent(), /30 s.*1 inicio/);
            assert.match(await page.locator('#eventStatisticsStatus').textContent(), /dia/);
            const requestsBeforeStatistics = order.length;
            await page.locator('#eventHistoryStats-tab').click();
            await page.waitForFunction(() => eventStatisticsCharts.length === 2);
            assert.ok(await page.locator('#eventStatisticsCountChart canvas').count() > 0);
            await page.locator('[data-stat-subject="1"]').click();
            await page.locator('[data-stat-subject="2"]').click();
            await page.locator('[data-stat-subject="3"]').click();
            await page.selectOption('#eventStatisticsUnit', 'hour');
            const statistics = await page.evaluate(() => {
                const result = buildEventStatistics(eventHistory.result.intervals, eventHistory.range.from, eventHistory.range.to, 'hour', 0);
                return {rows: result.rows.length, duration: result.rows.reduce((sum, row) => sum + row.values.outage.durationMs, 0),
                    starts: result.rows.reduce((sum, row) => sum + row.values.outage.starts, 0)};
            });
            assert.equal(await page.locator('#eventStatisticsRows tr').count(), statistics.rows);
            assert.equal(statistics.duration, 30000); assert.equal(statistics.starts, 1);
            const durationDisplay = await page.evaluate(() => {
                const axis = eventStatisticsCharts[1].options.axisY;
                return {title: axis.title, label: axis.labelFormatter({value: 90}),
                    point: eventStatisticsCharts[1].options.data[0].dataPoints.find(point => point.y > 0)};
            });
            assert.equal(durationDisplay.title, 'Tiempo');
            assert.equal(durationDisplay.label, '1 min 30 s');
            assert.equal(durationDisplay.point.y, 30);
            assert.equal(durationDisplay.point.tooltipText, '30 s');
            assert.equal(order.length, requestsBeforeStatistics);
            await page.selectOption('#eventStatisticsUnit', 'auto');
            await page.screenshot({path: 'tmp/event-statistics-charts-' + width + '.png', fullPage: true});
            await page.locator('#eventStatisticsTables-tab').click();
            assert.equal(await page.locator('#eventStatisticsSummary').isVisible(), true);
            await page.locator('#eventHistoryEvents-tab').click();
            await page.locator('#eventHistoryRows td:nth-child(3) button').click();
            assert.equal(await page.locator('#eventHistoryDetail button').count(), 2);
            await page.locator('#eventHistoryRows td:first-child button').click();
            await page.waitForFunction(() => !eventHistoryPreview.loading && eventHistoryPreview.charts.length === 2);
            assert.equal(await page.locator('#eventHistoryPreviewCapture').inputValue(), '1');
            assert.match(await page.locator('#eventHistoryPreviewWaveStatus').textContent(), /3 oscilografias.*17856 muestras Wave/);
            assert.match(await page.locator('#eventHistoryPreviewFastStatus').textContent(), /2 \/ 2 capturas FAST/);
            assert.deepEqual(waveOrder, [101, 201, 102]);
            const merged = await page.evaluate(() => ({events:eventHistoryPreview.events.length, waves:eventHistoryPreview.waves.length,
                gaps:eventHistoryPreview.charts[1].options.data[0].dataPoints.filter(point => point.y === null).length,
                maximum:eventHistoryPreview.charts[0].options.axisX.viewportMaximum}));
            assert.equal(merged.events, 2); assert.equal(merged.waves, 3); assert.ok(merged.gaps > 0); assert.ok(merged.maximum < 15000);
            assert.deepEqual(await page.locator('#eventHistoryPreviewSections button').allTextContents(), ['Inicio','Fin']);
            assert.match(await page.locator('#eventHistoryPreviewSectionStatus').textContent(), /Tramo 1 de 2/);
            await page.evaluate(() => {
                eventHistoryPreview.fastSegments.push({timestamp:eventHistoryPreview.item.start+60000,samples:[{relativeMs:0,vr:220}]});
                renderEventHistoryPreview();
            });
            assert.deepEqual(await page.locator('#eventHistoryPreviewSections button').allTextContents(), ['Inicio','Tramo 2','Fin']);
            await page.evaluate(() => {eventHistoryPreview.fastSegments.pop();renderEventHistoryPreview();});
            await page.getByRole('button', {name:'Fin',exact:true}).click();
            const endRange = await page.evaluate(() => { const section = eventHistoryPreview.sections[1]; return [section.start-50,section.end+50]; });
            assert.deepEqual(await page.evaluate(() => eventHistoryPreview.charts.map(chart =>
                [chart.options.axisX.viewportMinimum,chart.options.axisX.viewportMaximum])), [endRange,endRange]);
            const linked = await page.evaluate(() => {
                const chart = eventHistoryPreview.charts[1];
                chart.options.rangeChanged({trigger:'zoom',axisX:[{viewportMinimum:123,viewportMaximum:456}]});
                return [eventHistoryPreview.charts[0].options.axisX.viewportMinimum,eventHistoryPreview.charts[0].options.axisX.viewportMaximum];
            });
            assert.deepEqual(linked, [123,456]);
            const requestsBeforeSignals = order.length + waveOrder.length;
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases R"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['ir','vr']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases S"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['is','vs']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases S"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['is','vs']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases T"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['it','vt']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases N"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['inn','vn']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases R"]').click();
            await page.locator('#eventHistoryPreviewSignals [aria-label="Voltaje S"]').click();
            await page.locator('#eventHistoryPreviewSignals [aria-label="Corriente S"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['ir','is','vr','vs']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Corriente S"]').click();
            assert.deepEqual(await page.evaluate(() => eventHistoryPreview.charts.map(chart => chart.options.data.filter(series => series.visible).map(series => series.signalKey).sort())),
                [['ir','vr','vs'],['ir','vr','vs']]);
            assert.equal(await page.locator('#eventHistoryPreviewSignals [aria-label="Fases S"]').getAttribute('aria-pressed'),'mixed');
            assert.equal(await page.evaluate(() => eventHistoryPreview.charts.every(chart => chart.options.data.find(series => series.signalKey === 'ir').axisYType === 'secondary')), true);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Todas las senales de voltaje"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['vn','vr','vs','vt']);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Todas las senales de voltaje"]').click();
            assert.equal(await page.evaluate(() => eventHistoryPreview.signals.size), 4);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Todas las senales de corriente"]').click();
            assert.deepEqual(await page.evaluate(() => [...eventHistoryPreview.signals].sort()), ['inn','ir','is','it']);
            assert.equal(await page.evaluate(() => eventHistoryPreview.charts.every(chart => chart.options.axisY.title === 'Corriente (A)')), true);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Todas las senales de corriente"]').click();
            assert.equal(await page.evaluate(() => eventHistoryPreview.signals.size), 4);
            for (const phase of ['R','S','T','N']) await page.locator('#eventHistoryPreviewSignals [aria-label="Corriente '+phase+'"]').click();
            assert.match(await page.locator('#eventHistoryPreviewSignalStatus').textContent(), /Sin senales/);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Fases N"]').click();
            assert.deepEqual(await page.evaluate(() => eventHistoryPreview.charts.map(chart => chart.options.data.filter(series => series.visible).map(series => series.signalKey).sort())),
                [['inn','vn'],['inn']]);
            assert.match(await page.locator('#eventHistoryPreviewSignalStatus').textContent(), /solo disponible en FAST/);
            assert.equal(await page.locator('#eventHistoryPreviewSignals [aria-label="Fases RST"]').count(), 0);
            await page.locator('#eventHistoryPreviewSignals [aria-label="Todas las senales de voltaje"]').click();
            for (const phase of ['R','S','T','N']) await page.locator('#eventHistoryPreviewSignals [aria-label="Corriente '+phase+'"]').click();
            assert.equal(await page.evaluate(() => eventHistoryPreview.signals.size), 8);
            await page.evaluate(() => {
                const chart = eventHistoryPreview.charts[1]; chart.options.legend.itemclick({dataSeries:chart.options.data[0]});
            });
            assert.equal(await page.locator('#eventHistoryPreviewSignals [aria-label="Voltaje R"]').getAttribute('aria-pressed'), 'false');
            assert.deepEqual(await page.evaluate(() => [eventHistoryPreview.charts[0].options.axisX.viewportMinimum,eventHistoryPreview.charts[0].options.axisX.viewportMaximum]), [123,456]);
            assert.equal(order.length + waveOrder.length, requestsBeforeSignals);
            const signalLayout = await page.evaluate(() => {
                const bar = document.getElementById('eventHistoryPreviewSignals'), buttons = [...bar.querySelectorAll('button')];
                return {height:bar.getBoundingClientRect().height, rows:new Set(buttons.map(button => Math.round(button.getBoundingClientRect().top))).size,
                    overflow:document.documentElement.scrollWidth>innerWidth};
            });
            assert.ok(signalLayout.height <= 40); assert.equal(signalLayout.rows,1); assert.equal(signalLayout.overflow,false);
            await page.evaluate(() => {document.getElementById('eventHistoryPreviewSignals').scrollLeft=0;});
            await page.screenshot({path:'tmp/event-history-signals-'+width+'.png'});
            await page.locator('#eventHistoryPreviewPan').check();
            assert.equal(await page.evaluate(() => eventHistoryPreview.charts.every(chart => chart.options.panEnabled)), true);
            await page.locator('#eventHistoryPreviewPan').uncheck();
            await page.locator('#eventHistoryPreviewFull').click();
            assert.ok(await page.evaluate(() => eventHistoryPreview.charts[0].options.axisX.viewportMaximum >= 30000));
            const requestsBeforeExpand = order.length + waveOrder.length;
            await page.locator('#eventHistoryPreviewExpand').click();
            await page.waitForFunction(() => document.querySelector('#eventHistoryPreview .modal-fullscreen'));
            const expanded = await page.locator('#eventHistoryPreview .modal-dialog').boundingBox();
            assert.ok(Math.abs(expanded.width-width) <= 1); assert.ok(Math.abs(expanded.height-900) <= 1);
            assert.ok((await page.locator('#eventHistoryPreviewExpand').boundingBox()).x > width-120);
            assert.equal(await page.locator('#eventHistoryPreviewExpand').getAttribute('aria-pressed'),'true');
            await page.screenshot({path:'tmp/event-history-fullscreen-'+width+'.png'});
            await page.keyboard.press('Escape');
            assert.equal(await page.locator('#eventHistoryPreviewExpand').getAttribute('aria-pressed'),'false');
            assert.equal(await page.locator('#eventHistoryPreview').isVisible(),true);
            assert.equal(order.length + waveOrder.length,requestsBeforeExpand);
            assert.equal(await page.evaluate(() => showAppPage.current), 'eventHistory');
            await page.screenshot({path: 'tmp/event-history-preview-' + width + '.png', fullPage: true});
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state: 'hidden'});
            assert.equal(await page.evaluate(() => eventHistoryPreview.event), null);
            await page.locator('#eventHistoryRows td:nth-child(4) button').click();
            await page.waitForFunction(() => !eventHistoryPreview.loading && eventHistoryPreview.event?.id === 2 && eventHistoryPreview.charts.length === 2);
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state: 'hidden'});
            assert.equal(peak, 1);
            const periods = await page.evaluate(() => {
                const original = loadEventHistory; let calls = 0;
                loadEventHistory = () => { ++calls; };
                applyEventHistoryPreset('today');
                const today = document.getElementById('eventHistoryFrom').value, end = document.getElementById('eventHistoryTo').value;
                moveEventHistoryPeriod(-1); const previous = document.getElementById('eventHistoryFrom').value;
                moveEventHistoryPeriod(1); const next = document.getElementById('eventHistoryFrom').value;
                moveEventHistoryPeriod(-1); moveEventHistoryPeriod(0); const current = document.getElementById('eventHistoryFrom').value;
                applyEventHistoryPreset('thisWeek'); loadEventHistory = original;
                return {today, end, previous, next, current, calls};
            });
            assert.ok(periods.end.endsWith('23:59:59')); assert.ok(periods.today.endsWith('00:00'));
            assert.notEqual(periods.today, periods.previous); assert.equal(periods.today, periods.next); assert.equal(periods.today, periods.current);
            assert.equal(periods.calls, 6);
            await page.selectOption('#eventHistoryLane', '1:V');
            assert.match(await page.locator('#eventHistoryRows').textContent(), /No hay eventos/);
            await page.selectOption('#eventHistoryLane', 'all');
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
            await page.screenshot({path: 'tmp/event-history-' + width + '.png', fullPage: true});
            await page.locator('#eventHistoryLoad').click();
            await page.waitForFunction(() => document.getElementById('eventHistoryStatus').textContent.startsWith('Leyendo captura'));
            await page.locator('#eventHistoryStop').click();
            await page.waitForFunction(() => !eventHistory.controller);
            assert.match(await page.locator('#eventHistoryStatus').textContent(), /detenido/);
            await new Promise(resolve => setTimeout(resolve, 400));
            await page.locator('#eventHistoryLoad').click();
            await page.waitForFunction(() => !eventHistory.controller && eventHistory.captures.length === 2);
            await page.locator('#eventHistoryRows td:nth-child(4) button').click();
            await page.waitForFunction(() => eventHistoryPreview.event?.id === 2);
            await page.locator('#eventHistoryPreviewDetails').click();
            await page.waitForFunction(() => gridEventsViewer.selectedId === 2);
            assert.equal(await page.locator('#pageTitle').textContent(), 'Eventos');
            assert.deepEqual(errors, []);
            await page.close();
            console.log('PASS Historial web ' + width + ': progresivo, secuencial, cancelable y estadisticas sin nuevas descargas.');
        }
    } finally { await browser.close(); }
}

run().catch(error => { console.error(error); process.exitCode = 1; });
