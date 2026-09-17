const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');
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
        page.on('pageerror', error => errors.push(error.message));
        await page.route('http://app.test/**', async route => {
            const url = new URL(route.request().url());
            const relative = url.pathname === '/' ? 'index.html' : url.pathname.slice(1);
            const fileName = path.join(root, 'data', relative);
            if (!fileName.startsWith(path.join(root, 'data')) || !fs.existsSync(fileName))
                return route.fulfill({status: 404, body: ''});
            return route.fulfill({body: fs.readFileSync(fileName), contentType: contentType(fileName)});
        });
        await page.goto('http://app.test/');
        await page.waitForFunction(() => document.getElementById('appRoot').style.display !== 'none');
        assert.equal(await page.textContent('#pageTitle'), 'Resumen');
        for (const [pageId, title] of [['allMeasures', 'Mediciones'], ['events', 'Eventos'], ['scopePage', 'Osciloscopio'], ['settings', 'Configuración']]) {
            await page.click('[data-page="' + pageId + '"]');
            assert.equal(await page.textContent('#pageTitle'), title);
            assert.equal(await page.locator('[data-page="' + pageId + '"]').getAttribute('aria-current'), 'page');
        }
        await page.click('[data-page="settings"]');
        await page.evaluate(() => { document.activeElement.blur(); scrollTo(0, 0); });
        assert.deepEqual(await page.locator('.app-page-link.active').evaluateAll(links => links.map(link => link.dataset.page)), ['settings']);
        assert.equal(await page.evaluate(() => [...document.querySelectorAll('.app-page-link')].every(link => {
            const rect = link.getBoundingClientRect();
            return rect.left >= 0 && rect.right <= 200;
        })), true);
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/navigation-1280.png')});
        await page.click('#settingsTab_capture');
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/settings-capture-1280.png')});
        await page.click('#settingsTab_calibration');
        await page.screenshot({path: path.join(root, '.pio/build/grid-web/settings-calibration-1280.png')});
        await page.setViewportSize({width: 390, height: 900});
        await page.click('.navbar-toggler');
        await page.waitForFunction(() => document.getElementById('navbarNavAltMarkup').classList.contains('show'));
        await page.click('[data-page="events"]');
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
