let tableRows = [
    {name: "Voltaje", unit: "vrms", visible: true, decimals: 2, source: "meter"},
    {name: "Neutro-tierra Fast RMS", unit: "fastVrms", visible: true, decimals: 3, source: "meter"},
    {name: "Voltaje Fases", unit: "vvrms", visible: true, decimals: 2, source: "meter"},
    {name: "Corriente", unit: "irms", visible: true, decimals: 3, source: "meter"},
    {name: "Potencia", unit: "watt", visible: true, decimals: 1, source: "meter"},
    {name: "Potencia Reactiva", unit: "var", visible: true, decimals: 1, source: "meter"},
    {name: "Potencia Aparente", unit: "va", visible: true, decimals: 1, source: "meter"},
    {name: "Frecuencia", unit: "freq", visible: true, decimals: 2, source: "meter"},
    {name: "Factor de potencia", unit: "fp", visible: true, decimals: 2, source: "meter"},
    {name: "Energía activa", unit: "wattH", visible: true, decimals: 2, source: "energia"},
    {name: "Energía reactiva", unit: "varH", visible: true, decimals: 2, source: "energia"},
    {name: "Energía aparente", unit: "vaH", visible: true, decimals: 2, source: "energia"},
    {name: "Desfasaje fases", unit: "degv", visible: true, decimals: 2, source: "angulos"},
    {name: "Desfasaje corriente", unit: "degvi", visible: true, decimals: 2, source: "angulos"},
    {name: "Desfasaje fases corriente", unit: "degi", visible: true, decimals: 2, source: "angulos"},
    {name: "Distorsión Voltaje", unit: "thdV", visible: true, decimals: 2, source: "thd"},
    {name: "Distorsión Corriente", unit: "thdI", visible: true, decimals: 2, source: "thd"},
];

let decimalPlaces = {};
let meterPolling = {active: false, timer: 0, cycle: 0};
let meterGaugeObserver = null;
let phasorData = {angles: null, voltage: null, current: null};
let storageStatusTimer = 0;
let storageWebFsInfo = {};
let historyEstimateRequest = null;
let historyEstimateRevision = 0;
let loadedSettingsBackup = null;
let loadedSettingsBackupErrors = {};
let settingsBackupBusy = false;
let settingsBackupRestartRequired = false;

// Mapa fijo inicial del firmware: particiones fisicas y bancos WebFS en bloques NAND de 128 KiB.
const nandStorageLayout = [
    {key: "metadata", label: "Metadata", startBlock: 0, blockCount: 16},
    {key: "history", label: "Histórico", startBlock: 16, blockCount: 1536},
    {key: "waveEvents", label: "Eventos Wave", startBlock: 1552, blockCount: 320},
    {key: "gridEvents", label: "Eventos de red", startBlock: 1872, blockCount: 48},
    {key: "diagnostics", label: "Diagnóstico", startBlock: 1920, blockCount: 80},
    {key: "webFsA", label: "WebFS A", startBlock: 2000, blockCount: 24},
    {key: "webFsB", label: "WebFS B", startBlock: 2024, blockCount: 24},
];

let tableHeaders = [
    {name: "Variable", short: "", visible: true},
    {name: "Fase R", short: "R", visible: true},
    {name: "Fase S", short: "S", visible: true},
    {name: "Fase T", short: "T", visible: true},
    {name: "Neutro", short: "N", visible: true},
    {name: "Total", short: "Tot", visible: true},
    {name: "Promedio", short: "Avg", visible: true},
];

function initMeterUI() {
    $("#gaugeVoltaje").simpleGauge(getGaugeOptions("voltage"));
    $("#gaugeVoltajeF").simpleGauge(getGaugeOptions("lineVoltage"));
    $("#gaugeCorriente").simpleGauge(getGaugeOptions("current"));
    $("#gaugePotencia").simpleGauge(getGaugeOptions("power"));
    observeMeterGaugeSizes();
    $("#phasorShowVoltage, #phasorShowCurrent").on("change", updatePhasorDiagram);
    createTable();
    initSettingsUI();
    initHistoryViewer();
    initAppAppearance();
    initHistoryCache();
	initGridEventsViewer();
    initEventHistory();
	initLogsViewer();

}

// Activa o detiene el refresco de medidas; active solo debe ser true en vistas de medición.
function setMeterPollingActive(active) {
    if (meterPolling.active === active) return;
    meterPolling.active = active;
    clearTimeout(meterPolling.timer);
    if (active) { meterPolling.cycle = 0; refreshMeterPolling(); }
}

// Ejecuta una consulta de medición y enruta el resultado a los callbacks existentes.
async function requestMeterData(path, onSuccess, onFail) {
    try {
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), board.timeout);
        const response = await fetch(board.IP + "/" + path, {cache: "no-store", signal: controller.signal});
        clearTimeout(timeout);
        if (!response.ok) throw new Error("HTTP " + response.status);
        onSuccess(await response.json());
    }
    catch (error) { onFail(error); }
}

// Actualiza medidas en serie para no crear ráfagas HTTP sobre el ESP32.
async function refreshMeterPolling() {
    if (!meterPolling.active) return;
    await requestMeterData("meter", meterOk, meterFail);
    if (++meterPolling.cycle % 5 === 1) {
        await requestMeterData("meter?energy", energyOk, energyFail);
        await requestMeterData("meter?angles", angleOk, angleFail);
        await requestMeterData("meter?thd", thdOk, thdFail);
    }
    if (meterPolling.active) meterPolling.timer = setTimeout(refreshMeterPolling, board.getUpdateTime("normal"));
}

// Actualiza el valor numerico del gauge.
// id: identificador del gauge. value: medicion que se muestra.
function updateMeterGauge(id, value) {
    if (Number.isFinite(Number(value))) $("#" + id).simpleGauge("setValue", Number(value));
}

// Define escala, formato digital y zonas de color del gauge.
// type: voltage, lineVoltage, current o power.
function getGaugeOptions(type) {
    const options = {
        min: 0, max: 100, value: 0, type: "analog digital",
        container: {scale: 95, style: "background:linear-gradient(335deg,#ddd 0%,#fff 45%,#bbb 100%);border-radius:50%;border:6px solid #bbb;"},
        digital: {text: "{value.1}", style: "font-size:24px;color:auto;"},
        analog: {minAngle: -150, maxAngle: 150},
        labels: {text: "{value}", count: 8, scale: 72, style: "font-size:14px;"},
        ticks: {count: 10, scale1: 84, scale2: 93, style: "width:2px;color:#999;"},
        subTicks: {count: 4, scale1: 93, scale2: 96, style: "width:1px;color:#bbb;"},
        bars: {scale1: 88, scale2: 94, colors: [[0, "#378618", 0, 0], [70, "#ffa500", 0, 0], [85, "#dd2222", 0, 0]]},
        pointer: {scale: 95, style: "color:#222;opacity:.8;filter:drop-shadow(-2px 2px 2px rgba(0,0,0,.4));"}
    };
    if (type === "voltage") {
        options.min = 180; options.max = 260; options.digital.text = "{value.1} V";
        options.bars.colors = [[180, "#dd2222", 0, 0], [190, "#ffa500", 0, 0], [200, "#378618", 0, 0], [240, "#ffa500", 0, 0], [250, "#dd2222", 0, 0]];
    } else if (type === "lineVoltage") {
        options.min = 340; options.max = 420; options.digital.text = "{value.1} V";
        options.bars.colors = [[340, "#dd2222", 0, 0], [350, "#ffa500", 0, 0], [360, "#378618", 0, 0], [400, "#ffa500", 0, 0], [410, "#dd2222", 0, 0]];
    } else if (type === "current") {
        options.max = 100; options.digital.text = "{value.3} A";
    } else if (type === "power") {
        options.max = 20; options.digital.text = "{value.3} kW";
        options.bars.colors = [[0, "#378618", 0, 0], [14, "#ffa500", 0, 0], [17, "#dd2222", 0, 0]];
    }
    return options;
}

// Redibuja los gauges cuando cambia su tamano visible; sin parametros.
function observeMeterGaugeSizes() {
    if (!("ResizeObserver" in window)) return;
    meterGaugeObserver = new ResizeObserver(entries => {
        entries.forEach(entry => $(entry.target).simpleGauge("draw"));
    });
    document.querySelectorAll(".gauge").forEach(gauge => meterGaugeObserver.observe(gauge));
}

// Mantiene compatibilidad si el navegador conserva un script viejo.
function initUI() {
    initMeterUI();
}

const calibrationGroups = [
    {
        title: "Calibración de fase",
        fields: [
            {key: "APHCAL0", label: "Fase R"},
            {key: "BPHCAL0", label: "Fase S"},
            {key: "CPHCAL0", label: "Fase T"},
            {key: "NPHCAL", label: "Neutro"},
        ],
    },
    {
        title: "Ganancia de corriente",
        fields: [
            {key: "AIGAIN", label: "Fase R"},
            {key: "BIGAIN", label: "Fase S"},
            {key: "CIGAIN", label: "Fase T"},
            {key: "NIGAIN", label: "Neutro"},
        ],
    },
    {
        title: "Ganancia de voltaje",
        fields: [
            {key: "AVGAIN", label: "Fase R"},
            {key: "BVGAIN", label: "Fase S"},
            {key: "CVGAIN", label: "Fase T"},
        ],
    },
    {
        title: "Offset RMS de corriente",
        fields: [
            {key: "AIRMSOS", label: "Fase R"},
            {key: "BIRMSOS", label: "Fase S"},
            {key: "CIRMSOS", label: "Fase T"},
            {key: "NIRMSOS", label: "Neutro"},
        ],
    },
    {
        title: "Offset RMS de voltaje",
        fields: [
            {key: "AVRMSOS", label: "Fase R"},
            {key: "BVRMSOS", label: "Fase S"},
            {key: "CVRMSOS", label: "Fase T"},
        ],
    },
    {
        title: "Offset fundamental",
        fields: [
            {key: "AIFRMSOS", label: "Corriente R"},
            {key: "BIFRMSOS", label: "Corriente S"},
            {key: "CIFRMSOS", label: "Corriente T"},
            {key: "AVFRMSOS", label: "Voltaje R"},
            {key: "BVFRMSOS", label: "Voltaje S"},
            {key: "CVFRMSOS", label: "Voltaje T"},
        ],
    },
    {
        title: "Offset RMS un ciclo",
        fields: [
            {key: "AIRMSONEOS", label: "Corriente R"},
            {key: "BIRMSONEOS", label: "Corriente S"},
            {key: "CIRMSONEOS", label: "Corriente T"},
            {key: "NIRMSONEOS", label: "Corriente N"},
            {key: "AVRMSONEOS", label: "Voltaje R"},
            {key: "BVRMSONEOS", label: "Voltaje S"},
            {key: "CVRMSONEOS", label: "Voltaje T"},
        ],
    },
    {
        title: "Offset RMS 10/12 ciclos",
        fields: [
            {key: "AIRMS1012OS", label: "Corriente R"},
            {key: "BIRMS1012OS", label: "Corriente S"},
            {key: "CIRMS1012OS", label: "Corriente T"},
            {key: "NIRMS1012OS", label: "Corriente N"},
            {key: "AVRMS1012OS", label: "Voltaje R"},
            {key: "BVRMS1012OS", label: "Voltaje S"},
            {key: "CVRMS1012OS", label: "Voltaje T"},
        ],
    },
];

const meterOptionGroups = [
    {
        title: "Perturbaciones de voltaje (Dip / Swell)",
        description: "Comparten los límites por fase de arriba: DIP por debajo del límite bajo y swell por encima del alto. Se guardan al recuperar la normalidad; la histéresis y el tiempo de recuperación también son compartidos.",
        namespace: "meter",
        fields: [
            {key: "dipEnabled", label: "Guardar DIP puntuales", type: "bool"},
            {key: "dipMs", label: "Confirmación DIP", type: "int", unit: "ms"},
            {key: "swellEnabled", label: "Guardar swell puntuales", type: "bool"},
            {key: "swellMs", label: "Confirmación swell", type: "int", unit: "ms"},
        ],
    },
    {
        title: "Eventos de corriente",
        description: "Umbral común para R/S/T/N. Los picos cortos se guardan al terminar; si la condición persiste pasa a alta corriente sostenida y se registra siempre.",
        namespace: "meter",
        fields: [
            {key: "overcurrent", label: "Guardar sobrecorrientes puntuales", type: "bool"},
            {key: "ocCurrent", label: "Nivel sobrecorriente", type: "float", unit: "A"},
            {key: "ocHyst", label: "Histéresis de corriente", type: "float", unit: "A"},
            {key: "ocMs", label: "Mínimo sobrecorriente", type: "int", unit: "ms"},
        ],
    },
];

const settingsBackupSections = [
    {key: "network", label: "Red y servicios"},
    {key: "grid", label: "Estados de red"},
    {key: "history", label: "Registro histórico"},
    {key: "capture", label: "Captura de eventos"},
    {key: "wave", label: "Oscilografías"},
    {key: "energy", label: "Contadores de energía"},
    {key: "calibration", label: "Calibración"},
];

const backupNetworkKeys = [
    "wifi-mode", "sta-ssid", "sta-pass", "staticIp", "sta-ip", "sta-mask", "sta-gw", "sta-dns1", "sta-dns2",
    "ap-ssid", "ap-pass", "ap-hide", "ap-custom-ip", "ap-ip", "ap-mask", "ap-gw", "ota-enabled", "ota-name", "ota-pass",
    "time-start", "time-dsttime", "time-minwest", "time-server", "time-sync", "debug-enabled", "debug-name", "debug-pass",
    "mqtt-enabled", "mqtt-server", "mqtt-port", "mqtt-user", "mqtt-pass",
];

const backupGridKeys = ["lowR", "highR", "lowS", "highS", "lowT", "highT", "noVoltage", "hysteresis",
    "lossConfirmMs", "recoveryConfirmMs", "abnormalConfirmMs",
    "neutralHigh", "neutralHysteresis", "neutralConfirmMs", "neutralRecoveryMs"];
const backupHistoryKeys = ["enabled", "groupConfig", "samplePeriodDeciseconds"];
const backupEnergyKeys = ["rWattH", "rVarH", "rVaH", "sWattH", "sVarH", "sVaH", "tWattH", "tVarH", "tVaH", "WattH", "VarH", "VaH"];

// Interpreta solo representaciones booleanas admitidas; value proviene del formulario o del JSON.
function settingsBoolean(value) {
    if([true,1,'1','true'].includes(value))return true;
    if([false,0,'0','false'].includes(value))return false;
    throw Error('Valor booleano inválido.');
}

// Comprueba numero finito y rango; value/min/max/integer definen el valor y sus limites.
function settingsNumber(value,min,max,integer=false) {
    return ['number','string'].includes(typeof value)&&String(value).trim()!==''&&Number.isFinite(Number(value))&&
        Number(value)>=min&&Number(value)<=max&&(!integer||Number.isInteger(Number(value)));
}

// Compara valores solicitados contra su lectura posterior; expected/actual son objetos planos de una seccion.
function assertSettingsMatch(expected,actual) {
    const different=Object.keys(expected).filter(key=>{
        if(!Object.hasOwn(actual,key))return true;
        const value=expected[key],stored=actual[key];
        if(getCalibrationKeys().includes(key)&&isHex32(String(value))) {
            if(!isHex32(String(stored))&&!settingsNumber(stored,-2147483648,4294967295,true))return true;
            return (parseInt(value,16)>>>0)!==(isHex32(String(stored))?parseInt(stored,16)>>>0:Number(stored)>>>0);
        }
        if(typeof stored==='boolean'||typeof value==='boolean') {
            try{return settingsBoolean(value)!==settingsBoolean(stored);}catch(error){return true;}
        }
        if(typeof stored==='number') {
            if(!settingsNumber(value,-Number.MAX_VALUE,Number.MAX_VALUE))return true;
            const floating=backupEnergyKeys.includes(key)||['ocCurrent','ocHyst','neutralRms','neutralFast','samplePeriod'].includes(key)||
                backupGridKeys.includes(key)&&!key.endsWith('Ms')||/^(volt|curr)\./.test(key)||/^neut\.(high|hyst)$/.test(key);
            return floating?Math.fround(Number(value))!==Math.fround(stored):Number(value)!==stored;
        }
        return value!==stored;
    });
    if(different.length)throw Error('No coincide lo guardado: '+different.join(', ')+'. Recargue antes de continuar.');
}

// Valida limites y relaciones de red; values contiene el grupo completo de estados de red.
function validateGridValues(values) {
    const limits={noVoltage:[1,100],hysteresis:[0,50],lossConfirmMs:[20,2000],recoveryConfirmMs:[20,1000],abnormalConfirmMs:[0,1500],
        neutralHigh:[0.025,409.55],neutralHysteresis:[0,409.55],neutralConfirmMs:[20,2000],neutralRecoveryMs:[20,1000]};
    for(const [key,[min,max]] of Object.entries(limits))
        if(key==='abnormalConfirmMs'&&!Object.hasOwn(values,key))continue;
        else
        if(!settingsNumber(values[key],min,max,key.endsWith('Ms')))throw Error('Valor fuera de rango en '+key+'.');
    if(Object.hasOwn(values,'abnormalConfirmMs') && Number(values.abnormalConfirmMs)<Math.min(Number(values.lossConfirmMs),1500))
        throw Error('Confirmación de red anormal: el mínimo es el tiempo de pérdida de fase, limitado a 1500 ms.');
    for(const phase of ['R','S','T']) {
        const low=Number(values['low'+phase]),high=Number(values['high'+phase]),hyst=Number(values.hysteresis);
        if(!settingsNumber(values['low'+phase],1,500)||!settingsNumber(values['high'+phase],1,500)||
            low<=Number(values.noVoltage)+hyst||high<=low+2*hyst)throw Error('Límites o histéresis incompatibles en fase '+phase+'.');
    }
    if(Number(values.neutralHysteresis)>=Number(values.neutralHigh))throw Error('La histéresis de neutro debe ser menor al umbral.');
}

// Valida campos de deteccion presentes; values puede ser una seccion parcial del respaldo.
function validateCaptureValues(values) {
    for(const key of ['dipEnabled','swellEnabled','overcurrent'])if(Object.hasOwn(values,key))settingsBoolean(values[key]);
    for(const key of ['dipMs','swellMs','ocMs'])if(Object.hasOwn(values,key)&&
        (!settingsNumber(values[key],10,2000,true)||Number(values[key])%10))throw Error('Confirmaciones: 10 a 2000 ms, en pasos de 10 ms.');
    for(const key of ['ocCurrent','ocHyst'])if(Object.hasOwn(values,key)&&
        (!settingsNumber(values[key],0,163.82)||key==='ocCurrent'&&Number(values[key])===0))throw Error('Sobrecorriente fuera de rango.');
    if(Object.hasOwn(values,'ocCurrent')&&Object.hasOwn(values,'ocHyst')&&Number(values.ocHyst)>=Number(values.ocCurrent))
        throw Error('La histéresis de corriente debe ser menor al umbral.');
}

// Valida campos Wi-Fi y servicios; values incluye las opciones finales conocidas antes de guardar.
function validateNetworkValues(values) {
    const ranges={'wifi-mode':[0,3],'time-minwest':[-840,840],'time-dsttime':[-120,120],'time-start':[0,4294967295],'mqtt-port':[1,65535]};
    for(const [key,[min,max]] of Object.entries(ranges))if(Object.hasOwn(values,key)&&!settingsNumber(values[key],min,max,true))throw Error('Valor fuera de rango en '+key+'.');
    for(const key of ['staticIp','ap-hide','ap-custom-ip','ota-enabled','time-sync','debug-enabled','mqtt-enabled'])
        if(Object.hasOwn(values,key))settingsBoolean(values[key]);
    for(const [key,value] of Object.entries(values)) {
        if(backupNetworkKeys.includes(key)&&!Object.hasOwn(ranges,key)&&!['staticIp','ap-hide','ap-custom-ip','ota-enabled','time-sync','debug-enabled','mqtt-enabled'].includes(key)) {
            if(typeof value!=='string'||new TextEncoder().encode(value).length>255)throw Error('Texto inválido en '+key+'.');
        }
    }
    for(const prefix of ['sta','ap']) {
        const ssid=values[prefix+'-ssid'],password=values[prefix+'-pass'];
        if(ssid!==undefined&&(new TextEncoder().encode(ssid).length>32||prefix==='ap'&&!ssid))throw Error('SSID inválido: máximo 32 bytes.');
        if(password!==undefined) {
            const size=new TextEncoder().encode(password).length;
            if(size!==0&&!(size>=8&&size<=63)&&!(prefix==='sta'&&/^[a-f\d]{64}$/i.test(password)))throw Error('Contraseña inválida en '+prefix+'.');
        }
        const toggle=prefix==='sta'?'staticIp':'ap-custom-ip';
        if(Object.hasOwn(values,toggle)&&settingsBoolean(values[toggle])&&['ip','mask','gw'].every(key=>Object.hasOwn(values,prefix+'-'+key))) {
            const ip=settingsIp(values[prefix+'-ip']),mask=settingsIp(values[prefix+'-mask']),gateway=settingsIp(values[prefix+'-gw']);
            const host=(~mask)>>>0;
            if(!mask||!host||(host&(host+1))!==0||(ip&host)===0||(ip&host)===host||
                (gateway&host)===0||(gateway&host)===host||(ip>>>24)<1||(ip>>>24)>=224||
                (gateway>>>24)<1||(gateway>>>24)>=224||(ip&mask)!==(gateway&mask))throw Error('IP, máscara o puerta de enlace incompatibles en '+prefix+'.');
        }
    }
    for(const key of ['sta-dns1','sta-dns2'])if(Object.hasOwn(values,key))settingsIp(values[key]);
    if(Object.hasOwn(values,'time-server')&&!/^[a-z\d.-]{1,47}$/i.test(values['time-server']))throw Error('Servidor NTP inválido (máximo 47 caracteres).');
    for(const [key,max] of [['mqtt-server',63],['mqtt-user',31],['mqtt-pass',31]])
        if(Object.hasOwn(values,key)&&new TextEncoder().encode(values[key]).length>max)throw Error(key+': máximo '+max+' bytes.');
}

// Convierte value IPv4 a entero sin signo, rechazando octetos incompletos o fuera de rango.
function settingsIp(value) {
    if(typeof value!=='string'||!/^\d{1,3}(\.\d{1,3}){3}$/.test(value)||value.split('.').some(part=>Number(part)>255))throw Error('Dirección IPv4 inválida.');
    return value.split('.').reduce((ip,part)=>(ip*256+Number(part))>>>0,0);
}

// Inicializa la pestaña de opciones.
function initSettingsUI() {
    $("#settings").html(createSettingsHtml());
    initNetworkSettings();
    $("#btnLoadSettings").click(() => loadSettings("capture", true));
    $("#btnLoadCalibration").click(() => loadSettings("calibration", true));
    $("#btnSaveMeterOptions").click(saveMeterOptions);
    $("#waveConfigForm").on("submit", saveWaveConfig);
    $("#waveConfigRefresh").on("click", () => loadWaveConfig().catch(()=>{}));
    $("#btnSaveCalibration").click(saveCalibrationOptions);
    $("#btnRestoreCalibration").click(restoreDefaultCalibration);
    $("#neutralCalibrationReload").on("click", () => loadNeutralCalibration());
    $("#neutralCalibrationStart").on("click", startNeutralCalibration);
    $("#gridSettingsForm").on("submit", saveGridSettings);
    $("#historySettingsForm").on("submit", saveHistorySettings);
    $("#historySettingsForm").on("change", "input, select", function () {
        if (this.id === "history_energy") $("#history_energyExtraMode").prop("disabled", !this.checked);
        updateHistoryEstimate();
    });
    $("#history_period").on("input", updateHistoryEstimate);
    $("#btnLoadGridSettings").on("click", () => loadSettings("grid", true));
    $("#settings").on("click", ".eventSettingsHelpButton", function () { showEventSettingsHelp(this.dataset.help); });
    $("#settings_capture").on("change", "#opt_meter_dipEnabled, #opt_meter_swellEnabled, #opt_meter_overcurrent", updatePointWaveControls);
    $("#btnLoadHistorySettings").on("click", () => loadSettings("history", true));
    $("#btnRefreshStorage").on("click", () => loadSettings("storage", true));
    $("#btnFormatStorage").on("click", startStorageFormat);
    $("#storagePartitionRows").off("click", ".btnFormatPartition").on("click", ".btnFormatPartition", function () {
        startPartitionFormat(this.dataset.partition, this.dataset.label);
    });
    $("#btnExportSettingsBackup").on("click", exportSettingsBackup);
    $("#settingsBackupFile").on("change", function () { loadSettingsBackup(this.files[0]); });
    $("#btnImportSettingsBackup").on("click", importSettingsBackup);
    $("#btnRestartAfterImport").on("click", restartAfterSettingsImport);
    $("#btnCopyGridLimits").on("click", () => {
        ["S", "T"].forEach((phase) => ["low", "high"].forEach((limit) => {
            $("#grid_" + limit + phase).val($("#grid_" + limit + "R").val());
        }));
    });
    $("#settingsTabs button").on("shown.bs.tab", () => loadSettings());
}

// Crea el HTML base de las opciones.
function createSettingsHtml() {
    let html = '<div class="p-2"><ul class="nav nav-tabs" id="settingsTabs" role="tablist">';
    const sections = [["grid", "Eventos de red"], ["network", "Red y hora"], ["history", "Registro histórico"], ["application", "Aplicación"], ["storage", "Almacenamiento NAND"], ["calibration", "Calibración"], ["backup", "Importar / exportar"]];
    sections.forEach(([key, label], i) => {
        html += '<li class="nav-item" role="presentation"><button class="nav-link' + (i ? '' : ' active') + '" id="settingsTab_' + key + '" data-section="' + key + '" data-bs-toggle="tab" data-bs-target="#settings_' + key + '" type="button" role="tab" aria-controls="settings_' + key + '" aria-selected="' + !i + '">' + label + '</button></li>';
    });
    html += '</ul><div class="tab-content">';
    const contents = [createGridSettingsHtml() + createMeterSettingsHtml(), createNetworkSettingsHtml(), createHistorySettingsHtml(), createAppearanceSettingsHtml() + createApplicationSettingsHtml(), createStorageSettingsHtml(), createCalibrationSettingsHtml(), createSettingsBackupHtml()];
    sections.forEach(([key], i) => {
        html += '<section class="tab-pane settingsPane' + (i ? '' : ' active') + '" id="settings_' + key + '" role="tabpanel" aria-labelledby="settingsTab_' + key + '">';
        html += contents[i] + '</section>';
    });
    html += '</div></div><div class="modal fade" id="eventSettingsHelp" tabindex="-1" aria-labelledby="eventSettingsHelpTitle" aria-hidden="true"><div class="modal-dialog modal-dialog-centered modal-dialog-scrollable"><div class="modal-content"><div class="modal-header py-2"><h2 class="modal-title fs-6" id="eventSettingsHelpTitle"></h2><button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Cerrar ayuda"></button></div><div class="modal-body small" id="eventSettingsHelpBody"></div></div></div></div>';
    return html;
}

// Crea la interfaz de respaldo y restauracion de configuracion.
function createSettingsBackupHtml() {
    let html = '<div class="settingsSection">' + createSettingsHeaderHtml("Importar y exportar", "Respaldo local de ajustes del medidor.", '<button id="btnExportSettingsBackup" type="button" class="btn btn-sm btn-outline-primary">Exportar JSON</button>');
    html += '<div class="alert alert-warning py-2 small">El archivo incluye contraseñas en texto plano. Guardalo en un lugar privado.</div>';
    html += '<div class="settingsGroup"><h3>Secciones del respaldo</h3><div class="settingsGroupGrid settingsGroupGridTwo">';
    settingsBackupSections.forEach(({key, label}) => {
        html += '<div class="form-check"><input class="form-check-input backupExportSection" type="checkbox" id="backupExport_' + key + '" value="' + key + '" checked><label class="form-check-label" for="backupExport_' + key + '">' + label + '</label></div>';
    });
    html += '</div></div><div class="settingsGroup mt-3"><h3>Restaurar respaldo</h3>';
    html += '<input id="settingsBackupFile" class="form-control form-control-sm" type="file" accept=".json,application/json">';
    html += '<div id="settingsBackupWarning" class="small text-muted mt-2">Elegí un respaldo JSON para revisar sus secciones.</div>';
    html += '<div id="settingsBackupImportSections" class="settingsGroupGrid settingsGroupGridTwo mt-2"></div>';
    html += '<div class="d-flex flex-wrap gap-2 mt-3"><button id="btnImportSettingsBackup" type="button" class="btn btn-sm btn-success" disabled>Importar seleccionadas</button>';
    html += '<button id="btnRestartAfterImport" type="button" class="btn btn-sm btn-warning" hidden>Reiniciar para aplicar</button></div>';
    html += '<ol id="settingsBackupStatus" class="small mt-3 mb-0" aria-live="polite"></ol></div></div>';
    return html;
}

// Crea el panel de estado, mapa y acciones disponibles para la NAND; sin parametros.
function createStorageSettingsHtml() {
    const actions = createSettingsIconButton('btnRefreshStorage','Actualizar almacenamiento',false) +
        '<button id="btnFormatStorage" type="button" class="btn btn-sm btn-outline-danger">Formatear NAND completa</button>';
    let html = '<div class="settingsSection">' + createSettingsHeaderHtml("Almacenamiento NAND", "Distribución física, capacidad útil y estado de las áreas de almacenamiento.", actions);
    html += '<div id="storageSettingsStatus" class="settingsStatusLine small text-muted" role="status">Sin consultar.</div>';
    html += '<div id="storageSummary" class="storageSummary small"></div>';
    html += '<div id="storageFormatProgress" class="storageFormatProgress" hidden><progress id="storageFormatProgressBar" max="100" value="0"></progress><span id="storageFormatProgressText"></span></div>';
    html += '<div class="storageWarning small">Cada acción individual borra físicamente solo esa partición. El formato completo borra toda la NAND y reinicializa el mapa.</div>';
    html += '<div class="table-responsive"><table class="table table-sm table-bordered align-middle storageTable"><thead><tr><th>Área</th><th>Bloques físicos</th><th>Tamaño</th><th>Útil</th><th>Contenido / estado</th><th>Acción</th></tr></thead><tbody id="storagePartitionRows"></tbody></table></div>';
    html += '</div>';
    return html;
}

// Formatea bytes binarios para mostrar capacidades; bytes es la cantidad a convertir.
function formatStorageSize(bytes) {
    const value = Number(bytes);
    if (!Number.isFinite(value) || value < 0) return "—";
    if (value >= 1024 * 1024) return (value / (1024 * 1024)).toFixed(1) + " MiB";
    if (value >= 1024) return (value / 1024).toFixed(0) + " KiB";
    return value + " B";
}

// Lee estado NAND y, salvo durante el progreso del formateo, el estado de WebFS.
// includeWebFs evita repetir la segunda consulta mientras solo se muestra progreso.
function loadStorageStatus(includeWebFs = true) {
    return $.ajax({url: board.IP + "/storage", cache: false, timeout: Math.max(board.timeout, 5000)})
        .then((status) => {
            const partitionFormats = Object.values(status.partitions || {}).map((part) => part.format || {});
            const formatting = !!(status.format && status.format.active) || partitionFormats.some((part) => part.active);
            if (!includeWebFs && formatting) {
                renderStorageStatus(status, storageWebFsInfo);
                return status;
            }
            return $.ajax({url: board.IP + "/webfs", cache: false, timeout: Math.max(board.timeout, 5000)})
                .then((response) => {
                    storageWebFsInfo = response.nand || {};
                    renderStorageStatus(status, storageWebFsInfo);
                    return status;
                }, () => {
                    storageWebFsInfo = {};
                    renderStorageStatus(status, storageWebFsInfo);
                    return status;
                });
        })
        .fail(() => {
            clearTimeout(storageStatusTimer);
            $("#storageSettingsStatus").text("No se pudo consultar el estado del almacenamiento NAND.");
            $("#btnFormatStorage").prop("disabled", false);
        });
}

// Actualiza el mapa de particiones y el progreso; status y webFs son respuestas de sus endpoints.
function renderStorageStatus(status, webFs) {
    const p = status.partitions || {};
    const history = status.historyStore || {};
    const grid = status.gridStore || {};
    const stateNames = {ready: "Lista", formatting: "Formateando", unformatted: "Sin formato", format_error: "Error de formato", not_detected: "No detectada"};
    const content = {
        metadata: "Mapa de particiones y bitmap de bloques defectuosos",
        history: (history.writtenPages || 0) + " páginas escritas desde el inicio",
        waveEvents: "Oscilografias: 64 KiB por captura; dos por bloque NAND",
        gridEvents: ((Number(grid.recoveredEvents) || 0) + (Number(grid.acceptedEvents) || 0)) + " eventos conocidos",
        diagnostics: "Registros de diagnóstico",
        webFsA: storageWebFsLabel(webFs, "a"),
        webFsB: storageWebFsLabel(webFs, "b"),
    };
    for (const [key, metrics] of [["history", history], ["gridEvents", grid]]) {
        if (metrics.writeReadyMs > 0) content[key] += '<div class="small text-muted" title="Tiempos desde el arranque del firmware; escritura incluye preparar el primer destino libre, no escribir una muestra.">' +
            'Montaje ' + (metrics.mountReadyMs / 1000).toFixed(3) + ' s · lista para escribir ' + (metrics.writeReadyMs / 1000).toFixed(3) + ' s</div>';
    }
    const globalFormatActive = !!(status.format && status.format.active);
    const anyPartitionFormatActive = Object.values(p).some((part) => part.format && part.format.active);
    const anyFormatActive = globalFormatActive || anyPartitionFormatActive;
    const html = nandStorageLayout.map((row) => {
        const part = p[row.key] || {};
        const format = part.format || {};
        const bank = row.key === "webFsA" ? webFs.a : row.key === "webFsB" ? webFs.b : null;
        const blocks = Number(part.blocks) || row.blockCount;
        const configuredBytes = blocks * 131072;
        const usableBlocks = row.key === "metadata" ? null : Number(part.usableBlocks ?? blocks);
        const usableBytes = bank ? Number(bank.capacityBytes) || 0 :
            usableBlocks === null ? null : usableBlocks * 131072;
        const range = row.startBlock + "–" + (row.startBlock + row.blockCount - 1);
        const usableLabel = usableBytes === null ? "Reservados" : formatStorageSize(usableBytes) +
            "<div class=\"small text-muted\">" + (usableBlocks === null ? "" : usableBlocks + "/" + blocks + " bloques") + "</div>";
        let rowContent = content[row.key];
        if (format.active) {
            rowContent += "<div class=\"small text-primary\">Formateando " + Number(format.percent || 0) + "% · " +
                formatStorageSize(format.bytesDone) + " / " + formatStorageSize(format.totalBytes) + " · " +
                Number(format.blocksDone || 0) + "/" + Number(format.blocksTotal || 0) + " bloques</div>";
        } else if (format.done) {
            rowContent += "<div class=\"small " + (format.success ? "text-success" : "text-danger") + "\">" +
                (format.success ? "Formato completo" : "Formato fallido · " + Number(format.failedBlocks || 0) + " bloques con error") +
                " · " + formatStorageSize(format.bytesErased) + " borrados</div>";
        }
        const stateClass = part.enabled === false && status.state === "ready" && row.key !== "metadata" ? "text-muted" : "";
        const canRetry = format.done && !format.success;
        const canFormat = row.key !== "metadata" && !anyFormatActive && status.state === "ready" && (part.enabled !== false || canRetry);
        const isBusy = !!format.active;
        const action = row.key === "metadata" ? "No aplica" :
            "<button type=\"button\" class=\"btn btn-sm btn-outline-danger btnFormatPartition\" data-partition=\"" + row.key +
            "\" data-label=\"" + row.label + "\" " + (!canFormat || isBusy ? "disabled" : "") + ">" +
            (isBusy ? "Formateando…" : "Formatear") + "</button>";
        return "<tr><th>" + row.label + "</th><td>" + range + "</td><td>" + formatStorageSize(configuredBytes) +
            "</td><td>" + usableLabel + "</td><td class=\"" + stateClass + "\">" + rowContent + "</td><td>" + action + "</td></tr>";
    }).join("");
    $("#storagePartitionRows").html(html);
    $("#storageSummary").text((stateNames[status.state] || status.state || "Estado desconocido") + " · " +
        (Number(status.nand && status.nand.blocks) || 0) + " bloques NAND · " + formatStorageSize(status.nand && status.nand.capacityBytes));

    const format = status.format || {};
    const partitionBusy = anyPartitionFormatActive;
    const total = Number(format.totalBlocks) || 0;
    const scanned = Number(format.scannedBlocks) || 0;
    const usable = Number(format.usableBlocks) || 0;
    const erased = Number(format.erasedBlocks) || 0;
    const progress = total ? Math.min(100, Math.round(scanned < total ? scanned * 50 / total : 50 + (usable ? erased * 50 / usable : 0))) : 0;
    $("#storageFormatProgress").prop("hidden", !format.active);
    $("#storageFormatProgressBar").val(progress);
    $("#storageFormatProgressText").text("Formateo NAND completa: " + progress + "% · escaneados " + scanned + "/" + total +
        " · borrados " + erased + "/" + usable + " bloques");
    $("#btnFormatStorage").prop("disabled", format.active || partitionBusy || status.state === "not_detected");

    if (format.active || partitionBusy) {
        clearTimeout(storageStatusTimer);
        storageStatusTimer = setTimeout(() => loadStorageStatus(false), 1000);
        $("#storageSettingsStatus").text(format.active ? "Formateo completo en curso." : "Formateo de partición en curso; las demás áreas siguen disponibles.");
    } else {
        clearTimeout(storageStatusTimer);
        $("#storageSettingsStatus").text(format.completed ? "Formateo completo finalizado." :
            format.failed ? "El formateo completo terminó con errores." : "Estado actualizado.");
    }
}

// Solicita el borrado de key tras confirmar el nombre visible label.
function startPartitionFormat(key, label) {
    if (!key) return;
    const confirmation = prompt("Se borrarán físicamente todos los bloques utilizables de " + label + ". Escribí FORMAT para continuar.");
    if (confirmation !== "FORMAT") return;
    $(".btnFormatPartition, #btnFormatStorage").prop("disabled", true);
    $("#storageSettingsStatus").text("Iniciando formato de " + label + "...");
    $.ajax({type: "POST", url: board.IP + "/storage/partition/format", data: {partition: key, confirm: "FORMAT"}, timeout: Math.max(board.timeout, 5000)})
        .done(() => loadStorageStatus(false))
        .fail((xhr) => {
            $("#storageSettingsStatus").text(xhr.responseText || "No se pudo iniciar el formato de " + label + ".");
            $(".btnFormatPartition, #btnFormatStorage").prop("disabled", false);
        });
}

// Describe un banco WebFS; webFs contiene ambos bancos y bank es "a" o "b".
function storageWebFsLabel(webFs, bank) {
    const info = webFs[bank] || {};
    const active = webFs.active === bank;
    if (!info.valid) return active ? "Banco activo sin imagen válida" : "Sin imagen válida";
    return (active ? "Activo · " : "Respaldo · ") + formatStorageSize(info.imageSize);
}

// Solicita el formateo completo solo tras confirmacion literal del usuario; sin parametros.
function startStorageFormat() {
    const confirmation = prompt("Esto borrará TODA la NAND: histórico, eventos, diagnóstico y WebFS A/B. Escribí FORMAT para continuar.");
    if (confirmation !== "FORMAT") return;
    $("#btnFormatStorage").prop("disabled", true);
    $("#storageSettingsStatus").text("Solicitando formateo completo...");
    $.ajax({type: "POST", url: board.IP + "/storage/format", data: {confirm: "FORMAT"}, timeout: Math.max(board.timeout, 5000)})
        .done(() => loadStorageStatus())
        .fail((xhr) => {
            $("#storageSettingsStatus").text(xhr.responseText || "No se pudo iniciar el formateo completo.");
            $("#btnFormatStorage").prop("disabled", false);
        });
}

// Crea el formulario compacto de estados de red; sin parametros.
function createGridSettingsHtml() {
    const actions = createEventSettingsHelpButton("limits") + createSettingsIconButton("btnLoadGridSettings", "Recargar todo", false) + createSettingsIconButton("", "Guardar límites", true, "submit");
    let html = '<form id="gridSettingsForm" class="settingsSection">';
    html += createSettingsHeaderHtml("Estados de red y límites", "Umbrales de tensión por fase y sobrecorriente común a R/S/T. Los cambios de estado se guardan siempre; el registro de eventos puntuales se configura abajo.", actions);
    html += '<div id="gridSettingsStatus" class="settingsStatusLine small text-muted" role="status"></div>';
    html += '<fieldset id="gridSettingsFields" disabled><div class="settingsGroupGrid settingsGroupGridTwo">';
    html += '<div class="settingsGroup"><h3>Límites por fase</h3><div class="table-responsive"><table class="table table-sm align-middle settingsTable"><thead><tr><th>Fase</th><th>Baja / DIP (V)</th><th>Alta / Swell (V)</th></tr></thead><tbody>';
    ["R", "S", "T"].forEach((phase) => {
        html += '<tr><th>' + phase + '</th>';
        ["low", "high"].forEach((limit) => {
            html += '<td><input class="form-control form-control-sm" type="number" required min="1" max="500" step="0.01" name="' + limit + phase + '" id="grid_' + limit + phase + '" aria-label="' + (limit === "low" ? "Bajo" : "Alto") + ' voltaje fase ' + phase + '"></td>';
        });
        html += '</tr>';
    });
    html += '</tbody></table></div><button id="btnCopyGridLimits" type="button" class="btn btn-sm btn-outline-secondary">Copiar R a S/T</button><div class="settingsCurrentLimits border-top mt-2 pt-2">' +
        '<div class="settingsField"><label for="opt_meter_ocCurrent">Sobrecorriente R/S/T (A)</label><input id="opt_meter_ocCurrent" name="ocCurrent" class="form-control form-control-sm optionInput" data-key="ocCurrent" data-type="float" type="number" required min="0.01" max="163.82" step="any"></div>' +
        '<div class="settingsField"><label for="opt_meter_ocHyst">Histéresis de corriente (A)</label><input id="opt_meter_ocHyst" name="ocHyst" class="form-control form-control-sm optionInput" data-key="ocHyst" data-type="float" type="number" required min="0" max="163.81" step="any"></div></div></div>';
    html += '<div class="settingsGroup"><h3>Sin tensión y recuperación</h3>';
    const fields = [
        ["noVoltage", "Sin tensión (V)", 1, 100, "0.01"],
        ["hysteresis", "Histéresis (V)", 0, 50, "0.01"],
        ["lossConfirmMs", "Confirmación sin tensión (ms)", 20, 2000, "1"],
        ["recoveryConfirmMs", "Confirmación normalidad (ms)", 20, 1000, "1"],
        ["abnormalConfirmMs", "Confirmación red anormal (ms)", 20, 1500, "1"],
    ];
    fields.forEach(([key, label, min, max, step]) => {
        html += '<div class="settingsField"><label for="grid_' + key + '">' + label + '</label><input class="form-control form-control-sm" required type="number" id="grid_' + key + '" name="' + key + '" min="' + min + '" max="' + max + '" step="' + step + '"></div>';
    });
    html += '<div id="gridCapturePolicy" class="small text-muted mt-2"></div></div><div class="settingsGroup"><h3>Neutro-tierra</h3>';
    const neutralFields = [["neutralHigh", "Umbral (V)", 0.025, 409.55, "0.025"],
        ["neutralHysteresis", "Histéresis (V)", 0, 409.525, "0.025"],
        ["neutralConfirmMs", "Confirmación de tensión alta (ms)", 20, 2000, "1"],
        ["neutralRecoveryMs", "Confirmación de recuperación (ms)", 20, 1000, "1"]];
    neutralFields.forEach(([key, label, min, max, step]) => {
        html += '<div class="settingsField"><label for="grid_' + key + '">' + label + '</label><input class="form-control form-control-sm" required type="number" id="grid_' + key + '" name="' + key + '" min="' + min + '" max="' + max + '" step="' + step + '"></div>';
    });
    html += '</div></div></fieldset></form>';
    return html;
}

// Crea el formulario del perfil que se escribe periodicamente en la NAND.
function createHistorySettingsHtml() {
    const actions = createSettingsIconButton('btnLoadHistorySettings','Actualizar histórico',false) +
        createSettingsIconButton('','Guardar histórico',true,'submit');
    let html = '<form id="historySettingsForm" class="settingsSection">';
    html += createSettingsHeaderHtml("Registro histórico", "Muestras periódicas guardadas en la memoria NAND. Cada cambio inicia una página nueva.", actions);
    html += '<div id="historySettingsStatus" class="settingsStatusLine small text-muted" role="status"></div>';
    html += '<fieldset id="historySettingsFields" disabled><div class="settingsGroupGrid settingsGroupGridTwo">';
    html += '<div class="settingsGroup"><h3>Frecuencia</h3>';
    html += '<div class="form-check form-switch settingsSwitch"><input class="form-check-input" type="checkbox" id="history_enabled" name="enabled"><label class="form-check-label" for="history_enabled">Registrar histórico</label></div>';
    html += '<div class="settingsField"><label for="history_period">Período de muestra</label><div class="input-group input-group-sm"><input class="form-control" required type="number" id="history_period" name="period" min="0.1" max="300" step="0.1"><span class="input-group-text">s</span></div></div>';
    html += '<div class="small text-muted mt-2" id="historyProfileInfo">Tamaño de muestra y capacidad calculados por el medidor.</div>';
    html += '<div class="historyCapacityInfo small" id="historyCapacityInfo" role="status" aria-live="polite">La duración estimada aparecerá al cargar el perfil.</div>';
    html += '<p class="historyBaseInfo small text-muted mb-0">Siempre: tensión de R/S/T/N (0,01 V), corriente de R/S/T/N (mA) y estado de Wi-Fi, MQTT, alimentación/carga de batería y hora sincronizada.</p>';
    html += '<div class="form-check form-switch settingsSwitch mt-3"><input class="form-check-input" type="checkbox" id="history_battery" name="battery"><label class="form-check-label" for="history_battery">Guardar mediciones de batería</label></div>';
    html += '<div class="historyOptionDescription small text-muted">Agrega VBAT, VBUS, corriente de carga y nivel de batería; suma 3 bytes por muestra. El estado de batería ya se guarda siempre.</div>';
    html += '<div class="small text-muted mt-2">La duración es teórica con la partición llena; cambios de perfil y páginas parciales pueden reducirla un poco.</div></div>';
    html += '<div class="settingsGroup"><h3>Grupos opcionales</h3>';
    const modes = [
        ["activePower", "Potencia activa", "Total compacto o valores por fase R/S/T."],
        ["reactivePower", "Potencia reactiva", "Total compacto o valores por fase R/S/T."],
        ["apparentPower", "Potencia aparente", "Total compacto o valores por fase R/S/T."],
        ["frequency", "Frecuencia", "Frecuencia general; formato compacto (0,1 Hz) o completo (0,01 Hz)."],
    ];
    modes.forEach(([key, label, description]) => {
        const fullLabel = key === "frequency" ? "Completa" : "Por fase R/S/T";
        const compactLabel = key === "frequency" ? "Compacta" : "Total compacta";
        html += '<div class="settingsField"><label for="history_' + key + '">' + label + '</label><select class="form-select form-select-sm" id="history_' + key + '" name="' + key + '"><option value="off">No guardar</option><option value="compact">' + compactLabel + '</option><option value="full">' + fullLabel + '</option></select></div>';
        html += '<div class="historyOptionDescription small text-muted">' + description + '</div>';
    });
    const groups = [
        ["voltageFull", "Tensiones entre fases", "R-S, S-T y T-R; agrega 6 bytes por muestra."],
        ["powerFactor", "Factor de potencia", "Factor por fase R/S/T."],
        ["voltageThd", "THD de tensión", "Distorsión armónica de tensión por fase R/S/T."],
        ["currentThd", "THD de corriente", "Distorsión armónica de corriente por fase R/S/T."],
        ["voltageAngles", "Ángulos entre tensiones", "Ángulos R-S, S-T y T-R."],
    ];
    groups.forEach(([key, label, description]) => {
        html += '<div class="form-check form-switch settingsSwitch"><input class="form-check-input" type="checkbox" id="history_' + key + '" name="' + key + '"><label class="form-check-label" for="history_' + key + '">' + label + '</label></div>';
        html += '<div class="historyOptionDescription small text-muted">' + description + '</div>';
    });
    html += '<div class="form-check form-switch settingsSwitch"><input class="form-check-input" type="checkbox" id="history_energy" name="energy"><label class="form-check-label" for="history_energy">Energía activa por fase</label></div>';
    html += '<div class="historyOptionDescription small text-muted">Wh acumulados de R/S/T; opcionalmente agrega energía reactiva y/o aparente.</div>';
    html += '<div class="settingsField"><label for="history_energyExtraMode">Energías adicionales</label><select class="form-select form-select-sm" id="history_energyExtraMode" name="energyExtraMode">' +
        '<option value="0">Ninguna</option><option value="1">Reactiva total</option><option value="2">Aparente total</option>' +
        '<option value="3">Reactiva y aparente total</option><option value="4">Reactiva por fase</option><option value="5">Aparente por fase</option>' +
        '<option value="6">Reactiva por fase + aparente total</option><option value="7">Reactiva y aparente por fase</option></select></div>';
    html += '</div></div></fieldset></form>';
    return html;
}

// Completa el formulario desde la configuracion publicada por el recorder.
function fillHistorySettings(config) {
    $("#history_enabled").prop("checked", config.enabled);
    $("#history_period").val(config.samplePeriod);
    $("#history_voltageFull").prop("checked", config.voltageMode === "full");
    $("#history_activePower").val(config.activePower);
    $("#history_reactivePower").val(config.reactivePower);
    $("#history_apparentPower").val(config.apparentPower);
    $("#history_powerFactor").prop("checked", config.powerFactor);
    $("#history_frequency").val(config.frequency);
    $("#history_voltageThd").prop("checked", config.voltageThd);
    $("#history_currentThd").prop("checked", config.currentThd);
    $("#history_voltageAngles").prop("checked", config.voltageAngles);
    $("#history_battery").prop("checked", config.battery);
    $("#history_energy").prop("checked", config.energy);
    $("#history_energyExtraMode").val(String(config.energyExtraMode || 0)).prop("disabled", !config.energy);
    updateHistoryEstimate();
}

// Muestra el tamano de muestra y la autonomia estimada; estimate contiene el calculo del firmware.
function renderHistoryEstimate(estimate) {
    $("#historyProfileInfo").text("Muestra de " + estimate.sampleSize + " bytes · " + estimate.samplesPerPage + " muestras por página.");
    if (!estimate.ready) {
        $("#historyCapacityInfo").text("Duración no disponible: la partición histórica no está lista.");
        return;
    }
    const duration = formatHistoryRetention(Number(estimate.duration));
    const enabled = $("#history_enabled").prop("checked");
    const label = enabled ? "Autonomía estimada al llenar la partición" : "Con el registro deshabilitado, este perfil permitiría";
    const samples = Number(estimate.estimatedSamples).toLocaleString("es-AR");
    $("#historyCapacityInfo").text(label + ": " + duration + " · " + samples + " muestras · " + estimate.usableBlocks + " bloques disponibles.");
}

// Formatea la retencion calculada en segundos; durationSeconds proviene del firmware.
function formatHistoryRetention(durationSeconds) {
    if (!Number.isFinite(durationSeconds) || durationSeconds <= 0) return "sin capacidad estimable";
    const totalSeconds = Math.floor(durationSeconds);
    const days = Math.floor(totalSeconds / 86400);
    if (days) return days + " días " + Math.floor(totalSeconds % 86400 / 3600) + " h";
    const hours = Math.floor(totalSeconds / 3600);
    if (hours) return hours + " h " + Math.floor(totalSeconds % 3600 / 60) + " min";
    const minutes = Math.floor(totalSeconds / 60);
    if (minutes) return minutes + " min " + totalSeconds % 60 + " s";
    return durationSeconds.toFixed(1) + " s";
}

// Reune las opciones visibles del formulario en el formato usado por los endpoints.
function getHistoryConfigOptions() {
    const energy = $("#history_energy").prop("checked");
    return {
        samplePeriod: Number($("#history_period").val()),
        voltageMode: $("#history_voltageFull").prop("checked") ? "full" : "basic",
        activePower: $("#history_activePower").val(),
        reactivePower: $("#history_reactivePower").val(),
        apparentPower: $("#history_apparentPower").val(),
        powerFactor: $("#history_powerFactor").prop("checked"),
        voltageThd: $("#history_voltageThd").prop("checked"),
        currentThd: $("#history_currentThd").prop("checked"),
        frequency: $("#history_frequency").val(),
        voltageAngles: $("#history_voltageAngles").prop("checked"),
        battery: $("#history_battery").prop("checked"),
        energy,
        energyExtraMode: energy ? Number($("#history_energyExtraMode").val()) : 0,
    };
}

// Consulta de inmediato la estimacion para la seleccion actual; no guarda cambios y no recibe parametros.
function updateHistoryEstimate() {
    const revision = ++historyEstimateRevision;
    if (historyEstimateRequest) {
        historyEstimateRequest.abort();
        historyEstimateRequest = null;
    }
    const seconds = Number($("#history_period").val());
    if (!Number.isFinite(seconds) || seconds < 0.1 || seconds > 300) {
        $("#historyCapacityInfo").text("Ingresá un período entre 0,1 y 300 segundos para estimar la duración.");
        return;
    }
    const options = getHistoryConfigOptions();
    $("#historyCapacityInfo").text("Calculando capacidad con el medidor...");
    historyEstimateRequest = $.ajax({
        url: board.IP + "/history/estimate",
        data: options,
        cache: false,
        timeout: Math.max(board.timeout, 3000),
    }).done((estimate) => {
        if (revision === historyEstimateRevision) renderHistoryEstimate(estimate);
    }).fail((xhr, status) => {
        if (revision === historyEstimateRevision && status !== "abort")
            $("#historyCapacityInfo").text("No se pudo calcular la duración estimada.");
    }).always(() => {
        if (revision === historyEstimateRevision) historyEstimateRequest = null;
    });
}

// Lee una vez el perfil del histórico; sin parametros.
function loadHistorySettings() {
    $("#historySettingsFields").prop("disabled", true);
    $("#historySettingsStatus").text("Cargando...");
    return $.ajax({url: board.IP + "/history/config", cache: false, timeout: Math.max(board.timeout, 3000)})
        .done((config) => {
            fillHistorySettings(config);
            $("#historySettingsFields").prop("disabled", false);
            $("#historySettingsStatus").text("Perfil cargado.");
        })
        .fail(() => $("#historySettingsStatus").text("No se pudo cargar el perfil histórico."));
}

// Convierte options a bitmap solo para conservar el formato de respaldos existentes.
function historyOptionsToGroupConfig(options) {
    const mode = value => ({off: 0, compact: 1, full: 2})[String(value).toLowerCase()] || 0;
    let config = String(options.voltageMode).toLowerCase() === "full" ? 1 : 0;
    config |= mode(options.activePower) << 1;
    config |= mode(options.reactivePower) << 3;
    config |= mode(options.apparentPower) << 5;
    if (options.powerFactor) config |= 1 << 7;
    if (options.voltageThd) config |= 1 << 8;
    if (options.currentThd) config |= 1 << 9;
    config |= mode(options.frequency) << 10;
    if (options.voltageAngles) config |= 1 << 12;
    if (options.battery) config |= 1 << 13;
    if (options.energy) config |= (1 << 14) | (Number(options.energyExtraMode) << 15);
    return config;
}

// Convierte values del respaldo a opciones legibles para enviar al firmware.
function historyBackupToOptions(values) {
    const config = Number(values.groupConfig);
    const mode = shift => ({0: "off", 1: "compact", 2: "full"}[(config >> shift) & 3] || "off");
    const energy = Boolean(config & (1 << 14));
    return {
        enabled: settingsBoolean(values.enabled),
        samplePeriod: Number(values.samplePeriodDeciseconds) / 10,
        voltageMode: config & 1 ? "full" : "basic",
        activePower: mode(1),
        reactivePower: mode(3),
        apparentPower: mode(5),
        powerFactor: Boolean(config & (1 << 7)),
        voltageThd: Boolean(config & (1 << 8)),
        currentThd: Boolean(config & (1 << 9)),
        frequency: mode(10),
        voltageAngles: Boolean(config & (1 << 12)),
        battery: Boolean(config & (1 << 13)),
        energy,
        energyExtraMode: energy ? (config >> 15) & 7 : 0,
    };
}

// Valida y guarda el perfil del histórico; event contiene el envio del formulario.
async function saveHistorySettings(event) {
    event.preventDefault();
    const form = document.getElementById("historySettingsForm");
    if (!form.reportValidity()) return;
    const period = Number($("#history_period").val());
    if (!Number.isFinite(period) || period < 0.1 || period > 300 || Math.abs(period * 10 - Math.round(period * 10)) > 0.001) {
        $("#historySettingsStatus").text("El período debe estar entre 0,1 y 300 segundos.");
        return;
    }
    const values = getHistoryConfigOptions();
    values.enabled = $("#history_enabled").prop("checked");
    $("#historySettingsFields, #historySettingsForm button").prop("disabled", true);
    $("#historySettingsStatus").text("Guardando y cerrando la página actual...");
    try {
        const config=await saveHistoryConfig(values);
        fillHistorySettings(config);
        $("#historySettingsStatus").text("Guardado y aplicado sin reiniciar. Las muestras nuevas usan este perfil.");
    } catch(error) {$("#historySettingsStatus").text(error.message||error.responseText||"No se pudo guardar el perfil histórico.");}
    finally {$("#historySettingsFields, #historySettingsForm button").prop("disabled", false);}
}

// Guarda values del perfil y verifica el perfil activo al finalizar el consumidor.
async function saveHistoryConfig(values) {
    const response=await $.ajax({type:'POST',url:board.IP+'/history/config',data:values,timeout:Math.max(board.timeout,5000)});
    await waitHistorySettings(response);
    const config=await waitHistorySettings(await $.ajax({url:board.IP+'/history/config',cache:false,timeout:5000}));
    assertSettingsMatch(values,config);
    return config;
}

// Espera que el recorder publique el perfil; config contiene la respuesta inicial de guardado.
async function waitHistorySettings(config) {
    const deadline=Date.now()+30000;
    while(config.apply==='wait'||config.apply==='save') {
        if(Date.now()>=deadline)throw Error('El cambio sigue pendiente; espere a que termine la captura activa y recargue.');
        await new Promise(resolve=>setTimeout(resolve,500));
        config=await $.ajax({url:board.IP+'/history/config',cache:false,timeout:5000});
    }
    if(config.apply==='fail')throw Error('No se pudo guardar el perfil. Sigue activo el anterior.');
    return config;
}

// Crea el encabezado comun de una seccion; title y description son textos, actions contiene sus botones.
function createSettingsHeaderHtml(title, description, actions) {
    return '<header class="settingsSectionHeader' + (actions.includes("settingsIconButton") ? " settingsCompactHeader" : "") + '"><div><h2>' + title + '</h2><p>' + description +
        '</p></div><div class="settingsActions">' + actions + '</div></header>';
}

// Completa el formulario con values recibidos del medidor.
function fillGridSettings(values) {
    $("#gridCapturePolicy").text("Máximo puntual: " + (values.pointLimitMs / 1000).toFixed(2) + " s · " + values.capturePages + " páginas · 1 s posterior. Si persiste, se registra como estado sostenido.");
    $("#gridSettingsForm input[name]").each(function () {
        if (this.type === "checkbox") $(this).prop("checked", Boolean(Number(values[this.name])));
        else $(this).val(values[this.name]);
    });
}

let eventsConfig = null, eventsStatus = null, eventsLoad = null, eventsSaving = false;
const eventGridFields = {lowR:"volt.lowR",lowS:"volt.lowS",lowT:"volt.lowT",highR:"volt.highR",highS:"volt.highS",highT:"volt.highT",
    noVoltage:"volt.loss",hysteresis:"volt.hyst",lossConfirmMs:"time.loss",recoveryConfirmMs:"time.back",abnormalConfirmMs:"time.abnormal",
    neutralHigh:"neut.high",neutralHysteresis:"neut.hyst",neutralConfirmMs:"neut.wait",neutralRecoveryMs:"neut.back"};
const eventPointFields = {dipEnabled:"rec.dip",swellEnabled:"rec.swell",overcurrent:"rec.oc",dipMs:"time.dip",swellMs:"time.swell",ocMs:"time.oc",ocCurrent:"curr.high",ocHyst:"curr.hyst"};

// Consulta path con plazo; options aporta metodo/cuerpo y 204 representa estado sin cambios.
async function requestEvents(path, options = {}) {
    const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 15000);
    try {
        const response = await fetch(board.IP + path, {...options, cache:"no-store", signal:controller.signal});
        if (response.status === 204) return null;
        const value = await response.json();
        if (!response.ok) {
            const messages={rev_conflict:"La configuración cambió en otro cliente. Recargue antes de guardar.",
                save_busy:"Ya hay un guardado pendiente en la placa. Espere y recargue.",invalid_config:"Los límites o tiempos de eventos son incompatibles."};
            throw Error((value.key ? value.key + ": " : "") + (messages[value.error] || value.error || "HTTP " + response.status));
        }
        return value;
    } finally { clearTimeout(timer); }
}

// Lee la configuracion completa; force evita reutilizar el snapshot de esta vista.
async function readEventsConfig(force = false) {
    if (!eventsConfig || force) eventsConfig = await requestEvents("/events/config");
    if (!["volt","time","neut","curr","rec","wave"].every(key=>eventsConfig[key]) || !Number.isInteger(eventsConfig.rev)) throw Error("Configuración de eventos incompleta.");
    return eventsConfig;
}

// Consulta cambios respecto del ultimo seq; sin parametros y sin recargar los campos editables.
async function readEventsStatus() {
    const value = await requestEvents("/events/status" + (eventsStatus ? "?since=" + eventsStatus.seq : ""));
    if (value) eventsStatus = value;
    if (!eventsStatus) throw Error("No se pudo leer el estado de eventos.");
    return eventsStatus;
}

// Convierte config agrupada a las claves usadas por los formularios; fields define el mapeo.
function flattenEventFields(config, fields) {
    return Object.fromEntries(Object.entries(fields).map(([key,path])=>{const [group,name]=path.split("."); return [key,config[group][name]];}));
}

// Convierte values del formulario en parametros cortos usando fields como mapeo.
function mapEventFields(values, fields) {
    return Object.fromEntries(Object.entries(fields).filter(([key])=>Object.hasOwn(values,key)).map(([key,path])=>
        [path,path.startsWith('rec.')?Number(settingsBoolean(values[key])):Number(values[key])]));
}

// Adapta config/status a la tabla Wave existente sin consultas adicionales.
function eventWaveValues(config, status) {
    const wave = status.wave;
    return {preMs:config.wave.pre,saveEnabled:config.wave.on,pointMask:(config.wave.dip?1:0)|(config.wave.swell?2:0)|(config.wave.oc?4:0),
        enabled:wave.on,capturing:status.busy,actualPreMs:wave.pre,reservedBytes:wave.ram,storedCaptures:wave.count,
        occupiedSlots:wave.used,totalSlots:wave.slots,committed:wave.done,orphans:wave.orph,pendingLinks:wave.pending,
        gaps:wave.gaps,droppedTriggers:wave.drop,failedWrites:wave.fail,erasedBlocks:wave.free};
}

// Carga los tres bloques con un GET de configuracion y uno de estado; comparte cargas simultaneas.
async function loadUnifiedEventSettings() {
    if (eventsLoad) return eventsLoad;
    eventsLoad = (async () => {
        $("#gridSettingsFields, #captureFields, #btnSaveMeterOptions, #gridSettingsForm button[type=submit], #waveConfigForm button[type=submit], #wavePreMs, #waveSaveEnabled").prop("disabled",true);
        $("#waveConfigForm").data("loaded",false);
        try {
            const config = await readEventsConfig(true), status = await readEventsStatus();
            const grid = flattenEventFields(config,eventGridFields);
            grid.pointLimitMs = status.lim.point; grid.capturePages = status.lim.pages;
            fillGridSettings(grid); fillMeterOptions(flattenEventFields(config,eventPointFields));
            await loadWaveConfig(eventWaveValues(config,status));
            $("#gridSettingsFields, #captureFields, #btnSaveMeterOptions, #gridSettingsForm button[type=submit]").prop("disabled",false);
            updatePointWaveControls();
            $("#gridSettingsStatus").text("Ajustes cargados."); setSettingsStatus("Opciones cargadas.",false,"capture");
        } catch (error) {
            eventsConfig = null;
            $("#gridSettingsStatus").text("No se pudieron cargar los ajustes de red.");
            $("#waveConfigStatus").text("No se pudo cargar la configuración. Recargue este bloque.");
            setSettingsStatus("No se pudieron cargar las opciones de eventos. Recargue este bloque.",true,"capture");
            throw error;
        }
    })();
    try { return await eventsLoad; } finally { eventsLoad = null; }
}

// Envia values URL-encoded y espera la revision confirmada, sin otro POST de recarga.
async function saveEventConfig(values) {
    if (eventsSaving) throw Error("Ya hay un guardado en curso.");
    eventsSaving = true;
    $("#gridSettingsFields, #captureFields, #gridSettingsForm button, #waveConfigForm button, #wavePreMs, #waveSaveEnabled, #btnSaveMeterOptions, #btnLoadSettings").prop("disabled",true);
    try {
        if (!eventsConfig) throw Error("Recargue la configuración antes de guardar.");
        const current = eventsConfig;
        const body = new URLSearchParams({rev:current.rev,...values});
        const queued = await requestEvents("/events/config",{method:"POST",body});
        const deadline = Date.now()+30000;
        while (Date.now()<deadline) {
            const status = await readEventsStatus();
            if (status.cfg === queued.rev) {
                const confirmed=await readEventsConfig(true);
                if(confirmed.rev!==queued.rev)throw Error('La configuración cambió durante la verificación. Recargue los ajustes.');
                const actual=Object.fromEntries(Object.keys(values).map(path=>{const [group,key]=path.split('.');return [path,confirmed[group]?.[key]];}));
                assertSettingsMatch(values,actual);
                return confirmed;
            }
            if (status.save.state === "fail" && status.save.rev === queued.rev) throw Error("Falló el guardado NVS; se conserva la configuración anterior.");
            if (status.cfg !== current.rev) throw Error("La configuración cambió desde otro cliente. Recargue los ajustes.");
            await new Promise(resolve=>setTimeout(resolve,300));
        }
        throw Error("Guardado pendiente: espere a que termine la captura y recargue para comprobarlo.");
    } catch (error) { eventsConfig=null; throw error; }
    finally {
        eventsSaving=false;
        $("#gridSettingsFields, #captureFields, #gridSettingsForm button, #waveConfigForm button, #wavePreMs, #waveSaveEnabled, #btnSaveMeterOptions, #btnLoadSettings").prop("disabled",false);
        updatePointWaveControls();
    }
}

// Lee limites junto con eventos y Wave; sin parametros.
function loadGridSettings() { return loadUnifiedEventSettings(); }

// Valida y guarda el formulario; event es el envio del formulario web.
function saveGridSettings(event) {
    event.preventDefault();
    const form = document.getElementById("gridSettingsForm");
    if (!form.reportValidity()) return;
    const values = Object.fromEntries(new FormData(form));
    try { validateGridValues(values); validateCaptureValues(values); }
    catch(error) { $('#gridSettingsStatus').text(error.message); return; }
    const absence = Number(values.noVoltage), hysteresis = Number(values.hysteresis);
    if (Number(values.ocHyst) >= Number(values.ocCurrent)) {
        $("#gridSettingsStatus").text("La histéresis de corriente debe ser menor que el umbral de sobrecorriente."); return;
    }
    if (Number(values.neutralHysteresis) >= Number(values.neutralHigh)) {
        $("#gridSettingsStatus").text("La histéresis de neutro debe ser menor que el umbral."); return;
    }
    if (["R", "S", "T"].some((phase) => Number(values["low" + phase]) <= absence + hysteresis ||
        Number(values["high" + phase]) <= Number(values["low" + phase]) + 2 * hysteresis)) {
        $("#gridSettingsStatus").text("Los límites deben dejar espacio para la histéresis: sin tensión < bajo < alto.");
        return;
    }
    $("#gridSettingsFields, #btnLoadGridSettings").prop("disabled", true);
    $("#gridSettingsStatus").text("Guardando...");
    saveEventConfig(mapEventFields(values,{...eventGridFields,ocCurrent:"curr.high",ocHyst:"curr.hyst"}))
        .then(() => {
            fillGridSettings({...flattenEventFields(eventsConfig,eventGridFields),ocCurrent:eventsConfig.curr.high,ocHyst:eventsConfig.curr.hyst,
                pointLimitMs:eventsStatus.lim.point,capturePages:eventsStatus.lim.pages});
            $("#gridSettingsStatus").text("Guardado. Los estados se confirman nuevamente con estos ajustes.");
        })
        .catch(error => $("#gridSettingsStatus").text(error.message || "No se pudo confirmar el guardado. Recargue los ajustes."))
        .finally(() => $("#gridSettingsFields, #btnLoadGridSettings").prop("disabled", false));
}

// Crea un boton compacto; id/label lo identifican, save elige el icono y type su accion HTML.
function createSettingsIconButton(id, label, save, type = "button") {
    const path = save ? '<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h12l4 4v12a2 2 0 0 1-2 2Z"/><path d="M17 21v-8H7v8M7 3v5h8"/>' : '<path d="M20 7v5h-5M4 17v-5h5"/><path d="M6.1 7a7 7 0 0 1 11.6-2L20 8M4 16l2.3 3A7 7 0 0 0 18 17"/>';
    return '<button' + (id ? ' id="' + id + '"' : '') + ' type="' + type + '" class="btn btn-sm btn-outline-' + (save ? "success" : "secondary") + ' settingsIconButton" title="' + label + '" aria-label="' + label + '"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true">' + path + '</svg></button>';
}

// Crea el acceso a la ayuda del bloque indicado por key; no envia formularios.
function createEventSettingsHelpButton(key) {
    return '<button type="button" class="btn btn-sm btn-outline-secondary settingsIconButton eventSettingsHelpButton" data-help="' + key + '" title="Ayuda de este bloque" aria-label="Ayuda de este bloque"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><circle cx="12" cy="12" r="9"/><path d="M9.1 9a3 3 0 0 1 5.8 1c0 2-3 2-3 4M12 17h.01"/></svg></button>';
}

// Muestra ayuda contextual en una ventana; key identifica limites, deteccion u oscilografias.
function showEventSettingsHelp(key) {
    const help = {
        limits: ["Límites y estados de red", [
            "Baja / DIP y Alta / Swell definen los umbrales de tensión de cada fase. Copiar R a S/T copia únicamente esos dos valores de R a las otras fases.",
            "Sobrecorriente es un único umbral, común a las corrientes de R/S/T. Su histéresis evita cambios repetidos cerca del límite. Estos ajustes se guardan con el botón de este bloque.",
            "Sin tensión define la pérdida de fase; las confirmaciones de sin tensión y normalidad filtran cambios demasiado breves. Neutro-tierra tiene su propio umbral, histéresis y tiempos.",
            "Los DIP y swell usan los mismos límites de tensión. Si la condición persiste más que la duración máxima puntual, se registra como estado sostenido. Los estados y cortes se registran siempre."
        ]],
        points: ["Detección de eventos de línea", [
            "Configure los umbrales de tensión y corriente en Límites por fase. Aquí seleccione el tiempo mínimo de detección de cada evento y si desea registrar el evento y guardar su oscilografía.",
            "Confirmación es el tiempo mínimo que la señal debe permanecer fuera del umbral: de 10 a 2000 ms, en pasos de 10 ms. La histéresis y la confirmación de normalidad determinan la recuperación.",
            "Un DIP, swell o pico de corriente se registra como puntual al recuperar la normalidad, si no excede la duración máxima puntual. Si persiste, se trata como estado sostenido; apagar Registro no deshabilita los estados ni los cortes.",
            "Osc. requiere Registro para ese tipo de evento y la habilitación general de oscilografías. Al apagar Registro se desmarca y deshabilita Osc.; al reactivarlo puede elegir nuevamente si desea oscilografía. Guarde para aplicar los cambios."
        ]],
        wave: ["Oscilografías: captura y almacenamiento", [
            "Al cruzar un umbral se inicia una captura provisional, antes de confirmar el evento. Incluye las muestras previas disponibles y continúa hasta completar 744 ms. Una captura puede reunir disparos cercanos.",
            "Se guardan voltajes R/S/T y corrientes R/S/T/N a 8 kHz. No hay oscilografía de voltaje neutro-tierra. Cada captura ocupa 64 KiB: 31 páginas de 24 ms y una página final de enlaces a eventos FAST confirmados.",
            "La captura solo se cierra si recibe un enlace FAST durable a tiempo y de un tipo habilitado. Puede terminar antes de la clasificación definitiva del evento; sin enlace queda huérfana hasta reutilizar ese espacio. Una captura compartida puede conservarse por otro tipo habilitado.",
            "Antes del evento define la reserva de señal previa en RAM. Cambiar ese tiempo requiere reiniciar. La habilitación general controla nuevas oscilografías de estados, cortes, neutro y puntuales; no borra las existentes ni desactiva los registros FAST. Las capturas iniciadas terminan normalmente.",
            "La NAND se reutiliza circularmente y prepara bloques borrados en segundo plano. Destinos ocupados incluye provisionales y huérfanas, no solo capturas confirmadas. Los contadores marcados desde este arranque se reinician al arrancar la placa."
        ]]
    }[key];
    if (!help) return;
    $("#eventSettingsHelpTitle").text(help[0]);
    $("#eventSettingsHelpBody").empty();
    help[1].forEach(text => $("<p>").text(text).appendTo("#eventSettingsHelpBody"));
    bootstrap.Modal.getOrCreateInstance(document.getElementById("eventSettingsHelp")).show();
}

// Ajusta cada selector Osc. segun su Registro y el estado del formulario; sin parametros.
function updatePointWaveControls() {
    [["dipEnabled","waveDip"],["swellEnabled","waveSwell"],["overcurrent","waveCurrent"]].forEach(([record,id]) => {
        const enabled = $("#opt_meter_" + record).prop("checked"), input = $("#" + id);
        if (!enabled) input.prop("checked",false);
        input.prop("disabled",!enabled || eventsSaving || $("#captureFields").prop("disabled"));
        input.attr("title",enabled ? "Guardar oscilografía de este evento" : "Active Registro para habilitar la oscilografía");
    });
}

// Crea una fila con registro, oscilografia y confirmacion; key/waveKey/msKey son sus claves y label el tipo.
function createPointSettingsRow(label, key, waveKey, msKey) {
    return '<tr><th scope="row">' + label + '</th><td><input type="checkbox" class="form-check-input optionInput" id="opt_meter_' + key + '" data-key="' + key + '" data-type="bool" aria-label="Registrar ' + label + '"></td>' +
        '<td><input type="checkbox" class="form-check-input" id="' + waveKey + '" aria-label="Oscilografía de ' + label + '"></td>' +
        '<td><input class="form-control form-control-sm optionInput" id="opt_meter_' + msKey + '" data-key="' + msKey + '" data-type="int" type="number" min="10" max="2000" step="10" aria-label="Confirmación de ' + label + ' en ms"></td></tr>';
}

// Crea el bloque compacto de deteccion y guardado; no recibe parametros.
function createMeterSettingsHtml() {
    const actions = createEventSettingsHelpButton("points") + createSettingsIconButton("btnLoadSettings", "Recargar eventos", false) + createSettingsIconButton("btnSaveMeterOptions", "Guardar eventos y oscilografías", true);
    let html = '<div id="settings_capture" class="settingsSection border-top mt-3 pt-3">' + createSettingsHeaderHtml("Detección de eventos de línea", "DIP, swell y sobrecorriente: seleccione los tiempos mínimos de detección y si desea registrar cada evento y generar su oscilografía (umbrales en Límites por fase).", actions);
    html += '<div class="settingsStatus settingsStatusLine small text-muted" role="status"></div>';
    html += '<fieldset id="captureFields" disabled><table class="table table-sm align-middle settingsPointTable"><thead><tr><th>Evento</th><th title="Guardar registro FAST">Registro</th><th title="Guardar oscilografía vinculada al registro">Osc.</th><th>Conf. ms</th></tr></thead><tbody>' +
        createPointSettingsRow("DIP", "dipEnabled", "waveDip", "dipMs") + createPointSettingsRow("Swell", "swellEnabled", "waveSwell", "swellMs") +
        createPointSettingsRow("Sobrecorriente", "overcurrent", "waveCurrent", "ocMs") + '</tbody></table></fieldset>';
    html += '<form id="waveConfigForm" class="border-top mt-3 pt-2"><div class="d-flex align-items-center justify-content-between gap-2"><strong>Oscilografías NAND</strong><div class="d-flex gap-1">' +
        createEventSettingsHelpButton("wave") + createSettingsIconButton("waveConfigRefresh", "Recargar oscilografías", false) + createSettingsIconButton("", "Guardar oscilografías", true, "submit") + '</div></div>' +
        '<p class="small text-muted mb-1 mt-1">Guarda la forma de onda de tensión y corriente alrededor del disparo para analizar el detalle del evento, además de sus muestras FAST.</p>' +
        '<div class="waveConfigToolbar"><div class="wavePreControl"><label for="wavePreMs">Antes del evento</label><select id="wavePreMs" class="form-select form-select-sm" aria-describedby="wavePreHelp">' +
        [48,96].map(value => '<option value="' + value + '">' + value + ' ms</option>').join("") +
        '</select></div><label class="form-check form-switch mb-0"><input id="waveSaveEnabled" class="form-check-input" type="checkbox" checked>Habilitar oscilografías</label></div>' +
        '<p id="wavePreHelp" class="small text-muted mt-1 mb-1">Tiempo de señal anterior al disparo que se conserva en memoria. El resto de la captura muestra lo ocurrido después. Cambiar este tiempo requiere reiniciar.</p>' +
        '<div id="waveConfigStatus" class="small text-muted mt-2" role="status"></div><dl id="waveConfigMetrics" class="waveConfigMetrics small"></dl>' +
        '</form></div>';
    return html;
}

// Consulta reserva y contadores Wave, sin descargar muestras; sin parametros.
async function loadWaveConfig(value) {
    if (!value || !Object.hasOwn(value,"preMs")) return loadUnifiedEventSettings();
    $("#waveConfigForm").data("loaded", false);
    $("#waveConfigForm button[type=submit], #wavePreMs, #waveSaveEnabled").prop("disabled", true);
    try {
        if (typeof value.saveEnabled !== "boolean") throw Error("Actualice el firmware para configurar el guardado de oscilografías.");
        $("#wavePreMs").val(value.preMs);
        $("#waveSaveEnabled").prop("checked", value.saveEnabled !== false);
        if (!Number.isInteger(value.pointMask)) throw Error("Actualice el firmware para configurar oscilografías por tipo.");
        ["waveDip", "waveSwell", "waveCurrent"].forEach((id, index) => $("#" + id).prop("checked", !!(value.pointMask & (1 << index))));
        $("#waveConfigStatus").text(!value.enabled ? "Capturador no disponible" : value.capturing ? "Capturando" : value.saveEnabled === false ? "Nuevas capturas deshabilitadas" : "Listo para capturar");
        const metrics = [["Oscilografías guardadas", value.storedCaptures ?? "—"], ["Destinos ocupados¹", value.totalSlots ? value.occupiedSlots + " / " + value.totalSlots : "—"],
            ["Previo efectivo", value.actualPreMs + " ms"], ["RAM reservada", (value.reservedBytes / 1024).toFixed(1) + " KiB"],
            ["Confirmadas²", value.committed], ["Huérfanas²", value.orphans], ["Pendientes", value.pendingLinks],
            ["Huecos²", value.gaps], ["Disparos descartados²", value.droppedTriggers], ["Fallos NAND²", value.failedWrites], ["Bloques libres", value.erasedBlocks]];
        $("#waveConfigMetrics").empty();
        for (const [label, count] of metrics) $("#waveConfigMetrics").append($("<div>").append($("<dt>").text(label), $("<dd>").text(count ?? "—")));
        $("#waveConfigMetrics").append($('<small class="waveMetricsNote">').text("¹ Incluye provisionales y huérfanas. ² Desde este arranque."));
        $("#waveConfigForm button[type=submit], #wavePreMs, #waveSaveEnabled").prop("disabled", false);
        $("#waveConfigForm").data("loaded", true);
        updatePointWaveControls();
    } catch (error) { $("#waveConfigStatus").text(error.message); throw error; }
}

// Reune las habilitaciones Wave y el previo del formulario; exige una lectura valida previa.
function getWaveConfigValues() {
    if (!$("#waveConfigForm").data("loaded")) throw Error("Recargue la configuración de oscilografías antes de guardar.");
    const pointMask = [["waveDip","dipEnabled"], ["waveSwell","swellEnabled"], ["waveCurrent","overcurrent"]].reduce((mask, [id,record], index) => mask | ($("#" + id).prop("checked") && $("#opt_meter_" + record).prop("checked") ? 1 << index : 0), 0);
    return new URLSearchParams({preMs: $("#wavePreMs").val(), saveEnabled: $("#waveSaveEnabled").prop("checked") ? "1" : "0", pointMask});
}

// Persiste la prehistoria del formulario; event cancela la navegacion y muestra que requiere reinicio.
async function saveWaveConfig(event) {
    event.preventDefault();
    $("#waveConfigForm button").prop("disabled", true);
    try {
        const result=await saveEventConfig(waveFormEventFields());
        $("#waveConfigStatus").text(result.restartRequired?"Guardado. Habilitación aplicada; reinicie para aplicar el nuevo tiempo previo.":"Guardado y aplicado sin reiniciar.");
    } catch (error) { $("#waveConfigStatus").text(error.message); }
    finally { $("#waveConfigForm button").prop("disabled", false); }
}

// Obtiene las opciones Wave del formulario como parametros del endpoint unificado; sin parametros.
function waveFormEventFields() {
    const values = getWaveConfigValues(), mask=Number(values.get("pointMask"));
    return {"wave.pre":Number(values.get("preMs")),"wave.on":Number(values.get("saveEnabled")),
        "wave.dip":mask&1?1:0,"wave.swell":mask&2?1:0,"wave.oc":mask&4?1:0};
}

// Crea el bloque compacto de calibración.
function createCalibrationSettingsHtml() {
    const actions = createSettingsIconButton('btnLoadCalibration','Actualizar calibración',false) +
        createSettingsIconButton('btnSaveCalibration','Guardar calibración',true).replace('type="button"','type="button" disabled') +
        '<button class="btn btn-sm btn-warning" id="btnRestoreCalibration" type="button">Por defecto</button>';
    let html = '<div class="settingsSection">' + createSettingsHeaderHtml("Calibración", "Registros ADE9000 almacenados en hexadecimal.", actions);
    html += '<div class="settingsStatus settingsStatusLine small text-muted" role="status"></div>';
    html += '<fieldset id="calibrationFields" disabled><div class="table-responsive settingsGroup"><table class="table table-sm table-bordered align-middle mb-0 settingsTable calibrationTable">';
    html += "<thead><tr><th>Registro</th><th>Fase R</th><th>Fase S</th><th>Fase T</th><th>Neutro</th></tr></thead><tbody>";
    getCalibrationRows().forEach((row) => {
        html += '<tr><td class="small">' + row.name + "</td>";
        ["r", "s", "t", "n"].forEach((phase) => {
            html += "<td>" + createCalibrationCellHtml(row[phase]) + "</td>";
        });
        html += "</tr>";
    });
    html += '</tbody></table></div></fieldset><div class="settingsGroup mt-3"><div class="d-flex align-items-center justify-content-between gap-2"><h3 class="mb-0">Neutro a tierra · BL0942</h3>'+
        createSettingsIconButton('neutralCalibrationReload','Recargar calibración del neutro',false)+'</div>'+
        '<p class="small text-muted mt-2 mb-2">Use la misma tensión estable (50 a 300 V) en el canal del neutro y en la fase ADE9000 de referencia. Promedia un segundo y guarda los factores RMS y FAST; se aplican sin reiniciar.</p>'+
        '<div class="d-flex align-items-center flex-wrap gap-2"><label for="neutralCalibrationPhase">Referencia</label><select id="neutralCalibrationPhase" class="form-select form-select-sm w-auto"><option value="r">Fase R</option><option value="s">Fase S</option><option value="t">Fase T</option></select>'+
        '<button id="neutralCalibrationStart" type="button" class="btn btn-sm btn-success" disabled>Calibrar</button></div>'+
        '<div id="neutralCalibrationStatus" class="small text-muted mt-2" role="status">Cargue la calibración para consultar los factores activos.</div>'+
        '<div class="table-responsive"><table class="table table-sm mt-2 mb-0"><thead><tr><th>Factor RMS</th><th>Factor FAST</th><th>Referencia</th><th>Muestras FAST / RMS</th></tr></thead><tbody><tr>'+['Rms','Fast','Reference','Samples'].map(key=>'<td id="neutralCalibration'+key+'">—</td>').join('')+'</tr></tbody></table></div></div></div>';
    return html;
}

// Devuelve las filas compactas de calibración.
function getCalibrationRows() {
    return [
        {name: "Calibración de fase", r: "APHCAL0", s: "BPHCAL0", t: "CPHCAL0", n: "NPHCAL"},
        {name: "Ganancia corriente", r: "AIGAIN", s: "BIGAIN", t: "CIGAIN", n: "NIGAIN"},
        {name: "Ganancia voltaje", r: "AVGAIN", s: "BVGAIN", t: "CVGAIN"},
        {name: "Offset RMS corriente", r: "AIRMSOS", s: "BIRMSOS", t: "CIRMSOS", n: "NIRMSOS"},
        {name: "Offset RMS voltaje", r: "AVRMSOS", s: "BVRMSOS", t: "CVRMSOS"},
        {name: "Offset fundamental corriente", r: "AIFRMSOS", s: "BIFRMSOS", t: "CIFRMSOS"},
        {name: "Offset fundamental voltaje", r: "AVFRMSOS", s: "BVFRMSOS", t: "CVFRMSOS"},
        {name: "Offset RMS 1 ciclo corriente", r: "AIRMSONEOS", s: "BIRMSONEOS", t: "CIRMSONEOS", n: "NIRMSONEOS"},
        {name: "Offset RMS 1 ciclo voltaje", r: "AVRMSONEOS", s: "BVRMSONEOS", t: "CVRMSONEOS"},
        {name: "Offset RMS 10/12 corriente", r: "AIRMS1012OS", s: "BIRMS1012OS", t: "CIRMS1012OS", n: "NIRMS1012OS"},
        {name: "Offset RMS 10/12 voltaje", r: "AVRMS1012OS", s: "BVRMS1012OS", t: "CVRMS1012OS"},
    ];
}

// Crea una celda de calibración.
// key: clave del registro de calibración.
function createCalibrationCellHtml(key) {
    if (!key) return '<span class="text-muted">-</span>';
    const id = getOptionInputId("cal", key);
    return '<div class="small text-muted">' + key + '</div><input class="form-control form-control-sm optionInput font-monospace" type="text" id="' + id + '" data-key="' + key + '" data-type="hex">';
}

// Crea los grupos de opciones del medidor.
// groups: lista de grupos. prefix: prefijo usado en los ids.
function createOptionGroupsHtml(groups, prefix) {
    let html = "";
    groups.forEach((group) => {
        html += '<fieldset class="settingsGroup optionGroup"><legend>' + group.title + '</legend><div class="settingsFieldsGrid">';
        if (group.description) html += '<p class="small text-muted settingsOptionDescription">' + group.description + '</p>';
        group.fields.forEach((field) => {
            html += createOptionInputHtml(field, prefix);
        });
        html += "</div></fieldset>";
    });
    return html;
}

// Crea un input para una opción.
// field: definición del campo. prefix: prefijo usado en el id.
function createOptionInputHtml(field, prefix) {
    const id = getOptionInputId(prefix, field.key);
    if (field.type == "bool") {
        return '<div class="form-check form-switch settingsSwitch"><input class="form-check-input optionInput" type="checkbox" id="' + id + '" data-key="' + field.key + '" data-type="' + field.type + '"><label class="form-check-label" for="' + id + '">' + field.label + "</label></div>";
    }
    let unit = field.unit ? '<span class="input-group-text">' + field.unit + "</span>" : "";
    const attributes = prefix !== "meter" ? 'type="text"' : field.unit === "ms" ? 'type="number" min="10" max="2000" step="10"' : 'type="number" min="0" max="' + (field.unit === "A" ? 163.82 : 500) + '" step="any"';
    return '<div class="settingsField settingsFieldStack"><label for="' + id + '">' + field.label + '</label><div class="input-group input-group-sm"><input class="form-control optionInput" ' + attributes + ' id="' + id + '" data-key="' + field.key + '" data-type="' + field.type + '">' + unit + "</div></div>";
}

// Devuelve el id de un input de opción.
// prefix: prefijo del grupo. key: clave de Preferences.
function getOptionInputId(prefix, key) {
    return "opt_" + prefix + "_" + key;
}

let settingsLoadQueue = Promise.resolve();

// Carga section solo al abrirla; force permite recargar explicitamente. Serializa consultas de ajustes.
function loadSettings(section = $("#settingsTabs .active").data("section"), force = false) {
    if (section === "application") {fillAppAppearanceSettings(); return fillHistoryCacheSettings();}
    if (!["grid", "network", "history", "storage", "capture", "calibration"].includes(section)) return;
    const panel = $("#settings_" + section);
    if (panel.data("loading") || (!force && panel.data("loaded"))) return settingsLoadQueue;
    panel.data("loading", true);
    settingsLoadQueue = settingsLoadQueue.then(async () => {
        try {
            if (section === "grid") {
                await loadGridSettings();
                $("#settings_capture").data("loaded", true);
            }
            else if (section === "network") await loadNetworkSettings();
            else if (section === "history") await loadHistorySettings();
            else if (section === "storage") await loadStorageStatus();
            else if (section === "capture") await loadCaptureSettings();
            else {
                $("#" + section + "Fields").prop("disabled", true);
                $("#btnSaveCalibration").prop("disabled", true);
                setSettingsStatus("Cargando opciones...", false, section);
                const values = await readOptions("ADE9000", getCalibrationKeys());
                if (values.error) throw new Error(values.error);
                if(getCalibrationKeys().some(key=>!settingsNumber(values[key],-2147483648,4294967295,true)))
                    throw Error('Lectura de calibración incompleta o inválida.');
                fillCalibrationOptions(values);
                $("#" + section + "Fields").prop("disabled", false);
                $("#btnSaveCalibration").prop("disabled", false);
                setSettingsStatus("Opciones cargadas.", false, section);
                await loadNeutralCalibration();
            }
            panel.data("loaded", true);
        } catch (error) {
            panel.data("loaded", false);
            if (section === "storage") $("#storageSettingsStatus").text("No se pudo cargar el estado NAND.");
            else if (section !== "grid") setSettingsStatus("No se pudieron cargar las opciones.", true, section);
        } finally {
            panel.data("loading", false);
        }
    });
    return settingsLoadQueue;
}

let neutralCalibrationRun=0;

// Consulta factores activos y acompana una calibracion en curso hasta terminar; sin parametros.
async function loadNeutralCalibration() {
    const run=++neutralCalibrationRun,deadline=Date.now()+30000;
    $('#neutralCalibrationStart,#neutralCalibrationReload,#neutralCalibrationPhase').prop('disabled',true);
    try {
        while(run===neutralCalibrationRun) {
            const value=await $.ajax({url:board.IP+'/calibration/neutral',cache:false,timeout:5000});
            $('#neutralCalibrationRms').text(Number(value.rmsFactor).toFixed(7));
            $('#neutralCalibrationFast').text(Number(value.fastFactor).toFixed(7));
            $('#neutralCalibrationReference').text(Number(value.reference).toFixed(2)+' V');
            $('#neutralCalibrationSamples').text(value.samples+' / '+value.rmsSamples);
            const errors={unstable_signal:'La señal no fue estable. Repita con tensión estable.',invalid_reference:'La referencia debe estar entre 50 y 300 V.',missing_signal:'El BL0942 no tiene señal suficiente para calibrar.',insufficient_samples:'Faltan lecturas del BL0942 o de la referencia.',nvs_write_failed:'No se pudieron guardar los factores.',invalid_factors:'Los factores calculados están fuera de rango.'};
            const states={idle:'Factores activos cargados.',queued:'Esperando las mediciones...',sampling:'Promediando un segundo de mediciones...',saving:'Guardando factores...',done:'Calibración guardada y aplicada sin reiniciar.'};
            $('#neutralCalibrationStatus').text(value.error?(errors[value.error]||'No se pudo calibrar: '+value.error):(states[value.status]||value.status));
            if(!['queued','sampling','saving'].includes(value.status))return;
            if(Date.now()>=deadline)throw Error('La calibración sigue pendiente. Recargue para consultar su estado.');
            await new Promise(resolve=>setTimeout(resolve,500));
        }
    } catch(error){$('#neutralCalibrationStatus').text(error.message||'No se pudo consultar el BL0942.');}
    finally {if(run===neutralCalibrationRun)$('#neutralCalibrationStart,#neutralCalibrationReload,#neutralCalibrationPhase').prop('disabled',false);}
}

// Inicia la calibracion de neutro con la fase elegida en el formulario; sin parametros.
async function startNeutralCalibration() {
    $('#neutralCalibrationStart').prop('disabled',true);
    try {
        await $.ajax({type:'POST',url:board.IP+'/calibration/neutral',data:{phase:$('#neutralCalibrationPhase').val()},timeout:5000});
        await loadNeutralCalibration();
    } catch(error){$('#neutralCalibrationStatus').text(error.responseJSON?.error||'No se pudo iniciar la calibración. Compruebe las lecturas del BL0942.');}
    finally {$('#neutralCalibrationStart').prop('disabled',false);}
}

// Carga eventos puntuales y oscilografia dentro de Eventos de red; sin parametros y sin escrituras.
async function loadCaptureSettings() {
    return loadUnifiedEventSettings();
}

// Lee opciones de un namespace.
// namespace: nombre del namespace. keys: claves a leer.
function readOptions(namespace, keys) {
    let params = new URLSearchParams();
    params.append("ns", namespace);
    keys.forEach((key) => params.append(key, ""));
    return $.ajax({type: "GET", url: board.IP + "/options?" + params.toString(), cache: false, timeout: Math.max(board.timeout, 3000)});
}

// Explica el error HTTP y agrega la clave rechazada si el firmware la informa.
// error: objeto de error de fetch o jQuery.
function settingsErrorMessage(error) {
    const payload=error?.responseJSON;
    if(payload?.key)return payload.key+": "+(payload.error||"valor rechazado");
    return error?.message||payload?.error||error?.responseText||"No se pudo aplicar la configuración.";
}

// Escribe opciones en un namespace.
// namespace: nombre del namespace. values: objeto clave valor.
async function writeOptions(namespace, values) {
    let result;
    try { result=await $.ajax({type: "POST", url: board.IP + "/options", data: {...values, ns: namespace}, cache: false, timeout: Math.max(board.timeout, 5000)}); }
    catch(error) { if(error.responseJSON?.key)throw Error(settingsErrorMessage(error)); throw error; }
    if(result.verified===false)throw Error('El ESP no pudo verificar la escritura en NVS.');
    if(result.verified!==true)assertSettingsMatch(values,await readOptions(namespace,Object.keys(values)));
    return result;
}

// Devuelve las claves permitidas para una seccion del respaldo.
// section: identificador de seccion.
function getBackupSectionKeys(section) {
    if (section === "network") return backupNetworkKeys;
    if (section === "grid") return backupGridKeys;
    if (section === "history") return backupHistoryKeys;
    if (section === "capture") return getMeterOptionKeys();
    if (section === "wave") return ["preMs", "saveEnabled", "pointMask"];
    if (section === "energy") return backupEnergyKeys;
    if (section === "calibration") return [...getCalibrationKeys(), "neutralRms", "neutralFast"];
    return [];
}

// Valida la forma y los valores de una seccion antes de guardarla.
// section: identificador de seccion. values: objeto leido del JSON.
function validateBackupSection(section, values) {
    if (!values || typeof values !== "object" || Array.isArray(values)) throw new Error("La sección no es un objeto.");
    const allowed = getBackupSectionKeys(section);
    const keys = Object.keys(values);
    if (!keys.length) throw new Error("La sección está vacía.");
    const unknownKeys = keys.filter((key) => !allowed.includes(key));
    if (unknownKeys.length) throw new Error("Contiene campos no reconocidos: " + unknownKeys.join(", ") + ".");
    if (section === "wave" && (!allowed.every(key => Object.hasOwn(values, key)) ||
        !Number.isInteger(values.preMs) || values.preMs < 48 || values.preMs > 96 || values.preMs % 48 ||
        ![true, false, 0, 1].includes(values.saveEnabled) || !Number.isInteger(values.pointMask) || values.pointMask < 0 || values.pointMask > 7))
        throw new Error("La configuración de oscilografías no es válida.");
    if (keys.some((key) => values[key] === null || !["string", "number", "boolean"].includes(typeof values[key])))
        throw new Error("Contiene un valor con formato inválido.");
    if (section === "grid" && !backupGridKeys.filter(key => key !== 'abnormalConfirmMs').every((key) => Object.prototype.hasOwnProperty.call(values, key)))
        throw new Error("Faltan límites o tiempos de red.");
    if (section === "history" && (!backupHistoryKeys.every((key) => Object.prototype.hasOwnProperty.call(values, key)) ||
        ![true, false, 0, 1, "true", "false", "0", "1"].includes(values.enabled) ||
        !settingsNumber(values.groupConfig,0,0xFFFFFF,true) || !settingsNumber(values.samplePeriodDeciseconds,1,3000,true)))
        throw new Error("La configuración histórica no es válida.");
    if (section === "energy" && keys.some((key) => !Number.isFinite(Number(values[key]))))
        throw new Error("Los totalizadores deben ser números finitos.");
    if (section === "capture" && keys.some((key) => ["dipMs", "swellMs", "ocMs"].includes(key) &&
        (!Number.isInteger(Number(values[key])) || Number(values[key]) < 10 || Number(values[key]) > 2000 || Number(values[key]) % 10)))
        throw new Error("Confirmaciones de captura: 10 a 2000 ms, en pasos de 10 ms.");
    if (section === "grid" && keys.some((key) => !Number.isFinite(Number(values[key]))))
        throw new Error("Los ajustes de red deben ser numéricos.");
    if (section === "calibration" && keys.some((key) => ["neutralRms", "neutralFast"].includes(key) ?
        !Number.isFinite(Number(values[key])) || Number(values[key]) < 0.1 || Number(values[key]) > 10 : !isHex32(String(values[key]))))
        throw new Error("La calibración debe contener enteros hexadecimales de 32 bits.");
    if(section==='network')validateNetworkValues(values);
    if(section==='grid')validateGridValues(values);
    if(section==='capture')validateCaptureValues(values);
    if(section==='energy'&&keys.some(key=>!settingsNumber(values[key],-3.4028234663852886e38,3.4028234663852886e38)))throw Error('Totalizador fuera del rango float32.');
    if(section==='history') {
        const group=Number(values.groupConfig);
        if((group&0xFC0000)||[1,3,5,10].some(shift=>((group>>shift)&3)===3)||!(group&(1<<14))&&(group&(7<<15)))throw Error('Grupos históricos incompatibles con el formato actual.');
    }
    return true;
}

// Lee una seccion y completa solo opciones de captura ausentes con sus valores predeterminados del firmware.
// section: identificador de seccion.
async function readBackupSection(section, fresh = true) {
    let values;
    let defaultsUsed = [];
    if (["grid","capture","wave"].includes(section)) {
        const config = await readEventsConfig(fresh);
        if (section === "grid") values = flattenEventFields(config,eventGridFields);
        else if (section === "capture") values = flattenEventFields(config,eventPointFields);
        else values = {preMs:config.wave.pre,saveEnabled:config.wave.on,pointMask:(config.wave.dip?1:0)|(config.wave.swell?2:0)|(config.wave.oc?4:0)};
    } else if (section === "network") {
        const response = await $.ajax({url: board.IP + "/network/config", cache: false, timeout: Math.max(board.timeout, 5000)});
        const keys = getBackupSectionKeys(section);
        const missingKeys = keys.filter((key) => !Object.prototype.hasOwnProperty.call(response, key));
        if (missingKeys.length) throw new Error("Opciones de red ausentes: " + missingKeys.join(", ") + ".");
        values = Object.fromEntries(keys.map((key) => [key, response[key]]));
    } else if (section === "calibration") {
        const namespace = "ADE9000";
        const keys = getBackupSectionKeys(section);
        const response = await readOptions(namespace, keys);
        const missingKeys = keys.filter((key) => !Object.prototype.hasOwnProperty.call(response, key) || response[key] === "key no existe");
        const invalidKeys = keys.filter((key) => response[key] === "invalid type");
        const unresolvedKeys = missingKeys;
        if (invalidKeys.length || unresolvedKeys.length)
            throw new Error("Opciones ausentes o inválidas: " + [...unresolvedKeys, ...invalidKeys].join(", ") + ".");
        values = Object.fromEntries(keys.map((key) => {
            const value = response[key];
            return [key, section === "calibration" && !["neutralRms", "neutralFast"].includes(key) ? toHex32(value) : value];
        }));
    } else if (section === "history") {
        const response = await $.ajax({url: board.IP + "/history/config", cache: false, timeout: Math.max(board.timeout, 5000)});
        values = {enabled: response.enabled, groupConfig: historyOptionsToGroupConfig(response),
            samplePeriodDeciseconds: Math.round(Number(response.samplePeriod) * 10)};
    } else if (section === "energy") {
        const response=await readOptions('meter',backupEnergyKeys);
        values=Object.fromEntries(backupEnergyKeys.map(key=>[key,response[key]]));
    } else throw new Error("Sección desconocida.");
    validateBackupSection(section, values);
    return {values, defaultsUsed};
}

// Descarga un respaldo con las secciones seleccionadas en la interfaz.
async function exportSettingsBackup() {
    if(settingsBackupBusy)return;
    const selected = $(".backupExportSection:checked").map((_, input) => input.value).get();
    if (!selected.length) { $("#settingsBackupStatus").text("Seleccioná al menos una sección."); return; }
    setSettingsBackupBusy(true);
    const status = $("#settingsBackupStatus").empty();
    const sections = {};
    eventsConfig=null;
    try {
        for (const key of selected) {
            const label = settingsBackupSections.find((item) => item.key === key).label;
            const row = $("<li>").text(label + ": leyendo...").appendTo(status);
            const result = await readBackupSection(key,false);
            sections[key] = result.values;
            row.text(label + ": lista.");
        }
        const backup = {format: "pm9000-config", version: 1, exportedAt: new Date().toISOString(), sections};
        const blobUrl = URL.createObjectURL(new Blob([JSON.stringify(backup, null, 2)], {type: "application/json"}));
        const link = document.createElement("a");
        link.href = blobUrl;
        link.download = "power-meter-config-" + new Date().toISOString().replace(/[:.]/g, "-") + ".json";
        link.click();
        setTimeout(() => URL.revokeObjectURL(blobUrl), 1000);
    } catch (error) {
        $("<li class=\"text-danger\">").text("No se generó el respaldo: " + error.message).appendTo(status);
    } finally {
        setSettingsBackupBusy(false);
    }
}

// Carga y valida un archivo JSON para restaurar.
// file: archivo elegido por el usuario.
async function loadSettingsBackup(file) {
    if(settingsBackupBusy)return;
    loadedSettingsBackup = null;
    loadedSettingsBackupErrors = {};
    $("#settingsBackupImportSections").empty();
    $("#btnImportSettingsBackup").prop("disabled", true);
    $("#btnRestartAfterImport").prop("hidden", true);
    if (!file) return;
    try {
        if(file.size>262144)throw Error('El respaldo supera los 256 KiB permitidos.');
        const backup = JSON.parse(await file.text());
        if (!backup || backup.format !== "pm9000-config" || backup.version !== 1 || !backup.sections || typeof backup.sections !== "object" || Array.isArray(backup.sections))
            throw new Error("El archivo no es un respaldo Power Meter compatible.");
        if(Object.keys(backup.sections).some(key=>!settingsBackupSections.some(section=>section.key===key)))
            throw Error('El archivo contiene secciones desconocidas.');
        const available = settingsBackupSections.filter(({key}) => Object.prototype.hasOwnProperty.call(backup.sections, key));
        if (!available.length) throw new Error("El archivo no contiene secciones conocidas.");
        available.forEach(({key, label}) => {
            try { validateBackupSection(key, backup.sections[key]); }
            catch(error) { loadedSettingsBackupErrors[key]=error.message; }
            const invalid=loadedSettingsBackupErrors[key];
            const option=$('<div class="form-check">').appendTo('#settingsBackupImportSections');
            $('<input class="form-check-input backupImportSection" type="checkbox" checked>').attr({id:'backupImport_'+key,value:key}).appendTo(option);
            $('<label class="form-check-label">').attr('for','backupImport_'+key).toggleClass('text-danger',!!invalid)
                .text(label+(invalid?' (revisar)':'')).attr('title',invalid||'').appendTo(option);
        });
        loadedSettingsBackup = backup;
        $("#settingsBackupWarning").removeClass("text-danger").text("Respaldo válido. " + available.length + " secciones; todas seleccionadas.");
        $("#btnImportSettingsBackup").prop("disabled", false);
    } catch (error) {
        $("#settingsBackupWarning").text("No se puede usar el archivo: " + error.message).addClass("text-danger");
    }
}

// Bloquea controles de respaldo durante la operacion; busy indica si hay una tarea en curso.
function setSettingsBackupBusy(busy) {
    settingsBackupBusy=busy;
    $('#settings .tab-pane').not('#settings_backup').prop('inert',busy);
    $('#settingsTabs button').prop('disabled',busy);
    $('#settingsBackupFile, .backupExportSection, .backupImportSection, #btnExportSettingsBackup, #btnRestartAfterImport').prop('disabled',busy);
    $('#btnImportSettingsBackup').prop('disabled',busy||!loadedSettingsBackup);
}

// Valida cada seccion seleccionada y devuelve sus errores sin bloquear las demas.
async function preflightSettingsBackup(sections) {
    const errors={...loadedSettingsBackupErrors};
    for(const [key,values] of Object.entries(sections)) {
        try { validateBackupSection(key,values); }
        catch(error) { errors[key]=error.message; }
    }
    if(sections.network&&!errors.network) {
        try {
            const current=await $.ajax({url:board.IP+'/network/config',cache:false,timeout:5000});
            validateNetworkValues({...current,...sections.network});
        } catch(error) { errors.network=error.message||error.responseText||'No se pudieron validar los ajustes actuales de red.'; }
    }
    if(sections.capture&&!errors.capture) {
        try {
            const current=await readEventsConfig(true);
            validateCaptureValues({...flattenEventFields(current,eventPointFields),...sections.capture});
        } catch(error) { errors.capture=error.message||'No se pudieron validar los ajustes actuales de eventos.'; }
    }
    return errors;
}

// Aplica una seccion usando su endpoint especifico.
// section: identificador. values: configuracion validada de esa seccion.
async function applyBackupSection(section, values) {
    if (["grid","capture","wave"].includes(section)) {
        await readEventsConfig(true);
        if (section === "grid") return saveEventConfig(mapEventFields(values,eventGridFields));
        if (section === "capture") return saveEventConfig(mapEventFields(values,eventPointFields));
        return saveEventConfig({"wave.pre":values.preMs,"wave.on":Number(values.saveEnabled),
            "wave.dip":values.pointMask&1?1:0,"wave.swell":values.pointMask&2?1:0,"wave.oc":values.pointMask&4?1:0});
    }
    if (section === "network") return saveNetworkConfig(values);
    if (section === "history") return saveHistoryConfig(historyBackupToOptions(values));
    if (section === "energy") return writeOptions("meter", values);
    if (section === "calibration") return writeOptions("ADE9000", values);
    throw new Error("Sección desconocida.");
}

// Restaura las secciones marcadas, una por una, y deja visible el resultado de cada una.
async function importSettingsBackup() {
    if (!loadedSettingsBackup || settingsBackupBusy) return;
    let selected = $(".backupImportSection:checked").map((_, input) => input.value).get();
    if (!selected.length) { $("#settingsBackupWarning").text("Seleccioná al menos una sección."); return; }
    if (!confirm("Se van a sobrescribir " + selected.length + " secciones del medidor. ¿Continuar?")) return;
    selected = selected.filter((key) => key !== "network").concat(selected.includes("network") ? ["network"] : []);
    const sections=Object.fromEntries(selected.map(key=>[key,{...loadedSettingsBackup.sections[key]}]));
    settingsBackupRestartRequired = false;
    setSettingsBackupBusy(true);
    $("#btnRestartAfterImport").prop("hidden", true);
    const status = $("#settingsBackupStatus").empty();
    let failed = false;
    try {
    const validationErrors=await preflightSettingsBackup(sections);
    for (const key of selected) {
        const label = settingsBackupSections.find((item) => item.key === key).label;
        const row = $("<li>").text(label + ": aplicando...").appendTo(status);
        if(validationErrors[key]) {
            failed=true;
            row.addClass('text-danger').text(label+': error de validación. '+validationErrors[key]);
            continue;
        }
        try {
            const result=await applyBackupSection(key, sections[key]);
            const restart=["energy", "calibration"].includes(key)||result?.restartRequired===true;
            if(restart)settingsBackupRestartRequired=true;
            row.text(label+(restart?": verificada; requiere reinicio.":": aplicada y verificada."));
        } catch (error) {
            failed = true;
            row.addClass("text-danger").text(label + ": error; puede haber quedado parcialmente escrita. " + settingsErrorMessage(error));
        }
    }
    } catch(error) {
        failed=true;
        $('<li class="text-danger">').text('Error al preparar la importación: '+error.message).appendTo(status);
    } finally {
        eventsConfig=null;
        $('#settingsTabs button').each(function() { $('#settings_'+$(this).data('section')).data('loaded',false); });
        setSettingsBackupBusy(false);
    }
    if (settingsBackupRestartRequired) $("#btnRestartAfterImport").prop("hidden", false);
    $("#settingsBackupWarning").text(failed ? "Importación terminada con errores. Revisá el resultado de cada sección; las demás se procesaron." : "Importación terminada." + (settingsBackupRestartRequired ? " Reiniciá para aplicar los cambios pendientes." : ""));
}

// Reinicia el medidor sin guardar encima los totalizadores recién importados.
function restartAfterSettingsImport() {
    if (!settingsBackupRestartRequired || !confirm("El medidor se reiniciará ahora. ¿Continuar?")) return;
    $("#btnRestartAfterImport").prop("disabled", true).text("Reiniciando...");
    $.ajax({type: "POST", url: board.IP + "/restart", timeout: Math.max(board.timeout, 5000)})
        .fail(() => $("#settingsBackupWarning").text("No se confirmó el reinicio. Podés reiniciar el medidor manualmente."));
}

// Devuelve las claves de opciones del medidor.
function getMeterOptionKeys() {
    return meterOptionGroups.flatMap((group) => group.fields.map((field) => field.key));
}

// Devuelve las claves de calibración.
function getCalibrationKeys() {
    return calibrationGroups.flatMap((group) => group.fields.map((field) => field.key));
}

// Completa los inputs de opciones del medidor.
// values: valores recibidos desde el firmware.
function fillMeterOptions(values) {
    meterOptionGroups.forEach((group) => {
        group.fields.forEach((field) => {
            const input = $("#" + getOptionInputId("meter", field.key));
            if (field.type == "bool") input.prop("checked", Number(values[field.key]) != 0);
            else input.val(field.scale ? Number(values[field.key]) * field.scale : values[field.key]);
        });
    });
}

// Completa los inputs de calibración.
// values: valores recibidos desde el firmware.
function fillCalibrationOptions(values) {
    getCalibrationKeys().forEach((key) => {
        $("#" + getOptionInputId("cal", key)).val(toHex32(values[key]));
    });
}

// Guarda las opciones del medidor y las aplica.
function saveMeterOptions() {
    const values = collectMeterOptionValues();
    if (!values) return;
    let waveValues;
    try { waveValues = waveFormEventFields(); }
    catch (error) { setSettingsStatus(error.message, true, "capture"); return; }
    setSettingsStatus("Guardando opciones de captura...", false, "capture");
    $("#btnSaveMeterOptions, #btnLoadSettings, #captureFields").prop("disabled", true);
    const {ocCurrent,ocHyst,...pointValues} = values;
    saveEventConfig({...mapEventFields(pointValues,eventPointFields),...waveValues})
        .then(() => {
            setSettingsStatus("Opciones de captura guardadas y aplicadas.", false, "capture");
        })
        .catch(error => setSettingsStatus(error.message || "No se pudo guardar la configuración.", true, "capture"))
        .finally(() => $("#btnSaveMeterOptions, #btnLoadSettings, #captureFields").prop("disabled", false));
}

// Guarda los valores de calibración.
async function saveCalibrationOptions() {
    let values = collectCalibrationValues();
    if (values == null) return;
    setSettingsStatus("Guardando calibración...", false, "calibration");
    $("#btnSaveCalibration, #btnLoadCalibration, #btnRestoreCalibration, #calibrationFields").prop("disabled", true);
    try {
        await writeOptions("ADE9000",values);
        setSettingsStatus("Calibración guardada y verificada. Reinicie para aplicarla.",false,"calibration");
    } catch(error){setSettingsStatus(error.message||"No se pudo guardar la calibración.",true,"calibration");}
    finally {$("#btnSaveCalibration, #btnLoadCalibration, #btnRestoreCalibration, #calibrationFields").prop("disabled",false);}
}

// Restaura la calibración por defecto.
async function restoreDefaultCalibration() {
    if (!confirm("¿Restaurar la calibración por defecto? Se reemplazarán los valores guardados.")) return;
    setSettingsStatus("Restaurando calibración por defecto...", false, "calibration");
    $('#btnRestoreCalibration,#btnSaveCalibration,#btnLoadCalibration,#calibrationFields').prop('disabled',true);
    try {
        const result=await $.ajax({type:'POST',url:board.IP+'/calibration/restore',timeout:10000});
        if(!result.verified)throw Error('El firmware no confirmó la verificación de los valores por defecto.');
        const values=await readOptions('ADE9000',getCalibrationKeys());
        if(getCalibrationKeys().some(key=>!settingsNumber(values[key],-2147483648,4294967295,true)))throw Error('No se pudo releer la calibración.');
        fillCalibrationOptions(values);
        setSettingsStatus('Calibración por defecto restaurada y verificada. Reinicie para aplicarla.',false,'calibration');
        await loadNeutralCalibration();
    } catch(error) {setSettingsStatus(error.message||'No se pudo restaurar la calibración.',true,'calibration');}
    finally {$('#btnRestoreCalibration,#btnSaveCalibration,#btnLoadCalibration,#calibrationFields').prop('disabled',false);}
}

// Recolecta los valores de opciones del medidor.
function collectMeterOptionValues() {
    let values = {};
    let valid = true;
    meterOptionGroups.forEach((group) => {
        group.fields.forEach((field) => {
            const input = $("#" + getOptionInputId("meter", field.key));
            const value = field.scale ? Number(input.val()) / field.scale : input.val();
            if (field.unit === "ms" && (!Number.isInteger(Number(value)) || value < 10 || value > 2000 || value % 10)) valid = false;
            if (field.unit === "V" && (input.val().trim() === "" || !Number.isFinite(Number(value)) || value < 0 || value > 500)) valid = false;
            if (field.unit === "A" && (input.val().trim() === "" || !Number.isFinite(Number(value)) || value < 0 || value > 163.82)) valid = false;
            values[field.key] = field.type == "bool" ? (input.prop("checked") ? "1" : "0") : value;
        });
    });
    if (Number(values.ocHyst) >= Number(values.ocCurrent) || Number(values.ocCurrent) <= 0) valid = false;
    if (!valid) setSettingsStatus("Revise los umbrales (0–500 V / 0–163,82 A) y los tiempos (10–2000 ms, pasos de 10 ms).", true, "capture");
    return valid ? values : null;
}

// Recolecta los valores de calibración.
function collectCalibrationValues() {
    let values = {};
    for (const key of getCalibrationKeys()) {
        const value = $("#" + getOptionInputId("cal", key)).val().trim();
        if (!isHex32(value)) {
            setSettingsStatus("Valor hexadecimal inválido en " + key + ".", true);
            return null;
        }
        values[key] = value;
    }
    return values;
}

// Valida un hexadecimal de 32 bits.
// value: texto ingresado por el usuario.
function isHex32(value) {
    return /^0x[0-9a-fA-F]{1,8}$/.test(value);
}

// Convierte un valor numérico a hexadecimal de 32 bits.
// value: valor decimal recibido desde el firmware.
function toHex32(value) {
    value = Number(value);
    if (!isFinite(value)) value = 0;
    return "0x" + (value >>> 0).toString(16).padStart(8, "0");
}

// Muestra el estado de la pestaña de opciones.
// message: texto a mostrar. isError: indica si debe mostrarse como error.
function setSettingsStatus(message, isError = false, section = $("#settingsTabs .active").data("section")) {
    $("#settings_" + section + " .settingsStatus").text(message).toggleClass("text-danger", isError).toggleClass("text-muted", !isError);
}

// Devuelve un detalle corto de error Ajax para mostrar al usuario.
// error: objeto de error recibido desde jQuery.
function getAjaxErrorText(error) {
    if (!error || !error.status) return "";
    return " Código: " + error.status + ".";
}

// Escapa texto para insertarlo como HTML seguro.
// text: valor a convertir en texto seguro.
function escapeHtml(text) {
    return String(text).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;" }[char]));
}

// Formatea la antigüedad de un estado; ageMs es el tiempo transcurrido en milisegundos.
function formatGridStateAge(ageMs) {
    const seconds = Math.floor(ageMs / 1000);
    if (seconds < 60) return `${seconds} s`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)} min`;
    if (seconds < 86400) return `${Math.floor(seconds / 3600)} h ${Math.floor(seconds % 3600 / 60)} min`;
    return `${Math.floor(seconds / 86400)} d ${Math.floor(seconds % 86400 / 3600)} h`;
}

// Muestra estados confirmados y su antigüedad; states/ages vienen del JSON de mediciones.
function showGridStates(states = {}, ages = {}) {
    const labels = {normal: "Normal", undervoltage: "Bajo voltaje", overvoltage: "Alto voltaje",
        phase_loss: "Sin tensión", abnormal: "Fuera de rango", outage: "Corte total"};
    ["r", "s", "t", "overall"].forEach((key) => {
        const state = states[key];
        const color = state === "normal" ? "text-success" :
            state === "phase_loss" || state === "outage" ? "text-danger" : labels[state] ? "text-warning" : "text-muted";
        const stateClass = state === "normal" ? "normal" :
            state === "phase_loss" || state === "outage" ? "danger" : labels[state] ? "warning" : "unknown";
        $("#gridStatusCard_" + key).attr("data-state", stateClass);
        $("#gridState_" + key).attr("class", "fw-bold " + color).text(labels[state] || "Sin datos");
        const ageMs = Number(ages[key]);
        $("#gridStateAge_" + key).text(labels[state] && Number.isFinite(ageMs) && ageMs >= 0
            ? "Desde hace " + formatGridStateAge(ageMs) : "");
    });
}

function meterOk(resp) {
    showGridStates(resp.gridState, resp.gridStateAgeMs);
    ["r", "s", "t"].forEach((key) => {
        $("#gridVoltage_" + key).text(formatNumber(resp.vrms[key], "V"));
        $("#gridCurrent_" + key).text(formatNumber(resp.irms[key], "A"));
    });
    phasorData.voltage = resp.vrms;
    phasorData.current = resp.irms;
    updatePhasorDiagram();
    if (meterOk.fail) $(".meter").removeClass("notLoading");
    meterOk.fail = false;

    updateMeterGauge("gaugeVoltaje", resp.vrms.avg);
    updateMeterGauge("gaugeVoltajeF", resp.vvrms.avg);
    updateMeterGauge("gaugeCorriente", resp.irms.avg);
    updateMeterGauge("gaugePotencia", resp.watt.tot / 1000);

    // //Llenar las celdas de voltaje
    $(".vvrmsR").text(formatNumber(resp.vvrms.r, "V"));
    $(".vvrmsS").text(formatNumber(resp.vvrms.s, "V"));
    $(".vvrmsT").text(formatNumber(resp.vvrms.t, "V"));
    $(".vvrmsAvg").text(formatNumber(resp.vvrms.avg, "V"));

    $(".vrmsR").text(formatNumber(resp.vrms.r, "V"));
    $(".vrmsS").text(formatNumber(resp.vrms.s, "V"));
    $(".vrmsT").text(formatNumber(resp.vrms.t, "V"));
    $(".vrmsN").text(Number.isFinite(resp.vrms.n) ? formatNumber(resp.vrms.n, "V") : "Sin datos");
    $(".fastVrmsN").text(Number.isFinite(resp.fastVn) ? formatNumber(resp.fastVn, "V") : "Sin datos");
    $(".vrmsAvg").text(formatNumber(resp.vrms.avg, "V"));

    // //Llenar las celdas de corriente
    $(".irmsR").text(formatNumber(resp.irms.r, "A"));
    $(".irmsS").text(formatNumber(resp.irms.s, "A"));
    $(".irmsT").text(formatNumber(resp.irms.t, "A"));
    $(".irmsN").text(formatNumber(resp.irms.n, "A"));
    $(".irmsAvg").text(formatNumber(resp.irms.avg, "A"));

    // //Potencia activa
    $(".wattR").text(formatNumber(resp.watt.r, "W"));
    $(".wattS").text(formatNumber(resp.watt.s, "W"));
    $(".wattT").text(formatNumber(resp.watt.t, "W"));
    $(".wattAvg").text(formatNumber(resp.watt.avg, "W"));
    $(".wattTot").text(formatNumber(resp.watt.tot, "W"));

    // //Potencia reactiva
    $(".varR").text(formatNumber(resp.var.r, "VAR"));
    $(".varS").text(formatNumber(resp.var.s, "VAR"));
    $(".varT").text(formatNumber(resp.var.t, "VAR"));
    $(".varAvg").text(formatNumber(resp.var.avg, "VAR"));
    $(".varTot").text(formatNumber(resp.var.tot, "VAR"));

    // //Potencia aparente
    $(".vaR").text(formatNumber(resp.va.r, "VA"));
    $(".vaS").text(formatNumber(resp.va.s, "VA"));
    $(".vaT").text(formatNumber(resp.va.t, "VA"));
    $(".vaAvg").text(formatNumber(resp.va.avg, "VA"));
    $(".vaTot").text(formatNumber(resp.va.tot, "VA"));

    $(".freqR").text(formatNumber(resp.freq, "Hz"));
    $(".freqS").text(formatNumber(resp.freq, "Hz"));
    $(".freqT").text(formatNumber(resp.freq, "Hz"));
    $(".freqAvg").text(formatNumber(resp.freq, "Hz"));

    $(".fpR").text(resp.pf.r.toFixed(decimalPlaces.fp));
    $(".fpS").text(resp.pf.s.toFixed(decimalPlaces.fp));
    $(".fpT").text(resp.pf.t.toFixed(decimalPlaces.fp));
    $(".fpAvg").text(resp.pf.tot.toFixed(decimalPlaces.fp));
}

function meterFail(response) {
    showGridStates();
    $(".meter").addClass("notLoading");
    meterOk.fail = true;
}

function energyOk(resp) {
    if (energyOk.fail) $(".energia").removeClass("notLoading");
    energyOk.fail = false;

    // //Energía activa
    $(".wattHR").text(formatEnergy(resp.wattH.r, "Wh", "kWh"));
    $(".wattHS").text(formatEnergy(resp.wattH.s, "Wh", "kWh"));
    $(".wattHT").text(formatEnergy(resp.wattH.t, "Wh", "kWh"));
    $(".wattHTot").text(formatEnergy(resp.wattH.tot, "Wh", "kWh"));

    // //Energía reactiva
    $(".varHR").text(formatEnergy(resp.varH.r, "VARh", "kVARh"));
    $(".varHS").text(formatEnergy(resp.varH.s, "VARh", "kVARh"));
    $(".varHT").text(formatEnergy(resp.varH.t, "VARh", "kVARh"));
    $(".varHTot").text(formatEnergy(resp.varH.tot, "VARh", "kVARh"));

    // //Energía aparente
    $(".vaHR").text(formatEnergy(resp.vaH.r, "VAh", "kVAh"));
    $(".vaHS").text(formatEnergy(resp.vaH.s, "VAh", "kVAh"));
    $(".vaHT").text(formatEnergy(resp.vaH.t, "VAh", "kVAh"));
    $(".vaHTot").text(formatEnergy(resp.vaH.tot, "VAh", "kVAh"));
}

function energyFail(response) {
    energyOk.fail = true;
    $(".energia").addClass("notLoading");
}

function angleOk(resp) {
    // //Ángulos entre fases de voltaje
    if (angleOk.fail) $(".angulos").removeClass("notLoading");
    angleOk.fail = false;
    phasorData.angles = resp;
    updatePhasorDiagram();

    $(".degvR").text(formatNumber(resp.voltage.r, "°", "degv"));
    $(".degvS").text(formatNumber(resp.voltage.s, "°", "degv"));
    $(".degvT").text(formatNumber(resp.voltage.t, "°", "degv"));

    // //Ángulos entre fases de voltaje y fases de corriente
    $(".degviR").text(formatNumber(resp.vi.r, "°", "degvi"));
    $(".degviS").text(formatNumber(resp.vi.s, "°", "degvi"));
    $(".degviT").text(formatNumber(resp.vi.t, "°", "degvi"));

    // //Ángulos entre fases de corriente
    $(".degiR").text(formatNumber(resp.current.r, "°", "degi"));
    $(".degiS").text(formatNumber(resp.current.s, "°", "degi"));
    $(".degiT").text(formatNumber(resp.current.t, "°", "degi"));
}

// Dibuja un vector SVG con magnitud normalizada y orientacion en grados.
// kind: Voltage o Current; phase: r, s o t; magnitude/maxMagnitude: valor y escala;
// angle: orientacion absoluta; visible: indica si el vector debe mostrarse.
function setPhasorVector(kind, phase, magnitude, maxMagnitude, angle, visible) {
    const suffix = phase.toUpperCase();
    const line = document.getElementById("phasor" + kind + suffix);
    const dot = document.getElementById("phasor" + kind + "Dot" + suffix);
    const radius = visible && maxMagnitude > 0 ? 120 * magnitude / maxMagnitude : 0;
    const radians = angle * Math.PI / 180;
    const x = 165 + radius * Math.cos(radians);
    const y = 160 - radius * Math.sin(radians);
    line.setAttribute("x2", x.toFixed(1));
    line.setAttribute("y2", y.toFixed(1));
    line.style.display = radius > 0.5 ? "" : "none";
    dot.setAttribute("cx", x.toFixed(1));
    dot.setAttribute("cy", y.toFixed(1));
    dot.style.display = radius > 0.5 ? "" : "none";
}

// Actualiza el diagrama y las lecturas de fase con los ultimos datos disponibles.
// No recibe parametros; utiliza phasorData y los selectores de visibilidad.
function updatePhasorDiagram() {
    const {angles, voltage, current} = phasorData;
    if (!angles || !voltage || !current) return;

    const phases = [
        {key: "r", voltageAngle: 0},
        {key: "s", voltageAngle: Number(angles.voltage.r)},
        {key: "t", voltageAngle: Number(angles.voltage.t)}
    ].map(phase => ({
        ...phase,
        volts: Number(voltage[phase.key]),
        amps: Number(current[phase.key]),
        angleVI: Number(angles.vi[phase.key])
    }));
    if (!phases.every(phase => Number.isFinite(phase.voltageAngle) && Number.isFinite(phase.volts) &&
        Number.isFinite(phase.amps) && Number.isFinite(phase.angleVI))) return;

    const maxVolts = Math.max(...phases.map(phase => phase.volts));
    const maxAmps = Math.max(...phases.map(phase => phase.amps));
    const showVoltage = $("#phasorShowVoltage").prop("checked");
    const showCurrent = $("#phasorShowCurrent").prop("checked");

    phases.forEach(phase => {
        const currentAngle = ((phase.voltageAngle + phase.angleVI) % 360 + 360) % 360;
        setPhasorVector("Voltage", phase.key, phase.volts, maxVolts, phase.voltageAngle,
            showVoltage && phase.volts > 0);
        setPhasorVector("Current", phase.key, phase.amps, maxAmps, currentAngle,
            showCurrent && phase.amps >= 0.2);
        $("#phasorV" + phase.key.toUpperCase()).text(formatNumber(phase.volts, "V"));
        $("#phasorI" + phase.key.toUpperCase()).text(formatNumber(phase.amps, "A"));
        $("#phasorAngle" + phase.key.toUpperCase()).text(formatNumber(phase.angleVI, "°", "degvi"));
    });

    $("#phasorStatus").text(maxVolts > 0
        ? "Flechas solidas: tensión. Punteadas: corriente. Corrientes menores a 0,2 A se omiten."
        : "No hay tensión disponible para representar.");
}

function angleFail(response) {
    $(".angulos").addClass("notLoading");
    angleOk.fail = true;
}

function thdOk(resp) {
    if (thdOk.fail) $(".thd").removeClass("notLoading");
    thdOk.fail = false;

    $(".thdVR").text(formatNumber(resp.voltageTHD.r, "%"));
    $(".thdVS").text(formatNumber(resp.voltageTHD.s, "%"));
    $(".thdVT").text(formatNumber(resp.voltageTHD.t, "%"));
    $(".thdVAvg").text(formatNumber((resp.voltageTHD.r + resp.voltageTHD.s + resp.voltageTHD.t) / 3.0, "%"));

    $(".thdIR").text(formatNumber(resp.currentTHD.r, "%"));
    $(".thdIS").text(formatNumber(resp.currentTHD.s, "%"));
    $(".thdIT").text(formatNumber(resp.currentTHD.t, "%"));
    $(".thdIAvg").text(formatNumber((resp.currentTHD.r + resp.currentTHD.s + resp.currentTHD.t) / 3.0, "%"));
}

function thdFail(response) {
    $(".thd").addClass("notLoading");
    thdOk.fail = true;
}

function formatNumber(value, unit, unitType = "") {
    value = Number(value);
    if (!isFinite(value)) return "-- " + unit;

    let places = 2;
    if (unit == "V") places = decimalPlaces.vrms;
    else if (unit == "A") places = decimalPlaces.irms;
    else if (unit == "W") places = decimalPlaces.watt;
    else if (unit == "VAR") places = decimalPlaces.var;
    else if (unit == "VA") places = decimalPlaces.va;
    else if (unit == "kWh") places = 3;
    else if (unit == "kVARh") places = 3;
    else if (unit == "kVAh") places = 3;
    else if (unit == "Wh") places = decimalPlaces.wattH;
    else if (unit == "°" && unitType) places = decimalPlaces[unitType];

    return value.toFixed(places) + " " + unit;
}

// Formatea energía en unidad chica o kilo según el valor.
// value: energía en la unidad chica. unit: unidad chica. kiloUnit: unidad grande.
function formatEnergy(value, unit, kiloUnit) {
    value = Number(value);
    if (!isFinite(value)) return "-- " + unit;
    if (Math.abs(value) < 1000) return formatNumber(value, unit);
    return formatNumber(value / 1000.0, kiloUnit);
}

function createTable() {
    decimalPlaces = {}; //Crear una tabla que contenga la cantidad de lugrares decimales por variable
    tableRows.forEach((row) => {
        decimalPlaces[row.unit] = row.decimals;
    });

    //Crear tabla
    let header = '<thead class="table"><tr class="valueTableHead">';
    let rows = [];
    let row;

    tableHeaders.forEach((headerItem, index) => {
        if (headerItem.visible) header += '<th scope="col" class="valueTableRow">' + headerItem.name + "</th>";
    });
    header += " </tr></thead>";

    tableRows.forEach((rowItem, index) => {
        if (rowItem.visible) {
            row = "<tr>";
            tableHeaders.forEach((headerItem, indexHead) => {
                if (rowItem.visible) {
                    let id = rowItem.unit + headerItem.short;
                    if (indexHead == 0) row += `<td scope="row">${rowItem.name}</td>`;
                    else row += `<td id="pt_${id}" class="valueTable ${rowItem.source} ${id}">${rowItem.unit === "fastVrms" ? "—" : ""}</td>`;
                }
            });
            row += "</tr>";
            rows.push(row);
        }
    });

    let body = '<tbody class="table-group-divider">';
    rows.forEach((element) => {
        body += element;
    });
    body += "</tbody>";
    $("#powerTable").html('<table class="table table-bordered">' + header + body + "</table>");
}
