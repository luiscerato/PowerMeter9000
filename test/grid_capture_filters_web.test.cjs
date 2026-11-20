const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {codec, gridFixture} = require('./grid_fixture.cjs');
const runtime = path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));
const now = Math.floor(Date.now()/1000)*1000;

// Adapta el fixture a id/time y conserva sus transiciones secundarias con CRC valido.
function fixture(id,time) {
    const bytes=gridFixture(), view=new DataView(bytes.buffer);
    view.setUint32(1,id,true); view.setUint32(5,time/1000,true); view.setUint16(9,0,true);
    view.setUint8(13,32); view.setUint16(28,codec.gridCrc16(bytes,0,28),true);
    for (const page of [1,2]) { const at=page*2048;view.setUint32(at,id,true);view.setUint16(at+8,codec.gridCrc16(bytes,at,170,at+8),true); }
    return Buffer.from(bytes);
}

// Verifica controles, fechas, transiciones secundarias y persistencia sin consultar la placa; sin parametros.
async function run() {
    const browser=await chromium.launch({headless:true,channel:'msedge'});
    try {
        for (const width of [1280,390]) {
            const page=await browser.newPage({viewport:{width,height:900}}), errors=[];
            let downloads=0,active=0,peak=0;
            page.on('pageerror',error=>errors.push(error.message));
            await page.route('http://captures.test/**',async route=>{
                const url=new URL(route.request().url());
                if(url.pathname==='/capture-cache')return route.fulfill({json:{ready:true,deviceId:'001122334455',fastEpoch:'0000000000000001',waveEpoch:'0000000000000001'}});
                if(url.pathname==='/grid-events')return route.fulfill({json:{eventCount:2,oldestId:1,newestId:2}});
                if(url.pathname==='/grid-events/list')return route.fulfill({json:{events:[{i:2,t:now,y:'R',k:3},{i:1,t:now-86400000,y:'R',k:3}]}});
                if(url.pathname==='/grid-events/data') {
                    const id=Number(url.searchParams.get('eventId')); ++downloads; ++active;peak=Math.max(peak,active);
                    await new Promise(resolve=>setTimeout(resolve,70));--active;
                    return route.fulfill({body:fixture(id,now-(id===1?86400000:0)),contentType:'application/octet-stream',headers:{'X-Capture-Identity':'001122334455:0000000000000001'}});
                }
                const file=path.join(process.cwd(),'data',url.pathname==='/'?'index.html':url.pathname.slice(1));
                if(!fs.existsSync(file))return route.fulfill({status:404,body:''});
                return route.fulfill({body:fs.readFileSync(file),contentType:{'.html':'text/html','.js':'application/javascript','.css':'text/css'}[path.extname(file)]||'application/octet-stream'});
            });
            await page.goto('http://captures.test/#gridEvents');
            await page.waitForFunction(()=>!gridEventsViewer.loading && gridEventsViewer.events.length===2);
            assert.equal(await page.locator('#gridEventsList button').count(),2);
            await page.selectOption('#gridCapturePreset','today');
            await page.waitForFunction(()=>document.querySelectorAll('#gridEventsList button').length===1);
            await page.locator('#gridCaptureTypes [data-capture-key=all]').click();
            await page.locator('#gridCaptureTypes [data-capture-key=outage]').click();
            await page.locator('#gridCapturePhases [data-capture-key=all]').click();
            await page.locator('#gridCapturePhases [data-capture-key="1"]').click();
            await page.waitForFunction(()=>document.querySelector('#gridCaptureFilterStatus').textContent.startsWith('1 de 2'));
            assert.equal(await page.locator('#gridEventsList button').count(),1,'corte secundario de S, no disparador de R');
            assert.match(await page.locator('#gridEventsList button strong').textContent(), /Fase S.*Corte/);
            await page.locator('[aria-label="Opciones de las gráficas"]').click();
            await page.locator('#gridEventMarkers').waitFor({state:'visible'});
            await page.locator('#gridEventMarkers').uncheck();
            assert.equal(await page.evaluate(()=>gridEventsViewer.showMarkers),false);
            await page.locator('[aria-label="Opciones de las gráficas"]').click();
            await page.locator('#gridEventExpand').click();
            assert.equal(await page.locator('#gridEventExpand').getAttribute('aria-pressed'),'true');
            await page.locator('[aria-label="Opciones de las gráficas"]').click();
            await page.locator('#gridEventLinkViews').waitFor({state:'visible'});
            await page.locator('[aria-label="Opciones de las gráficas"]').click();
            await page.keyboard.press('Escape');
            assert.equal(await page.locator('#gridEventExpand').getAttribute('aria-pressed'),'false');
            assert.equal(peak,1);
            const before=downloads;
            await page.reload();
            await page.waitForFunction(()=>!gridEventsViewer.loading && document.querySelector('#gridCaptureFilterStatus').textContent.startsWith('1 de 2'));
            assert.equal(await page.locator('#gridCapturePreset').inputValue(),'today');
            assert.equal(await page.locator('#gridCapturePhases [data-capture-key="1"]').getAttribute('aria-pressed'),'true');
            assert.equal(downloads,before,'reutiliza captura persistida');
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
            await page.screenshot({path:'tmp/capture-filters-'+width+'.png',fullPage:true});
            assert.deepEqual(errors,[]); await page.close();
            console.log('PASS filtros de capturas '+width+': fechas, secundarios, cache, secuencia y persistencia');
        }
    } finally { await browser.close(); }
}
run().catch(error=>{console.error(error);process.exitCode=1;});
