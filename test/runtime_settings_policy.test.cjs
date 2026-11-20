const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');

// Extrae la funcion real de source indicada por signature para ejecutar sus decisiones en el arnes.
function block(source,signature) {
    const start=source.indexOf(signature);assert.ok(start>=0,signature);
    let end=source.indexOf('{',start)+1,depth=1;
    while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}
    return source.slice(start,end);
}
const wifi=fs.readFileSync('src/wifiUtils.cpp','utf8'),history=fs.readFileSync('src/storage/history_recorder.cpp','utf8');
const queue=block(wifi,'void networkQueueApply(').replace('void networkQueueApply','constexpr void networkQueueApply');
const poll=block(wifi,'static void pollNetworkConfig() {').replace('static void','constexpr void');
const setHistory=block(history,'bool HistoryRecorder::setConfig(').replace('bool HistoryRecorder::','constexpr bool ');
const pollHistory=block(history,'void HistoryRecorder::pollConfig()').replace('void HistoryRecorder::','constexpr void ');
const code=`#include <stdint.h>
#define portENTER_CRITICAL(x) do {} while(0)
#define portEXIT_CRITICAL(x) do {} while(0)
struct networkSettingsGuard {bool locked=true;};
struct Network {
    uint8_t pendingNetworkServices=0,networkState=0;uint32_t networkApplyAfterMs=0,now=0;
    bool busy=false,readOk=true,wifiOk=true,queueDuringRead=false;unsigned reads=0,radio=0,ntp=0;
    constexpr uint32_t millis(){return now;}
    constexpr bool waveCaptureBusy(){return busy;}
    constexpr bool readOptions(){++reads;if(queueDuringRead){queueDuringRead=false;networkQueueApply(2);}return readOk;}
    constexpr bool applyWifiRuntime(bool){if(!readOptions())return false;++radio;return wifiOk;}
    constexpr bool TimeSetServer(){if(!readOptions())return false;++ntp;return true;}
    ${queue}
    ${poll}
};
constexpr bool consumers() {
    Network n;n.pollNetworkConfig();if(n.reads)return false;
    n.networkQueueApply(2);n.now=999;n.pollNetworkConfig();if(n.reads)return false;
    n.now=1000;n.busy=true;n.pollNetworkConfig();if(n.reads)return false;
    n.busy=false;n.pollNetworkConfig();if(n.reads!=1||n.ntp!=1||n.radio)return false;
    n.pollNetworkConfig();if(n.reads!=1)return false;
    n.networkQueueApply(1);n.now+=1000;n.queueDuringRead=true;n.pollNetworkConfig();
    if(n.radio!=1||n.pendingNetworkServices!=2||n.networkState!=1)return false;
    n.now+=1000;n.pollNetworkConfig();return n.reads==3&&n.radio==1&&n.ntp==2&&n.networkState==0;
}
constexpr bool failuresAndWrap() {
    Network n;n.now=UINT32_MAX-500;n.networkQueueApply(3);n.now=498;n.pollNetworkConfig();if(n.reads)return false;
    n.now=499;n.readOk=false;n.pollNetworkConfig();return n.reads==2&&!n.radio&&!n.ntp&&n.networkState==3;
}
struct HistoryRecorderConfig {bool enabled=true;uint32_t groupConfig=0;uint16_t samplePeriod=10;};
struct GridEventStoreMetrics {bool captureActive=false;};
struct Grid {bool busy=false;constexpr Grid& gridEvents(){return *this;}constexpr void getMetrics(GridEventStoreMetrics& g){g.captureActive=busy;}};
struct Store {bool ready=true;unsigned flushes=0;constexpr bool flush(){++flushes;return ready;}};
struct History {
    HistoryRecorderConfig config,pendingConfig;uint8_t configState=0;bool busy=false,writeOk=true;unsigned writes=0;
    uint32_t nextSampleMs=100;bool sampleScheduleReady=true;Grid storageManager;Store store;
    constexpr bool isConfigSupported(const HistoryRecorderConfig& c){return c.samplePeriod>0;}
    constexpr bool waveCaptureBusy(){return busy;}
    constexpr bool saveConfig(const HistoryRecorderConfig&){++writes;return writeOk;}
    ${setHistory}
    ${pollHistory}
};
constexpr bool historyApply() {
    History h;HistoryRecorderConfig c;c.samplePeriod=20;
    if(!h.setConfig(c)||h.config.samplePeriod!=10||h.writes||h.setConfig(c))return false;
    h.busy=true;h.pollConfig();if(h.writes)return false;
    h.busy=false;h.storageManager.busy=true;h.pollConfig();if(h.writes||h.store.flushes)return false;
    h.storageManager.busy=false;h.store.ready=false;h.pollConfig();if(h.writes)return false;
    h.store.ready=true;h.writeOk=false;h.pollConfig();if(h.config.samplePeriod!=10||h.configState!=3)return false;
    h.writeOk=true;h.setConfig(c);h.pollConfig();if(h.config.samplePeriod!=20||h.configState||h.sampleScheduleReady)return false;
    h.setConfig(c);h.pollConfig();return h.writes==2;
}
static_assert(consumers(),"Each service consumes its request once; requeued changes survive");
static_assert(failuresAndWrap(),"Read failure does not apply stale data; deadline handles clock wrap");
static_assert(historyApply(),"History owns changes, defers captures and keeps active config on failure");`;
const output='.pio/build/runtime-settings-policy.cpp';fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,code);
execFileSync(path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe'),['-std=c++14','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: consumidores independientes, cola concurrente, fallos NVS, captura activa y wrap.');
