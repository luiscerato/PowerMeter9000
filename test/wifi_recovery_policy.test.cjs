const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {execFileSync}=require('node:child_process');

// Extrae la funcion real indicada por signature de source, incluyendo todas sus llaves.
function block(source,signature) {
    const start=source.indexOf(signature);assert.ok(start>=0);
    let end=source.indexOf('{',start)+1,depth=1;
    while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}
    return source.slice(start,end);
}
const source=fs.readFileSync('src/wifiUtils.cpp','utf8');
const poll=block(source,'static void pollWifiRecovery()').replace('static void','constexpr void');
const web=fs.readFileSync('src/webserver.cpp','utf8');
const ipNumber=block(web,'static uint32_t networkIpNumber(').replace('static uint32_t','constexpr uint32_t');
const code=`#include <stdint.h>
#define debugI(...) do {} while(0)
#define WIFI_MODE_STA 1
struct networkSettingsGuard {bool locked=true;};
struct IPAddress {uint8_t bytes[4];constexpr uint8_t operator[](int i)const{return bytes[i];}};
${ipNumber}
static_assert(networkIpNumber(IPAddress{{192,168,1,90}})==0xc0a8015aU,"IP order must not depend on ESP endianness");
static_assert((~networkIpNumber(IPAddress{{255,255,254,0}}))==511U,"The existing /23 mask must remain valid");
enum class Wifi_Mode {Off,Station,SoftAP,Station_AP};
struct Machine {
    Wifi_Mode configuredWifiMode=Wifi_Mode::Station;
    bool recoveryAp=false,stationHasSsid=true,apAttempted=false,startOk=true;
    uint32_t lastStationOnlineMs=0,stationOnlineSinceMs=0,now=0,attempts=0;
    struct Radio {bool online=false;int clients=0;bool stopped=false;
        constexpr bool isConnected(){return online;}
        constexpr int softAPgetStationNum(){return clients;}
        constexpr void softAPdisconnect(bool){stopped=true;}
        constexpr void mode(int){}
    } WiFi;
    constexpr uint32_t millis(){return now;}
    constexpr bool startConfiguredAp(bool){++attempts;return startOk;}
    ${poll}
};
constexpr bool fallback() {
    Machine m;m.now=29999;m.pollWifiRecovery();if(m.attempts)return false;
    m.now=30000;m.pollWifiRecovery();if(!m.recoveryAp)return false;
    m.WiFi.online=true;m.now=31000;m.pollWifiRecovery();m.WiFi.clients=1;m.now=61000;m.pollWifiRecovery();
    if(!m.recoveryAp)return false;m.WiFi.clients=0;m.pollWifiRecovery();return !m.recoveryAp&&m.WiFi.stopped;
}
constexpr bool missingSsid() {
    Machine m;m.stationHasSsid=false;m.startOk=false;m.now=1;m.pollWifiRecovery();m.now=2;m.pollWifiRecovery();
    if(m.attempts!=1)return false;m.now=30001;m.startOk=true;m.pollWifiRecovery();return m.recoveryAp&&m.attempts==2;
}
constexpr bool modesAndWrap() {
    Machine m;m.configuredWifiMode=Wifi_Mode::Off;m.now=60000;m.pollWifiRecovery();if(m.attempts)return false;
    m.configuredWifiMode=Wifi_Mode::SoftAP;m.pollWifiRecovery();if(m.attempts)return false;
    m.configuredWifiMode=Wifi_Mode::Station;m.lastStationOnlineMs=UINT32_MAX-20000U;m.now=10000U;m.pollWifiRecovery();return m.recoveryAp;
}
static_assert(fallback(),"AP waits 30 seconds and preserves connected clients");
static_assert(missingSsid(),"Empty SSID recovers immediately without a hot retry loop");
static_assert(modesAndWrap(),"Manual modes and clock wrap are respected");`;
const output='.pio/build/wifi-recovery-policy.cpp';fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,code);
execFileSync(path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe'),['-std=c++14','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: politica real de AP, timeout, recuperacion, clientes, fallos y wrap.');
