const assert = require('node:assert/strict');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [process.cwd(), runtime]}));

// Verifica CanvasJS real en pantalla width; mide tooltip compartido con siete canales y una marca de corte.
async function checkView(browser, width) {
    const page = await browser.newPage({viewport: {width, height: 800}}), errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.setContent('<meta name="viewport" content="width=device-width, initial-scale=1"><div id="chart" style="height:440px;width:100%"></div>');
    await page.addStyleTag({path: 'data/lib/bootstrap.min.css'});
    await page.addStyleTag({path: 'data/style.css'});
    await page.addScriptTag({path: 'data/lib/canvasjs.min.js'});
    await page.addScriptTag({path: 'data/chart_tooltips.js'});
    await page.evaluate(() => {
        const series = ['Voltaje R', 'Voltaje S', 'Voltaje T', 'Corriente R', 'Corriente S', 'Corriente T', 'Corriente N', 'Oculta'];
        window.chart = new CanvasJS.Chart('chart', {animationEnabled: false,
            toolTip: chartTooltip.options({title: 'FAST #87', markers: [{at: 0, label: 'Red: normal → corte'}]}),
            data: series.map((name, index) => ({type: 'line', name, visible: index !== 7,
                color: ['#d9534f', '#5cb85c', '#337ab7'][index % 3], signalUnit: index < 3 ? 'V' : 'A',
                shortName: (index < 3 ? 'V ' : 'I ') + 'RSTN'[index < 3 ? index : index - 3],
                dataPoints: Array.from({length: 201}, (_, sample) => ({x: sample - 100, y: index < 3 ? 230 + sample / 100 : 0.5}))}))});
        chart.render(); chart.toolTip.showAtX(0);
    });
    await page.locator('.meterTooltip').waitFor({state: 'visible'});
    assert.equal(await page.locator('.meterTooltipRow').count(), 7);
    const text = await page.locator('.meterTooltip').textContent();
    assert.match(text, /231,00 V/); assert.match(text, /0,500 A/); assert.match(text, /normal → corte/);
    assert.doesNotMatch(text, /Oculta/);
    const bounds = await page.locator('.meterTooltip').boundingBox();
    assert.ok(bounds.x >= -1 && bounds.x + bounds.width <= width + 1, 'tooltip dentro del ancho visible');
    if (width < 768) {
        assert.ok(bounds.height < 95 && bounds.width < 240, 'tooltip movil reducido un 15% del original');
        assert.equal(await page.locator('.meterTooltip').evaluate(element => getComputedStyle(element).fontSize), '8.5px');
    } else assert.equal(await page.locator('.meterTooltip').evaluate(element => getComputedStyle(element).fontSize), '12px');
    await page.screenshot({path: 'tmp/chart-tooltip-' + width + '.png'});
    assert.deepEqual(errors, []);
    await page.close();
    console.log('PASS: tooltip CanvasJS ' + width + 'px, siete señales, unidades, marca y tamaño compacto.');
}

// Ejecuta la verificacion visual en PC y movil; sin parametros.
async function run() {
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try { await checkView(browser, 1280); await checkView(browser, 390); }
    finally { await browser.close(); }
}
run().catch(error => { console.error(error); process.exitCode = 1; });
