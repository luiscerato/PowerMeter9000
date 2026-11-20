const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const origin = process.env.METER_URL || 'http://192.168.1.90';

// Espera ms entre consultas; no ocupa CPU ni mantiene solicitudes abiertas.
function sleep(ms) {return new Promise(resolve=>setTimeout(resolve,ms));}

// Compara datos sinteticos con lecturas historicas reales; sin parametros, --long agrega 16 MiB sinteticos.
async function run() {
    const scope = await (await fetch(origin+'/history')).json();
    const before = await (await fetch(origin+'/storage?detail=1')).json();
    const decoder = vm.createContext({ArrayBuffer,DataView,Uint8Array,Map,Set,Date,URLSearchParams});
    vm.runInContext(fs.readFileSync('data/history_ui.js','utf8')+'\n'+fs.readFileSync('data/history_cache.js','utf8'),decoder);
    const results = [];
    const rounds = Number(process.env.TRANSPORT_ROUNDS || 3);
    assert.ok(Number.isInteger(rounds) && rounds >= 1 && rounds <= 20);
    for (let round=0;round<rounds;++round) {
        for (const kind of round%2 ? ['history256','history128','synthetic128'] : ['synthetic128','history128','history256']) {
            const history = kind.startsWith('history'), kib = kind.endsWith('256') ? 256 : 128;
            const path = history ? '/history/data?'+new URLSearchParams({from:scope.oldestTimestamp,to:scope.committedTo,
                pages:kib/2,before:scope.latestSequence,identity:scope.identity}) : '/test/speed.raw';
            results.push(await measure(path,kib*1024,history,kind,round+1));
            await sleep(2000);
        }
    }
    if (process.argv.includes('--long')) results.push(await measure('/test/speed.csv',16*1048576,false,'synthetic16MiB',1));
    const after = await (await fetch(origin+'/storage?detail=1')).json();
    const output = 'logs/history-transport-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';
    fs.mkdirSync('logs',{recursive:true});fs.writeFileSync(output,JSON.stringify({at:new Date().toISOString(),origin,results,before,after},null,2));
    console.log('RESULT '+output);
    for (const stage of ['nand','prepare','queue']) {
        const first=before.historyRead?.[stage],last=after.historyRead?.[stage];
        if (!first || !last) continue;
        const count=last.count-first.count;
        console.log(JSON.stringify({stage,count,meanUs:count ? Math.round((last.totalUs-first.totalUs)/count) : 0,
            maxSinceBootUs:last.maxUs,over1ms:last.over1ms-first.over1ms,over2ms:last.over2ms-first.over2ms,
            over5ms:last.over5ms-first.over5ms,over10ms:last.over10ms-first.over10ms}));
    }

    // Mide headers/cuerpo de path y valida expected bytes; history selecciona CRC real o asteriscos, kind/round identifican prueba.
    async function measure(path,expected,history,kind,round) {
        const started=performance.now(),response=await fetch(origin+path,{cache:'no-store',signal:AbortSignal.timeout(120000)}),headersAt=performance.now();
        assert.equal(response.status,200);
        const reader=response.body.getReader(),chunks=[];
        let firstDataAt=0,bytes=0;
        while(true) {
            const {done,value}=await reader.read();
            if(done)break;
            if(!value.length)continue;
            if(!firstDataAt)firstDataAt=performance.now();
            chunks.push(value);bytes+=value.length;
        }
        const ended=performance.now();assert.equal(bytes,expected);
        const joined=new Uint8Array(bytes);
        let position=0;
        for(const chunk of chunks){joined.set(chunk,position);position+=chunk.length;}
        const buffer=joined.buffer;
        if(history) {
            assert.equal(response.headers.get('X-History-Identity'),scope.identity);
            decoder.input=buffer;
            const pages=vm.runInContext('inspectHistoryCacheChunk(input)',decoder),first=Number(response.headers.get('X-History-First-Sequence'));
            assert.equal(pages.length,expected/2048);
            for(const page of pages)assert.equal(page.sequence,(first+page.offset/2048)&0xFFFFFF);
        } else assert.ok(new Uint8Array(buffer).every(value=>value===42));
        const row={kind,round,bytes:buffer.byteLength,headersMs:Math.round(headersAt-started),bodyMs:Math.round(ended-headersAt),
            firstDataMs:Math.round(firstDataAt-started),streamMs:Math.round(ended-firstDataAt),
            totalMs:Math.round(ended-started),KiBs:Math.round(expected*1000/(ended-started)/1024),valid:true};
        console.log(JSON.stringify(row));return row;
    }
}
run().catch(error=>{console.error(error);process.exitCode=1;});
