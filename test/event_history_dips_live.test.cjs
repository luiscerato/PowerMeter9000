const assert = require('node:assert/strict');
const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));

// Verifica DIP reales y su Wave sin escribir ajustes ni generar eventos; baseUrl es el origen del ESP.
async function run(baseUrl) {
    const browser = await chromium.launch({headless:true,channel:'msedge'});
    try {
        const page = await browser.newPage({viewport:{width:1280,height:900}}), errors = [], binaries = [];
        let indexes = 0;
        page.on('pageerror',error=>errors.push(error.message));
        page.on('request',request=>{
            const url = new URL(request.url());
            if (url.pathname === '/grid-events') ++indexes;
            if (url.pathname === '/grid-events/data') binaries.push(Number(url.searchParams.get('eventId')));
        });
        await page.goto(baseUrl+'/#eventHistory',{waitUntil:'domcontentloaded',timeout:30000});
        await page.locator('#appRoot').waitFor({state:'visible',timeout:60000});
        await page.waitForFunction(()=>eventHistory.loaded&&!eventHistory.controller,{}, {timeout:120000});
        assert.deepEqual(await page.evaluate(()=>eventHistory.failures),[]);
        for(const id of [43,44,45])assert.equal(await page.locator('.eventHistoryPoint[data-event-ids~="'+id+'"]').count(),3);
        await page.locator('#eventHistoryTimeline').scrollIntoViewIfNeeded();
        await page.screenshot({path:'tmp/dips-flechas-historial-live.png'});
        for (const [id,waveId] of [[43,69],[44,70],[45,71]]) {
            const items = await page.evaluate(id=>eventHistory.result.intervals.filter(item=>item.point===1&&item.ids.includes(id)),id);
            assert.equal(items.length,3, 'DIP visible para R/S/T #'+id);
            const key = items.find(item=>item.subject===0).key;
            await page.locator('#eventHistoryRows tr[data-history-key="'+key+'"] td:first-child button').click();
            await page.waitForFunction(()=>!eventHistoryPreview.loading&&eventHistoryPreview.waves.length>0,{}, {timeout:30000});
            const detail = await page.evaluate(()=>({ids:eventHistoryPreview.events.map(event=>event.id),
                waves:eventHistoryPreview.waves.map(wave=>wave.id),invalid:eventHistoryPreview.waves.reduce((sum,wave)=>sum+wave.invalidPackets,0)}));
            assert.ok(detail.ids.includes(id)); assert.ok(detail.waves.includes(waveId)); assert.equal(detail.invalid,0);
            assert.equal(await page.locator('#eventHistoryPreviewWarnings').textContent(),'');
            await page.screenshot({path:'tmp/dip-'+id+'-historial-live.png'});
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state:'hidden'});
            console.log('PASS DIP '+id+': '+JSON.stringify(detail));
        }
        const before = indexes, readCount = binaries.length;
        await page.evaluate(()=>{showAppPage('home');showAppPage('eventHistory');});
        await page.waitForFunction(before=>!eventHistory.controller&&eventHistory.loaded, before, {timeout:120000});
        assert.ok(indexes>before,'volver a Historial consulta novedades');
        assert.ok(binaries.slice(readCount).every(id=>![43,44,45].includes(id)),'DIP completos se reutilizan');
        const refreshed = await page.evaluate(()=>({status:document.getElementById('eventHistoryStatus').textContent,
            captures:eventHistory.captures.map(item=>item.id),failures:eventHistory.failures,
            dipCount:eventHistory.result.intervals.filter(item=>item.point===1&&item.ids.some(id=>[43,44,45].includes(id))).length}));
        assert.equal(refreshed.dipCount,9,JSON.stringify({...refreshed,errors}));
        await page.screenshot({path:'tmp/dips-lista-historial-live.png'});
        assert.deepEqual(errors,[]);
        console.log('PASS actualizacion al volver, nueve filas DIP R/S/T y cache sin repetir sus binarios');
    } finally {await browser.close();}
}

run(process.argv[2]||'http://192.168.1.90').catch(error=>{console.error(error);process.exitCode=1;});
