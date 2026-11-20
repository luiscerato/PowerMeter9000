const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths: [process.cwd(), runtime]}));
const html = fs.readFileSync('data/index.html', 'utf8');

// Prueba carga secuencial y marca responsive; width fija viewport y fail provoca un error de recurso.
async function checkView(browser, width, fail = false) {
    const page = await browser.newPage({viewport: {width, height: 850}});
    let release, active = 0, maximum = 0;
    const gate = new Promise(resolve => {release = resolve;});
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.route('http://meter.test/**', async route => {
        const name = new URL(route.request().url()).pathname.slice(1);
        if (!name) return route.fulfill({contentType: 'text/html', body: html});
        if (name === 'favicon.png') return route.fulfill({status: 204});
        ++active; maximum = Math.max(maximum, active);
        if (name === 'lib/bootstrap.min.css') await gate;
        const css = name.endsWith('.css');
        --active;
        return route.fulfill({status: fail && name.endsWith('.js') ? 503 : 200,
            contentType: css ? 'text/css' : 'application/javascript',
            body: css ? fs.readFileSync('data/' + name, 'utf8') : ''});
    });
    await page.goto('http://meter.test/', {waitUntil: 'domcontentloaded'});
    assert.equal(await page.locator('#bootBar').getAttribute('aria-valuenow'), '0');
    assert.equal(await page.locator('#bootDetails').evaluate(el => el.open), false);
    const logo = await page.locator('.bootBrand').boundingBox();
    assert.ok(logo.x >= 0 && logo.x + logo.width <= width);
    await page.screenshot({path: 'tmp/boot-brand-' + width + (fail ? '-error' : '') + '.png'});
    release();
    if (fail) {
        await page.locator('#bootLoader[data-error]').waitFor();
        assert.equal(await page.locator('#bootDetails').evaluate(el => el.open), true);
        assert.match(await page.locator('#bootProgress').inputValue(), /ERROR cargando:/);
        assert.equal(await page.locator('#appRoot').isVisible(), false);
    } else {
        await page.locator('#appRoot').waitFor({state: 'visible'});
        assert.equal(await page.locator('#bootBar').getAttribute('aria-valuenow'), '100');
        assert.equal(await page.locator('#bootLoader').isVisible(), false);
        const brand = await page.locator('.sidebarHeader .powerBrand').boundingBox();
        const button = await page.locator(width < 768 ? '.navbar-toggler' : '#sidebarToggle').boundingBox();
        assert.ok(brand.x >= 0 && brand.x + brand.width <= width);
        assert.ok(brand.x + brand.width <= button.x, 'Marca no tapa controles');
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
        await page.screenshot({path: 'tmp/boot-brand-nav-' + width + '.png'});
    }
    assert.equal(maximum, 1, 'Solo un recurso en vuelo');
    assert.deepEqual(errors, []);
    await page.close();
    console.log('PASS: marca web ' + width + 'px, progreso secuencial y ' + (fail ? 'diagnostico de error' : 'menu sin solapamientos'));
}

// Ejecuta las pruebas desktop/mobile y cierra Chromium incluso si falla una asercion; sin parametros.
async function run() {
    fs.mkdirSync('tmp', {recursive: true});
    const browser = await chromium.launch({headless: true, channel: 'chrome'});
    try {for (const width of [1440, 390, 320]) await checkView(browser, width); await checkView(browser, 390, true);}
    finally {await browser.close();}
}
run().catch(error => {console.error(error); process.exitCode = 1;});
