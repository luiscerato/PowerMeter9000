const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const origin = process.env.METER_URL || 'http://192.168.1.90';

// Verifica descarga limitada y continuidad usando NAND real; sin parametros, solo realiza lecturas.
async function run() {
    const scope = await (await fetch(origin+'/history')).json();
    assert.match(scope.identity,/^[0-9a-f]{12}:[0-9a-f]{16}$/i);
    const total=Number(process.env.HISTORY_BLOCKS||2);
    assert.ok(Number.isInteger(total)&&total>0&&total<=16);
    assert.ok(scope.committedPages > total*64,'Se requieren suficientes bloques existentes');
    const context = vm.createContext({ArrayBuffer,DataView,Uint8Array,Map,Set,Date,URLSearchParams});
    vm.runInContext(fs.readFileSync('data/history_ui.js','utf8')+'\n'+fs.readFileSync('data/history_cache.js','utf8'),context);
    let before = null;
    for (let index=0; index<total; ++index) {
        const query = new URLSearchParams({from:String(scope.oldestTimestamp),to:String(scope.committedTo),pages:'64',identity:scope.identity});
        if (before!=null) query.set('before',before);
        const start=Date.now(),response=await fetch(origin+'/history/data?'+query,{signal:AbortSignal.timeout(45000)});
        const headersAt=Date.now();
        assert.equal(response.status,200);
        const input=await response.arrayBuffer();context.input=input;
        const pages=vm.runInContext('inspectHistoryCacheChunk(input)',context);
        const first=Number(response.headers.get('X-History-First-Sequence')),count=Number(response.headers.get('X-History-Pages'));
        assert.equal(input.byteLength,128*1024); assert.equal(count,64); assert.equal(pages.length,64);
        assert.equal(response.headers.get('X-History-Identity'),scope.identity);
        for(const page of pages)assert.equal(page.sequence,(first+page.offset/2048)&0xFFFFFF);
        if(before!=null) assert.equal(pages.at(-1).sequence,before);
        before=(first-1)&0xFFFFFF;
        const completedAt=Date.now();
        console.log(JSON.stringify({block:index+1,bytes:input.byteLength,first,last:pages.at(-1).sequence,headersMs:headersAt-start,
            bodyMs:completedAt-headersAt,elapsedMs:completedAt-start,KiBs:Math.round(input.byteLength*1000/(completedAt-start)/1024),crc:'ok'}));
        await new Promise(resolve=>setTimeout(resolve,1000));
    }
    assert.equal((await fetch(origin+'/history/data?from=1&to=2&pages=0')).status,400);
    assert.equal((await fetch(origin+'/history/data?from=1&to=2&pages=64&identity=wrong')).status,409);
    console.log('PASS: NAND real, '+total+' bloques consecutivos de 128 KiB, CRC, cursor y validacion de parametros.');
}
run().catch(error=>{console.error(error);process.exitCode=1;});
