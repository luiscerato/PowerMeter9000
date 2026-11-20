const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');
const source=fs.readFileSync('src/event_settings.cpp','utf8');

// Extrae una funcion o estructura real; text/signature identifican el bloque.
function block(text,signature) {
    const start=text.indexOf(signature);assert.ok(start>=0,signature);
    let end=text.indexOf('{',start)+1,depth=1;
    while(depth){if(text[end]==='{')++depth;if(text[end]==='}')--depth;++end;}
    return text.slice(start,end);
}
const header=fs.readFileSync('src/event_settings.h','utf8').replace(/#pragma once|#include[^\n]+/g,'').replace('bool validEventSettings','constexpr bool validEventSettings');
const grid=fs.readFileSync('src/grid_state.h','utf8').replace(/#pragma once|#include[^\n]+/g,'').replaceAll('inline ','constexpr ');
const capture=fs.readFileSync('src/event_capture_config.h','utf8').replace(/#pragma once|#include[^\n]+/g,'');
const valid=block(source,'bool validEventSettings(').replace('bool validEventSettings','constexpr bool validEventSettings');
const queue=block(source,'eventQueueResult eventSettingsQueue(').replace('eventQueueResult eventSettingsQueue','constexpr eventQueueResult eventSettingsQueue');
const get=block(source,'void eventSettingsGet(').replace('void eventSettingsGet','constexpr void eventSettingsGet');
const poll=block(source,'void eventSettingsPoll(').replace('void eventSettingsPoll','constexpr void eventSettingsPoll').replace('Preferences prefs;','Preferences prefs(writeOk, writes);').replace('memcmp(&record, &stored, sizeof(record)) == 0','true');
const code=`#include <stdint.h>
#include <stddef.h>
#include <cmath>
using std::isfinite;
#include "capture_policy.h"
#define portENTER_CRITICAL(value) do {} while(0)
#define portEXIT_CRITICAL(value) do {} while(0)
${grid}
${capture}
${header}
constexpr uint32_t settingsMagic=0x45564331;
${block(source,'struct settingsRecord')};
${valid}
namespace waveEventFormat { constexpr uint32_t crc32(const void*,size_t){return 0;} }
struct Preferences {
    bool ok;unsigned& writes;
    constexpr Preferences(bool ok,unsigned& count):ok(ok),writes(count){}
    constexpr bool begin(const char*,bool){return true;}
    constexpr size_t putBytes(const char*,const void*,size_t count){++writes;return ok?count:0;}
    constexpr size_t getBytes(const char*,void*,size_t count){return ok?count:0;}
    constexpr void end(){}
};
struct GridEventStoreMetrics {bool captureActive=false;};
struct Storage {
    bool capturing=false,enabled=true;unsigned applied=0;uint8_t mask=7;
    constexpr Storage& gridEvents(){return *this;}
    constexpr Storage& waveEvents(){return *this;}
    constexpr void getMetrics(GridEventStoreMetrics& dest){dest.captureActive=capturing;}
    constexpr void applyConfig(bool on,uint8_t value){enabled=on;mask=value;++applied;}
};
struct Store {
    eventSettingsSnapshot current;eventSettingsData pending;
    uint32_t lastRevision=1;uint16_t bootPreMs=96;int settingsMutex=0;bool waveBusy=false,writeOk=true;unsigned writes=0,applied=0;
    Storage storageManager;
    constexpr bool waveCaptureBusy(){return waveBusy;}
    constexpr void meterQueueEventSettings(const gridSettings&,const eventCaptureConfig&){++applied;}
    ${get}
    ${queue}
    ${poll}
};
// Comprueba validacion cruzada, revisiones y una sola escritura por guardado completo.
constexpr bool commit() {
    Store store;eventSettingsData data;uint32_t next=0;
    data.grid.phases[0].lowVoltage=180;data.points.dipEnabled=false;data.waveMask=4;
    if(store.eventSettingsQueue(data,0,&next)!=eventQueueResult::conflict)return false;
    if(store.eventSettingsQueue(data,1,&next)!=eventQueueResult::accepted||next!=2)return false;
    if(store.current.config.grid.phases[0].lowVoltage!=190)return false;
    if(store.eventSettingsQueue(data,1,nullptr)!=eventQueueResult::busy)return false;
    store.waveBusy=true;store.eventSettingsPoll();if(store.writes)return false;
    store.waveBusy=false;store.storageManager.capturing=true;store.eventSettingsPoll();if(store.writes)return false;
    store.storageManager.capturing=false;store.eventSettingsPoll();
    return store.writes==1&&store.applied==1&&store.current.revision==2&&store.current.config.grid.phases[0].lowVoltage==180&&
        !store.current.config.points.dipEnabled&&store.storageManager.mask==4;
}
// Un fallo NVS conserva todos los ajustes anteriores y no recicla el ID de intento.
constexpr bool failure() {
    Store store;eventSettingsData data;uint32_t next=0;data.preMs=288;
    store.eventSettingsQueue(data,1,&next);store.writeOk=false;store.eventSettingsPoll();
    if(store.current.revision!=1||store.current.config.preMs!=96||store.applied||store.current.state!=eventSaveState::failed)return false;
    store.writeOk=true;store.eventSettingsQueue(data,1,&next);store.eventSettingsPoll();
    eventSettingsSnapshot snapshot;store.eventSettingsGet(snapshot);
    return next==3&&store.current.revision==3&&store.writes==2&&store.current.config.preMs==288&&snapshot.restartRequired;
}
// Rechaza limites incompatibles, NaN, confirmaciones fuera de paso y reservas invalidas.
constexpr bool invalid() {
    eventSettingsData data;data.grid.phases[0].lowVoltage=20;if(validEventSettings(data))return false;
    data=eventSettingsData{};data.points.currentHysteresis=data.points.overCurrent;if(validEventSettings(data))return false;
    data=eventSettingsData{};data.points.dipConfirmMs=55;if(validEventSettings(data))return false;
    data=eventSettingsData{};data.preMs=193;if(validEventSettings(data))return false;
    data=eventSettingsData{};data.waveMask=8;return !validEventSettings(data);
}
static_assert(commit(),"Atomic apply and deferred single-key write");
static_assert(failure(),"No partial config on NVS failure or reused attempt IDs");
static_assert(invalid(),"Cross-field validation");
static_assert(sizeof(settingsRecord)<=128,"Small persistent config record");
`;
const output='.pio/build/event-settings-policy-test.cpp';
fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,code);
const compiler=path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler,['-std=c++14','-Isrc','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: metodos reales, revision, guardado diferido, una clave NVS, fallo sin aplicacion parcial y limites.');
