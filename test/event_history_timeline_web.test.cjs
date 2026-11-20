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
        let downloads = 0;
        page.on('pageerror',error=>errors.push(error.message));
        await page.route('http://timeline.test/**',route=>{
            const url=new URL(route.request().url());
            if(url.pathname==='/grid-events')return route.fulfill({json:{eventCount:3,oldestId:43,newestId:45}});
            if(url.pathname==='/grid-events/list')return route.fulfill({json:{events:[43,44,45].map(id=>({i:id,t:start+(id-43)*1000}))}});
            if(url.pathname==='/grid-events/data'){++downloads;const id=Number(url.searchParams.get('eventId'));return route.fulfill({body:dipFixture(id,start+(id-43)*1000),contentType:'application/octet-stream'});}
            if(url.pathname==='/wave-events')return route.fulfill({json:{captures:[]}});
            const file=path.join(process.cwd(),'data',url.pathname==='/'?'index.html':url.pathname.slice(1));
            if(!fs.existsSync(file))return route.fulfill({status:404,body:''});
            return route.fulfill({body:fs.readFileSync(file),contentType:{'.html':'text/html','.js':'application/javascript','.css':'text/css'}[path.extname(file)]||'application/octet-stream'});
        });
        await page.goto('http://timeline.test/#eventHistory');
        await page.waitForFunction(()=>eventHistory.loaded&&!eventHistory.controller&&eventHistory.captures.length===3);
        assert.equal(await page.locator('#eventHistoryStats-tab').getAttribute('aria-selected'), 'true');
        assert.equal(await page.locator('#eventHistoryLane').inputValue(), 'all');
        assert.equal(await page.locator('#eventHistoryLane option').count(), 6);
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
            const controlsLayout=await page.evaluate(()=>{
                const dates=[...document.querySelector('.eventHistoryControls').children].map(node=>node.getBoundingClientRect());
                const actions=document.querySelector('.eventHistoryActions').getBoundingClientRect(),filters=document.querySelector('.eventHistoryFilters').getBoundingClientRect();
                return {dateRows:new Set(dates.map(rect=>Math.round(rect.top))).size,actions,filters};
            });
            assert.equal(controlsLayout.dateRows,1);
            if(width>=992){
                assert.ok(controlsLayout.filters.left>=controlsLayout.actions.right-1,'Filtros en la segunda fila, a la derecha en escritorio');
                assert.ok(Math.abs((controlsLayout.filters.top+controlsLayout.filters.bottom)-(controlsLayout.actions.top+controlsLayout.actions.bottom))<2);
            }else assert.ok(controlsLayout.filters.top>=controlsLayout.actions.bottom-1,'Filtros debajo de los controles en pantallas estrechas');
            await page.screenshot({path:'tmp/event-timeline-points-'+width+'.png'});
            await page.locator('.eventHistoryPoint').click();
            await page.locator('.eventHistoryPointMenu').waitFor({state:'visible'});
            assert.equal(await page.locator('.eventHistoryPointMenu button').count(),3);
            await page.locator('.eventHistoryPointMenu button').filter({hasText:'#45'}).click();
            await page.waitForFunction(()=>!eventHistoryPreview.loading&&eventHistoryPreview.event?.id===45);
            await page.locator('#eventHistoryPreview .btn-close').click();
            await page.locator('#eventHistoryPreview').waitFor({state:'hidden'});
        }
        const previousDownloads = downloads;
        await page.evaluate(() => {
            const original = eventHistory.captures[0];
            eventHistory.captures.push({...original, id: 46, records: original.records.map(record => ({...record,
                states: {...record.states, isCurrent: true, pointType: 3}}))});
        });
        await page.click('[data-history-group="points"]');
        assert.equal(await page.locator('#eventHistoryRows tr').count(), 4);
        assert.deepEqual(await page.locator('.eventHistoryLaneLabel').allTextContents(), ['Fase R']);
        assert.equal(await page.locator('#eventStatisticsDurationChart').isVisible(), false);
        assert.equal(await page.locator('#eventStatisticsSummaryHeader th').count(), 4);
        assert.match(await page.locator('#eventHistoryRows').textContent(), /corriente/i);
        await page.click('[data-history-type="dip"]');
        assert.equal(await page.locator('#eventHistoryRows tr').count(), 1);
        assert.match(await page.locator('#eventHistoryRows').textContent(), /corriente/i);
        assert.equal(await page.locator('[data-history-type="dip"]').getAttribute('aria-pressed'), 'false');
        assert.equal(await page.locator('#eventStatisticsSummaryHeader th').count(), 3);
        await page.selectOption('#eventHistoryLane', '1:V');
        assert.match(await page.locator('#eventHistoryRows').textContent(), /No hay eventos/);
        await page.selectOption('#eventHistoryLane', 'all');
        await page.click('[data-history-group="none"]');
        assert.equal(await page.locator('.eventHistoryLane').count(), 0);
        assert.equal(await page.locator('#eventStatisticsSummaryHeader th').count(), 1);
        assert.match(await page.locator('#eventHistoryRows').textContent(), /Seleccione los tipos/);
        await page.click('[data-history-type="pulse"]');
        assert.equal(await page.locator('#eventHistoryRows tr').count(), 1);
        assert.equal(await page.locator('#eventStatisticsSummaryHeader th').count(), 2);
        await page.click('[data-history-group="states"]');
        assert.equal(await page.locator('.eventHistoryPoint').count(), 0);
        assert.equal(await page.locator('#eventStatisticsSummaryHeader th').count(), 5);
        await page.click('[data-history-group="all"]');
        assert.match(await page.locator('#eventHistoryFromLabel').textContent(), /\d{2}\/\d{2} \d{2}:\d{2}/);
        assert.match(await page.locator('#eventHistoryToLabel').textContent(), /\d{2}\/\d{2} \d{2}:\d{2}/);
        await page.selectOption('#eventHistoryLane', '4:V');
        assert.equal(await page.evaluate(() => eventHistory.lane), '4:V');
        assert.equal(await page.locator('#eventHistoryFilterPoints').isVisible(), false);
        assert.deepEqual(await page.evaluate(() => [...eventStatisticsSubjects]), [0, 1, 2, 3]);
        await page.locator('[data-stat-subject="red"]').click();
        assert.deepEqual(await page.evaluate(() => eventStatisticsCharts[0].options.data.map(series => series.name)), ['Sin tension', 'Red anormal', 'Dips', 'Swells', 'Picos de corriente']);
        await page.selectOption('#eventHistoryLane', 'all');
        assert.equal(await page.evaluate(() => eventHistory.lane), 'all');
        assert.deepEqual(await page.evaluate(() => [...eventStatisticsSubjects]), [4]);
        await page.locator('[data-stat-subjects="all"]').click();
        assert.deepEqual(await page.evaluate(() => [...eventStatisticsSubjects]), [0, 1, 2, 3]);
        await page.locator('[data-stat-subject="1"]').click();
        assert.deepEqual(await page.evaluate(() => [...eventStatisticsSubjects]), [0, 2, 3]);
        assert.match(await page.locator('#eventStatisticsStatus').textContent(), /Fase R \+ Fase T \+ Neutro-tierra/);
        await page.locator('[data-stat-subjects="all"]').click();
        assert.equal(downloads, previousDownloads, 'los filtros no vuelven a descargar capturas');
        const layout = await page.evaluate(() => {
            const dates = [...document.querySelector('.eventHistoryControls').children].map(node => node.getBoundingClientRect());
            const actions = [...document.querySelector('.eventHistoryActions').children].map(node => node.getBoundingClientRect());
            return {dateRows: new Set(dates.map(rect => Math.round(rect.top))).size,
                actionCenters: actions.map(rect => rect.top + rect.height / 2),
                overflow: document.documentElement.scrollWidth > innerWidth};
        });
        assert.equal(layout.dateRows, 1); assert.equal(layout.overflow, false);
        assert.ok(Math.max(...layout.actionCenters) - Math.min(...layout.actionCenters) < 2);
        await page.screenshot({path: 'tmp/event-history-compact-mobile.png', fullPage: true});
        await page.evaluate(() => {
            const at = eventHistory.range.from + 1000;
            eventHistory.captures.push({id: 46, timestamp: at, startup: true, records: [
                {offsetMs: 0, previousDurationMs: 0, root: true, states: {subject: 4, previous: 0, current: 1}}]});
            selectEventHistoryTypes('all'); renderEventHistory();
        });
        assert.ok(await page.locator('.eventHistoryRestart').count() > 0);
        assert.equal(await page.locator('.eventHistoryRestart').first().evaluate(node => getComputedStyle(node).borderRadius), '50%');
        const centers = await page.locator('.eventHistoryRestart').first().evaluate(node => {
            const marker = node.getBoundingClientRect(), track = node.closest('.eventHistoryTrack').getBoundingClientRect();
            return {marker: marker.top + marker.height / 2, track: track.top + track.height / 2};
        });
        assert.ok(Math.abs(centers.marker - centers.track) < 1);
        await page.locator('.eventHistoryRestart').first().click();
        await page.locator('#eventHistoryPreview').waitFor({state: 'visible'});
        assert.match(await page.locator('#eventHistoryPreviewTitle').textContent(), /Reinicio/);
        assert.equal(await page.locator('#pageTitle').textContent(), 'Historial');
        assert.deepEqual(errors,[]);
        console.log('PASS triangulos compactos, agrupacion de cercanos, resize PC/movil y apertura de captura');
    }finally{await browser.close();}
}

run().catch(error=>{console.error(error);process.exitCode=1;});
