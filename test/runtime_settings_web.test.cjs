const assert=require('node:assert/strict');
const path=require('node:path');
const fs=require('node:fs');
const runtime=path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));

// Recorre calibracion y perfil diferido con la interfaz real y una placa simulada; sin parametros.
async function run() {
    const browser=await chromium.launch({headless:true,channel:'msedge'});
    try {
        for(const width of [1280,390]) {
            const page=await browser.newPage({viewport:{width,height:900}}),errors=[],posts=[];
            let calibrating=false,calibrationReads=0,historyPending=0,historyFail=false;
            const history={enabled:true,samplePeriod:1,voltageMode:'basic',activePower:'full',reactivePower:'full',apparentPower:'off',powerFactor:true,
                voltageThd:false,currentThd:false,frequency:'full',voltageAngles:false,battery:false,energy:true,energyExtraMode:1,apply:'ok'};
            page.on('pageerror',error=>errors.push(error.message));
            await page.route('http://runtime.test/**',async route=>{
                const request=route.request(),url=new URL(request.url());
                if(url.pathname==='/options')return route.fulfill({json:Object.fromEntries([...url.searchParams.keys()].filter(key=>key!=='ns').map(key=>[key,0]))});
                if(url.pathname==='/calibration/neutral') {
                    if(request.method()==='POST') {
                        assert.match(request.headers()['content-type'],/application\/x-www-form-urlencoded/);
                        posts.push(Object.fromEntries(new URLSearchParams(request.postData())));calibrating=true;calibrationReads=0;
                        return route.fulfill({status:202,json:{status:'queued'}});
                    }
                    const done=calibrating&&++calibrationReads>=2;
                    return route.fulfill({json:{status:done?'done':calibrating?'sampling':'idle',error:'',phase:2,
                        samples:done?100:0,rmsSamples:done?3:0,reference:done?220:0,rmsFactor:done?1.125:1,fastFactor:done?1.12345:1}});
                }
                if(url.pathname==='/history/config') {
                    if(request.method()==='POST') {
                        posts.push(Object.fromEntries(new URLSearchParams(request.postData())));historyPending=2;
                        return route.fulfill({status:202,json:{...history,apply:'wait'}});
                    }
                    if(historyPending>0) {
                        --historyPending;
                        if(!historyPending&&!historyFail)history.samplePeriod=2;
                        return route.fulfill({json:{...history,apply:historyPending?'wait':historyFail?'fail':'ok'}});
                    }
                    return route.fulfill({json:history});
                }
                return route.fulfill({contentType:'text/html',body:'<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><div id="settings"></div>'});
            });
            await page.goto('http://runtime.test/');
            for(const file of ['lib/bootstrap.min.css','style.css'])await page.addStyleTag({path:'data/'+file});
            for(const file of ['lib/jquery-3.6.4.min.js','app_appearance.js','history_cache.js','index.js','network_settings.js'])await page.addScriptTag({path:'data/'+file});
            await page.evaluate(async()=>{
                window.board={IP:'http://runtime.test',timeout:1000};updateHistoryEstimate=()=>{};initSettingsUI();
                $('.tab-pane').removeClass('active show');$('#settings_calibration').addClass('active show');await loadSettings('calibration',true);
            });
            assert.equal(await page.textContent('#neutralCalibrationRms'),'1.0000000');
            await page.selectOption('#neutralCalibrationPhase','t');await page.click('#neutralCalibrationStart');
            await page.waitForFunction(()=>document.querySelector('#neutralCalibrationStatus').textContent.includes('aplicada sin reiniciar'));
            assert.deepEqual(posts[0],{phase:'t'});assert.equal(await page.textContent('#neutralCalibrationSamples'),'100 / 3');
            assert.equal(await page.textContent('#neutralCalibrationFast'),'1.1234500');assert.equal(await page.isEnabled('#neutralCalibrationStart'),true);
            await page.screenshot({path:'tmp/neutral-calibration-'+width+'.png',fullPage:true});
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
            await page.evaluate(async()=>{$('.tab-pane').removeClass('active show');$('#settings_history').addClass('active show');await loadSettings('history',true);});
            await page.fill('#history_period','2');await page.click('#historySettingsForm button[type=submit]');
            await page.waitForFunction(()=>document.querySelector('#historySettingsStatus').textContent.includes('aplicado sin reiniciar'));
            assert.equal(posts[1].samplePeriod,'2');assert.equal(await page.inputValue('#history_period'),'2');
            historyFail=true;await page.fill('#history_period','3');await page.click('#historySettingsForm button[type=submit]');
            await page.waitForFunction(()=>document.querySelector('#historySettingsStatus').textContent.includes('Sigue activo el anterior'));
            assert.equal(await page.isEnabled('#history_period'),true);assert.equal(history.samplePeriod,2);
            assert.deepEqual(errors,[]);await page.close();
            console.log('PASS '+width+': calibracion BL0942, referencia URL-encoded, factores activos, historico diferido y fallo NVS.');
        }
    } finally {await browser.close();}
}
fs.mkdirSync('tmp',{recursive:true});
run().catch(error=>{console.error(error);process.exitCode=1;});
