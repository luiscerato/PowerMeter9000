const path = require('node:path');
const runtime = path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));

// Mide arranque frio/caliente y refresco del historial sin escribir ajustes; baseUrl es el medidor.
async function run(baseUrl) {
    const browser = await chromium.launch({headless:true,channel:'msedge'});
    try {
        const page = await browser.newPage(), errors = [];
        page.on('pageerror',error=>errors.push(error.message));
        for (const mode of ['cold','warm']) {
            const start = Date.now();
            if(mode==='cold') await page.goto(baseUrl+'/#eventHistory',{waitUntil:'domcontentloaded',timeout:30000});
            else await page.reload({waitUntil:'domcontentloaded',timeout:30000});
            await page.locator('#appRoot').waitFor({state:'visible',timeout:60000});
            const bootMs = Date.now()-start;
            await page.waitForFunction(()=>eventHistory.loaded&&!eventHistory.controller,{}, {timeout:120000});
            console.log(mode,JSON.stringify(await page.evaluate(({bootMs,totalMs})=>({bootMs,totalMs,
                captures:eventHistory.captures.length,failures:eventHistory.failures,
                resources:performance.getEntriesByType('resource').filter(r=>/\.js|\.css|grid-events|capture-cache/.test(r.name))
                    .map(r=>({url:new URL(r.name).pathname,ms:Math.round(r.duration),bytes:r.transferSize}))}),{bootMs,totalMs:Date.now()-start})));
        }
        console.log('refresh',JSON.stringify(await page.evaluate(async()=>{
            const counts={}, timings={}, requests=[];
            for (const name of ['renderEventHistory','buildEventHistory','decodeGridEvent','summarizeEventHistory','renderEventStatistics']) {
                const original=window[name];
                window[name]=function(...args){const start=performance.now();try{return original.apply(this,args);}
                    finally{timings[name]=(timings[name]||0)+performance.now()-start;counts[name]=(counts[name]||0)+1;}};
            }
            const original=readEventHistoryResponse;
            readEventHistoryResponse=async function(...args){const start=performance.now();try{return await original(...args);}
                finally{requests.push({path:args[0],ms:Math.round(performance.now()-start)});}};
            const start=performance.now(), downloads=captureCacheState.downloads;
            await loadEventHistory();
            return {ms:Math.round(performance.now()-start),counts,timings,requests,
                downloads:captureCacheState.downloads-downloads,captures:eventHistory.captures.length,failures:eventHistory.failures};
        })));
        if(errors.length)throw Error(errors.join('\n'));
    } finally {await browser.close();}
}

run(process.argv[2]||'http://192.168.1.90').catch(error=>{console.error(error);process.exitCode=1;});
