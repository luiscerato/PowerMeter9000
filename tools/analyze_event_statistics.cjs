const fs = require('node:fs');
const assert = require('node:assert/strict');
const {codec} = require('../test/grid_fixture.cjs');
const {buildEventHistory} = require('../data/event_history.js');
const {buildEventStatistics} = require('../data/event_statistics.js');
const origin = process.env.METER_URL || 'http://192.168.1.90';

// Resume event con el mismo contrato de la web; conserva integridad, origen y transiciones.
function summarize(event) {
    return {id:event.id,timestamp:event.timestamp,estimated:event.estimatedTime,startup:event.startup,partial:event.partial,
        dropped:!!(event.timeStatus&64),synthetic:!!(event.timeStatus&128),records:Array.from(codec.gridEventRecords(event))};
}

// Descarga el catalogo y sus capturas en secuencia; sin parametros, solo lee la placa y guarda el diagnostico local.
async function run() {
    const info = await (await fetch(origin+'/grid-events',{signal:AbortSignal.timeout(15000)})).json();
    const directory = 'logs/event-statistics-'+new Date().toISOString().replace(/[:.]/g,'-');
    fs.mkdirSync(directory,{recursive:true}); const captures=[];
    for (let id=info.oldestId;id<=info.newestId;++id) {
        const response = await fetch(origin+'/grid-events/data?eventId='+id,{signal:AbortSignal.timeout(20000)});
        if (response.status===404) continue;
        assert.equal(response.status,200,'Captura '+id);
        const bytes = await response.arrayBuffer(), event = codec.decodeGridEvent(bytes);
        fs.writeFileSync(directory+'/'+id+'.bin',Buffer.from(bytes)); captures.push(summarize(event));
        if (id%20===0) console.log('Capturas '+id+'/'+info.newestId);
        await new Promise(resolve=>setTimeout(resolve,100));
    }
    const from = Math.min(...captures.map(event=>event.timestamp)), to = Date.now();
    const result = buildEventHistory(captures,from,to), stats = buildEventStatistics(result.intervals,from,to,'day');
    const outages = result.intervals.filter(item=>!item.point&&!item.current&&item.state===(item.subject===4?3:4))
        .map(item=>({...item,hours:(item.end-item.start)/3600000,startIso:new Date(item.start).toISOString(),endIso:new Date(item.end).toISOString()}));
    fs.writeFileSync(directory+'/summary.json',JSON.stringify({from,to,captures,result,stats},null,2));
    console.log(JSON.stringify({summary:stats.summary,outages},null,2)); console.log('RESULT '+directory);
}
run().catch(error=>{console.error(error);process.exitCode=1;});
