const assert=require('node:assert/strict');
const test=require('node:test');
const fs=require('node:fs');
const vm=require('node:vm');

// Ejecuta el cliente real con una API simulada; mode selecciona exito, fallo NVS o conflicto.
function client(mode='ok') {
    let revision=1,seq=1,polled=0;
    const calls=[];
    const config={rev:1,volt:{},time:{},neut:{},curr:{},rec:{},wave:{}};
    const context={AbortController,URLSearchParams,Date,clearTimeout,
        setTimeout:(fn,ms)=>setTimeout(fn,ms===300?0:ms),$:()=>({prop(){return this;},attr(){return this;}}),board:{IP:'http://meter'},
        fetch:async(url,options)=>{
            const path=new URL(url).pathname;calls.push({path,method:options.method||'GET',body:options.body?.toString()});
            const response=(status,value)=>({status,ok:status<400,json:async()=>value});
            if(path==='/events/config'&&options.method==='POST') {
                if(mode==='conflict')return response(409,{error:'rev_conflict'});
                revision=2;seq=2;
                for(const [path,value] of options.body)if(path!=='rev') {
                    const [group,key]=path.split('.');config[group][key]=Number(value);
                }
                if(mode==='mismatch')config.rec.dip=1;
                return response(202,{ok:true,rev:2});
            }
            if(path==='/events/config')return response(200,{...config,rev:revision===2&&polled>1&&['ok','mismatch'].includes(mode)?2:1});
            if(path==='/events/status') {
                if(new URL(url).searchParams.get('since')===String(seq)&&mode==='unchanged')return response(204,null);
                ++polled;
                if(mode==='fail')return response(200,{seq:2,cfg:1,save:{state:'fail',rev:2}});
                return response(200,{seq:++seq,cfg:polled>1?revision:1,save:{state:polled>1?'ok':'wait',rev:2}});
            }
            throw Error('Unexpected request '+url);
        }};
    vm.createContext(context);vm.runInContext(fs.readFileSync('data/index.js','utf8'),context);
    return {context,calls,revision:()=>revision};
}
test('guarda por formulario con un POST y espera la revision confirmada',async()=>{
    const {context,calls}=client();await context.readEventsConfig();
    await context.saveEventConfig({'rec.dip':0,'wave.dip':0});
    const posts=calls.filter(call=>call.method==='POST');assert.equal(posts.length,1);
    assert.equal(posts[0].path,'/events/config');
    assert.equal(posts[0].body,'rev=1&rec.dip=0&wave.dip=0');
    assert.ok(calls.filter(call=>call.path==='/events/status').length>=2);
});
test('fallo NVS se informa sin declararlo guardado ni volver a escribir',async()=>{
    const {context,calls}=client('fail');await context.readEventsConfig();
    await assert.rejects(context.saveEventConfig({'wave.pre':288}),/configuración anterior/);
    assert.equal(calls.filter(call=>call.method==='POST').length,1);
    await assert.rejects(context.saveEventConfig({'wave.pre':288}),/Recargue/);
    assert.equal(calls.filter(call=>call.method==='POST').length,1);
});

test('revision confirmada pero valores distintos detiene el guardado',async()=>{
    const {context,calls}=client('mismatch');await context.readEventsConfig();
    await assert.rejects(context.saveEventConfig({'rec.dip':0}),/No coincide lo guardado: rec.dip/);
    assert.equal(calls.filter(call=>call.method==='POST').length,1);
});
test('revision vieja exige recargar antes de otro intento',async()=>{
    const {context,calls}=client('conflict');await context.readEventsConfig();
    await assert.rejects(context.saveEventConfig({'rec.dip':1}),/otro cliente/);
    await assert.rejects(context.saveEventConfig({'rec.dip':1}),/Recargue/);
    assert.equal(calls.filter(call=>call.method==='POST').length,1);
});
test('204 reutiliza el ultimo estado sin parsear otro JSON',async()=>{
    const {context}=client('unchanged');const first=await context.readEventsStatus();
    const second=await context.readEventsStatus();assert.equal(second,first);
});
