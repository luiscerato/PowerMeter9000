const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {codec,gridFixture} = require('./grid_fixture.cjs');
const runtime = path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));
const start = Math.floor(Date.now()/1000)*1000-60000;

// Construye un DIP binario con CRC; id/at fijan captura y fecha, conservando muestras validas del fixture.
function dipFixture(id,at) {
    const bytes = gridFixture(), view = new DataView(bytes.buffer);
    view.setUint32(1,id,true);view.setUint32(5,at/1000,true);view.setUint16(9,0,true);
    view.setUint16(11,512|1<<10,true);view.setUint8(13,32);view.setUint32(14,7,true);
    view.setUint16(28,codec.gridCrc16(bytes,0,28),true);
    for(const page of [1,2]){const offset=page*2048;view.setUint32(offset,id,true);view.setUint8(offset+7,0);view.setUint16(offset+8,codec.gridCrc16(bytes,offset,170,offset+8),true);}
    return Buffer.from(bytes);
}

// Comprueba flechas cercanas, resize y apertura desde ID; no recibe parametros ni consulta el medidor.
async function run() {
    const browser = await chromium.launch({headless:true,channel:'msedge'});
    try {
        const page = await browser.newPage({viewport:{width:1280,height:900}}), errors=[];
        page.on('pageerror',error=>errors.push(error.message));
        await page.route('http://timeline.test/**',route=>{
            const url=new URL(route.request().url());
            if(url.pathname==='/grid-events')return route.fulfill({json:{eventCount:3,oldestId:43,newestId:45}});
            if(url.pathname==='/grid-events/list')return route.fulfill({json:{events:[43,44,45].map(id=>({i:id,t:start+(id-43)*1000}))}});
            if(url.pathname==='/grid-events/data'){const id=Number(url.searchParams.get('eventId'));return route.fulfill({body:dipFixture(id,start+(id-43)*1000),contentType:'application/octet-stream'});}
            if(url.pathname==='/wave-events')return route.fulfill({json:{captures:[]}});
            const file=path.join(process.cwd(),'data',url.pathname==='/'?'index.html':url.pathname.slice(1));
            if(!fs.existsSync(file))return route.fulfill({status:404,body:''});
            return route.fulfill({body:fs.readFileSync(file),contentType:{'.html':'text/html','.js':'application/javascript','.css':'text/css'}[path.extname(file)]||'application/octet-stream'});
        });
        await page.goto('http://timeline.test/#eventHistory');
        await page.waitForFunction(()=>eventHistory.loaded&&!eventHistory.controller&&eventHistory.captures.length===3);
        for(const width of [1280,390]){
            await page.setViewportSize({width,height:900});
            await page.waitForTimeout(150);
            assert.deepEqual(await page.locator('.eventHistoryPoint').allTextContents(),['']);
            assert.equal(await page.locator('.eventHistoryPoint').getAttribute('data-event-ids'),'43 44 45');
            assert.equal(await page.locator('.eventHistoryTrack button').count(),0);
            const bounds=await page.evaluate(()=>[...document.querySelectorAll('.eventHistoryPoint')].map(button=>{
                const rect=button.getBoundingClientRect(),track=button.closest('.eventHistoryLanePlot').querySelector('.eventHistoryTrack').getBoundingClientRect();
                return {left:rect.left,right:rect.right,top:rect.top,bottom:rect.bottom,trackLeft:track.left,trackRight:track.right,trackTop:track.top};
            }));
            for(const rect of bounds){assert.ok(rect.left>=rect.trackLeft-1);assert.ok(rect.right<=rect.trackRight+1);assert.ok(rect.bottom<=rect.trackTop);assert.ok(rect.bottom-rect.top<=14);}
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
            await page.screenshot({path:'tmp/event-timeline-points-'+width+'.png'});
            await page.locator('.eventHistoryPoint').click();
            await page.locator('.eventHistoryPointMenu').waitFor({state:'visible'});
            assert.equal(await page.locator('.eventHistoryPointMenu button').count(),3);
            await page.locator('.eventHistoryPointMenu button').filter({hasText:'#45'}).click();
            await page.waitForFunction(()=>!eventHistoryPreview.loading&&eventHistoryPreview.event?.id===45);
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state:'hidden'});
        }
        assert.deepEqual(errors,[]);
        console.log('PASS triangulos compactos, agrupacion de cercanos, resize PC/movil y apertura de captura');
    }finally{await browser.close();}
}

run().catch(error=>{console.error(error);process.exitCode=1;});
