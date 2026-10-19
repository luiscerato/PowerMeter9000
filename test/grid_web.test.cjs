const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
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
            lossConfirmMs: 250, voltageConfirmMs: 5000, recoveryConfirmMs: 250, capturePages: 3,
            neutralEnabled: false, neutralHigh: 25, neutralHysteresis: 2, neutralConfirmMs: 100, neutralRecoveryMs: 250};
        let posts = 0, optionLoads = 0, failLoad = false, failSave = false;
        await page.route('http://meter.test/**', async route => {
            const request = route.request(), url = new URL(request.url());
            if (url.pathname === '/grid/config') {
                if ((failSave && request.method() === 'POST') || (failLoad && request.method() === 'GET'))
                    return route.fulfill({status: 500, body: 'Error de prueba'});
                if (request.method() === 'POST') {
                    posts++;
                    values = Object.fromEntries([...new URLSearchParams(request.postData())].map(([key, value]) => [key, Number(value)]));
                }
                return route.fulfill({json: values});
            }
            if (url.pathname === '/options') {
                optionLoads++;
                return route.fulfill({json: {}});
            }
            return route.fulfill({contentType: 'text/html', body: '<html><head><meta name="viewport" content="width=device-width, initial-scale=1"></head><body><div id="states"></div><div id="settings"></div></body></html>'});
        });
        await page.goto('http://meter.test/');
        await page.addStyleTag({path: path.join(root, 'data/lib/bootstrap.min.css')});
        await page.addStyleTag({path: path.join(root, 'data/style.css')});
        await page.addScriptTag({path: path.join(root, 'data/lib/jquery-3.6.4.min.js')});
        await page.addScriptTag({path: path.join(root, 'data/lib/bootstrap.bundle.min.js')});
        await page.addScriptTag({path: path.join(root, 'data/index.js')});
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
        assert.equal(optionLoads, 0);
        assert.equal(await page.inputValue('#grid_lowS'), '180');
        assert.equal(await page.isChecked('#grid_neutralEnabled'), false);
        assert.equal(await page.inputValue('#grid_neutralHigh'), '25');
        await page.click('#btnCopyGridLimits');
        assert.equal(await page.inputValue('#grid_highT'), '250');
        await page.fill('#grid_lowR', '195');
        await page.click('#gridSettingsForm button[type=submit]');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent.startsWith('Guardado.'));
        assert.equal(values.lowR, 195);
        assert.equal(values.lowS, 185);
        assert.equal(posts, 1);
        assert.equal(values.neutralEnabled, 0);
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
        await page.click('#settingsTab_capture');
        await page.waitForFunction(() => document.querySelector('#settings_capture .settingsStatus').textContent === 'Opciones cargadas.');
        assert.equal(optionLoads, 1);
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
            await page.screenshot({path: path.join(output, `settings-${width}.png`)});
            await page.locator('#grid_neutralHigh').scrollIntoViewIfNeeded();
            await page.screenshot({path: path.join(output, `neutral-${width}.png`)});
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true,
                JSON.stringify(await page.evaluate(() => [...document.querySelectorAll('*')].filter(el => el.getBoundingClientRect().right > innerWidth + 1).map(el => [el.tagName, el.id, el.className]).slice(0, 12))));
        }
        failSave = false;
        await page.fill('#grid_neutralHysteresis', '25');
        await page.click('#gridSettingsForm button[type=submit]');
        assert.match(await page.textContent('#gridSettingsStatus'), /histéresis de neutro/);
        assert.equal(posts, 1);
        await page.fill('#grid_neutralHysteresis', '2');
        await page.check('#grid_neutralEnabled');
        await page.click('#gridSettingsForm button[type=submit]');
        await page.waitForFunction(() => document.getElementById('gridSettingsStatus').textContent.startsWith('Guardado.'));
        assert.equal(values.neutralEnabled, 1);
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
        assert.deepEqual(errors, []);
        console.log('PASS: carga, guardado, copia por fase, validacion, errores, estados y vista movil/escritorio.');
    } finally {
        await browser.close();
    }
}
run().catch(error => { console.error(error); process.exitCode = 1; });
