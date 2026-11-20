const fs = require('node:fs');
const assert = require('node:assert/strict');
const {codec} = require('../test/grid_fixture.cjs');
const origin = process.env.METER_URL || 'http://192.168.1.90';
const keys = ['vr','vs','vt','ir','is','it','inn','vn'];

// Resume datos originales y separadores visuales de event, sin modificar ninguna muestra.
function inspect(event) {
    const samples = event.samples, drawn = Array.from(codec.gridFastDisplaySamples(event));
    const jumps = samples.flatMap((sample,index) => index && sample.relativeMs-samples[index-1].relativeMs > 18 ?
        [{index,from:samples[index-1].relativeMs,to:sample.relativeMs,delta:sample.relativeMs-samples[index-1].relativeMs}] : []);
    const separators = drawn.flatMap((sample,index) => !keys.some(key=>Number.isFinite(sample[key])) ?
        [{at:sample.relativeMs,from:drawn[index-1]?.relativeMs,to:drawn[index+1]?.relativeMs}] : []);
    return {id:event.id,startup:event.startup,resetReason:event.settings?.resetReason,partial:event.partial,invalidPages:event.invalidPages,
        count:samples.length,drawn:drawn.length,pre:event.preSampleCount,first:event.firstOffset,
        missing:Object.fromEntries(keys.map(key=>[key,samples.filter(sample=>!Number.isFinite(sample[key])).length])),
        transitions:event.transitions.map(item=>({at:item.offsetMs,states:item.states})),jumps,separators};
}

// Descarga IDs en secuencia y conserva binarios/diagnostico; sin parametros, solo lee la placa.
async function run() {
    const ids = process.argv.slice(2).map(Number);
    if (!ids.length) ids.push(85,86,87,88,89,90,103,104,105,106,107,108,109,110,111,112,113);
    assert.ok(ids.every(id=>Number.isInteger(id)&&id>0));
    const directory = 'logs/fast-gaps-'+new Date().toISOString().replace(/[:.]/g,'-');
    fs.mkdirSync(directory,{recursive:true}); const results=[];
    for (const id of ids) {
        const response = await fetch(origin+'/grid-events/data?eventId='+id,{signal:AbortSignal.timeout(20000)});
        assert.equal(response.status,200,'Captura '+id);
        const buffer = await response.arrayBuffer(), event = codec.decodeGridEvent(buffer);
        assert.equal(event.id,id); assert.equal(event.partial,false,'CRC completo '+id);
        fs.writeFileSync(directory+'/'+id+'.bin',Buffer.from(buffer));
        const row = inspect(event); results.push(row); console.log(JSON.stringify(row));
        await new Promise(resolve=>setTimeout(resolve,200));
    }
    fs.writeFileSync(directory+'/summary.json',JSON.stringify(results,null,2)); console.log('RESULT '+directory);
}
if (require.main === module) run().catch(error=>{console.error(error);process.exitCode=1;});
module.exports = {inspect};
