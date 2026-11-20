const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [process.cwd(), runtime]}));

// Prueba la vista real con ondas conocidas sin acceder al ESP; width/height definen el tamano de pantalla.
async function checkView(browser, width, height = 900) {
    const page = await browser.newPage({viewport: {width, height}}), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('http://quality.test/**', route => route.fulfill({contentType: 'text/html', body: '<html><body><nav><a id="navscope"><svg></svg></a></nav></body></html>'}));
    await page.goto('http://quality.test/');
    for (const file of ['data/lib/bootstrap.min.css', 'data/style.css']) await page.addStyleTag({path: file});
    const html = fs.readFileSync('data/index.html', 'utf8');
    await page.evaluate(html => {
        const source = new DOMParser().parseFromString(html, 'text/html');
        const quality = source.getElementById('qualityPage');
        quality.classList.remove('d-none');
        const app = document.createElement('div'), main = document.createElement('main');
        app.id = 'appRoot'; main.id = 'mainContent'; main.className = 'qualityLayout';
        main.append(source.querySelector('.pageHeading'), quality, source.querySelector('.scopeFft'));
        app.append(source.getElementById('navbar'), main, source.getElementById('divFooter'));
        document.body.replaceChildren(app);
        document.getElementById('pageTitle').textContent = 'Calidad de energía';
    }, html);
    for (const file of ['lib/canvasjs.min.js', 'chart_tooltips.js', 'scope.js', 'scope_fft_vendor.js', 'scope_fft.js'])
        await page.addScriptTag({path: 'data/' + file});
    await page.evaluate(() => {
        powerQuality.init(); powerQuality.active = powerQuality.quality = true;
        Scope.view = 'quality'; Scope.shouldReconnect = true;
        Scope.Data.configSynced = true; Scope.qualityLocked = true;
        Scope.Data.Scale.Voltage = 100; Scope.Data.Scale.Current = 1;
        Scope.Data.waves.forEach((wave, index) => { wave.visible = index < 2; });
        powerQuality.voltageRange = 400; powerQuality.currentRange = 5;
        ScopeFft.begin('scopeFftChart');
        ScopeFft.channels = [0, 1].map(index => {
            const rms = index ? 230 : 2;
            const samples = Float64Array.from({length: 2048}, (_, i) => {
                const angle = 2 * Math.PI * 50 * i / 8000;
                return Math.SQRT2 * rms * (Math.sin(angle) + 0.1 * Math.sin(angle * 3));
            });
            return {samples, referenceHz: 50, step: index ? 0.1 : 0.001, receivedAt: Date.now()};
        });
        powerQuality.meter = {vrms: {r: 231.15}, irms: {r: 2.01}, watt: {r: 460}, pf: {r: 0.99}, freq: 50,
            voltageTHD: {r: 2.5}, currentTHD: {r: 7.5}};
        powerQuality.meterAt = Date.now();
        powerQuality.syncControls(); ScopeFft.updateSpectrum();
    });
    assert.match(await page.locator('#scopeFftStatus').textContent(), /10 ciclos.*200\.0 ms.*5\.00 Hz/);
    assert.equal(await page.locator('#qualityAnalysisRows tr').count(), 2);
    for (const id of ['qualityMeasurements', 'qualityAnalysis', 'qualityHarmonics']) {
        assert.equal(await page.locator('#' + id).evaluate(element => element.open), true);
        await page.locator('#' + id + ' summary').click();
        assert.equal(await page.locator('#' + id).evaluate(element => element.open), false);
        await page.locator('#' + id + ' summary').click();
    }
    assert.match(await page.locator('#qualityAdeRows').textContent(), /2\.50 %.*7\.50 %/);
    assert.equal(await page.locator('#qualityAnalysisRows tr').first().locator('th, td').count(), 8);
    assert.match(await page.locator('#qualityAnalysisRows').textContent(), /10\.00 %/);
    await page.waitForFunction(() => document.querySelectorAll('#qualityHarmonicRows tr').length === 40);
    assert.equal(await page.locator('#qualityHarmonicRows tr').count(), 40);
    assert.deepEqual(await page.locator('#qualityHarmonicHead th[colspan]').allTextContents(), ['IR (A)', 'VR (V)']);
    assert.equal(await page.locator('#qualityHarmonicRows tr').first().locator('th, td').count(), 6);
    assert.match(await page.locator('#qualityHarmonicRows tr').nth(2).textContent(), /150\.00.*10\.00/);
    await page.evaluate(() => {
        window.qualitySeries = ScopeFft.chart.options.data;
        window.qualityVoltageSeries = ScopeFft.chart.options.data[1];
    });
    await page.locator('#qualityChannels button[data-mask="42"]').click();
    assert.equal(await page.evaluate(() => ScopeFft.chart.options.data[1].visible), true);
    await page.locator('#qualityChannels button[data-mask="85"]').click();
    assert.equal(await page.evaluate(() => ScopeFft.chart.options.data[0].visible), true);
    assert.equal(await page.evaluate(() => ScopeFft.chart.options.data[1].visible), false);
    assert.equal(await page.evaluate(() => ScopeFft.chart.options.data === window.qualitySeries && ScopeFft.chart.options.data[1] === window.qualityVoltageSeries), true);
    await page.locator('#qualityChannels button[data-mask="3"]').click();
    assert.equal(await page.evaluate(() => {
        const record = ScopeFft.channels[1], points = ScopeFft.chart.options.data[1].dataPoints;
        Scope.applyQualityConfig('quality_cfg,1,300,5,3,10');
        return ScopeFft.channels[1] === record && ScopeFft.chart.options.data[1].dataPoints === points && ScopeFft.chart.options.data[1].visible;
    }), true, 'el nuevo rango no vacia el espectro anterior');
    assert.match(await page.locator('#scopeFftStatus').textContent(), /Captura anterior/);
    const positions = await page.evaluate(() => {
        const graph = document.getElementById('scopeFftChart').getBoundingClientRect();
        const readings = document.querySelector('.qualityReadings').getBoundingClientRect();
        return {height: graph.height, right: graph.right, readingsLeft: readings.left, bottom: graph.bottom, readingsTop: readings.top,
            spectrumTop: document.getElementById('qualitySpectrum').getBoundingClientRect().top};
    });
    if (width > 1050) {
        assert.ok(positions.height >= height * 0.60, 'grafico alto en escritorio');
        assert.ok(positions.readingsLeft >= positions.right, 'lecturas a la derecha');
        assert.ok(Math.abs(positions.readingsTop - positions.spectrumTop) < 2, 'las dos columnas arrancan juntas');
    } else assert.ok(positions.readingsTop >= positions.bottom, 'lecturas debajo en movil');
    assert.equal(await page.locator('#scopeFftSync, #scopeFftEnabled, #qualityStart').count(), 0);
    const tooltip = await page.evaluate(() => ScopeFft.tooltip({entries: [{
        dataSeries: ScopeFft.chart.options.data[1], dataPoint: ScopeFft.channels[1].analysis.points[30]}]}));
    assert.match(tooltip, /150,00 Hz.*Voltaje R.*H3.*23,000 V RMS.*10,00 % H1.*10,00 %/);
    await page.evaluate(() => {
        window.qualitySent = [];
        Scope.websocket = {readyState: WebSocket.OPEN, send: message => window.qualitySent.push(message)};
    });
    assert.equal(await page.locator('#qualityRanges input').count(), 0, 'solo hay escalas, sin rangos manuales');
    await page.locator('#scopeFftVoltage').selectOption('50');
    assert.equal(await page.evaluate(() => ScopeFft.chart.options.axisY2.maximum), 250);
    const captureRange = await page.evaluate(() => window.qualitySent.at(-1).split(',').slice(1, 3).map(Number));
    assert.ok(Math.abs(captureRange[0] - 250 * Math.SQRT2 * 1.1) < 1e-8);
    assert.ok(Math.abs(captureRange[1] - 5 * Math.SQRT2 * 1.1) < 1e-8);
    assert.equal(await page.evaluate(() => {
        const channels = ScopeFft.channels;
        ScopeFft.setEnabled(0);
        return ScopeFft.enabled && ScopeFft.channels === channels && window.qualitySent.at(-1) === 'scope_fft_on,1';
    }), true, 'Calidad reactiva el streaming sin descartar el espectro');
    await page.locator('#scopeFftExpand').click();
    assert.equal(await page.locator('.scopeFft').evaluate(element => element.classList.contains('isExpanded')), true);
    await page.locator('#scopeFftExpand').click();
    assert.ok(await page.locator('#qualityAnalysisRows').isVisible());
    await page.locator('#scopeFftReset').click();
    await page.evaluate(() => {
        Scope.Data.waves.forEach(wave => { wave.visible = true; });
        ScopeFft.channels[0].clipped = true;
        powerQuality.syncControls();
    });
    assert.equal(await page.locator('#qualityHarmonicRows tr').first().locator('th, td').count(), 16);
    assert.equal(await page.locator('#qualityHarmonicRows tr').first().locator('td').nth(1).textContent(), '—');
    assert.equal(await page.locator('#qualityHarmonicRows tr').first().locator('td').nth(5).textContent(), '—');
    assert.ok(await page.locator('#qualityClipping').isVisible());
    assert.match(await page.locator('#qualityClipping').textContent(), /Corriente R.*incorrectos.*THD no valida/);
    assert.equal(await page.evaluate(() => document.getElementById('qualityClipping').getBoundingClientRect().top >= document.getElementById('scopeFftChart').getBoundingClientRect().bottom), true);
    if (width > 1050) assert.ok(await page.evaluate(() => document.documentElement.scrollHeight <= innerHeight + 2), 'sin scroll vertical de pagina en escritorio');
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), 'sin desborde horizontal de pagina');
    await page.evaluate(() => {
        Scope.Data.waves.forEach((wave, index) => { wave.visible = index < 2; });
        ScopeFft.channels[0].clipped = false;
        ScopeFft.channels.forEach(record => { record.receivedAt = Date.now(); });
        powerQuality.syncControls();
    });
    assert.equal(await page.locator('#qualityClipping').isVisible(), false);
    await page.screenshot({path: 'tmp/quality-polish-' + width + '-' + height + '.png', fullPage: true});
    await page.evaluate(() => { Scope.Data.waves.forEach(wave => { wave.visible = false; }); powerQuality.syncControls(); });
    assert.match(await page.locator('#qualityHarmonicRows').textContent(), /Sin canales seleccionados/);
    assert.deepEqual(errors, []);
    await page.close();
    console.log('PASS: Calidad ' + width + 'px, FFT y RMS, THD 10%, detalle, Hann, escalas y ampliacion.');
}

// Ejecuta las dos dimensiones contra recursos locales; sin parametros.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try { await checkView(browser, 1280); await checkView(browser, 390); await checkView(browser, 1366, 720); }
    finally { await browser.close(); }
}
run().catch(error => { console.error(error); process.exitCode = 1; });
