const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/scope_stream.cpp', 'utf8');

// Extrae una funcion real como constexpr para ejecutarla durante la compilacion; start/end delimitan su texto.
function extract(start, end) {
    const first = source.indexOf(start), last = source.indexOf(end, first);
    assert.ok(first >= 0 && last > first);
    return 'constexpr ' + source.slice(first, last);
}
const helpers = extract('void scopeWrite12(', '// Convierte un valor ADE').replace('\nint16_t scopeRead12(', '\nconstexpr int16_t scopeRead12(');
const trigger = extract('bool scopeFindTrigger(', '// Forma cuatro tramos');
const copy = extract('void scopeFillFftFromRing(', '// Entrega el fragmento');
const capture = extract('void scopeProcessQuality(', '// Decodifica y valida');
const priority = extract('void scopeRefreshPriority(', '// Comprueba credito').replace('bool quality = false, changed;', 'bool quality = false, changed = false;');
const code = `#include <stdint.h>
#include <stddef.h>
#include <algorithm>
#include <math.h>
constexpr uint16_t scopeMaxPoints=512, scopeFftPoints=512;
constexpr uint8_t scopeChannelCount=7, scopeFftHeaderBytes=52;
constexpr uint32_t scopeFftIntervalUs=500000;
struct AsyncWebSocket {};
struct scopeSettings { uint8_t triggerChannel=0, rising=1, channelMask=1; };
${helpers}
struct captureState {
    uint8_t scopeRing[5376]={}, scopeFlagsRing[512]={};
    uint16_t scopeRingHead=0, scopeRingCount=0, scopeFftCount=0, scopeFftTriggerIndex=0;
    uint32_t scopePointCount=0, scopeFftStartedUs=0, scopeFftWaitUs=0, scopeFftTrigger=UINT32_MAX, scopeFftId=0;
    uint64_t scopeFftFirstUs=0;
    uint8_t scopeFftChannels=0, scopeFftPart=0, scopeFftCancelReason=0;
    float scopeFftFrequencies[3]={};
    bool scopeFftCapturing=false, valid=true, silent=false;
    int16_t scopePrevious=0;
    uint8_t scopePreviousValid=0, scopeSlopeCount=0;
    unsigned complete=0, fragments=0, failPart=4;
    struct packet { uint8_t data[5428]={}; constexpr uint8_t* get() { return data; } } storage;
    packet* scopeFftPacket=nullptr;
    ${trigger}
    constexpr bool scopePrepareFftPart(AsyncWebSocket*, const scopeSettings&, uint32_t) {
        if (scopeFftPart == failPart) {
            scopeFftCapturing=false; scopeFftCancelReason=2; return false;
        }
        scopeFftPacket=&storage; scopeFftCount=0; return true;
    }
    ${copy}
    constexpr bool scopePublishFftPart(AsyncWebSocket*) {
        const unsigned first=unsigned((scopeFftFirstUs-1000000)/125)+scopeFftPart*512;
        for (unsigned i=0;i<512;++i) {
            const int16_t expected=silent?0:int16_t((first+i)%64)-32;
            if (scopeRead12(storage.data+52,i)!=expected) valid=false;
        }
        if (!scopeFftPart && scopeFftTriggerIndex!=(silent?UINT16_MAX:256)) valid=false;
        ++fragments; scopeFftCount=0; scopeFftPacket=nullptr;
        if (++scopeFftPart==4) {
            ++complete; scopeFftCapturing=false; scopeFftPart=0; scopeFftTrigger=UINT32_MAX; scopeFftWaitUs=0;
        }
        return true;
    }
    ${capture}
};
constexpr bool verify(bool silent, uint32_t initialCount, bool fail) {
    captureState state; state.silent=silent; state.scopePointCount=initialCount;
    if(fail) state.failPart=1;
    scopeSettings settings; float frequencies[3]={50,50,50};
    for (unsigned i=0;i<10000;++i) {
        int16_t values[7]={}; values[0]=silent?0:int16_t(i%64)-32;
        state.scopeProcessQuality(nullptr, values, 0, 1000000+uint64_t(i)*125, settings, frequencies, 1, 0);
    }
    return state.valid && (fail ? state.fragments==1 && state.complete==0 && state.scopeFftCancelReason==2 : state.complete>=2);
}
static_assert(verify(false,0,false), "All four fragments must be contiguous and trigger centered");
static_assert(verify(false,UINT32_MAX-700,false), "Point counter wrap must preserve pretrigger and continuity");
static_assert(verify(true,0,false), "No trigger must still generate complete automatic captures");
static_assert(verify(false,0,true), "Allocation failure must cancel, not resume a partial capture");
#define portENTER_CRITICAL(mux) (void)0
#define portEXIT_CRITICAL(mux) (void)0
constexpr uint32_t scopeQualityTimeoutMs=5000;
struct priorityState {
    struct viewer { uint32_t id=0, qualitySeenMs=0; uint8_t streams=1; };
    using scopeViewer=viewer;
    viewer scopeViewers[2]={};
    bool scopeFftEnabled=true, scopeQualityActive=false;
    uint32_t scopeConfigGeneration=0, now=0;
    constexpr uint32_t millis() { return now; }
    constexpr void scopePublishConfiguration(AsyncWebSocket*) {}
    ${priority}
};
constexpr bool verifyPriority() {
    priorityState state;
    state.scopeViewers[0]={1,1000,2}; state.now=5999; state.scopeRefreshPriority(nullptr);
    if(!state.scopeQualityActive) return false;
    state.now=6000; state.scopeRefreshPriority(nullptr);
    if(state.scopeQualityActive) return false;
    state.scopeViewers[1]={2,5999,2}; state.scopeRefreshPriority(nullptr);
    if(!state.scopeQualityActive) return false;
    state.scopeViewers[1].streams=1; state.scopeRefreshPriority(nullptr);
    if(state.scopeQualityActive) return false;
    state.scopeViewers[0]={1,UINT32_MAX-2000,2}; state.now=1000; state.scopeRefreshPriority(nullptr);
    if(!state.scopeQualityActive) return false;
    state.scopeFftEnabled=false; state.scopeRefreshPriority(nullptr);
    return !state.scopeQualityActive;
}
static_assert(verifyPriority(), "Lease expiry, multiple viewers, stop and millis wrap must release priority correctly");
`;
const output = '.pio/build/quality-capture-test.cpp';
fs.mkdirSync(path.dirname(output), {recursive: true}); fs.writeFileSync(output, code);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: captura C++ real continua, pretrigger, wrap, sin señal, fallo de memoria y prioridad por lease.');
