const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const crypto = require('node:crypto');
const origin = process.env.METER_URL || 'http://192.168.1.90';
const pauseMs = 2000, rounds = 3, roundKiB = 512;

// Espera ms sin ocupar CPU; ms es la duracion del descanso entre descargas.
function sleep(ms) {return new Promise(resolve => setTimeout(resolve, ms));}

// Resume una lista numerica; values contiene duraciones o cantidades observadas.
function stats(values) {
    if (!values.length) return null;
    const sorted = [...values].sort((a,b) => a-b);
    return {count: sorted.length, min: Math.round(sorted[0]), mean: Math.round(sorted.reduce((a,b) => a+b,0)/sorted.length),
        median: Math.round(sorted[Math.floor(sorted.length/2)]), p95: Math.round(sorted[Math.ceil(sorted.length*0.95)-1]), max: Math.round(sorted.at(-1))};
}

// Lee un JSON del ESP sin cache; path identifica una ruta de solo consulta.
async function readJson(path) {
    const response = await fetch(origin + path, {cache:'no-store',signal:AbortSignal.timeout(10000)});
    assert.equal(response.status,200,path);
    return response.json();
}

// Mide grupos con igual cantidad de datos, CRC y consultas status concurrentes; sin parametros, no modifica el ESP.
async function run() {
    const before = await readJson('/storage?detail=1'), scope = await readJson('/history');
    assert.match(scope.identity,/^[0-9a-f]{12}:[0-9a-f]{16}$/i);
    assert.ok(scope.committedPages >= rounds * roundKiB / 2);
    const decoder = vm.createContext({ArrayBuffer,DataView,Uint8Array,Map,Set,Date,URLSearchParams});
    vm.runInContext(fs.readFileSync('data/history_ui.js','utf8')+'\n'+fs.readFileSync('data/history_cache.js','utf8'),decoder);
    const groups = [32,64,128,256,512].map(kib => ({kib,blocks:[],status:[],hash:crypto.createHash('sha256')}));
    let current = null, stopped = false;
    // Sonda status solo durante una descarga; stopped termina la tarea y evita solicitudes solapadas.
    const monitor = (async () => {
        while (!stopped) {
            const sample = current;
            if (sample) {
                const started = performance.now();
                try {
                    const result = await readJson('/status');
                    sample.group.status.push({ms:performance.now()-started,free:result.free,phase:sample.phase});
                } catch (error) {sample.group.status.push({error:error.message});}
                await sleep(250);
            } else await sleep(50);
        }
    })();
    try {
        for (let round=0; round<rounds; ++round) {
            // Se alterna orden para no favorecer un tamano por cambios de Wi-Fi o carga con el tiempo.
            const order = round === 0 ? [32,128,512,64,256] : round === 1 ? [256,64,512,128,32] : [128,512,32,256,64];
            for (const kib of order) {
                const group = groups.find(item => item.kib === kib);
                let cursor = (scope.latestSequence - round * roundKiB / 2) & 0xFFFFFF;
                const batch = [];
                for (let block=0; block<roundKiB/kib; ++block) {
                    const query = new URLSearchParams({from:String(scope.oldestTimestamp),to:String(scope.committedTo),
                        pages:String(kib/2),before:String(cursor),identity:scope.identity});
                    const sample = {group,phase:'headers'};current = sample;
                    const started = performance.now();
                    const response = await fetch(origin+'/history/data?'+query,{cache:'no-store',signal:AbortSignal.timeout(30000)});
                    const headersAt = performance.now();sample.phase = 'body';
                    assert.equal(response.status,200);
                    const buffer = await response.arrayBuffer(), ended = performance.now();current = null;
                    assert.equal(buffer.byteLength,kib*1024);
                    assert.equal(response.headers.get('X-History-Identity'),scope.identity);
                    const first = Number(response.headers.get('X-History-First-Sequence'));
                    assert.equal((first+kib/2-1)&0xFFFFFF,cursor);
                    decoder.input = buffer;
                    const pages = vm.runInContext('inspectHistoryCacheChunk(input)',decoder);
                    assert.equal(pages.length,kib/2);
                    for (const page of pages) assert.equal(page.sequence,(first+page.offset/2048)&0xFFFFFF);
                    // El hash por pagina descendente permite comparar exactamente los mismos datos con cualquier tamano.
                    for (let index=kib/2-1;index>=0;--index) group.hash.update(new Uint8Array(buffer,index*2048,2048));
                    const row = {round:round+1,headersMs:headersAt-started,bodyMs:ended-headersAt,totalMs:ended-started,bytes:buffer.byteLength};
                    group.blocks.push(row);batch.push(row.totalMs);cursor=(first-1)&0xFFFFFF;
                    const resting = performance.now();await sleep(pauseMs);row.pauseMs=performance.now()-resting;
                }
                console.log(JSON.stringify({round:round+1,kib,blocks:batch.length,total:stats(batch),status:stats(group.status.filter(item=>!item.error).map(item=>item.ms))}));
            }
        }
    } finally {current = null;stopped = true;await monitor;}
    const hashes = groups.map(group => group.hash.digest('hex'));
    assert.ok(hashes.every(hash => hash === hashes[0]),'Todos los tamanos deben transferir exactamente las mismas paginas');
    const after = await readJson('/storage?detail=1');
    assert.equal(after.system.resetReason,before.system.resetReason);
    assert.ok(after.system.uptimeMs > before.system.uptimeMs,'Sin reinicios durante la medicion');
    const summary = groups.map((group,index) => {
        const samples = group.status.filter(item=>!item.error), sum = group.blocks.reduce((total,row)=>total+row.totalMs,0);
        const pauses = group.blocks.reduce((total,row)=>total+row.pauseMs,0), totalKiB=rounds*roundKiB;
        return {kib:group.kib,blocks:group.blocks.length,totalMiB:totalKiB/1024,totalMs:stats(group.blocks.map(row=>row.totalMs)),
            headersMs:stats(group.blocks.map(row=>row.headersMs)),bodyMs:stats(group.blocks.map(row=>row.bodyMs)),
            transferKiBs:Math.round(totalKiB*1000/sum),effectiveMiBMinute:Number((totalKiB*60000/(sum+pauses)/1024).toFixed(2)),
            statusMs:stats(samples.map(row=>row.ms)),statusErrors:group.status.filter(item=>item.error).length,
            minObservedFree:Math.min(...samples.map(row=>row.free)),sha256:hashes[index]};
    });
    const result = {at:new Date().toISOString(),origin,pauseMs,rounds,roundKiB,summary,before,after,samples:groups.map(({kib,blocks,status})=>({kib,blocks,status}))};
    fs.mkdirSync('logs',{recursive:true});
    const filename='logs/history-block-benchmark-'+new Date().toISOString().replace(/[:.]/g,'-')+'.json';
    fs.writeFileSync(filename,JSON.stringify(result,null,2));
    console.log('SUMMARY '+JSON.stringify(summary));console.log('RESULT '+filename);
}
run().catch(error=>{console.error(error);process.exitCode=1;});
