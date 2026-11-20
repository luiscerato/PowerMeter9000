const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const test=require('node:test');
const context={TextEncoder};
vm.createContext(context);
vm.runInContext(fs.readFileSync('data/index.js','utf8'),context);
const firmware=fs.readFileSync('src/webserver.cpp','utf8');
const eventApi=fs.readFileSync('src/web/events_http.cpp','utf8');

test('rechazos de firmware identifican la clave sin devolver su valor',()=>{
    assert.match(firmware,/invalid_or_unsupported_option", values\[count\]\.key/);
    assert.match(firmware,/invalid_network_config", invalidKey/);
    assert.match(eventApi,/invalid_value", field\.key/);
    assert.match(eventApi,/"invalid_config", invalidKey/);
});

test('rangos numericos, booleanos y bitmap rechazan valores ambiguos',()=>{
    for(const value of ['',true,null,'NaN','1e999'])assert.equal(context.settingsNumber(value,0,2000),false);
    for(const value of ['false',false,0,'0'])assert.equal(context.settingsBoolean(value),false);
    assert.throws(()=>context.settingsBoolean('yes'));
    for(const groupConfig of [6,0x40000,0x8000])assert.throws(()=>context.validateBackupSection('history',{enabled:true,groupConfig,samplePeriodDeciseconds:10}));
    assert.throws(()=>context.validateBackupSection('history',{enabled:true,groupConfig:true,samplePeriodDeciseconds:10}));
    context.validateBackupSection('energy',{WattH:0});
    assert.throws(()=>context.validateBackupSection('energy',{WattH:1e39}));
    for(const dipMs of [0,9,15,2001,2010])assert.throws(()=>context.validateCaptureValues({dipMs}));
    context.validateCaptureValues({dipMs:10,ocMs:2000});
    assert.throws(()=>context.validateCaptureValues({ocCurrent:10,ocHyst:10}));
    assert.equal(context.historyBackupToOptions({enabled:'false',groupConfig:0,samplePeriodDeciseconds:10}).enabled,false);
});

test('red valida bytes UTF8, subred, puerta de enlace y limites de los consumidores',()=>{
    for(const value of [0,65536,'1e999'])assert.throws(()=>context.validateNetworkValues({'mqtt-port':value}));
    for(const [key,size] of [['mqtt-server',64],['mqtt-user',32],['mqtt-pass',32]])assert.throws(()=>context.validateNetworkValues({[key]:'x'.repeat(size)}));
    assert.throws(()=>context.validateNetworkValues({'sta-ssid':'á'.repeat(17)}));
    const values={staticIp:true,'sta-ip':'192.168.1.90','sta-mask':'255.255.254.0','sta-gw':'192.168.0.1'};
    context.validateNetworkValues(values);
    for(const gateway of ['192.168.0.0','192.168.1.255','10.0.0.1','192.168.0.256'])
        assert.throws(()=>context.validateNetworkValues({...values,'sta-gw':gateway}));
    assert.throws(()=>context.validateNetworkValues({...values,'sta-mask':'255.0.255.0'}));
    context.validateNetworkValues({'sta-pass':'','ap-pass':'12345678','time-server':'ar.pool.ntp.org'});
});

test('verifica valores float32 sin tolerancias amplias ni revelar secretos',()=>{
    context.assertSettingsMatch({neutralRms:1.123456789},{neutralRms:Math.fround(1.123456789)});
    context.assertSettingsMatch({AIGAIN:'0xffffffff'},{AIGAIN:-1});
    context.assertSettingsMatch({'rec.dip':0},{'rec.dip':false});
    assert.throws(()=>context.assertSettingsMatch({AIGAIN:'0xffffffff'},{AIGAIN:'key no existe'}));
    assert.throws(()=>context.assertSettingsMatch({neutralRms:1.123456789},{neutralRms:1.123}));
    assert.throws(()=>context.assertSettingsMatch({'sta-pass':'secret'},{'sta-pass':'different'}),error=>!error.message.includes('secret')&&!error.message.includes('different'));
});
