const assert = require('node:assert/strict');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));

// Verifica la web servida por baseUrl sin generar eventos ni modificar opciones.
async function checkLiveWave(baseUrl) {
    const browser = await chromium.launch({headless:true,channel:'msedge'});
    try {
        for (const width of [1280,390]) {
            const page = await browser.newPage({viewport:{width,height:900}}), errors = [];
            page.on('pageerror',error => errors.push(error.message));
            await page.goto(baseUrl,{waitUntil:'domcontentloaded',timeout:30000});
            await page.waitForFunction(() => typeof gridWaveViewer !== 'undefined');
            await page.locator('#appRoot').waitFor({state:'visible',timeout:60000});
            if (!await page.locator('#gridEvents-tab').isVisible()) await page.locator('.navbar-toggler').click();
            await page.locator('#gridEvents-tab').click();
            await page.locator('#gridEventsList button').first().click({timeout:30000});
            await page.waitForFunction(() => document.getElementById('gridEventSummary').textContent.includes('Captura #'));
            await page.locator('#gridEventWaveDetails summary').click();
            await page.waitForFunction(() => document.getElementById('gridWaveStatus').textContent.includes('muestras'),{},{timeout:30000});
            const status = await page.locator('#gridWaveStatus').textContent();
            assert.match(status,/11904 muestras/);
            assert.equal(await page.evaluate(() => gridWaveViewer.chart.options.data[0].dataPoints.length),11904);
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth),true);
            await page.screenshot({path:'tmp/live-wave-'+width+'.png',fullPage:true});
            assert.deepEqual(errors,[]);
            console.log('PASS live '+width+': '+status);
            await page.close();
        }
    } finally {await browser.close();}
}
checkLiveWave(process.argv[2] || 'http://192.168.1.90/').catch(error => {console.error(error);process.exitCode=1;});
