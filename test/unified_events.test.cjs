const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
require('./incident_policy.test.cjs');
require('./grid_reading.test.cjs');
const {codec, gridFixture} = require('./grid_fixture.cjs');
const {waveFixture} = require('./wave_events.test.cjs');
const fixture = gridFixture(), decoded = codec.decodeGridEvent(fixture.buffer);
assert.equal(decoded.format,4); assert.equal(decoded.states.pointType,3);
assert.equal(decoded.previousDurationMs,150); assert.equal(decoded.samples.length,4); assert.equal(decoded.partial,false);
assert.equal(decoded.snapshot.frequency > 50,true); assert.equal(decoded.transitions[0].offsetMs,-20);
assert.equal(decoded.transitions[0].states.limited,true); assert.equal(decoded.transitions[1].states.isPoint,false);
assert.deepEqual(Array.from(decoded.settings.low),[190,190,190]); assert.equal(decoded.settings.pointLimitMs,3700);
assert.equal(decoded.settings.currentHysteresis,1); assert.equal(decoded.settings.ocMs,10);
assert.equal(decoded.settings.preMs,1130); assert.equal(decoded.settings.postMs,1000); assert.equal(decoded.settings.windowMs,5850);
assert.equal(codec.decodeGridStates(1024 | 1 << 3 | 5 << 6).isCurrent,true);
assert.equal(codec.gridStateName(5,0),'Alta corriente');
for (const version of [1,2,3]) {
    const old = fixture.slice(); old[0] = version;
    assert.throws(() => codec.decodeGridEvent(old.buffer),/no se admiten/);
}
const damaged = fixture.slice(); damaged[2048+18] ^= 1;
assert.equal(codec.decodeGridEvent(damaged.buffer).invalidPages,1);
const partial = fixture.slice(); partial.fill(255,2048);
assert.equal(codec.decodeGridEvent(partial.buffer).samples.length,2);
assert.equal(codec.decodeGridEvent(partial.buffer).partial,true);
const overflow = fixture.slice(), view = new DataView(overflow.buffer);
overflow[4096+7] |= 128; view.setUint16(4104,codec.gridCrc16(overflow,4096,170,4104),true);
assert.equal(codec.decodeGridEvent(overflow.buffer).timeStatus & 64,64);

// Comprueba codec, paneles y configuracion reales; sin parametros, escritorio y movil.
async function browserCheck() {
    const root = path.join(__dirname,'..');
    const runtime = path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
    const {chromium} = require(require.resolve('playwright',{paths:[root,runtime]}));
    const browser = await chromium.launch({headless:true,channel:'msedge'});
    try {
        for (const width of [1280,390]) {
            const page = await browser.newPage({viewport:{width,height:900}}), errors = [];
            page.on('pageerror',error => errors.push(error.message));
            await page.setContent('<html lang="es"><body><div class="container-fluid" id="host"></div></body></html>');
            for (const name of ['lib/bootstrap.min.css','style.css']) await page.addStyleTag({path:path.join(root,'data',name)});
            for (const name of ['lib/jquery-3.6.4.min.js','lib/canvasjs.min.js','chart_tooltips.js','grid_events_ui.js','wave_events_ui.js','capture_cache.js']) await page.addScriptTag({path:path.join(root,'data',name)});
            await page.evaluate(({html,bytes}) => {
                const doc = new DOMParser().parseFromString(html,'text/html'), section = doc.getElementById('gridEvents');
                section.classList.add('active'); document.getElementById('host').append(section);
                initGridEventsViewer();
                const event = decodeGridEvent(new Uint8Array(bytes).buffer), sample = event.samples[0];
                event.samples = Array.from({length:601}, (_, index) => ({...sample,relativeMs:index*10-1000}));
                renderGridEvent(event);
            },{html:fs.readFileSync('data/index.html','utf8'),bytes:Array.from(fixture)});
            assert.match(await page.locator('#gridEventSummary').textContent(),/Captura #77/);
            assert.match(await page.locator('#gridEventTrigger').textContent(),/Sobrecorriente/);
            assert.match(await page.locator('#gridEventOverview').textContent(),/Sin retorno registrado/);
            assert.match(await page.locator('#gridEventTransitions').textContent(),/Sin tensión/);
            assert.match(await page.locator('#gridEventWarnings').textContent(),/Prueba artificial/);
            const beforeMarkers = await page.evaluate(() => gridEventsViewer.voltageChart.axisX[0].get('stripLines').length);
            assert.equal(beforeMarkers,3);
            await page.locator('#gridEventMarkers').uncheck();
            assert.equal(await page.evaluate(() => gridEventsViewer.voltageChart.axisX[0].get('stripLines').length),0);
            await page.locator('#gridEventMarkers').check();
            await page.locator('#gridEventTransitions .gridEventRecord:not([disabled])').first().click();
            assert.equal(await page.locator('#gridEventTransitions .gridEventRecord[aria-pressed="true"]').count(),1);
            assert.equal(await page.evaluate(() => gridEventsViewer.selectedRecord),1);
            assert.deepEqual(await page.evaluate(() => [gridEventsViewer.voltageChart,gridEventsViewer.currentChart].map(chart => [chart.axisX[0].get('viewportMinimum'),chart.axisX[0].get('viewportMaximum')])),[[-70,110],[-70,110]]);
            await page.locator('#gridEventTransitions tr[data-record="3"] td:nth-child(2)').click();
            assert.deepEqual(await page.evaluate(() => [gridEventsViewer.voltageChart.axisX[0].get('viewportMinimum'),gridEventsViewer.voltageChart.axisX[0].get('viewportMaximum')]),[150,250]);
            await page.locator('#gridEventOverview [data-subject="0:A"]').click();
            assert.equal(await page.locator('#gridEventOverview [aria-pressed="true"]').count(),1);
            assert.deepEqual(await page.evaluate(() => [gridEventsViewer.voltageChart,gridEventsViewer.currentChart].map(chart => [chart.axisX[0].get('viewportMinimum'),chart.axisX[0].get('viewportMaximum')])),[[-50,200],[-50,200]]);
            await page.locator('#gridEventCurrentToggle').click();
            assert.equal(await page.locator('#gridEventCurrentChart').isVisible(),true);
            assert.deepEqual(await page.evaluate(() => [gridEventsViewer.currentChart.axisX[0].get('viewportMinimum'),gridEventsViewer.currentChart.axisX[0].get('viewportMaximum')]),[-50,200]);
            await page.locator('#gridEventCurrentToggle').click();
            await page.locator('#gridEventResetZoom').click();
            assert.equal(await page.evaluate(() => gridEventsViewer.voltageChart.axisX[0].get('viewportMinimum') <= -1000 && gridEventsViewer.voltageChart.axisX[0].get('viewportMaximum') >= 5000),true);
            const chart = await page.locator('#gridEventVoltageChart').boundingBox(), info = await page.locator('#gridEventContextDetails').boundingBox();
            assert.ok(info.y >= chart.y + chart.height); assert.ok(Math.abs(info.x-chart.x) < 1);
            await page.getByText('Contexto de medición',{exact:true}).click();
            await page.getByText('Configuración de captura',{exact:true}).click();
            assert.match(await page.locator('#gridEventSettings').textContent(),/3700 ms/);
            assert.match(await page.locator('#gridEventSettings').textContent(),/siempre/);
            await page.screenshot({path:path.join(root,'tmp/unified-events-'+width+'.png'),fullPage:true});
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),true);
            const cache = await page.evaluate(async bytes => {
                let requests = 0;
                window.board = {IP:''};
                refreshCaptureCacheScope = async () => ({ready:true,deviceId:'aabbccddeeff',fastEpoch:'123456789abcdef0',waveEpoch:'fedcba9876543210'});
                window.fetchEventHistory = async url => ({buffer:await (await fetch(url)).arrayBuffer(),identity:'aabbccddeeff:123456789abcdef0'});
                window.fetch = async () => {++requests;return {ok:true,arrayBuffer:async () => new Uint8Array(bytes).buffer};};
                const event = {i:77,t:decodeGridEvent(new Uint8Array(bytes).buffer).timestamp,y:'R',p:'N',s:'X'};
                gridEventsViewer.events = [event];
                await loadGridEvent(event); await loadGridEvent(event);
                const cachedRequests = requests;
                await loadGridEvent({...event,t:event.t+1});
                return {cachedRequests,requests,selected:gridEventsViewer.selectedId};
            },Array.from(fixture));
            assert.deepEqual(cache,{cachedRequests:1,requests:2,selected:77});
            const race = await page.evaluate(async bytes => {
                const first = new Uint8Array(bytes), second = first.slice(), view = new DataView(second.buffer);
                view.setUint32(1,78,true); view.setUint16(28,gridCrc16(second,0,28),true);
                for (const offset of [2048,4096]) {
                    view.setUint32(offset,78,true); view.setUint16(offset+8,gridCrc16(second,offset,170,offset+8),true);
                }
                let release;
                const wait = new Promise(resolve => {release=resolve;});
                gridEventsViewer.cache.clear(); captureCacheState.memory.clear(); captureCacheState.memoryBytes = 0;
                window.fetch = async url => {
                    if (url.endsWith('=77')) await wait;
                    return {ok:true,arrayBuffer:async () => url.endsWith('=77') ? first.buffer : second.buffer};
                };
                const timestamp = decodeGridEvent(first.buffer).timestamp;
                const slow = loadGridEvent({i:77,t:timestamp});
                await loadGridEvent({i:78,t:timestamp}); release(); await slow;
                const selected = gridEventsViewer.selectedId;
                gridEventsViewer.cache.clear(); captureCacheState.memory.clear(); captureCacheState.memoryBytes = 0; let partialRequests = 0;
                first.fill(255,2048);
                window.fetch = async () => {++partialRequests;return {ok:true,arrayBuffer:async () => first.buffer};};
                await loadGridEvent({i:77,t:timestamp}); await loadGridEvent({i:77,t:timestamp});
                return {selected,partialRequests,cached:gridEventsViewer.cache.size};
            },Array.from(fixture));
            assert.deepEqual(race,{selected:78,partialRequests:2,cached:0});
            await page.addScriptTag({path:path.join(root,'data/network_settings.js')});
            await page.addScriptTag({path:path.join(root,'data/index.js')});
            await page.addScriptTag({path:path.join(root,'data/app_appearance.js')});
            await page.addScriptTag({path:path.join(root,'data/history_cache.js')});
            const result = await page.evaluate(() => {
                const target = document.createElement('div'); target.id = 'settings'; target.hidden = true;
                document.body.append(target); target.innerHTML = createSettingsHtml();
                const values = Object.fromEntries(getMeterOptionKeys().map(key => [key,key === 'ocHyst' ? 1 : key === 'ocCurrent' ? 50 : 50]));
                fillMeterOptions(values);
                const input = $('#'+getOptionInputId('meter','dipMs'));
                input.val('2000'); const accepted = collectMeterOptionValues() !== null;
                input.val('2010'); const rejected = collectMeterOptionValues() === null;
                let importRejected = false;
                try {validateBackupSection('capture',{ocMs:2010});} catch (_) {importRejected = true;}
                return {accepted,rejected,importRejected,duplicateVoltages:getMeterOptionKeys().some(key => /^dip[RST]|^swell[RST]/.test(key)),
                    voltageTimer:document.getElementById('grid_voltageConfirmMs') !== null};
            });
            assert.deepEqual(result,{accepted:true,rejected:true,importRejected:true,duplicateVoltages:false,voltageTimer:false});
            if (process.argv[2]) {
                await page.evaluate(bytes => {
                    const event = decodeGridEvent(new Uint8Array(bytes).buffer);
                    gridEventsViewer.selectedId = event.id;
                    gridEventsViewer.events = [{i:event.id,t:event.timestamp,y:'R',p:'N',s:'X'}];
                    gridEventsViewer.cache.set(event.id,event);
                    document.querySelectorAll('.gridEventInfo').forEach(details => {details.open=false;});
                    renderGridEvent(event); renderGridEventsList();
                    setGridEventsStatus('Captura #' + event.id + ' cargada.');
                },Array.from(fs.readFileSync(process.argv[2])));
                await page.screenshot({path:path.join(root,'tmp/event-reading-real-'+width+'.png'),fullPage:true});
                const groupKey = await page.locator('#gridEventOverview [data-subject]').first().getAttribute('data-subject');
                await page.locator('#gridEventOverview [data-subject]').first().click();
                await page.screenshot({path:path.join(root,'tmp/event-zoom-real-'+width+'.png'),fullPage:true});
                const bytes = fs.readFileSync(process.argv[2]), event = codec.decodeGridEvent(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.length));
                const group = codec.buildGridOverview(event).find(item => item.key === groupKey);
                const range = codec.gridSelectionRange([...group.states,...group.points],event.samples[0].relativeMs,event.samples.at(-1).relativeMs,codec.gridEventRecords(event));
                assert.deepEqual(await page.evaluate(() => [gridEventsViewer.voltageChart.axisX[0].get('viewportMinimum'),gridEventsViewer.voltageChart.axisX[0].get('viewportMaximum')]),[range.minimum,range.maximum]);
                assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),true);
            }
            await page.evaluate(({waveBytes,fastBytes}) => {
                const event = decodeGridEvent(new Uint8Array(fastBytes).buffer);
                gridWaveViewer.event = event;
                renderGridWave(decodeWaveEvent(new Uint8Array(waveBytes).buffer,77),event);
                document.getElementById('gridEventWaveDetails').open = true;
                // Evita la consulta remota: aqui se verifica el bloque binario sintetico real.
                gridWaveViewer.event = null;
            },{waveBytes:Array.from(waveFixture()),fastBytes:Array.from(fixture)});
            await page.screenshot({path:path.join(root,'tmp/wave-events-'+width+'.png'),fullPage:true});
            assert.match(await page.locator('#gridWaveStatus').textContent(),/11904 muestras/);
            assert.equal(await page.evaluate(() => gridWaveViewer.chart.options.data.length),10);
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),true);
            assert.deepEqual(errors,[]); await page.close();
        }
    } finally {await browser.close();}
    console.log('PASS: formato unico, CRC, ajustes 48 bytes, corrientes y paneles/configuracion desktop/mobile.');
}
browserCheck().catch(error => {console.error(error);process.exitCode=1;});
