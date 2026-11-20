const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require(require.resolve('playwright', {paths: [process.cwd(),path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')]}));
const identity = 'aabbccddeeff:0123456789abcdef';
const base = Math.floor(Date.now()/1000)-7200;

// Calcula CRC del formato real; bytes contiene la cabecera o muestras.
function crc16(bytes) {
    let crc=65535;
    for (const value of bytes) {crc ^= value<<8; for(let bit=0;bit<8;++bit) crc=crc&32768?(crc<<1^0x1021)&65535:crc<<1&65535;}
    return crc;
}

// Construye una pagina V1 valida de dos muestras; sequence identifica su posicion logica.
function makePage(sequence) {
    const page=Buffer.alloc(2048,255);
    page.writeUInt32LE(base+sequence*60,0); page.writeUIntLE(sequence,4,3); page.writeUIntLE(0,7,3);
    page.writeUInt16LE(100,10); page.writeUInt16LE(2,12); page[14]=1; page[15]=0;
    page.fill(0,20,56); page.writeUInt16LE(crc16(page.subarray(0,16)),16); page.writeUInt16LE(crc16(page.subarray(20,56)),18);
    return page;
}

// Ejecuta pruebas reales de IndexedDB y consultas compartidas; sin parametros.
async function run() {
    const browser=await chromium.launch({headless:true,channel:'chrome'});
    const context=await browser.newContext({viewport:{width:390,height:850}}), page=await context.newPage();
    let downloads=0, inflight=0, maximum=0, corrupt=false, currentIdentity=identity, pageCount=72, lastPages=0;
    const errors=[]; page.on('pageerror',error=>errors.push(error.message));
    await context.route('http://history.test/**',async route=>{
        const url=new URL(route.request().url());
        if(url.pathname==='/')return route.fulfill({contentType:'text/html',body:'<meta name="viewport" content="width=device-width,initial-scale=1"><main id="settings_application"></main><div hidden><input id="historyFrom" type="datetime-local" step="1"><input id="historyTo" type="datetime-local" step="1"><select id="historyPreset"><option value="last-300">5 min</option><option value="last-3600">1 h</option></select></div>'});
        if(url.pathname==='/history')return route.fulfill({json:{ready:true,identity:currentIdentity,committedPages:pageCount,committedTo:base+(pageCount-1)*60+10,oldestTimestamp:base,latestSequence:pageCount-1}});
        if(url.pathname==='/history/data') {
            ++downloads; ++inflight; maximum=Math.max(maximum,inflight);
            const from=Number(url.searchParams.get('from')),to=Number(url.searchParams.get('to'));
            const last=Math.min(pageCount-1,url.searchParams.has('before')?Number(url.searchParams.get('before')):pageCount-1);
            const limit=Number(url.searchParams.get('pages')||4096);
            lastPages=limit;
            const pages=Array.from({length:last+1},(_,i)=>i).filter(i=>base+i*60<=to&&base+i*60+10>=from).slice(-limit);
            --inflight;
            if(!pages.length)return route.fulfill({status:404,json:{reason:'history_empty_in_range'}});
            const body=Buffer.concat(pages.map(makePage)); if(corrupt)body[20]^=1;
            return route.fulfill({contentType:'application/octet-stream',body,headers:{'X-History-Identity':currentIdentity,
                'X-History-First-Sequence':String(pages[0]),'X-History-Pages':String(pages.length)}});
        }
        return route.fulfill({status:404});
    });
    // Carga fuentes reales sin arrancar el resto de la aplicacion; sin parametros.
    async function prepare() {
        await page.goto('http://history.test/');
        await page.addStyleTag({path:'data/lib/bootstrap.min.css'}); await page.addStyleTag({path:'data/style.css'});
        await page.addScriptTag({path:'data/lib/jquery-3.6.4.min.js'});
        await page.addScriptTag({path:'data/history_ui.js'}); await page.addScriptTag({path:'data/history_cache.js'});
        await page.evaluate(()=>{
            window.board={IP:'http://history.test'};
            window.showAppPage=function(){}; showAppPage.current='home';
            document.querySelector('main').innerHTML=createApplicationSettingsHtml();
            window.setHistoryStatus=function(){};
            // Conserva bloques de 128 KiB para los casos de continuidad y huecos del fixture de 72 paginas.
            historyCacheOptions={...historyCacheDefaults,chunkKiB:128};
        });
    }
    try {
        await prepare();
        const initial=await page.evaluate(()=>{initHistoryViewer();return {preset:$('#historyPreset').val(),seconds:(new Date($('#historyTo').val())-new Date($('#historyFrom').val()))/1000};});
        assert.deepEqual(initial,{preset:'last-3600',seconds:3600});
        const defaults=await page.evaluate(()=>{
            const initial=loadHistoryCacheOptions();
            localStorage.removeItem('powerMeterHistoryBlockDefault');
            localStorage.setItem('powerMeterHistoryOptions',JSON.stringify({...historyCacheDefaults,chunkKiB:128,pauseSec:3}));
            const migrated=loadHistoryCacheOptions();
            localStorage.setItem('powerMeterHistoryOptions',JSON.stringify({...historyCacheDefaults,chunkKiB:128}));
            const explicit=loadHistoryCacheOptions();
            localStorage.removeItem('powerMeterHistoryBlockDefault');
            localStorage.setItem('powerMeterHistoryOptions',JSON.stringify({...historyCacheDefaults,chunkKiB:64}));
            const custom=loadHistoryCacheOptions();localStorage.removeItem('powerMeterHistoryOptions');
            return {block:initial.chunkKiB,pause:initial.pauseSec,migrated:migrated.chunkKiB,keptPause:migrated.pauseSec,explicit:explicit.chunkKiB,custom:custom.chunkKiB};
        });
        assert.deepEqual(defaults,{block:256,pause:2,migrated:256,keptPause:3,explicit:128,custom:64});
        const config=await page.evaluate(()=>({valid:!!validateHistoryCacheOptions(historyCacheDefaults),odd:validateHistoryCacheOptions({...historyCacheDefaults,chunkKiB:3}),zero:validateHistoryCacheOptions({...historyCacheDefaults,pauseSec:0})}));
        assert.deepEqual(config,{valid:true,odd:null,zero:null});
        await page.evaluate(async()=>{await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(downloads,1,'Un bloque de 128 KiB por ciclo');
        assert.equal(await page.evaluate(()=>historyCache.bytes),128*1024);
        await page.evaluate(async()=>{await syncHistoryCache();clearTimeout(historyCache.timer);await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(downloads,2,'No vuelve a descargar el periodo cubierto');
        const before=downloads;
        const records=await page.evaluate(async({from,to})=>decodeHistoryPages(await loadCachedHistoryRange(from,to)).length,{from:base,to:base+70*60+9});
        assert.equal(records,144); assert.equal(downloads,before,'Visor reutiliza el fondo');
        await prepare();
        await page.evaluate(async()=>{await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(downloads,before,'IndexedDB sobrevive al refresco');
        await page.evaluate(()=>{showAppPage.current='scopePage';});
        await page.evaluate(async()=>{await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(downloads,before,'FFT/osciloscopio pausa el fondo');
        corrupt=true;
        await assert.rejects(page.evaluate(async()=>{const s=await historyCacheScope(null,true);await getHistoryCacheChunk(s,s.oldestTimestamp,s.committedTo,null,null,true);}),/CRC/);
        corrupt=false;
        assert.equal(await page.evaluate(async()=>(await historyCacheMetadata()).length),2,'Fallo no borra copia valida');
        await page.evaluate(async()=>{
            const entry=(await historyCacheMetadata())[0], buffer=await readHistoryCacheChunk(entry);
            const other={...entry,key:'other-device',identity:'112233445566:0123456789abcdef'};
            await historyCacheTransaction(['chunks','metadata'],'readwrite',tx=>{
                tx.objectStore('chunks').put({key:other.key,buffer});tx.objectStore('metadata').put(other);
            });
        });
        currentIdentity='aabbccddeeff:fedcba9876543210';
        await page.evaluate(async()=>{showAppPage.current='home';await historyCacheScope(null,true);await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(downloads,before+2,'Cambio de generacion no reutiliza almacenamiento anterior');
        assert.equal(await page.evaluate(async()=>(await historyCacheMetadata()).filter(entry=>entry.identity==='aabbccddeeff:0123456789abcdef').length),0,'Formateo elimina binarios y metadata anteriores del mismo equipo');
        assert.equal(await page.evaluate(async()=>(await historyCacheMetadata()).some(entry=>entry.key==='other-device')),true,'No elimina datos de otra placa');
        pageCount=74;
        await page.evaluate(async()=>{await historyCacheScope(null,true);await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(lastPages,2,'Solo descarga las paginas nuevas, sin repetir 128 KiB');
        const afterNew=downloads;
        await page.evaluate(async()=>{await syncHistoryCache();clearTimeout(historyCache.timer);});
        assert.equal(downloads,afterNew+1,'Aunque tenga las ultimas paginas, sigue buscando las primeras que faltan');
        assert.equal(await page.evaluate(async()=>(await historyCacheMetadata()).some(entry=>entry.first===0&&entry.identity===historyCache.scope.identity)),true);
        // Simula una entrada cuya metadata existe pero el binario fue perdido por el navegador.
        await page.evaluate(async()=>{
            const entry=(await historyCacheMetadata()).find(item=>item.first===8);
            await historyCacheTransaction(['chunks'],'readwrite',tx=>tx.objectStore('chunks').delete(entry.key));
            historyCache.verified.clear();
            await syncHistoryCache();clearTimeout(historyCache.timer);
            await syncHistoryCache();clearTimeout(historyCache.timer);
        });
        assert.equal(downloads,afterNew+2,'Auditoria detecta y recupera binario ausente aunque su metadata diga completo');
        await page.evaluate(async()=>{
            const entry=(await historyCacheMetadata()).find(item=>item.first===8&&item.identity===historyCache.scope.identity);
            const buffer=await readHistoryCacheChunk(entry);new Uint8Array(buffer)[20]^=1;
            await historyCacheTransaction(['chunks'],'readwrite',tx=>tx.objectStore('chunks').put({key:entry.key,buffer}));
            historyCache.verified.clear();
            await syncHistoryCache();clearTimeout(historyCache.timer);
            await syncHistoryCache();clearTimeout(historyCache.timer);
        });
        assert.equal(downloads,afterNew+3,'Auditoria recupera un bloque cuyo CRC local se corrompio');
        // Un bloque entero de ceros tampoco se admite como secuencias confirmadas.
        const zeroRejected=await page.evaluate(()=>{
            try {verifyHistoryCacheChunk({key:'bad',first:0,last:0,bytes:2048,from:1},new ArrayBuffer(2048));return false;} catch (_) {return true;}
        });
        assert.equal(zeroRejected,true);
        // Envejecer solo ultimo uso confirma que la fecha medida no determina la limpieza.
        await page.evaluate(async()=>{await historyCacheTransaction(['metadata'],'readwrite',tx=>{const r=tx.objectStore('metadata').getAll();r.onsuccess=()=>{for(const entry of r.result)tx.objectStore('metadata').put({...entry,usedAt:Date.now()-31*86400000});};});await trimHistoryCache();});
        assert.equal(await page.evaluate(()=>historyCache.bytes),0);
        await page.evaluate(async()=>{await initHistoryCache();clearTimeout(historyCache.timer);});
        await page.locator('#appCache_chunkKiB').fill('64'); await page.locator('#appCache_pauseSec').fill('3');
        await page.locator('button[form="appCacheForm"]').click();
        await page.waitForFunction(()=>historyCacheOptions.chunkKiB===64&&historyCacheOptions.pauseSec===3);
        assert.equal(await page.evaluate(()=>JSON.parse(localStorage.getItem('powerMeterHistoryOptions')).chunkKiB),64);
        assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth),false);
        fs.mkdirSync('tmp',{recursive:true}); await page.screenshot({path:'tmp/history-cache-mobile.png'});
        assert.equal(maximum,1); assert.deepEqual(errors,[]);
        console.log('PASS: bloques 128 KiB, continuidad, CRC, cache persistente, reutilizacion, prioridad, generacion, TTL y ajustes mobile.');
    } finally {await browser.close();}
}
run().catch(error=>{console.error(error);process.exitCode=1;});
