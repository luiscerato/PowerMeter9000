const assert=require('node:assert/strict');
const path=require('node:path');
const fs=require('node:fs');
const runtime=path.join(process.env.USERPROFILE,'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[process.cwd(),runtime]}));

// Prueba la pestaña con API simulada; no cambia la red real ni reinicia el medidor.
async function run() {
    const browser=await chromium.launch({headless:true,channel:'msedge'});
    try {
        for(const width of [1280,390]) {
            const page=await browser.newPage({viewport:{width,height:900}}),errors=[],posts=[];
            let scans=0,fail=false,statusPolls=0,readBack=false,applyCount=0;
            const config={'wifi-mode':1,'sta-ssid':'Taller','staticIp':true,'sta-ip':'192.168.1.90','sta-mask':'255.255.254.0','sta-gw':'192.168.0.1','sta-dns1':'8.8.8.8','sta-dns2':'4.4.4.4',
                'ap-ssid':'Medidor','ap-ip':'192.168.4.1','ap-mask':'255.255.255.0','ap-gw':'192.168.4.1','ap-hide':false,'ap-custom-ip':false,
                'time-server':'ar.pool.ntp.org','time-sync':true,'time-minwest':-180,'time-dsttime':0,staHasPass:true,apHasPass:true,connected:true,address:'192.168.1.90',recovery:false};
            page.on('pageerror',e=>errors.push(e.message));
            await page.route('http://network.test/**',async route=>{
                const request=route.request(),url=new URL(request.url());
                if(url.pathname==='/network/config') {
                    if(request.method()==='POST') {
                        assert.equal(url.searchParams.get('defer'),'1');readBack=false;
                        const values=Object.fromEntries(new URLSearchParams(request.postData()));posts.push(values);
                        assert.match(request.headers()['content-type'],/application\/x-www-form-urlencoded/);
                        Object.assign(config,values);
                        for(const key of ['staticIp','ap-hide','ap-custom-ip','time-sync'])config[key]=values[key]==='1';
                        for(const prefix of ['sta','ap'])if(Object.hasOwn(values,prefix+'-pass'))config[prefix+'HasPass']=!!values[prefix+'-pass'];
                        statusPolls=0;
                        return route.fulfill({json:{ok:true,verified:true,restartRequired:false,pending:3,apply:'saved'}});
                    }
                    readBack=true;
                    return route.fulfill(fail?{status:503,body:'offline'}:{json:config});
                }
                if(url.pathname==='/network/apply') {
                    assert.equal(readBack,true);assert.equal(new URLSearchParams(request.postData()).get('services'),'3');
                    ++applyCount;return route.fulfill({status:202,json:{ok:true,apply:'wait'}});
                }
                if(url.pathname==='/network/status')return route.fulfill({json:{apply:++statusPolls===1?'wait':'ok'}});
                if(url.pathname==='/network/scan')return route.fulfill({json:++scans===1?{scan:'running',list:[]}:
                    {scan:'completed',list:[{ssid:'Red "abierta" <x>',rssi:-45,auth:'OPEN'},{ssid:'Taller',rssi:-60,auth:'WPA2_PSK'}]}});
                return route.fulfill({contentType:'text/html',body:'<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1"><div id="settings"><section id="settings_network"></section></div>'});
            });
            await page.goto('http://network.test/');
            for(const file of ['lib/bootstrap.min.css','style.css'])await page.addStyleTag({path:'data/'+file});
            for(const file of ['lib/jquery-3.6.4.min.js','index.js','network_settings.js'])await page.addScriptTag({path:'data/'+file});
            await page.evaluate(async()=>{window.board={IP:'http://network.test',timeout:1000};$('#settings_network').html(createNetworkSettingsHtml());initNetworkSettings();await loadNetworkSettings();});
            assert.equal(await page.inputValue('#net_sta-pass'),'');
            assert.equal(await page.isEnabled('#net_sta-ip'),true);
            await page.uncheck('#net_staticIp');assert.equal(await page.isDisabled('#net_sta-ip'),true);
            await page.click('#networkSettingsForm button[type=submit]');
            await page.waitForFunction(()=>document.getElementById('networkSettingsStatus').textContent.includes('aplicadas sin reiniciar'));
            assert.equal(posts.length,1);assert.equal(posts[0].staticIp,'0');
            assert.equal(Object.hasOwn(posts[0],'sta-pass'),false);assert.equal(Object.hasOwn(posts[0],'ap-pass'),false);
            assert.ok(statusPolls>=2);assert.equal(applyCount,1);assert.equal(await page.isHidden('#networkRestart'),true);
            await page.click('#networkScan');
            await page.waitForFunction(()=>document.querySelectorAll('#networkScanList option').length===3);
            await page.selectOption('#networkScanList','Red "abierta" <x>');
            assert.equal(await page.isChecked('#net_staOpen'),true);assert.equal(await page.isDisabled('#net_sta-pass'),true);
            assert.equal(await page.locator('x').count(),0);
            await page.click('#networkSettingsForm button[type=submit]');
            await page.waitForFunction(()=>document.getElementById('networkSettingsStatus').textContent.includes('aplicadas sin reiniciar'));
            assert.equal(posts[1]['sta-pass'],'');assert.equal(posts[1]['sta-ssid'],'Red "abierta" <x>');
            await page.uncheck('#net_staOpen');await page.fill('#net_sta-pass','corta');
            await page.click('#networkSettingsForm button[type=submit]');assert.equal(posts.length,2);
            assert.match(await page.textContent('#networkSettingsStatus'),/contraseña/);
            await page.check('#net_staOpen');
            await page.screenshot({path:'tmp/network-settings-'+width+'.png',fullPage:true});
            assert.equal(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),true);
            fail=true;await page.click('#networkReload');
            await page.waitForFunction(()=>document.getElementById('networkSettingsFields').disabled);
            fail=false;await page.click('#networkReload');
            await page.waitForFunction(()=>!document.getElementById('networkSettingsFields').disabled);
            assert.deepEqual(errors,[]);
            console.log('PASS red/hora '+width+': campos, claves omitidas, red abierta, escaneo, URL-encoded y errores.');
            await page.close();
        }
    } finally {await browser.close();}
}

fs.mkdirSync('tmp',{recursive:true});
run().catch(error=>{console.error(error);process.exitCode=1;});
