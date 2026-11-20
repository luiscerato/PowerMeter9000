const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const {routeEvents}=require('./events_api_fixture.cjs');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [root, runtime]}));

// Prueba el formulario real con respuestas HTTP simuladas, sin conectarse al medidor; sin parametros.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try {
        const page = await browser.newPage({viewport: {width: 1280, height: 900}});
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        let values = {schemaVersion: 1, lowR: 185, highR: 250, lowS: 180, highS: 255,
            lowT: 190, highT: 260, noVoltage: 20, hysteresis: 5,
            lossConfirmMs: 250, pointLimitMs: 3700, recoveryConfirmMs: 250, capturePages: 5,
            neutralHigh: 25, neutralHysteresis: 2, neutralConfirmMs: 100, neutralRecoveryMs: 250};
        let posts = 0, optionLoads = 0, optionPosts = 0, reloads = 0, failOptions = false, failLoad = false, failSave = false;
        const meterOptions = {dipEnabled:1,dipMs:50,swellEnabled:1,swellMs:50,overcurrent:1,ocCurrent:50,ocHyst:1,ocMs:10};
        let waveSaveEnabled = true, wavePosts = 0, wavePointMask = 7;
        const eventState={grid:values,meter:meterOptions,wave:{preMs:192,saveEnabled:true,pointMask:7},
            onGet:()=>++optionLoads,failGet:()=>failLoad||failOptions,failPost:fields=>failSave&&fields.has('volt.lowR'),
            onPost:fields=>{
                if(fields.has('volt.lowR'))++posts;
                if(fields.has('rec.dip'))++optionPosts;
                if(fields.has('wave.on'))++wavePosts;
                waveSaveEnabled=eventState.wave.saveEnabled;wavePointMask=eventState.wave.pointMask;
            }};
        await page.route('http://meter.test/**', async route => {
            const request = route.request(), url = new URL(request.url());
            if(url.pathname.startsWith('/events/'))return routeEvents(route,eventState);
            if (url.pathname === '/grid/config') {
                if ((failSave && request.method() === 'POST') || (failLoad && request.method() === 'GET'))
                    return route.fulfill({status: 500, body: 'Error de prueba'});
                if (request.method() === 'POST') {
                    posts++;
                    values = Object.fromEntries([...new URLSearchParams(request.postData())].map(([key, value]) => [key, Number(value)]));
                    values.pointLimitMs = 3700; values.capturePages = 5;
                }
                return route.fulfill({json: values});
            }
            if (url.pathname === '/options') {
                if (request.method() === 'POST') {
                    ++optionPosts;
                    const fields = new URLSearchParams(request.postData());
                    for (const key of Object.keys(meterOptions)) if (fields.has(key)) meterOptions[key] = Number(fields.get(key));
                    return route.fulfill({body:'ok'});
                }
                optionLoads++;
                return route.fulfill(failOptions ? {status:500,body:'Error de prueba'} : {json:meterOptions});
            }
            if (url.pathname === '/options/reload') { ++reloads; return route.fulfill({body:'ok'}); }
            if (url.pathname === '/wave/config') {
                if (request.method() === 'POST') {
                    ++wavePosts;
                    waveSaveEnabled = new URLSearchParams(request.postData()).get('saveEnabled') === '1';
                    wavePointMask = Number(new URLSearchParams(request.postData()).get('pointMask'));
                    return route.fulfill({json:{ok:true,restartRequired:true}});
                }
                return route.fulfill({json:{preMs:192,actualPreMs:192,reservedBytes:16384,enabled:true,saveEnabled:waveSaveEnabled,pointMask:wavePointMask,
                    storedCaptures:17,occupiedSlots:20,totalSlots:640,
                    committed:2,orphans:0,gaps:0,droppedTriggers:0,failedWrites:0,erasedBlocks:4,pendingLinks:0}});
            }
            return route.fulfill({contentType: 'text/html', body: '<html><head><meta name="viewport" content="width=device-width, initial-scale=1"></head><body><div id="states"></div><div id="settings"></div></body></html>'});
        });
        await page.goto('http://meter.test/');
        await page.addStyleTag({path: path.join(root, 'data/lib/bootstrap.min.css')});
        await page.addStyleTag({path: path.join(root, 'data/style.css')});
        await page.addScriptTag({path: path.join(root, 'data/lib/jquery-3.6.4.min.js')});
        await page.addScriptTag({path: path.join(root, 'data/lib/bootstrap.bundle.min.js')});
        await page.addScriptTag({path: path.join(root, 'data/app_appearance.js')});
        await page.addScriptTag({path: path.join(root, 'data/history_cache.js')});
        await page.addScriptTag({path: path.join(root, 'data/index.js')});
        await page.addScriptTag({path: path.join(root, 'data/network_settings.js')});
        const html = fs.readFileSync(path.join(root, 'data/index.html'), 'utf8');
        await page.evaluate(html => {
            const doc = new DOMParser().parseFromString(html, 'text/html');
            document.getElementById('states').append(doc.querySelector('.gridStatusGrid'));
            const table = document.createElement('div'); table.id = 'powerTable'; table.hidden = true;
            document.body.append(table); createTable();
            window.board = {IP: 'http://meter.test', timeout: 500};
            initSettingsUI();
            loadSettings('grid');
        }, html);
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent === 'Ajustes cargados.');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent === 'Opciones cargadas.');
        assert.equal(optionLoads, 1); assert.equal(optionPosts,0);
        assert.equal(await page.locator('#settingsTab_capture').count(),0);
        assert.equal(await page.locator('#settings_grid #settings_capture #captureFields').count(),1);
        assert.match(await page.locator('#settingsTab_grid').textContent(),/Eventos de red/);
        assert.equal(await page.locator('#gridSettingsForm form').count(),0);
        assert.equal(await page.isChecked('#waveSaveEnabled'),true);
        assert.equal(await page.locator('.waveConfigToolbar #waveSaveEnabled').count(), 1);
        assert.equal(await page.locator('label[for=wavePreMs]').textContent(), 'Antes del evento');
        assert.match(await page.textContent('#wavePreHelp'), /anterior al disparo/);
        assert.equal(await page.isChecked('#waveDip'),true);
        assert.equal(await page.locator('#gridSettingsForm #opt_meter_ocCurrent').count(),1);
        assert.equal(await page.locator('#settings_capture #opt_meter_ocCurrent').count(),0);
        assert.doesNotMatch(await page.locator('#settings_capture .settingsSectionHeader').textContent(),/software|cada 10 ms/);
        for (const [record,wave] of [['dipEnabled','waveDip'],['swellEnabled','waveSwell'],['overcurrent','waveCurrent']]) {
            await page.uncheck('#opt_meter_'+record);
            assert.equal(await page.isDisabled('#'+wave),true);
            assert.equal(await page.isChecked('#'+wave),false);
            await page.check('#opt_meter_'+record);
            assert.equal(await page.isEnabled('#'+wave),true);
            assert.equal(await page.isChecked('#'+wave),false);
            await page.check('#'+wave);
        }
        for (const key of ['limits','points','wave']) {
            await page.click('.eventSettingsHelpButton[data-help="'+key+'"]');
            await page.locator('#eventSettingsHelp').waitFor({state:'visible'});
            assert.ok((await page.locator('#eventSettingsHelpBody p').count())>=4);
            await page.click('#eventSettingsHelp .btn-close');
            await page.locator('#eventSettingsHelp').waitFor({state:'hidden'});
        }
        assert.equal(optionPosts+posts+wavePosts,0,'la ayuda y los selectores no guardan automaticamente');
        await page.uncheck('#waveDip');
        assert.match(await page.textContent('#waveConfigMetrics'),/Oscilografías guardadas17/);
        await page.uncheck('#waveSaveEnabled');
        await page.click('#waveConfigForm button[type=submit]');
        await page.waitForFunction(() => document.getElementById('waveConfigStatus').textContent === 'Guardado y aplicado sin reiniciar.');
        assert.equal(wavePosts,1); assert.equal(waveSaveEnabled,false); assert.equal(wavePointMask,6);
        await page.click('#waveConfigRefresh');
        await page.waitForFunction(() => document.getElementById('waveConfigStatus').textContent === 'Nuevas capturas deshabilitadas');
        assert.equal(await page.isChecked('#waveSaveEnabled'),false);
        assert.equal(await page.isChecked('#waveDip'),false);
        assert.equal(await page.inputValue('#grid_lowS'), '180');
        assert.equal(await page.locator('#grid_neutralEnabled').count(), 0);
        assert.equal(await page.inputValue('#grid_neutralHigh'), '25');
        await page.click('#btnCopyGridLimits');
        assert.equal(await page.inputValue('#grid_highT'), '250');
        await page.fill('#grid_lowR', '195');
        await page.fill('#opt_meter_ocCurrent','60');
        await page.click('#gridSettingsForm button[type=submit]');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent.startsWith('Guardado.'));
        assert.equal(values.lowR, 195);
        assert.equal(values.lowS, 185);
        assert.equal(posts, 1);
        assert.equal(meterOptions.ocCurrent,60,'los limites guardan tambien sobrecorriente');
        assert.equal(Object.hasOwn(values, 'neutralEnabled'), false);
        assert.equal(values.neutralConfirmMs, 100);
        await page.fill('#grid_lowR', '249');
        await page.click('#gridSettingsForm button[type=submit]');
        assert.match(await page.textContent('#gridSettingsStatus'), /histéresis/);
        assert.equal(posts, 1);
        await page.click('#btnLoadGridSettings');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent === 'Ajustes cargados.');
        assert.equal(await page.inputValue('#grid_lowR'), '195');
        failSave = true;
        await page.click('#gridSettingsForm button[type=submit]');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent === 'Error de prueba');
        assert.equal(await page.isEnabled('#grid_lowR'), true);
        failLoad = true;
        await page.click('#btnLoadGridSettings');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent.includes('No se pudieron'));
        assert.equal(await page.isDisabled('#grid_lowR'), true);
        failLoad = false;
        await page.click('#btnLoadGridSettings');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent === 'Ajustes cargados.');
        await page.locator('#btnSaveMeterOptions').scrollIntoViewIfNeeded();
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent === 'Opciones cargadas.');
        await page.fill('#opt_meter_dipMs','100');
        await page.uncheck('#waveSwell');
        await page.click('#btnSaveMeterOptions');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent.includes('guardadas y aplicadas'));
        assert.equal(optionPosts,1); assert.equal(reloads,0); assert.equal(meterOptions.dipMs,100);
        assert.equal(wavePointMask,4); assert.equal(wavePosts,2);
        assert.equal(posts,1, 'guardar eventos no guarda limites');
        await page.fill('#opt_meter_dipMs','55');
        await page.click('#btnSaveMeterOptions');
        assert.equal(optionPosts,1); assert.match(await page.locator('#settings_capture .settingsStatus').textContent(),/Revise/);
        failOptions = true;
        await page.click('#btnLoadSettings');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent.includes('No se pudieron'));
        assert.equal(await page.isDisabled('#opt_meter_dipMs'),true);
        failOptions = false;
        await page.click('#btnLoadSettings');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent === 'Opciones cargadas.');
        assert.equal(await page.inputValue('#opt_meter_dipMs'),'100');
        await page.click('#settingsTab_grid');
        await page.evaluate(() => showGridStates({r: 'normal', s: 'undervoltage', t: 'phase_loss', overall: 'abnormal'},
            {r: 1551510, s: 90000, t: 1200, overall: 1200}));
        assert.equal(await page.textContent('#gridState_s'), 'Bajo voltaje');
        assert.equal(await page.textContent('#gridStateAge_r'), 'Desde hace 25 min');
        assert.equal(await page.textContent('#gridStateAge_s'), 'Desde hace 1 min');
        await page.evaluate(() => {
            window.updateMeterGauge = () => {};
            window.updatePhasorDiagram = () => {};
            window.formatNumber = (value, unit) => value.toFixed(2) + ' ' + unit;
            const group = {r: 230, s: 230, t: 230, n: 230.12, avg: 230, tot: 0};
            window.neutralFixture = {vrms: {...group}, vvrms: group, irms: group, pf: group,
                watt: group, var: group, va: group, freq: 50, fastVn: 229.75};
            meterOk(window.neutralFixture);
        });
        assert.equal(await page.textContent('.vrmsN'), '230.12 V');
        assert.equal(await page.textContent('.fastVrmsN'), '229.75 V');
        assert.equal(await page.locator('#states .fastVrmsN, #gridVoltageSpread').count(), 0);
        const output = path.join(root, '.pio/build/grid-web');
        fs.mkdirSync(output, {recursive: true});
        for (const width of [1280, 390]) {
            await page.setViewportSize({width, height: 900});
            const titleBox = await page.locator('#settings_capture h2').boundingBox();
            const saveBox = await page.locator('#btnSaveMeterOptions').boundingBox();
            assert.ok(Math.abs(titleBox.y - saveBox.y) < 16, 'iconos junto al encabezado');
            assert.equal(await page.locator('.settingsPointTable tbody tr').count(), 3);
            await page.screenshot({path: path.join(output, `settings-${width}.png`)});
            await page.locator('#gridSettingsForm').scrollIntoViewIfNeeded();
            await page.screenshot({path:path.join(output,`limits-${width}.png`)});
            await page.locator('#grid_neutralHigh').scrollIntoViewIfNeeded();
            await page.screenshot({path: path.join(output, `neutral-${width}.png`)});
            await page.locator('#waveConfigForm').scrollIntoViewIfNeeded();
            await page.screenshot({path: path.join(output, `wave-${width}.png`)});
            await page.locator('.eventSettingsHelpButton[data-help="wave"]').click();
            await page.locator('#eventSettingsHelp').waitFor({state:'visible'});
            await page.waitForFunction(() => getComputedStyle(document.getElementById('eventSettingsHelp')).opacity==='1' &&
                getComputedStyle(document.querySelector('#eventSettingsHelp .modal-dialog')).transform==='none');
            await page.screenshot({path:path.join(output,`help-${width}.png`)});
            await page.locator('#eventSettingsHelp .btn-close').click();
            await page.locator('#eventSettingsHelp').waitFor({state:'hidden'});
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true,
                JSON.stringify(await page.evaluate(() => [...document.querySelectorAll('*')].filter(el => el.getBoundingClientRect().right > innerWidth + 1).map(el => [el.tagName, el.id, el.className]).slice(0, 12))));
        }
        failSave = false;
        await page.fill('#grid_neutralHysteresis', '25');
        await page.click('#gridSettingsForm button[type=submit]');
        assert.match(await page.textContent('#gridSettingsStatus'), /histéresis de neutro/);
        assert.equal(posts, 1);
        await page.fill('#grid_neutralHysteresis', '2');
        await page.click('#gridSettingsForm button[type=submit]');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent.startsWith('Guardado.'));
        assert.equal(Object.hasOwn(values, 'neutralEnabled'), false);
        assert.equal(posts, 2);
        await page.evaluate(() => {
            window.neutralFixture.vrms.n = null;
            window.neutralFixture.fastVn = null;
            meterOk(window.neutralFixture);
        });
        assert.equal(await page.textContent('.vrmsN'), 'Sin datos');
        assert.equal(await page.textContent('.fastVrmsN'), 'Sin datos');
        await page.evaluate(() => meterFail());
        assert.equal(await page.textContent('#gridState_r'), 'Sin datos');
        assert.equal(await page.textContent('#gridStateAge_r'), '');
        for (const id of ['dipEnabled','swellEnabled','overcurrent']) await page.uncheck('#opt_meter_'+id);
        await page.click('#btnSaveMeterOptions');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent.includes('guardadas y aplicadas'));
        assert.equal(wavePointMask,0,'un registro deshabilitado no guarda Osc. activa');
        await page.click('#btnLoadSettings');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent==='Opciones cargadas.');
        for (const id of ['waveDip','waveSwell','waveCurrent']) {
            assert.equal(await page.isDisabled('#'+id),true);
            assert.equal(await page.isChecked('#'+id),false);
        }
        assert.deepEqual(errors, []);
        console.log('PASS: carga, guardado, copia por fase, validacion, errores, estados y vista movil/escritorio.');
    } finally {
        await browser.close();
    }
}
run().catch(error => { console.error(error); process.exitCode = 1; });
