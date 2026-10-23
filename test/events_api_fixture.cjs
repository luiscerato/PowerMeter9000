// Construye el JSON agrupado desde los valores planos usados por las pruebas; state es mutable.
function config(state) {
    const g=state.grid,m=state.meter,w=state.wave;
    return {rev:state.rev||1,save:{state:'ok',rev:0},
        volt:{lowR:g.lowR,lowS:g.lowS,lowT:g.lowT,highR:g.highR,highS:g.highS,highT:g.highT,loss:g.noVoltage,hyst:g.hysteresis},
        time:{dip:m.dipMs,swell:m.swellMs,oc:m.ocMs,loss:g.lossConfirmMs,back:g.recoveryConfirmMs},
        neut:{high:g.neutralHigh,hyst:g.neutralHysteresis,wait:g.neutralConfirmMs,back:g.neutralRecoveryMs},
        curr:{high:m.ocCurrent,hyst:m.ocHyst},rec:{dip:!!m.dipEnabled,swell:!!m.swellEnabled,oc:!!m.overcurrent},
        wave:{on:w.saveEnabled,pre:w.preMs,dip:!!(w.pointMask&1),swell:!!(w.pointMask&2),oc:!!(w.pointMask&4)}};
}
// Atiende solo las rutas nuevas de config/status; route es Playwright y state conserva el fixture.
async function routeEvents(route,state) {
    const request=route.request(),url=new URL(request.url());
    if (url.pathname==='/events/status') {
        const w=state.wave;
        return route.fulfill({json:{seq:state.rev||1,cfg:state.rev||1,save:{state:'ok',rev:0},busy:false,last:45,
            wave:{on:true,count:w.storedCaptures??17,used:w.occupiedSlots??20,slots:w.totalSlots??640,pre:192,ram:16384,
                done:2,orph:0,pending:0,gaps:0,drop:0,fail:0,free:4},lim:{point:3700,pages:5}}});
    }
    if (url.pathname!=='/events/config') return false;
    if (request.method()==='GET') {
        state.onGet?.();
        if (state.failGet?.()) return route.fulfill({status:500,json:{error:'Error de prueba'}});
        return route.fulfill({json:config(state)});
    }
    const fields=new URLSearchParams(request.postData());
    if (state.failPost?.(fields)) return route.fulfill({status:500,json:{error:'Error de prueba'}});
    if (Number(fields.get('rev'))!==(state.rev||1)) return route.fulfill({status:409,json:{error:'rev_conflict'}});
    const g=state.grid,m=state.meter,w=state.wave;
    const mapping={ 'volt.lowR':[g,'lowR'],'volt.lowS':[g,'lowS'],'volt.lowT':[g,'lowT'],'volt.highR':[g,'highR'],
        'volt.highS':[g,'highS'],'volt.highT':[g,'highT'],'volt.loss':[g,'noVoltage'],'volt.hyst':[g,'hysteresis'],
        'time.loss':[g,'lossConfirmMs'],'time.back':[g,'recoveryConfirmMs'],'neut.high':[g,'neutralHigh'],
        'neut.hyst':[g,'neutralHysteresis'],'neut.wait':[g,'neutralConfirmMs'],'neut.back':[g,'neutralRecoveryMs'],
        'time.dip':[m,'dipMs'],'time.swell':[m,'swellMs'],'time.oc':[m,'ocMs'],
        'curr.high':[m,'ocCurrent'],'curr.hyst':[m,'ocHyst'],'rec.dip':[m,'dipEnabled'],
        'rec.swell':[m,'swellEnabled'],'rec.oc':[m,'overcurrent'],'wave.pre':[w,'preMs']};
    for (const [key,value] of fields) if (mapping[key]) {const [target,name]=mapping[key];target[name]=Number(value);}
    if (fields.has('wave.on')) w.saveEnabled=fields.get('wave.on')==='1';
    for (const [key,bit] of [['wave.dip',1],['wave.swell',2],['wave.oc',4]]) if(fields.has(key))
        w.pointMask=Number(fields.get(key))?w.pointMask|bit:w.pointMask&~bit;
    state.rev=(state.rev||1)+1;state.onPost?.(fields);
    return route.fulfill({status:202,json:{ok:true,rev:state.rev}});
}
// Devuelve ajustes validos predeterminados para pruebas de navegacion sin personalizaciones.
function defaultState() {
    return {grid:{lowR:190,lowS:190,lowT:190,highR:250,highS:250,highT:250,noVoltage:20,hysteresis:5,
        lossConfirmMs:250,recoveryConfirmMs:250,neutralHigh:25,neutralHysteresis:2,neutralConfirmMs:100,neutralRecoveryMs:250},
        meter:{dipEnabled:1,swellEnabled:1,overcurrent:1,dipMs:50,swellMs:50,ocMs:10,ocCurrent:50,ocHyst:1},
        wave:{preMs:192,saveEnabled:true,pointMask:7}};
}
module.exports={config,routeEvents,defaultState};
