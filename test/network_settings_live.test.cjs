const assert=require('node:assert/strict');
const path=require('node:path');
const runtime=path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));

// Verifica lectura y escaneo real sin guardar ni reiniciar; baseUrl identifica el medidor.
async function run(baseUrl) {
    const browser=await chromium.launch({headless:true,channel:'msedge'});
    try {
        const context=await browser.newContext();
        for(const width of [1280,390]) {
            const page=await context.newPage(),errors=[],writes=[];
            page.on('pageerror',error=>errors.push(error.message));
            page.on('request',request=>{if(request.method()==='POST')writes.push(request.url());});
            await page.setViewportSize({width,height:900});
            await page.goto(baseUrl+'/#settings',{waitUntil:'domcontentloaded',timeout:30000});
            await page.locator('#appRoot').waitFor({state:'visible',timeout:60000});
            await page.click('#settingsTab_network');
            await page.waitForFunction(()=>networkSettings.config!==null,{}, {timeout:30000});
            assert.equal(await page.isEnabled('#net_sta-ssid'),true);
            assert.equal(await page.inputValue('#net_sta-pass'),'');assert.equal(await page.inputValue('#net_ap-pass'),'');
            assert.equal(await page.inputValue('#net_sta-ip'),'192.168.1.90');
            assert.equal(await page.isChecked('#net_staticIp'),true);
            if(width===1280) {
                await page.click('#networkScan');
                await page.waitForFunction(()=>!document.getElementById('networkScan').disabled,{}, {timeout:30000});
                assert.ok(await page.locator('#networkScanList option').count()>1);
            }
            await page.screenshot({path:'tmp/network-settings-live-'+width+'.png',fullPage:true});
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
            assert.deepEqual(errors,[]);assert.deepEqual(writes,[]);
            console.log('PASS red/hora real '+width+': IP estatica conservada, claves ocultas, sin POST ni errores.');
            await page.close();
        }
    } finally {await browser.close();}
}

run(process.argv[2]||'http://192.168.1.90').catch(error=>{console.error(error);process.exitCode=1;});
