const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const source = fs.readFileSync('src/storage/history_read_session.cpp','utf8');
const start = source.indexOf('size_t HistoryReadSession::copyChunk('), body = source.indexOf('{',start);
let end = body+1, depth=1;
while(depth) {if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}
const method = source.slice(start,end).replace('size_t HistoryReadSession::','constexpr size_t ');
const header = fs.readFileSync('src/storage/history_read_session.h','utf8');
const timingTypes = header.slice(header.indexOf('struct HistoryReadTiming {'),header.indexOf('struct HistoryReadSessionMetrics {'));
const timingHelper = source.slice(source.indexOf('static void recordHistoryTiming('),source.indexOf('// Copia los tiempos globales protegidos;'))
    .replace('static void','constexpr void');
const harness = `
#include <cstdint>
#include <cstddef>
constexpr int pdPASS=1;
using TickType_t=uint32_t;
constexpr TickType_t pdMS_TO_TICKS(uint32_t value) {return value;}
constexpr size_t CONFIG_LWIP_TCP_MSS=1436;
constexpr size_t RESPONSE_TRY_AGAIN=size_t(-1);
constexpr size_t min(size_t a,size_t b) {return a<b?a:b;}
struct Metrics {uint32_t emptyBufferCalls=0,emptyBufferEpisodes=0,totalBufferWaitUs=0,lastBufferWaitUs=0,maxBufferWaitUs=0;};
${timingTypes}
int historyReadMetricsMux=0;
constexpr void portENTER_CRITICAL(int*) {}
constexpr void portEXIT_CRITICAL(int*) {}
${timingHelper}
struct Reader {
    static constexpr uint8_t bufferCount=3; static constexpr size_t pageSize=2048;
    uint8_t buffers[3][2048]{};uint8_t currentBuffer=bufferCount;
    size_t currentLength=0,currentOffset=0,totalBytes=6144;
    uint32_t sessionId=1,lastConsumerMs=0,bufferWaitStartedUs=0;
    uint32_t nowMs=10,nextDelayMs=0,eachDelayMs=0;
    bool active=true,producerFailed=false,waitingForBuffer=false;
    int readyQueue=1,freeQueue=2,historyReadMetricsMux=0;
    uint8_t next=0,queued=3,freed=0;
    Metrics historyReadMetrics,sessionMetrics;
    HistoryReadTimings historyReadTimings;
    constexpr uint32_t millis() {return nowMs;}
    constexpr uint32_t micros() {return nowMs*1000;}
    constexpr void portENTER_CRITICAL(int*) {}
    constexpr void portEXIT_CRITICAL(int*) {}
    constexpr void cancel(uint32_t id) {if(id==sessionId)active=false;}
    constexpr int xQueueReceive(int,uint8_t* value,int wait) {
        if(next>=queued)return 0;
        if(nextDelayMs>uint32_t(wait)){nowMs+=wait;nextDelayMs-=wait;return 0;}
        nowMs+=nextDelayMs;nextDelayMs=eachDelayMs;*value=next++;return pdPASS;
    }
    constexpr int xQueueSend(int,uint8_t*,int) {++freed;return pdPASS;}
    constexpr void memcpy(uint8_t* out,const uint8_t* in,size_t count) {for(size_t i=0;i<count;++i)out[i]=in[i];}
    ${method}
};
// Verifica copia entre paginas, parciales, falta de datos y final exacto; sin parametros.
constexpr bool check() {
    Reader r;for(size_t i=0;i<2048;++i){r.buffers[0][i]=1;r.buffers[1][i]=2;r.buffers[2][i]=3;}
    uint8_t out[5000]{};
    size_t offset=0;
    while(offset<6144) {
        const size_t count=r.copyChunk(out,5000,offset,1);
        if(!count || count>5744 || count==RESPONSE_TRY_AGAIN)return false;
        for(size_t i=0;i<count;++i)if(out[i]!=(offset+i)/2048+1)return false;
        offset+=count;
    }
    if(r.active || r.freed!=3)return false;
    Reader partial;partial.queued=1;
    if(partial.copyChunk(out,5000,0,1)!=2048 || partial.historyReadMetrics.emptyBufferCalls) return false;
    if(partial.copyChunk(out,5000,2048,1)!=RESPONSE_TRY_AGAIN || !partial.waitingForBuffer) return false;
    partial.queued=2;
    if(partial.copyChunk(out,100,2048,1)!=100 || partial.waitingForBuffer) return false;
    Reader failed;failed.queued=0;failed.producerFailed=true;
    if(failed.copyChunk(out,100,0,1) || failed.active) return false;
    Reader old;if(old.copyChunk(out,100,0,2) || old.next) return false;
    Reader delayed;delayed.nextDelayMs=8;
    if(delayed.copyChunk(out,5000,0,1)!=5000 || delayed.nowMs!=18)return false;
    if(delayed.historyReadTimings.queue.count!=1 || delayed.historyReadTimings.queue.totalUs!=8000 ||
        delayed.historyReadTimings.queue.over5ms!=1 || delayed.historyReadTimings.queue.over10ms)return false;
    Reader budget;budget.nextDelayMs=budget.eachDelayMs=8;
    if(budget.copyChunk(out,5000,0,1)!=4096 || budget.nowMs!=30)return false;
    Reader timeout;timeout.nextDelayMs=30;
    if(timeout.copyChunk(out,5000,0,1)!=RESPONSE_TRY_AGAIN || !timeout.active || timeout.nowMs!=30)return false;
    if(timeout.historyReadTimings.queue.maxUs!=20000)return false;
    if(timeout.copyChunk(out,5000,0,1)!=5000 || timeout.nowMs!=40)return false;
    Reader cap;uint8_t big[6000]{};
    if(cap.copyChunk(big,6000,0,1)!=5744)return false;
    return true;
}
static_assert(check(),"Streaming continuo sin perder bytes, con espera de cola acotada");
`;
assert.doesNotMatch(method,/vTaskDelay|new |malloc/);
assert.match(method,/pdMS_TO_TICKS\(waitedMs < 20 \? 20 - waitedMs : 0\)/);
const output=path.join('.pio','build','history-stream-test.cpp');
fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output,harness);
execFileSync(path.join(process.env.USERPROFILE,'.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe'),['-std=c++14','-fsyntax-only',output],{stdio:'inherit'});
console.log('PASS: callback C++ real cruza paginas, conserva parciales y limita la espera de cola a 20 ms.');
