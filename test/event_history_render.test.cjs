const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

// Simula lecturas rapidas/lentas con el codigo real; step es el avance en ms por captura.
async function checkRendering(step) {
    let now=1000,renders=0;
    const context=vm.createContext({Date:{now:()=>now},console,
        $:()=>({text(){return this;}}),
        loadCachedFast:async id=>({id}),gridEventRecords:()=>[]});
    vm.runInContext(fs.readFileSync('data/event_history.js','utf8'),context);
    context.renderEventHistory=()=>{++renders;vm.runInContext('eventHistory.renderAt=Date.now()',context);};
    vm.runInContext('eventHistory.run=1;eventHistory.renderAt=Date.now()',context);
    for(let i=1;i<=54;++i) {
        now+=step;
        await context.appendEventHistoryCapture({i,t:1},{aborted:false},1);
    }
    assert.equal(vm.runInContext('eventHistory.captures.length',context),54);
    assert.ok(renders<=Math.floor(54*step/150));
    if(step>=150)assert.equal(renders,54,'las descargas lentas siguen mostrando cada avance');
    else assert.equal(renders,0,'la cache rapida no reconstruye 54 veces la pantalla');
}

// Verifica progresividad sin redibujar cada lectura local; sin parametros.
async function run() {
    await checkRendering(1);await checkRendering(200);
    const source=fs.readFileSync('data/event_history.js','utf8');
    assert.match(source,/eventHistory\.controller = null;[\s\S]*renderEventHistory\(\);/);
    console.log('PASS: 54 lecturas de cache agrupadas; avance lento y render final conservados.');
}

run().catch(error=>{console.error(error);process.exitCode=1;});
