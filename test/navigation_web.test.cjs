const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
const {routeEvents,defaultState}=require('./events_api_fixture.cjs');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [root, runtime]}));

// Devuelve el tipo de contenido de un recurso local; fileName es su nombre.
function contentType(fileName) {
    const extension = path.extname(fileName);
    return {'.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.ico': 'image/x-icon'}[extension] || 'application/octet-stream';
}

// Prueba la navegacion completa usando los recursos reales y respuestas simuladas; sin parametros.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try {
        const page = await browser.newPage({viewport: {width: 1280, height: 900}});
        const errors = [];
        const eventState=defaultState();
        let storageFormatCalls = 0;
        let storageFormatConfirmation = "";
        let partitionFormatCalls = 0;
        let partitionFormatBody = "";
        const historyEstimateRequests = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route('http://app.test/**', async route => {
            const url = new URL(route.request().url());
            if(url.pathname.startsWith('/events/'))return routeEvents(route,eventState);
            if (url.pathname === '/storage') return route.fulfill({json: {
                state: 'ready', nand: {blocks: 2048, capacityBytes: 268435456},
                partitions: {history: {blocks: 1536, usableBlocks: 1535}, waveEvents: {blocks: 320, usableBlocks: 320},
                    gridEvents: {blocks: 48, usableBlocks: 48}, diagnostics: {blocks: 80, usableBlocks: 80}},
                historyStore: {writtenPages: 321}, gridStore: {recoveredEvents: 2, acceptedEvents: 3},
                format: {active: false, completed: false, failed: false, totalBlocks: 2048}
            }});
            if (url.pathname === '/webfs') return route.fulfill({json: {nand: {active: 'a',
                a: {valid: true, imageSize: 272384, capacityBytes: 3143680},
                b: {valid: true, imageSize: 270336, capacityBytes: 3143680}}}});
            if (url.pathname === '/history/config' && route.request().method() === 'GET') return route.fulfill({json: {
                enabled: true, samplePeriod: 1,
                voltageMode: 'basic', activePower: 'full', reactivePower: 'full', apparentPower: 'off',
                powerFactor: true, voltageThd: false, currentThd: false, frequency: 'full',
                voltageAngles: false, battery: false, energy: true, energyExtraMode: 1
            }});
            if (url.pathname === '/history/estimate') {
                const options = Object.fromEntries(url.searchParams.entries());
                const mode = value => ({off: 0, compact: 1, full: 2}[value] || 0);
                const enabled = name => options[name] === 'true';
                let groupConfig = options.voltageMode === 'full' ? 1 : 0;
                groupConfig |= mode(options.activePower) << 1;
                groupConfig |= mode(options.reactivePower) << 3;
                groupConfig |= mode(options.apparentPower) << 5;
                if (enabled('powerFactor')) groupConfig |= 1 << 7;
                if (enabled('voltageThd')) groupConfig |= 1 << 8;
                if (enabled('currentThd')) groupConfig |= 1 << 9;
                groupConfig |= mode(options.frequency) << 10;
                if (enabled('voltageAngles')) groupConfig |= 1 << 12;
                if (enabled('battery')) groupConfig |= 1 << 13;
                if (enabled('energy')) groupConfig |= (1 << 14) | (Number(options.energyExtraMode) << 15);
                const samplePeriodDeciseconds = Math.round(Number(options.samplePeriod) * 10);
                historyEstimateRequests.push({groupConfig, samplePeriod: options.samplePeriod, options});
                const modeSize = mode => mode === 2 ? 8 : mode === 1 ? 3 : 0;
                const frequencyMode = groupConfig >> 10 & 3;
                const frequencySize = frequencyMode === 2 ? 2 : frequencyMode === 1 ? 1 : 0;
                const energyExtras = [0, 3, 3, 6, 9, 9, 12, 18];
                const sampleSize = 1 + (groupConfig & 1 ? 14 : 8) + 9 +
                    modeSize(groupConfig >> 1 & 3) + modeSize(groupConfig >> 3 & 3) + modeSize(groupConfig >> 5 & 3) +
                    (groupConfig & 128 ? 4 : 0) + (groupConfig & 256 ? 4 : 0) + (groupConfig & 512 ? 4 : 0) +
                    frequencySize + (groupConfig & 4096 ? 5 : 0) + (groupConfig & 8192 ? 3 : 0) +
                    (groupConfig & 16384 ? 9 + energyExtras[groupConfig >> 15 & 7] : 0);
                const samplesPerPage = Math.floor(2028 / sampleSize);
                const estimatedSamples = 1535 * 64 * samplesPerPage;
                return route.fulfill({json: {sampleSize, samplesPerPage, ready: true,
                    usableBlocks: 1535, capacityPages: 98240, estimatedSamples,
                    duration: estimatedSamples * samplePeriodDeciseconds / 10}});
            }
            if (url.pathname === '/storage/format' && route.request().method() === 'POST') {
                storageFormatCalls++;
                storageFormatConfirmation = route.request().postData();
                return route.fulfill({status: 202, json: {started: true, state: 'formatting'}});
            }
            if (url.pathname === '/storage/partition/format' && route.request().method() === 'POST') {
                partitionFormatCalls++;
                partitionFormatBody = route.request().postData();
                return route.fulfill({status: 202, json: {ok: true, formatting: true}});
            }
            const relative = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
            const fileName = path.join(root, 'data', relative);
            if (!fileName.startsWith(path.join(root, 'data')) || !fs.existsSync(fileName))
                return route.fulfill({status: 404, body: ''});
            return route.fulfill({body: fs.readFileSync(fileName), contentType: contentType(fileName)});
        });
        await page.goto('http://app.test/');
        await page.waitForFunction(() => document.getElementById('appRoot').style.display !== 'none');
        assert.equal(await page.textContent('#pageTitle'), 'Resumen');
        await page.evaluate(() => {
            const phase = {r: 230, s: 231, t: 229, n: 0, avg: 230};
            const current = {r: 10, s: 8, t: 6, n: 0, avg: 8};
            const power = {r: 0, s: 0, t: 0, avg: 0, tot: 0};
            meterOk({gridState: {r: 'normal', s: 'normal', t: 'normal', overall: 'normal'},
                vrms: phase, vvrms: phase, irms: current, watt: power, var: power, va: power,
                freq: 50, pf: {r: 1, s: 1, t: 1, tot: 1}});
            angleOk({voltage: {r: 120, s: 120, t: 240}, vi: {r: 30, s: -30, t: 0}, current: {r: 120, s: 120, t: 120}});
        });
        assert.equal(await page.textContent('#gridState_r'), 'Normal');
        assert.equal(await page.textContent('#gridVoltage_r'), '230.00 V');
        assert.equal(await page.textContent('#gridCurrent_s'), '8.000 A');
        assert.equal(await page.textContent('#gridVoltage_t'), '229.00 V');
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/summary-states-1280.png')});
        assert.ok(Math.abs(Number(await page.locator('#phasorVoltageS').getAttribute('x2')) - 105) < 1);
        assert.ok(Math.abs(Number(await page.locator('#phasorVoltageS').getAttribute('y2')) - 56) < 1);
        assert.ok(Math.abs(Number(await page.locator('#phasorCurrentR').getAttribute('x2')) - 269) < 1);
        await page.uncheck('#phasorShowCurrent');
        assert.equal(await page.locator('#phasorCurrentR').evaluate(el => el.style.display), 'none');
        await page.check('#phasorShowCurrent');
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/phasors-1280.png')});
        await page.setViewportSize({width: 390, height: 900});
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        const statusCardPositions = await page.locator('.gridStatusCard').evaluateAll(cards => cards.map(card => {
            const rect = card.getBoundingClientRect();
            return {left: rect.left, top: rect.top};
        }));
        assert.equal(statusCardPositions.length, 4);
        assert.equal(statusCardPositions[0].top, statusCardPositions[1].top);
        assert.equal(statusCardPositions[2].top, statusCardPositions[3].top);
        await page.evaluate(() => scrollTo(0, 0));
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/summary-states-390.png')});
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/phasors-390.png')});
        await page.setViewportSize({width: 1280, height: 900});
        for (const [pageId, title] of [['allMeasures', 'Mediciones'], ['gridEvents', 'Eventos'], ['scopePage', 'Osciloscopio'], ['settings', 'Configuración']]) {
            await page.click('[data-page="' + pageId + '"]');
            assert.equal(await page.textContent('#pageTitle'), title);
            assert.equal(await page.locator('[data-page="' + pageId + '"]').getAttribute('aria-current'), 'page');
        }
        await page.click('[data-page="settings"]');
        await page.evaluate(() => { document.activeElement.blur(); scrollTo(0, 0); });
        assert.deepEqual(await page.locator('.app-page-link.active').evaluateAll(links => links.map(link => link.dataset.page)), ['settings']);
        await page.evaluate(() => updateHistoryUrl('2026-09-27T08:00:00', '2026-09-27T09:00:00'));
        assert.equal(new URL(page.url()).hash, '#history');
        assert.equal(new URL(page.url()).searchParams.has('from'), true);
        assert.equal(new URL(page.url()).searchParams.has('to'), true);
        await page.click('[data-page="home"]');
        const homeUrl = new URL(page.url());
        assert.equal(homeUrl.hash, '#home');
        assert.equal(homeUrl.searchParams.has('from'), false);
        assert.equal(homeUrl.searchParams.has('to'), false);
        await page.click('[data-page="settings"]');
        assert.equal(await page.evaluate(() => [...document.querySelectorAll('.app-page-link')].every(link => {
            const rect = link.getBoundingClientRect();
            return rect.left >= 0 && rect.right <= 200;
        })), true);
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/navigation-1280.png')});
        await page.click('#settingsTab_grid');
        await page.locator('#settings_capture').scrollIntoViewIfNeeded();
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/settings-capture-1280.png')});
        await page.click('#settingsTab_calibration');
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/settings-calibration-1280.png')});
        const historyConfigResponse = page.waitForResponse(response => response.url().includes('/history/config'));
        await page.click('#settingsTab_history');
        await historyConfigResponse;
        await page.waitForFunction(() => document.getElementById('historyCapacityInfo').textContent.includes('44 días'));
        assert.match(await page.textContent('#historyProfileInfo'), /52 bytes · 39 muestras por página/);
        assert.match(await page.textContent('.historyBaseInfo'), /Wi-Fi, MQTT, alimentación\/carga de batería/);
        const estimateAfterOption = page.waitForResponse(response => response.url().includes('/history/estimate'));
        await page.locator('#history_frequency').selectOption('off');
        await estimateAfterOption;
        assert.equal(historyEstimateRequests.at(-1).groupConfig, 49300);
        assert.equal(historyEstimateRequests.at(-1).samplePeriod, '1');
        assert.equal(historyEstimateRequests.at(-1).options.frequency, 'off');
        const estimateAfterBattery = page.waitForResponse(response => response.url().includes('/history/estimate'));
        await page.check('#history_battery');
        await estimateAfterBattery;
        assert.equal(historyEstimateRequests.at(-1).groupConfig, 57492);
        assert.equal(historyEstimateRequests.at(-1).options.battery, 'true');
        const estimateAfterPeriod = page.waitForResponse(response => response.url().includes('/history/estimate'));
        await page.locator('#history_period').fill('2');
        await estimateAfterPeriod;
        assert.equal(historyEstimateRequests.at(-1).groupConfig, 57492);
        assert.equal(historyEstimateRequests.at(-1).samplePeriod, '2');
        const estimateAfterCompactPower = page.waitForResponse(response => response.url().includes('/history/estimate'));
        await page.locator('#history_activePower').selectOption('compact');
        await estimateAfterCompactPower;
        assert.equal(historyEstimateRequests.at(-1).groupConfig, 57490);
        assert.equal(historyEstimateRequests.at(-1).options.activePower, 'compact');
        const estimateAfterEnergyMode = page.waitForResponse(response => response.url().includes('/history/estimate'));
        await page.locator('#history_energyExtraMode').selectOption('7');
        await estimateAfterEnergyMode;
        assert.equal(historyEstimateRequests.at(-1).groupConfig, 254098);
        assert.equal(historyEstimateRequests.at(-1).options.energyExtraMode, '7');
        const estimateAfterCompactFrequency = page.waitForResponse(response => response.url().includes('/history/estimate'));
        await page.locator('#history_frequency').selectOption('compact');
        await estimateAfterCompactFrequency;
        assert.equal(historyEstimateRequests.at(-1).groupConfig, 255122);
        assert.equal(historyEstimateRequests.at(-1).options.frequency, 'compact');
        for (const [selector, expected] of [['#history_voltageThd', 255378], ['#history_currentThd', 255890], ['#history_voltageAngles', 259986]]) {
            const response = page.waitForResponse(item => item.url().includes('/history/estimate'));
            await page.check(selector);
            await response;
            assert.equal(historyEstimateRequests.at(-1).groupConfig, expected);
            assert.equal(historyEstimateRequests.at(-1).samplePeriod, '2');
        }
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/settings-history-1280.png')});
        await page.click('#settingsTab_storage');
        await page.waitForFunction(() => document.getElementById('storageSummary').textContent.includes('256.0 MiB'));
        assert.equal(await page.locator('#settingsTab_storage').getAttribute('aria-selected'), 'true');
        assert.equal(await page.locator('#storagePartitionRows tr').count(), 7);
        assert.match(await page.locator('#storagePartitionRows').textContent(), /16–1551/);
        assert.match(await page.locator('#storagePartitionRows').textContent(), /Activo · 266 KiB/);
        assert.match(await page.textContent('.storageWarning'), /borra toda la NAND/i);
        page.on('dialog', dialog => dialog.accept('FORMAT'));
        const partitionResponse = page.waitForResponse(response => response.url().endsWith('/storage/partition/format') && response.request().method() === 'POST');
        await page.locator('#storagePartitionRows tr').filter({hasText: 'Histórico'}).locator('.btnFormatPartition').click();
        await partitionResponse;
        assert.equal(partitionFormatCalls, 1);
        assert.equal(partitionFormatBody, 'partition=history&confirm=FORMAT');
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/storage-1280.png')});
        const formatResponse = page.waitForResponse(response => response.url().endsWith('/storage/format') && response.request().method() === 'POST');
        await page.click('#btnFormatStorage');
        await formatResponse;
        assert.equal(storageFormatCalls, 1);
        assert.equal(storageFormatConfirmation, 'confirm=FORMAT');
        await page.setViewportSize({width: 390, height: 900});
        await page.click('.navbar-toggler');
        await page.waitForFunction(() => document.getElementById('navbarNavAltMarkup').classList.contains('show'));
        await page.click('[data-page="gridEvents"]');
        await page.waitForFunction(() => {
            const menu = document.getElementById('navbarNavAltMarkup');
            return !menu.classList.contains('show') && !menu.classList.contains('collapsing');
        });
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/navigation-390.png')});
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        assert.deepEqual(errors, []);
        console.log('PASS: navegacion completa, hash, menu movil y cambio entre las cinco vistas.');
    } finally {
        await browser.close();
    }
}

run().catch(error => { console.error(error); process.exitCode = 1; });
