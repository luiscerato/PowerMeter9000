let networkSettings = {config:null, scanRun:0, restart:false};
const networkTextKeys = ['sta-ssid','sta-ip','sta-mask','sta-gw','sta-dns1','sta-dns2','ap-ssid','ap-ip','ap-mask','ap-gw','time-server'];
const networkBoolKeys = ['staticIp','ap-hide','ap-custom-ip','time-sync'];

// Crea un campo compacto; key identifica la preferencia y label/type describen el control.
function networkSettingsField(key,label,type='text') {
    return '<div class="settingsField"><label for="net_'+key+'">'+label+'</label><input class="form-control form-control-sm" id="net_'+key+'" type="'+type+'"'+
        (type==='password'?' autocomplete="new-password" maxlength="64"':'')+'></div>';
}

// Crea la pestaña de red, AP y NTP; sin parametros.
function createNetworkSettingsHtml() {
    const actions=createSettingsIconButton('networkReload','Recargar red y hora',false)+createSettingsIconButton('','Guardar red y hora',true,'submit');
    let html='<form id="networkSettingsForm" class="settingsSection">'+createSettingsHeaderHtml('Red y hora','Se aplican sin reiniciar el medidor. Cambiar Wi-Fi o IP puede interrumpir la conexión con esta página.',actions)+
        '<div id="networkSettingsStatus" class="settingsStatusLine small text-muted" role="status"></div><fieldset id="networkSettingsFields" disabled><div class="settingsGroupGrid settingsGroupGridTwo">'+
        '<div class="settingsGroup"><h3>Wi-Fi</h3><div class="settingsField"><label for="net_wifi-mode">Modo</label><select id="net_wifi-mode" class="form-select form-select-sm"><option value="1">Estación</option><option value="3">Estación + AP</option><option value="2">Solo AP</option><option value="0">Apagado</option></select></div>'+
        networkSettingsField('sta-ssid','Nombre de red (SSID)')+
        '<div class="d-flex gap-2 mb-2"><button id="networkScan" type="button" class="btn btn-sm btn-outline-secondary text-nowrap flex-shrink-0">Buscar redes</button><select id="networkScanList" class="form-select form-select-sm" style="min-width:0" aria-label="Redes Wi-Fi disponibles"><option value="">Seleccione una red o escriba el SSID</option></select></div>'+
        networkSettingsField('sta-pass','Contraseña','password')+
        '<label class="form-check small mb-2"><input id="net_staOpen" type="checkbox" class="form-check-input">Red sin contraseña</label><p class="small text-muted mb-0">Deje la contraseña vacía para conservar la guardada. Para una red abierta, marque la opción anterior.</p></div>'+
        '<div class="settingsGroup"><h3>Dirección de la estación</h3><label class="form-check mb-2"><input id="net_staticIp" type="checkbox" class="form-check-input">Usar IP estática (sin marcar: DHCP)</label><div id="networkStaticFields">';
    for(const [key,label] of [['sta-ip','IP'],['sta-mask','Máscara'],['sta-gw','Puerta de enlace'],['sta-dns1','DNS principal'],['sta-dns2','DNS alternativo']])html+=networkSettingsField(key,label);
    html+='</div></div><div class="settingsGroup"><h3>Punto de acceso y recuperación</h3>'+networkSettingsField('ap-ssid','Nombre del AP')+networkSettingsField('ap-pass','Contraseña del AP','password')+
        '<label class="form-check small mb-2"><input id="net_apOpen" type="checkbox" class="form-check-input">AP sin contraseña</label><p id="networkApWarning" class="small text-danger">Un AP abierto permite acceder a la configuración a cualquier persona cercana.</p>'+
        '<label class="form-check small mb-2"><input id="net_ap-hide" type="checkbox" class="form-check-input">Ocultar AP manual</label><label class="form-check small mb-2"><input id="net_ap-custom-ip" type="checkbox" class="form-check-input">IP personalizada del AP</label><div id="networkApIpFields">';
    for(const [key,label] of [['ap-ip','IP del AP'],['ap-mask','Máscara'],['ap-gw','Puerta de enlace']])html+=networkSettingsField(key,label);
    html+='</div><p class="small text-muted mb-0">En modo estación, si no conecta durante 30 segundos se habilita este AP, visible aunque se haya marcado oculto. Sin SSID configurado se habilita de inmediato. La IP predeterminada es 192.168.4.1. Sigue intentando conectarse y cierra el AP tras 30 segundos conectado, cuando ya no tiene clientes.</p></div>'+
        '<div class="settingsGroup"><h3>Hora y NTP</h3><label class="form-check mb-2"><input id="net_time-sync" type="checkbox" class="form-check-input">Sincronizar por internet (NTP)</label>'+networkSettingsField('time-server','Servidor NTP')+
        networkSettingsField('time-minwest','Desfase UTC (minutos)','number')+networkSettingsField('time-dsttime','Horario de verano (minutos)','number')+
        '<p class="small text-muted mb-0">Argentina: −180 minutos, sin horario de verano (0). Al arrancar se toma la hora del RTC; NTP corrige el reloj y sincroniza el RTC cuando hay conexión.</p></div></div></fieldset>'+
        '<button id="networkRestart" type="button" class="btn btn-sm btn-outline-warning mt-2" hidden>Reiniciar para aplicar</button></form>';
    return html;
}

// Enlaza los controles de la pestaña; sin parametros.
function initNetworkSettings() {
    $('#networkSettingsForm').on('submit',saveNetworkSettings);
    $('#networkReload').on('click',()=>loadSettings('network',true));
    $('#networkScan').on('click',scanNetworkSettings);
    $('#networkSettingsFields').on('change','input,select',updateNetworkSettingsControls);
    $('#net_sta-ssid,#net_ap-ssid').attr('maxlength',32);
    $('#net_time-server').attr('maxlength',47);
    $('#net_time-minwest').attr({min:-840,max:840,step:1});
    $('#net_time-dsttime').attr({min:-120,max:120,step:1});
    $('#networkScanList').on('change',function(){
        const option=this.selectedOptions[0];
        if(!option?.value)return;
        $('#net_sta-ssid').val(option.value);
        $('#net_staOpen').prop('checked',option.dataset.open==='true');
        $('#net_sta-pass').val(''); updateNetworkSettingsControls();
    });
    $('#networkRestart').on('click',async()=>{
        if(!confirm('Se reiniciará el medidor y puede cambiar su dirección IP. ¿Continuar?'))return;
        try {await $.ajax({type:'POST',url:board.IP+'/restart',timeout:5000});$('#networkSettingsStatus').text('Reiniciando. Conecte a la nueva red o dirección si cambió.');}
        catch(error){$('#networkSettingsStatus').text('No se confirmó el reinicio. Compruebe la pantalla del medidor.');}
    });
}

// Ajusta controles dependientes sin borrar valores; sin parametros.
function updateNetworkSettingsControls() {
    $('#networkStaticFields input').prop('disabled',!$('#net_staticIp').prop('checked'));
    $('#networkApIpFields input').prop('disabled',!$('#net_ap-custom-ip').prop('checked'));
    $('#net_sta-pass').prop('disabled',$('#net_staOpen').prop('checked'));
    $('#net_ap-pass').prop('disabled',$('#net_apOpen').prop('checked'));
    $('#net_time-server').prop('disabled',!$('#net_time-sync').prop('checked'));
    $('#networkApWarning').toggleClass('d-none',!$('#net_apOpen').prop('checked'));
}

// Lee ajustes y estado sin solicitar las contraseñas guardadas; sin parametros.
async function loadNetworkSettings() {
    $('#networkSettingsFields, #networkSettingsForm button[type=submit]').prop('disabled',true);
    try {
        const config=await $.ajax({url:board.IP+'/network/config?view=setup',cache:false,timeout:10000});
        if(typeof config.staHasPass!=='boolean')throw Error('Actualice el firmware para configurar la red.');
        networkSettings.config=config;
        for(const key of [...networkTextKeys,'wifi-mode','time-minwest','time-dsttime'])$('#net_'+key).val(config[key]);
        for(const key of networkBoolKeys)$('#net_'+key).prop('checked',!!config[key]);
        for(const prefix of ['sta','ap']) {
            $('#net_'+prefix+'-pass').val('').attr('placeholder',config[prefix+'HasPass']?'Guardada; dejar vacío para conservar':'Sin clave guardada');
            $('#net_'+prefix+'Open').prop('checked',!config[prefix+'HasPass']);
        }
        $('#networkSettingsStatus').text((config.connected?'Estación conectada: '+config.address:'Estación sin conexión')+(config.apActive?' · AP'+(config.recovery?' de recuperación':'')+': '+config.apAddress:''));
        $('#networkSettingsFields, #networkSettingsForm button[type=submit]').prop('disabled',false);
        updateNetworkSettingsControls();
    } catch(error) {networkSettings.config=null;$('#networkSettingsStatus').text(error.message||'No se pudo cargar la red.');throw error;}
}

// Guarda campos URL-encoded y espera su aplicacion; event es el submit del formulario.
async function saveNetworkSettings(event) {
    event.preventDefault();
    if(!networkSettings.config || !document.getElementById('networkSettingsForm').reportValidity())return;
    const values={};
    for(const key of [...networkTextKeys,'wifi-mode','time-minwest','time-dsttime'])values[key]=$('#net_'+key).val();
    for(const key of networkBoolKeys)values[key]=$('#net_'+key).prop('checked')?'1':'0';
    for(const prefix of ['sta','ap']) {
        const password=$('#net_'+prefix+'-pass').val(),open=$('#net_'+prefix+'Open').prop('checked');
        if(open)values[prefix+'-pass']='';
        else if(password) {
            if(password.length<8 || password.length>(prefix==='sta'?64:63)) {$('#networkSettingsStatus').text('La contraseña debe tener entre 8 y 63 caracteres (o 64 hexadecimales para estación).');return;}
            values[prefix+'-pass']=password;
        } else if(!networkSettings.config[prefix+'HasPass'] || prefix==='sta' && values['sta-ssid']!==networkSettings.config['sta-ssid']) {
            $('#networkSettingsStatus').text('Ingrese la contraseña de la red seleccionada o marque que es una red abierta.');return;
        }
    }
    if(values['wifi-mode']==='0'&&!confirm('Apagar Wi-Fi impide acceder a esta web, incluido el AP de recuperación. ¿Guardar?'))return;
    $('#networkSettingsFields, #networkSettingsForm button').prop('disabled',true);
    try {
        const result=await saveNetworkConfig(values);
        networkSettings.restart=!!result.restartRequired;
        $('#networkRestart').prop('hidden',!networkSettings.restart);
        await loadNetworkSettings();
        $('#networkSettingsStatus').text(networkSettings.restart?'Guardado. Algunos servicios requieren reiniciar.':'Guardado. Red y hora aplicadas sin reiniciar el medidor.');
    } catch(error) {$('#networkSettingsStatus').text(settingsErrorMessage(error));}
    finally {$('#networkSettingsForm button').prop('disabled',false);$('#networkSettingsFields').prop('disabled',!networkSettings.config);if(networkSettings.config)updateNetworkSettingsControls();}
}

// Guarda values, verifica NVS y luego solicita aplicar los servicios para conservar acceso durante la comprobacion.
async function saveNetworkConfig(values) {
    validateNetworkValues(values);
    const result=await $.ajax({type:'POST',url:board.IP+'/network/config?defer=1',data:values,timeout:10000});
    if(result.verified===false)throw Error('El ESP no pudo verificar la escritura en NVS.');
    if(result.verified!==true) {
        const stored=await $.ajax({url:board.IP+'/network/config',cache:false,timeout:10000});
        assertSettingsMatch(values,stored);
    }
    if(result.pending)await $.ajax({type:'POST',url:board.IP+'/network/apply',data:{services:result.pending},timeout:5000});
    if(result.pending||result.apply==='wait')await waitNetworkSettings();
    return result;
}

// Espera el consumidor de red sin recargar NVS; sin parametros.
async function waitNetworkSettings() {
    const deadline=Date.now()+30000;
    while(Date.now()<deadline) {
        await new Promise(resolve=>setTimeout(resolve,750));
        let status;
        try {status=await $.ajax({url:board.IP+'/network/status',cache:false,timeout:2500});}
        catch(error){continue;}
        if(status.apply==='fail')throw Error('Configuración guardada, pero falló su aplicación. Compruebe el AP de recuperación.');
        if(status.apply==='ok')return;
    }
    throw Error('Cambios guardados; no se pudo confirmar la aplicación desde esta dirección. Conecte a la nueva red/IP o al AP de recuperación.');
}

// Consulta el escaneo asincrono existente con un plazo de 20 s; sin parametros.
async function scanNetworkSettings() {
    const run=++networkSettings.scanRun,deadline=Date.now()+20000;
    $('#networkScan').prop('disabled',true);
    $('#networkScanList').empty().append($('<option>').val('').text('Buscando redes...'));
    try {
        while(Date.now()<deadline) {
            const result=await $.ajax({url:board.IP+'/network/scan',cache:false,timeout:5000});
            if(run!==networkSettings.scanRun)return;
            if(result.scan==='failed')throw Error('Falló el escaneo Wi-Fi.');
            if(result.scan==='completed') {
                const select=$('#networkScanList').empty().append($('<option>').val('').text('Seleccione una red'));
                const seen=new Set();
                for(const item of [...result.list].sort((a,b)=>b.rssi-a.rssi)) {
                    if(!item.ssid||seen.has(item.ssid))continue;seen.add(item.ssid);
                    const open=item.auth==='Open'||item.auth==='OPEN';
                    select.append($('<option>').val(item.ssid).attr('data-open',String(open)).text(item.ssid+' · '+item.rssi+' dBm · '+(open?'Abierta':'Protegida')));
                }
                return;
            }
            await new Promise(resolve=>setTimeout(resolve,500));
        }
        throw Error('El escaneo tardó demasiado; vuelva a intentar.');
    } catch(error){$('#networkSettingsStatus').text(error.message||'No se pudo buscar redes.');}
    finally {if(run===networkSettings.scanRun)$('#networkScan').prop('disabled',false);}
}
