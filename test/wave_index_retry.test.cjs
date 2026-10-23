const assert = require('node:assert/strict');
const test = require('node:test');
const fs = require('node:fs');
const vm = require('node:vm');

// Ejecuta las funciones web reales con responses simuladas; abort cancela durante la espera.
function client(responses, abort = false) {
    const controller = new AbortController();
    const calls = [];
    let clock = 0;
    const scope = {deviceId:'aabbccddeeff',fastEpoch:'fast',waveEpoch:'wave'};
    const context = {AbortController, DOMException, clearTimeout, board:{IP:'http://meter'},
        Date:{now:()=>clock},
        setTimeout:(fn,ms)=>setTimeout(()=>{clock += ms; fn();}, ms >= 20000 ? ms : 0),
        fetch:async url => {
            calls.push(url);
            const value = responses[Math.min(calls.length - 1, responses.length - 1)];
            if (abort) setTimeout(()=>controller.abort(),0);
            return {ok:value.status < 400,status:value.status,json:async()=>value.body};
        }};
    vm.createContext(context);
    vm.runInContext(fs.readFileSync('data/event_history.js','utf8'), context);
    vm.runInContext(fs.readFileSync('data/capture_cache.js','utf8'), context);
    return {calls, run:()=>context.loadCachedWaveIndex({id:45,timestamp:1000},scope,controller.signal)};
}
const ready = {status:200,body:{fastIdentity:'aabbccddeeff:fast',waveIdentity:'aabbccddeeff:wave',eventTimestamp:1000,captures:[{id:71}]}};

test('pendiente y ocupado se reintentan, no se convierten en lista vacia', async () => {
    const value = client([{status:202,body:{ready:false,reason:'wave_index_pending'}},
        {status:503,body:{ready:false,reason:'wave_index_busy'}},ready]);
    const result = await value.run();
    assert.equal(result.captures[0].id,71); assert.equal(value.calls.length,3);
    assert.ok(value.calls.every(url=>url.includes('eventId=45&unixUs=1000000')));
});
test('un fallo NAND no se oculta ni se reintenta como ocupado', async () => {
    const value = client([{status:500,body:{ready:false,reason:'wave_index_read_failed'}}]);
    await assert.rejects(value.run(), error=>error.status === 500 && error.reason === 'wave_index_read_failed');
    assert.equal(value.calls.length,1);
});
test('cancelar durante la espera evita otra peticion', async () => {
    const value = client([{status:202,body:{ready:false,reason:'wave_index_pending'}}], true);
    await assert.rejects(value.run(), error=>error.name === 'AbortError');
    assert.equal(value.calls.length,1);
});
test('un servidor siempre ocupado vence sin devolver un resultado falso', async () => {
    const value = client([{status:503,body:{ready:false,reason:'wave_index_busy'}}]);
    await assert.rejects(value.run(), /sigue ocupada/);
    assert.ok(value.calls.length <= 22);
});
