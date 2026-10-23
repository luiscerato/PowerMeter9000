const assert = require('node:assert/strict');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright', {paths:[process.cwd(),runtime]}));

// Consulta la configuracion publicada sin guardar; baseUrl es el origen del medidor.
async function run(baseUrl) {
    const browser = await chromium.launch({headless:true,channel:'msedge'});
    try {
        const context = await browser.newContext();
        for (const width of [1280,390]) {
            const page = await context.newPage(), errors = [], writes = [];
            page.on('pageerror',error=>errors.push(error.message));
            page.on('request',request=>{if(request.method()==='POST')writes.push(request.url());});
            await page.setViewportSize({width,height:900});
            await page.goto(baseUrl+'/#settings',{waitUntil:'domcontentloaded',timeout:30000});
            await page.locator('#appRoot').waitFor({state:'visible',timeout:60000});
            await page.waitForFunction(()=>document.querySelector('#settings_capture .settingsStatus')?.textContent==='Opciones cargadas.',{}, {timeout:30000});
            assert.equal(await page.locator('#settingsTab_capture').count(),0);
            assert.equal(await page.locator('#settings_grid #captureFields').count(),1);
            assert.equal(await page.locator('#gridSettingsForm form').count(),0);
            assert.equal(await page.isEnabled('#grid_lowR'),true);
            assert.equal(await page.isEnabled('#opt_meter_dipMs'),true);
            assert.equal(await page.locator('#gridSettingsForm #opt_meter_ocCurrent').count(),1);
            assert.equal(await page.locator('#settings_capture #opt_meter_ocCurrent').count(),0);
            assert.equal(await page.locator('.eventSettingsHelpButton').count(),3);
            assert.equal(await page.isEnabled('#waveSaveEnabled'),true);
            assert.equal(await page.locator('.settingsPointTable tbody tr').count(),3);
            for (const [id,record] of [['waveDip','dipEnabled'],['waveSwell','swellEnabled'],['waveCurrent','overcurrent']])
                assert.equal(await page.isEnabled('#'+id),await page.isChecked('#opt_meter_'+record));
            assert.equal(await page.locator('.waveConfigToolbar #waveSaveEnabled').count(),1);
            assert.equal(await page.locator('label[for=wavePreMs]').textContent(),'Antes del evento');
            assert.equal(await page.locator('#waveConfigMetrics dt').count(),11);
            assert.match(await page.textContent('#waveConfigMetrics'),/Oscilografías guardadas/);
            assert.equal(await page.locator('#settingsTab_grid').textContent(),'Eventos de red');
            const settings = await page.evaluate(()=>({lowR:document.getElementById('grid_lowR').value,
                dipEnabled:document.getElementById('opt_meter_dipEnabled').checked,dipMs:document.getElementById('opt_meter_dipMs').value,
                swellMs:document.getElementById('opt_meter_swellMs').value,ocCurrent:document.getElementById('opt_meter_ocCurrent').value,
                waveDip:document.getElementById('waveDip').checked,waveSwell:document.getElementById('waveSwell').checked,
                waveCurrent:document.getElementById('waveCurrent').checked}));
            await page.screenshot({path:'tmp/event-settings-limits-live-'+width+'.png'});
            await page.locator('#settings_capture').scrollIntoViewIfNeeded();
            await page.screenshot({path:'tmp/event-settings-detection-live-'+width+'.png'});
            await page.locator('#waveConfigForm').scrollIntoViewIfNeeded();
            await page.screenshot({path:'tmp/event-settings-wave-live-'+width+'.png'});
            await page.click('.eventSettingsHelpButton[data-help="points"]');
            await page.waitForFunction(()=>getComputedStyle(document.getElementById('eventSettingsHelp')).opacity==='1' &&
                getComputedStyle(document.querySelector('#eventSettingsHelp .modal-dialog')).transform==='none');
            assert.match(await page.textContent('#eventSettingsHelpBody'),/Osc\. requiere Registro/);
            await page.screenshot({path:'tmp/event-settings-help-live-'+width+'.png'});
            await page.click('#eventSettingsHelp .btn-close');
            await page.locator('#eventSettingsHelp').waitFor({state:'hidden'});
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
            assert.deepEqual(errors,[]); assert.deepEqual(writes,[]);
            console.log('PASS eventos de red '+width+': '+JSON.stringify(settings));
            await page.close();
        }
    } finally {await browser.close();}
}

run(process.argv[2]||'http://192.168.1.90').catch(error=>{console.error(error);process.exitCode=1;});
