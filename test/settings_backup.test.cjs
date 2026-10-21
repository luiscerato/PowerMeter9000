const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [root, runtime]}));

// Prueba exportacion, validacion y restauracion secuencial con respuestas HTTP simuladas; sin parametros.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    const context = await browser.newContext({acceptDownloads: true});
    try {
        const page = await context.newPage();
        const calls = [];
        const networkKeys = new Set(['sta-ssid', 'sta-pass', 'ap-pass', 'ota-pass', 'time-server', 'debug-pass', 'mqtt-pass', 'mqtt-server', 'mqtt-user', 'ota-name', 'debug-name', 'ap-ssid', 'sta-ip', 'sta-mask', 'sta-gw', 'sta-dns1', 'sta-dns2', 'ap-ip', 'ap-mask', 'ap-gw']);
        const boolKeys = new Set(['staticIp', 'ap-hide', 'ap-custom-ip', 'ota-enabled', 'time-sync', 'debug-enabled', 'mqtt-enabled', 'dipEnabled', 'swellEnabled', 'overcurrent']);
        const signedKeys = new Set(['wifi-mode', 'time-dsttime', 'time-minwest', 'dipMs', 'swellMs', 'ocMs']);
        const unsignedKeys = new Set(['time-start', 'mqtt-port', 'eventPrevSamples', 'eventPostSamples', 'eventMaxSamples']);
        const floatKeys = new Set(['ocCurrent', 'ocHyst']);
        await page.route('http://meter.test/**', async route => {
            const request = route.request(), url = new URL(request.url());
            if (url.pathname === '/network/config' && request.method() === 'GET') {
                const values = {};
                for (const key of networkKeys) values[key] = key === 'sta-pass' ? 'clear"text' : key + '-value';
                for (const key of ['staticIp', 'ap-hide', 'ap-custom-ip', 'ota-enabled', 'time-sync', 'debug-enabled', 'mqtt-enabled']) values[key] = true;
                for (const key of ['wifi-mode', 'time-dsttime', 'time-minwest']) values[key] = -180;
                values['time-start'] = 5;
                values['mqtt-port'] = 1883;
                return route.fulfill({json: values});
            }
            if (url.pathname === '/network/config' && request.method() === 'POST') {
                assert.equal(url.search, '');
                const form = new URLSearchParams(request.postData());
                assert.equal(form.get('sta-pass'), 'clear"text');
                assert.equal(form.has('ns'), false);
                calls.push('network');
                return route.fulfill({json: {ok: true, written: form.size, restartRequired: true}});
            }
            if (url.pathname === '/options' && request.method() === 'GET') {
                const namespace = url.searchParams.get('ns');
                const values = {};
                for (const key of url.searchParams.keys()) {
                    if (key === 'ns') continue;
                    if (namespace === 'ADE9000') values[key] = ['neutralRms', 'neutralFast'].includes(key) ? 1.17 : -1;
                    else if (networkKeys.has(key)) values[key] = key === 'sta-pass' ? 'clear"text' : key + '-value';
                    else if (boolKeys.has(key)) values[key] = 1;
                    else if (signedKeys.has(key)) values[key] = namespace === 'meter' ? 50 : -180;
                    else if (unsignedKeys.has(key)) values[key] = 5;
                    else if (floatKeys.has(key)) values[key] = key === 'ocCurrent' ? 85 : 1;
                }
                if (namespace === 'meter') {
                    values.eventPrevSamples = 'key no existe';
                    values.eventPostSamples = 'key no existe';
                }
                return route.fulfill({json: values});
            }
            if (url.pathname === '/options' && request.method() === 'POST') {
                const form = new URLSearchParams(request.postData());
                if (form.get('ns') === 'ADE9000') {
                    assert.equal(form.get('neutralRms'), '1.17');
                    assert.equal(form.get('neutralFast'), '1.17');
                }
                calls.push('opts:' + form.get('ns'));
                return route.fulfill({json: {ok: true, written: form.size - 1}});
            }
            if (url.pathname === '/grid/config') {
                if (request.method() === 'POST') calls.push('grid');
                return route.fulfill({json: {lowR: 185, highR: 250, lowS: 180, highS: 255, lowT: 190, highT: 260,
                    noVoltage: 20, hysteresis: 5, lossConfirmMs: 250, pointLimitMs: 3700, recoveryConfirmMs: 250, capturePages: 5,
                    neutralEnabled: false, neutralHigh: 25, neutralHysteresis: 2, neutralConfirmMs: 100, neutralRecoveryMs: 250}});
            }
            if (url.pathname === '/history/config') {
                if (request.method() === 'POST') {
                    const form = new URLSearchParams(request.postData());
                    assert.equal(form.has('groupConfig'), false);
                    assert.equal(form.get('samplePeriod'), '1');
                    assert.equal(form.get('voltageMode'), 'basic');
                    calls.push('history');
                }
                return route.fulfill({json: {enabled: true, samplePeriod: 1, voltageMode: 'basic',
                    activePower: 'full', reactivePower: 'full', apparentPower: 'off', powerFactor: true,
                    voltageThd: false, currentThd: false, frequency: 'full', voltageAngles: false,
                    battery: false, energy: true, energyExtraMode: 1}});
            }
            if (url.pathname === '/meter') return route.fulfill({json: {
                wattH: {r: 10, s: 20, t: 30, tot: 60}, varH: {r: 1, s: 2, t: 3, tot: 6}, vaH: {r: 11, s: 22, t: 33, tot: 66}
            }});
            if (url.pathname === '/options/reload') return route.fulfill({body: 'ok'});
            if (url.pathname === '/restart') return route.fulfill({json: {ok: true}});
            return route.fulfill({status: 404, body: 'Not found'});
        });
        await page.goto('http://meter.test/');
        await page.addScriptTag({path: path.join(root, 'data/lib/jquery-3.6.4.min.js')});
        await page.addScriptTag({path: path.join(root, 'data/lib/bootstrap.bundle.min.js')});
        await page.addScriptTag({path: path.join(root, 'data/index.js')});
        await page.evaluate(() => {
            document.body.innerHTML = '<div id="settings"></div>';
            window.board = {IP: 'http://meter.test', timeout: 1000};
            initSettingsUI();
        });
        assert.match(await page.textContent('#settings_backup'), /texto plano/);
        assert.doesNotMatch(await page.textContent('#settings_backup'), /Origen de la web/i);
        const downloadPromise = page.waitForEvent('download', {timeout: 6000});
        await page.click('#btnExportSettingsBackup');
        let download;
        try { download = await downloadPromise; }
        catch (error) {
            console.error('Export status:', await page.textContent('#settingsBackupStatus'));
            throw error;
        }
        const backupPath = await download.path();
        const backup = JSON.parse(fs.readFileSync(backupPath, 'utf8'));
        assert.equal(backup.format, 'pm9000-config');
        assert.equal(backup.sections.network['sta-pass'], 'clear"text');
        assert.equal(backup.sections.energy.WattH, 60);
        assert.equal(backup.sections.calibration.neutralRms, 1.17);
        assert.equal(backup.sections.calibration.neutralFast, 1.17);
        assert.equal(backup.sections.history.groupConfig, 0xC894);
        assert.equal(backup.sections.history.samplePeriodDeciseconds, 10);
        assert.equal(Object.hasOwn(backup.sections.capture, 'eventPrevSamples'), false);
        assert.equal(Object.hasOwn(backup.sections.capture, 'eventPostSamples'), false);
        assert.equal(Object.hasOwn(backup.sections.capture, 'eventMaxSamples'), false);
        assert.equal(Object.keys(backup.sections).length, 6);
        assert.equal(Object.hasOwn(backup.sections, 'web'), false);
        assert.equal(await page.evaluate(() => {
            try { validateBackupSection('calibration', {neutralRms: -1}); return false; }
            catch { return true; }
        }), true);
        assert.doesNotMatch(await page.textContent('#settingsBackupStatus'), /valores predeterminados no guardados/);

        page.on('dialog', dialog => dialog.accept());
        await page.setInputFiles('#settingsBackupFile', {name: 'backup.json', mimeType: 'application/json', buffer: Buffer.from(JSON.stringify(backup))});
        await page.waitForFunction(() => document.querySelectorAll('.backupImportSection').length === 6);
        await page.click('#btnImportSettingsBackup');
        await page.waitForFunction(() => document.querySelector('#settingsBackupWarning').textContent.startsWith('Importación terminada.'));
        assert.deepEqual(calls, ['grid', 'history', 'opts:meter', 'opts:meter', 'opts:ADE9000', 'network']);
        assert.equal(await page.isHidden('#btnRestartAfterImport'), false);
        console.log('PASS: respaldo JSON completo, escape de credenciales, validacion y restauracion secuencial con red al final.');
    } finally {
        await context.close();
        await browser.close();
    }
}

run().catch(error => { console.error(error); process.exitCode = 1; });
