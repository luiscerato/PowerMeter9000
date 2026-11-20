const assert = require('node:assert/strict');
const path = require('node:path');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')]}));

// Comprueba app desplegada y reutilizacion en navegador aislado; sin parametros, no cambia configuracion del ESP.
async function run() {
    const browser = await chromium.launch({headless:true,channel:'chrome'});
    try {
        const page = await browser.newPage({viewport:{width:390,height:850}}), errors=[];
        let downloads=0;
        page.on('pageerror',error=>errors.push(error.message));
        page.on('request',request=>{if(new URL(request.url()).pathname==='/history/data')++downloads;});
        await page.goto((process.env.METER_URL||'http://192.168.1.90')+'/#settings',{waitUntil:'domcontentloaded',timeout:60000});
        await page.locator('#appRoot').waitFor({state:'visible',timeout:60000});
        await page.locator('#settingsTab_application').click();
        await page.locator('#appCacheForm').waitFor({state:'visible'});
        assert.equal(await page.locator('#appCache_chunkKiB').inputValue(),'256');
        assert.equal(await page.locator('#appCache_pauseSec').inputValue(),'2');
        await page.evaluate(async()=>{historyCache.paused=true;pauseHistoryCacheRequest();await historyCache.task;});
        const result = await page.evaluate(async()=>{
            const scope=await historyCacheScope(null,true), from=Math.max(scope.oldestTimestamp,scope.committedTo-300), to=scope.committedTo;
            ++historyCache.foreground;
            try {
                const one=await getHistoryCacheChunk(scope,from,to,null,null);
                const two=await getHistoryCacheChunk(scope,from,to,null,null);
                // Consulta solo cobertura confirmada; un hueco anterior por reinicios puede requerir comprobar 404 en el ESP.
                return {from:Math.max(from,one.entry.from),to,bytes:one.buffer.byteLength,reused:historyCache.hits,identical:one.buffer.byteLength===two.buffer.byteLength};
            } finally {--historyCache.foreground;renderHistoryCacheStatus();}
        });
        assert.ok(result.bytes>0 && result.bytes<=256*1024); assert.ok(result.reused>0); assert.ok(result.identical);
        const before=downloads;
        const rows=await page.evaluate(async({from,to})=>{
            const buffer=await loadCachedHistoryRange(from,to-1);return decodeHistoryPages(buffer).length;
        },result);
        assert.ok(rows>0); assert.equal(downloads,before,'El visor reutiliza la descarga sin pedirla al ESP');
        assert.deepEqual(errors,[]);
        await page.screenshot({path:'tmp/history-cache-app-live.png'});
        console.log(JSON.stringify({bytes:result.bytes,cacheHits:result.reused,records:rows,extraDownloads:downloads-before,pageErrors:errors}));
    } finally {await browser.close();}
}
run().catch(error=>{console.error(error);process.exitCode=1;});
