const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {chromium} = require(require.resolve('playwright',{paths:[process.cwd(),path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules')]}));
const html = fs.readFileSync('data/index.html','utf8');
const historyHtml = html.slice(html.indexOf('<div class="tab-pane" id="history"'),html.indexOf('<div class="tab-pane" id="settings"'));

// Prueba el visor real con datos locales; sin parametros, no consulta ni modifica la placa.
async function run() {
    const browser = await chromium.launch({headless:true,channel:'chrome'});
    try {
        const page = await browser.newPage({viewport:{width:1280,height:900}}), errors=[];
        page.on('pageerror',error => errors.push(error.message));
        await page.route('http://history-view.test/**',route => route.fulfill({contentType:'text/html',body:'<meta name="viewport" content="width=device-width,initial-scale=1"><main>'+historyHtml+'<div id="appearance"></div></main>'}));
        await page.goto('http://history-view.test/');
        for (const file of ['lib/bootstrap.min.css','style.css']) await page.addStyleTag({path:'data/'+file});
        for (const file of ['lib/jquery-3.6.4.min.js','lib/canvasjs.min.js','chart_tooltips.js','app_appearance.js','history_ui.js']) await page.addScriptTag({path:'data/'+file});
        await page.evaluate(() => {
            document.getElementById('appearance').innerHTML = createAppearanceSettingsHtml();
            initHistoryViewer(); initAppAppearance();
            const now = Date.now();
            historyViewer.records = Array.from({length:120},(_,i) => ({timestamp:new Date(now+i*10000),voltageR:230+Math.sin(i),voltageS:229,voltageT:231,voltageN:0,
                currentR:.4,currentS:.5,currentT:.6,currentN:.02,activePowerR:100000,activePowerS:90000,activePowerT:50000,
                activeEnergyR:1e6+i,activeEnergyS:2e6+i,activeEnergyT:3e6+i,batteryVoltage:3.9,batteryInputVoltage:5,batteryCurrent:.2,batteryLevel:80,status:i%128}));
            renderHistoryCharts();
            for (const key of ['current','activePower','energy','connectivity','batteryVoltage','batteryInputVoltage','batteryCurrent','batteryLevel']) setHistoryChartVisibility(key,true);
        });
        assert.equal(await page.locator('.historyChartToggle').count(),17);
        assert.equal(await page.evaluate(()=>historyTotal({activePowerR:2,activePowerS:-1,activePowerT:4},'activePower')),5);
        assert.equal(await page.evaluate(()=>historyTotal({activePowerR:2,activePowerS:3},'activePower')),null);
        assert.equal(await page.evaluate(()=>historyTotal({activePowerTotal:8,activePowerR:2,activePowerS:3,activePowerT:4},'activePower')),8);
        assert.equal(await page.evaluate(()=>historyViewer.charts.activePower.options.data.find(s=>s.name==='Total').dataPoints[0].y),240000);
        assert.equal(await page.evaluate(()=>historyViewer.charts.energy.options.data.find(s=>s.name==='Activa total').dataPoints[0].y),6000000);
        await page.evaluate(()=>setSharedHistoryUnits(true));
        assert.equal(await page.evaluate(()=>historyViewer.charts.activePower.options.data.find(s=>s.name==='Total').dataPoints[0].y),240);
        assert.equal(await page.evaluate(()=>historyViewer.charts.energy.options.data.find(s=>s.name==='Activa total').signalUnit),'kWh');
        await page.evaluate(()=>setSharedHistoryUnits(false));
        await page.evaluate(()=>{window.writeOptions=async (namespace,values)=>{window.savedUnits={namespace,...values};};});
        await page.selectOption('#appHistoryUnits','1');
        await page.locator('#appHistoryUnitsForm button[type=submit]').click();
        await page.waitForFunction(()=>document.getElementById('appHistoryUnitsStatus').textContent==='Guardado en el medidor.');
        assert.deepEqual(await page.evaluate(()=>window.savedUnits),{namespace:'web',kilo:1});
        await page.evaluate(()=>setSharedHistoryUnits(false));
        const widths = await page.evaluate(() => Object.entries(historyViewer.charts).filter(([,chart])=>chart).map(([key,chart]) => ({key,left:chart.axisX[0].convertValueToPixel(chart.axisX[0].get('minimum')),right:chart.axisX[0].convertValueToPixel(chart.axisX[0].get('maximum'))})));
        console.log('Desktop plot bounds '+JSON.stringify(widths));
        assert.ok(Math.max(...widths.map(x=>x.left))-Math.min(...widths.map(x=>x.left))<2,'Mismo inicio de area de trazado');
        assert.ok(Math.max(...widths.map(x=>x.right))-Math.min(...widths.map(x=>x.right))<2,'Mismo fin de area de trazado');
        await page.evaluate(() => {const from=Number(historyViewer.records[20].timestamp),to=Number(historyViewer.records[60].timestamp); historyRangeChanged('voltage',{axisX:[{viewportMinimum:from,viewportMaximum:to}]});});
        assert.equal(await page.evaluate(() => new Set(Object.values(historyViewer.charts).filter(Boolean).map(chart=>chart.options.axisX.viewportMinimum)).size),1);
        await page.locator('#historyLinkViews').uncheck();
        await page.evaluate(() => setHistoryViewport('current',null));
        assert.ok(await page.evaluate(() => historyViewer.charts.current.options.axisX.viewportMinimum == null));
        assert.ok(await page.evaluate(() => Number.isFinite(historyViewer.charts.voltage.options.axisX.viewportMinimum)));
        assert.ok(await page.evaluate(() => Object.entries(historyViewer.charts).filter(([key,chart]) => chart && historyViewer.visible[key]).every(([,chart]) => {
            const axis = chart.axisX[0]; return Math.abs(axis.convertValueToPixel(axis.get('viewportMinimum') ?? axis.get('minimum'))-82)<2;
        })),'Trazado alineado tambien con zoom independiente');
        await page.locator('#historyPanelVoltage [data-action=scale]').click();
        await page.locator('#historyPanelVoltage [name=auto]').uncheck();
        await page.locator('#historyPanelVoltage [name=min]').fill('220');
        await page.locator('#historyPanelVoltage [name=max]').fill('240');
        await page.locator('#historyPanelVoltage [type=submit]').click();
        assert.equal(await page.evaluate(() => historyViewer.charts.voltage.options.axisY.minimum),220);
        assert.equal(await page.evaluate(() => {
            const form=$('#historyPanelVoltage form'); form.find('[name=min]').val(250); form.find('[name=max]').val(200);
            return applyHistoryScale('voltage',form);
        }),false);
        assert.equal(await page.evaluate(() => historyViewer.charts.voltage.options.axisY.minimum),220);
        await page.evaluate(() => {
            const form=$('#historyPanelVoltage form'); form.find('[name=auto]').prop('checked',true); applyHistoryScale('voltage',form);
            const series=historyViewer.charts.voltage.options.data.find(series=>series.name==='N');
            historyViewer.charts.voltage.options.legend.itemclick({chart:historyViewer.charts.voltage,dataSeries:series});
        });
        assert.ok(await page.evaluate(() => historyViewer.charts.voltage.axisY[0].get('minimum')>200),'Autoescala omite neutro oculto');
        await page.locator('#historyPanelVoltage [data-action=down]').click();
        assert.equal(await page.locator('#historyChartList > section').first().getAttribute('data-chart'),'current');
        const savedView = await page.evaluate(() => {
            const stored = JSON.parse(localStorage.getItem('powerMeterHistoryView'));
            historyViewer.order = Object.keys(historyChartNames);
            historyViewer.visible = Object.fromEntries(Object.keys(historyChartNames).map(key => [key,false]));
            historyViewer.linked = true;
            loadHistoryViewOptions();
            return {order:historyViewer.order,visible:historyViewer.visible,linked:historyViewer.linked,stored};
        });
        assert.equal(savedView.order[0],'current');
        assert.equal(savedView.visible.voltage,true);
        assert.equal(savedView.visible.current,true);
        assert.equal(savedView.visible.lineVoltage,false);
        assert.equal(savedView.linked,false);
        assert.ok(savedView.stored.order.includes('current') && typeof savedView.stored.visible.voltage==='boolean');
        await page.locator('#appColorR').fill('#123456');
        await page.locator('#appColorOpacity').fill('80');
        await page.locator('button[form=appAppearanceForm]').click();
        assert.equal(await page.evaluate(() => historyViewer.charts.voltage.options.data[0].color),'rgba(18,52,86,0.8)');
        await page.evaluate(() => {loadHistoryViewer = async () => {}; applyHistoryPreset('thisWeek');});
        assert.equal(await page.evaluate(() => new Date($('#historyFrom').val()).getDay()),0);
        assert.equal(await page.evaluate(() => new Date($('#historyTo').val()).getDay()),6);
        assert.equal(await page.evaluate(() => new Date($('#historyTo').val()).getSeconds()),59);
        await page.evaluate(() => applyHistoryPreset('lastWeek'));
        assert.equal(await page.evaluate(() => new Date($('#historyFrom').val()).getDay()),0);
        await page.evaluate(() => applyHistoryPreset('thisMonth'));
        assert.equal(await page.evaluate(() => new Date($('#historyFrom').val()).getDate()),1);
        await page.evaluate(() => {moveHistoryRange(-1); const to=new Date($('#historyTo').val()); if (to.getDate()!==new Date(to.getFullYear(),to.getMonth()+1,0).getDate()) throw Error('Fin de mes incorrecto');});
        await page.evaluate(() => {historyViewer.linked=true; setHistoryViewport(null,null); for(const key of historyViewer.order) setHistoryChartVisibility(key,['voltage','current','batteryVoltage'].includes(key));});
        fs.mkdirSync('tmp',{recursive:true});
        await page.screenshot({path:'tmp/history-view-desktop.png',fullPage:true});
        await page.setViewportSize({width:390,height:850});
        await page.evaluate(() => {for(const chart of Object.values(historyViewer.charts).filter(Boolean))chart.render();});
        assert.ok(await page.evaluate(() => document.documentElement.scrollWidth<=innerWidth),'Sin desborde horizontal mobile');
        const mobileBounds = await page.evaluate(() => ['voltage','current','batteryVoltage'].map(key=>{const axis=historyViewer.charts[key].axisX[0];return axis.convertValueToPixel(axis.get('minimum'));}));
        assert.ok(Math.max(...mobileBounds)-Math.min(...mobileBounds)<2,'Ejes alineados en mobile');
        await page.screenshot({path:'tmp/history-view-mobile.png',fullPage:true});
        assert.deepEqual(errors,[]);
        console.log('PASS: orden, escalas, zoom enlazado/independiente, bateria, calendario, colores y alineacion desktop/mobile.');
    } finally {await browser.close();}
}
run().catch(error=>{console.error(error);process.exitCode=1;});
