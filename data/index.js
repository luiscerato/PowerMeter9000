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
	initGridEventsViewer();
	initLogsViewer();
    $("#eventList").html('<div class="text-muted p-2">Seleccione la pestaña Eventos para cargar la lista.</div>');
    $("#eventRamList").html("");
    $("#eventChartContainer").html('<div class="text-muted p-3">Sin evento cargado.</div>');
    $("#eventTestBtn").on("click", createVirtualEvent);
    $("#eventRampBtn").on("click", createRampTestEvent);
    $("#eventRefreshBtn").on("click", refreshEventLists);

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
        namespace: "meter",
        fields: [
            {key: "dipEnabled", label: "Detectar Dip", type: "bool"},
            {key: "dipVoltage", label: "Umbral Dip", type: "float", unit: "V"},
            {key: "dipCicles", label: "Confirmación Dip (DIP_CYC)", type: "int"},
            {key: "swellEnabled", label: "Detectar Swell", type: "bool"},
            {key: "swellVoltage", label: "Umbral Swell", type: "float", unit: "V"},
            {key: "swellCicles", label: "Confirmación Swell (SWELL_CYC)", type: "int"},
        ],
    },
    {
        title: "Eventos de corriente",
        namespace: "meter",
        fields: [
            {key: "overcurrent", label: "Detectar sobrecorriente", type: "bool"},
            {key: "ocCurrent", label: "Nivel sobrecorriente", type: "float", unit: "A"},
        ],
    },
    {
        title: "Ventana de captura",
        namespace: "meter",
        fields: [
            {key: "eventPrevSamples", label: "Muestras previas", type: "int"},
            {key: "eventPostSamples", label: "Muestras posteriores", type: "int"},
            {key: "eventMaxSamples", label: "Muestras máximas", type: "int"},
        ],
    },
];

const settingsBackupSections = [
    {key: "network", label: "Red y servicios"},
    {key: "grid", label: "Estados de red"},
    {key: "history", label: "Registro histórico"},
    {key: "capture", label: "Captura de eventos"},
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
    "lossConfirmMs", "voltageConfirmMs", "recoveryConfirmMs", "capturePages",
    "neutralEnabled", "neutralHigh", "neutralHysteresis", "neutralConfirmMs", "neutralRecoveryMs"];
const backupHistoryKeys = ["enabled", "groupConfig", "samplePeriodDeciseconds"];
const backupEnergyKeys = ["rWattH", "rVarH", "rVaH", "sWattH", "sVarH", "sVaH", "tWattH", "tVarH", "tVaH", "WattH", "VarH", "VaH"];
const backupCaptureDefaults = {eventPrevSamples: 100, eventPostSamples: 100, eventMaxSamples: 300};

// Inicializa la pestaña de opciones.
function initSettingsUI() {
    $("#settings").html(createSettingsHtml());
    $("#btnLoadSettings").click(() => loadSettings("capture", true));
    $("#btnLoadCalibration").click(() => loadSettings("calibration", true));
    $("#btnSaveMeterOptions").click(saveMeterOptions);
    $("#btnSaveCalibration").click(saveCalibrationOptions);
    $("#btnRestoreCalibration").click(restoreDefaultCalibration);
    $("#gridSettingsForm").on("submit", saveGridSettings);
    $("#historySettingsForm").on("submit", saveHistorySettings);
    $("#historySettingsForm").on("change", "input, select", function () {
        if (this.id === "history_energy") $("#history_energyExtraMode").prop("disabled", !this.checked);
        updateHistoryEstimate();
    });
    $("#history_period").on("input", updateHistoryEstimate);
    $("#btnLoadGridSettings").on("click", () => loadSettings("grid", true));
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
    const sections = [["grid", "Estados de red"], ["history", "Registro histórico"], ["storage", "Almacenamiento NAND"], ["capture", "Captura de eventos"], ["calibration", "Calibración"], ["backup", "Importar / exportar"]];
    sections.forEach(([key, label], i) => {
        html += '<li class="nav-item" role="presentation"><button class="nav-link' + (i ? '' : ' active') + '" id="settingsTab_' + key + '" data-section="' + key + '" data-bs-toggle="tab" data-bs-target="#settings_' + key + '" type="button" role="tab" aria-controls="settings_' + key + '" aria-selected="' + !i + '">' + label + '</button></li>';
    });
    html += '</ul><div class="tab-content">';
    const contents = [createGridSettingsHtml(), createHistorySettingsHtml(), createStorageSettingsHtml(), createMeterSettingsHtml(), createCalibrationSettingsHtml(), createSettingsBackupHtml()];
    sections.forEach(([key], i) => {
        html += '<section class="tab-pane settingsPane' + (i ? '' : ' active') + '" id="settings_' + key + '" role="tabpanel" aria-labelledby="settingsTab_' + key + '">';
        html += contents[i] + '</section>';
    });
    html += '</div></div>';
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
    const actions = '<button id="btnRefreshStorage" type="button" class="btn btn-sm btn-outline-secondary">Actualizar</button>' +
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
        waveEvents: "Reservada; almacenamiento Wave pendiente",
        gridEvents: ((Number(grid.recoveredEvents) || 0) + (Number(grid.acceptedEvents) || 0)) + " eventos conocidos",
        diagnostics: "Registros de diagnóstico",
        webFsA: storageWebFsLabel(webFs, "a"),
        webFsB: storageWebFsLabel(webFs, "b"),
    };
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
    const actions = '<button id="btnLoadGridSettings" type="button" class="btn btn-sm btn-outline-secondary">Recargar</button>' +
        '<button type="submit" class="btn btn-sm btn-success">Guardar</button>';
    let html = '<form id="gridSettingsForm" class="settingsSection">';
    html += createSettingsHeaderHtml("Estados de red", "Límites confirmados para las fases y los indicadores LED.", actions);
    html += '<div id="gridSettingsStatus" class="settingsStatusLine small text-muted" role="status"></div>';
    html += '<fieldset id="gridSettingsFields" disabled><div class="settingsGroupGrid settingsGroupGridTwo">';
    html += '<div class="settingsGroup"><h3>Límites por fase</h3><div class="table-responsive"><table class="table table-sm align-middle settingsTable"><thead><tr><th>Fase</th><th>Bajo voltaje (V)</th><th>Alto voltaje (V)</th></tr></thead><tbody>';
    ["R", "S", "T"].forEach((phase) => {
        html += '<tr><th>' + phase + '</th>';
        ["low", "high"].forEach((limit) => {
            html += '<td><input class="form-control form-control-sm" type="number" required min="1" max="500" step="0.01" name="' + limit + phase + '" id="grid_' + limit + phase + '" aria-label="' + (limit === "low" ? "Bajo" : "Alto") + ' voltaje fase ' + phase + '"></td>';
        });
        html += '</tr>';
    });
    html += '</tbody></table></div><button id="btnCopyGridLimits" type="button" class="btn btn-sm btn-outline-secondary">Copiar R a S/T</button></div>';
    html += '<div class="settingsGroup"><h3>Confirmación</h3>';
    const fields = [
        ["noVoltage", "Sin tensión (V)", 1, 100, "0.01"],
        ["hysteresis", "Histéresis (V)", 0, 50, "0.01"],
        ["lossConfirmMs", "Confirmación sin tensión (ms)", 20, 60000, "1"],
        ["voltageConfirmMs", "Confirmación bajo/alto voltaje (ms)", 20, 600000, "1"],
        ["recoveryConfirmMs", "Confirmación normalidad (ms)", 20, 60000, "1"],
        ["capturePages", "Páginas por evento", 3, 6, "1"],
    ];
    fields.forEach(([key, label, min, max, step]) => {
        html += '<div class="settingsField"><label for="grid_' + key + '">' + label + '</label><input class="form-control form-control-sm" required type="number" id="grid_' + key + '" name="' + key + '" min="' + min + '" max="' + max + '" step="' + step + '"></div>';
    });
    html += '</div><div class="settingsGroup"><h3>Neutro-tierra</h3>';
    html += '<div class="form-check mb-2"><input class="form-check-input" type="checkbox" name="neutralEnabled" id="grid_neutralEnabled"><label class="form-check-label" for="grid_neutralEnabled">Capturar tensión alta y recuperación</label></div>';
    const neutralFields = [["neutralHigh", "Umbral (V)", 0.025, 409.55, "0.025"],
        ["neutralHysteresis", "Histéresis (V)", 0, 409.525, "0.025"],
        ["neutralConfirmMs", "Confirmación de tensión alta (ms)", 20, 600000, "1"],
        ["neutralRecoveryMs", "Confirmación de recuperación (ms)", 20, 60000, "1"]];
    neutralFields.forEach(([key, label, min, max, step]) => {
        html += '<div class="settingsField"><label for="grid_' + key + '">' + label + '</label><input class="form-control form-control-sm" required type="number" id="grid_' + key + '" name="' + key + '" min="' + min + '" max="' + max + '" step="' + step + '"></div>';
    });
    html += '</div></div></fieldset></form>';
    return html;
}

// Crea el formulario del perfil que se escribe periodicamente en la NAND.
function createHistorySettingsHtml() {
    const actions = '<button id="btnLoadHistorySettings" type="button" class="btn btn-sm btn-outline-secondary">Recargar</button>' +
        '<button type="submit" class="btn btn-sm btn-success">Guardar</button>';
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
        enabled: values.enabled,
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
function saveHistorySettings(event) {
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
    $("#historySettingsFields, #btnLoadHistorySettings").prop("disabled", true);
    $("#historySettingsStatus").text("Guardando y cerrando la página actual...");
    $.ajax({type: "POST", url: board.IP + "/history/config", data: values, timeout: Math.max(board.timeout, 5000)})
        .done((config) => {
            fillHistorySettings(config);
            $("#historySettingsStatus").text("Guardado. Las muestras nuevas usan este perfil.");
        })
        .fail((xhr) => $("#historySettingsStatus").text(xhr.responseText || "No se pudo guardar el perfil histórico."))
        .always(() => $("#historySettingsFields, #btnLoadHistorySettings").prop("disabled", false));
}

// Crea el encabezado comun de una seccion; title y description son textos, actions contiene sus botones.
function createSettingsHeaderHtml(title, description, actions) {
    return '<header class="settingsSectionHeader"><div><h2>' + title + '</h2><p>' + description +
        '</p></div><div class="settingsActions">' + actions + '</div></header>';
}

// Completa el formulario con values recibidos del medidor.
function fillGridSettings(values) {
    $("#gridSettingsForm input[name]").each(function () {
        if (this.type === "checkbox") $(this).prop("checked", Boolean(Number(values[this.name])));
        else $(this).val(values[this.name]);
    });
}

// Lee ajustes una sola vez por solicitud, sin consultas periodicas; sin parametros.
function loadGridSettings() {
    $("#gridSettingsFields").prop("disabled", true);
    $("#gridSettingsStatus").text("Cargando...");
    return $.ajax({url: board.IP + "/grid/config", cache: false, timeout: Math.max(board.timeout, 3000)})
        .done((values) => {
            fillGridSettings(values);
            $("#gridSettingsFields").prop("disabled", false);
            $("#gridSettingsStatus").text("Ajustes cargados.");
        })
        .fail(() => $("#gridSettingsStatus").text("No se pudieron cargar los ajustes de red."));
}

// Valida y guarda el formulario; event es el envio del formulario web.
function saveGridSettings(event) {
    event.preventDefault();
    const form = document.getElementById("gridSettingsForm");
    if (!form.reportValidity()) return;
    const values = Object.fromEntries(new FormData(form));
    values.neutralEnabled = document.getElementById("grid_neutralEnabled").checked ? 1 : 0;
    const absence = Number(values.noVoltage), hysteresis = Number(values.hysteresis);
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
    $.ajax({type: "POST", url: board.IP + "/grid/config", data: values, timeout: Math.max(board.timeout, 5000)})
        .done((saved) => {
            fillGridSettings(saved);
            $("#gridSettingsStatus").text("Guardado. Los estados se confirman nuevamente con estos ajustes.");
        })
        .fail((xhr) => $("#gridSettingsStatus").text(xhr.responseText || "No se pudo confirmar el guardado. Recargue los ajustes."))
        .always(() => $("#gridSettingsFields, #btnLoadGridSettings").prop("disabled", false));
}

// Crea el bloque de opciones del medidor.
function createMeterSettingsHtml() {
    const actions = '<button class="btn btn-sm btn-outline-secondary" id="btnLoadSettings" type="button">Recargar</button>' +
        '<button class="btn btn-sm btn-success" id="btnSaveMeterOptions" type="button" disabled>Guardar</button>';
    let html = '<div class="settingsSection">' + createSettingsHeaderHtml("Captura de eventos", "Umbrales Dip/Swell, sobrecorriente y tamaño de las capturas.", actions);
    html += '<div class="settingsStatus settingsStatusLine small text-muted" role="status"></div>';
    html += '<fieldset id="captureFields" disabled><div class="settingsGroupGrid">' + createOptionGroupsHtml(meterOptionGroups, "meter") + "</div></fieldset></div>";
    return html;
}

// Crea el bloque compacto de calibración.
function createCalibrationSettingsHtml() {
    const actions = '<button class="btn btn-sm btn-outline-secondary" id="btnLoadCalibration" type="button">Recargar</button>' +
        '<button class="btn btn-sm btn-success" id="btnSaveCalibration" type="button" disabled>Guardar</button>' +
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
    html += "</tbody></table></div></fieldset></div>";
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
    return '<div class="settingsField settingsFieldStack"><label for="' + id + '">' + field.label + '</label><div class="input-group input-group-sm"><input class="form-control optionInput" type="text" id="' + id + '" data-key="' + field.key + '" data-type="' + field.type + '">' + unit + "</div></div>";
}

// Devuelve el id de un input de opción.
// prefix: prefijo del grupo. key: clave de Preferences.
function getOptionInputId(prefix, key) {
    return "opt_" + prefix + "_" + key;
}

let settingsLoadQueue = Promise.resolve();

// Carga section solo al abrirla; force permite recargar explicitamente. Serializa consultas de ajustes.
function loadSettings(section = $("#settingsTabs .active").data("section"), force = false) {
    if (!["grid", "history", "storage", "capture", "calibration"].includes(section)) return;
    const panel = $("#settings_" + section);
    if (panel.data("loading") || (!force && panel.data("loaded"))) return settingsLoadQueue;
    panel.data("loading", true);
    settingsLoadQueue = settingsLoadQueue.then(async () => {
        try {
            if (section === "grid") await loadGridSettings();
            else if (section === "history") await loadHistorySettings();
            else if (section === "storage") await loadStorageStatus();
            else {
                $("#" + section + "Fields").prop("disabled", true);
                $(section === "capture" ? "#btnSaveMeterOptions" : "#btnSaveCalibration").prop("disabled", true);
                setSettingsStatus("Cargando opciones...", false, section);
                const values = await readOptions(section === "capture" ? "meter" : "ADE9000",
                    section === "capture" ? getMeterOptionKeys() : getCalibrationKeys());
                if (values.error) throw new Error(values.error);
                if (section === "capture") fillMeterOptions(values);
                else fillCalibrationOptions(values);
                $("#" + section + "Fields").prop("disabled", false);
                $(section === "capture" ? "#btnSaveMeterOptions" : "#btnSaveCalibration").prop("disabled", false);
                setSettingsStatus("Opciones cargadas.", false, section);
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

// Lee opciones de un namespace.
// namespace: nombre del namespace. keys: claves a leer.
function readOptions(namespace, keys) {
    let params = new URLSearchParams();
    params.append("ns", namespace);
    keys.forEach((key) => params.append(key, ""));
    return $.ajax({type: "GET", url: board.IP + "/options?" + params.toString(), cache: false, timeout: Math.max(board.timeout, 3000)});
}

// Escribe opciones en un namespace.
// namespace: nombre del namespace. values: objeto clave valor.
function writeOptions(namespace, values) {
    return $.ajax({type: "POST", url: board.IP + "/options", data: {...values, ns: namespace}, cache: false, timeout: Math.max(board.timeout, 5000)});
}

// Devuelve las claves permitidas para una seccion del respaldo.
// section: identificador de seccion.
function getBackupSectionKeys(section) {
    if (section === "network") return backupNetworkKeys;
    if (section === "grid") return backupGridKeys;
    if (section === "history") return backupHistoryKeys;
    if (section === "capture") return getMeterOptionKeys();
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
    if (keys.some((key) => values[key] === null || !["string", "number", "boolean"].includes(typeof values[key])))
        throw new Error("Contiene un valor con formato inválido.");
    if (section === "grid" && !backupGridKeys.filter((key) => !key.startsWith("neutral")).every((key) => Object.prototype.hasOwnProperty.call(values, key)))
        throw new Error("Faltan límites o tiempos de red.");
    if (section === "history" && (!backupHistoryKeys.every((key) => Object.prototype.hasOwnProperty.call(values, key)) ||
        ![true, false, 0, 1, "true", "false", "0", "1"].includes(values.enabled) ||
        !Number.isInteger(Number(values.groupConfig)) || Number(values.groupConfig) < 0 || Number(values.groupConfig) > 0xFFFFFF ||
        !Number.isInteger(Number(values.samplePeriodDeciseconds)) || Number(values.samplePeriodDeciseconds) < 1 || Number(values.samplePeriodDeciseconds) > 3000))
        throw new Error("La configuración histórica no es válida.");
    if (section === "energy" && keys.some((key) => !Number.isFinite(Number(values[key]))))
        throw new Error("Los totalizadores deben ser números finitos.");
    if (section === "grid" && keys.some((key) => !Number.isFinite(Number(values[key]))))
        throw new Error("Los ajustes de red deben ser numéricos.");
    if (section === "calibration" && keys.some((key) => ["neutralRms", "neutralFast"].includes(key) ?
        !Number.isFinite(Number(values[key])) || Number(values[key]) < 0.1 || Number(values[key]) > 10 : !isHex32(String(values[key]))))
        throw new Error("La calibración debe contener enteros hexadecimales de 32 bits.");
    return true;
}

// Lee una seccion y completa solo opciones de captura ausentes con sus valores predeterminados del firmware.
// section: identificador de seccion.
async function readBackupSection(section) {
    let values;
    let defaultsUsed = [];
    if (section === "network") {
        const response = await $.ajax({url: board.IP + "/network/config", cache: false, timeout: Math.max(board.timeout, 5000)});
        const keys = getBackupSectionKeys(section);
        const missingKeys = keys.filter((key) => !Object.prototype.hasOwnProperty.call(response, key));
        if (missingKeys.length) throw new Error("Opciones de red ausentes: " + missingKeys.join(", ") + ".");
        values = Object.fromEntries(keys.map((key) => [key, response[key]]));
    } else if (section === "capture" || section === "calibration") {
        const namespace = section === "capture" ? "meter" : "ADE9000";
        const keys = getBackupSectionKeys(section);
        const response = await readOptions(namespace, keys);
        const missingKeys = keys.filter((key) => !Object.prototype.hasOwnProperty.call(response, key) || response[key] === "key no existe");
        const invalidKeys = keys.filter((key) => response[key] === "invalid type");
        defaultsUsed = section === "capture" ? missingKeys.filter((key) => Object.prototype.hasOwnProperty.call(backupCaptureDefaults, key)) : [];
        const unresolvedKeys = missingKeys.filter((key) => !defaultsUsed.includes(key));
        if (invalidKeys.length || unresolvedKeys.length)
            throw new Error("Opciones ausentes o inválidas: " + [...unresolvedKeys, ...invalidKeys].join(", ") + ".");
        values = Object.fromEntries(keys.map((key) => {
            const value = defaultsUsed.includes(key) ? backupCaptureDefaults[key] : response[key];
            return [key, section === "calibration" && !["neutralRms", "neutralFast"].includes(key) ? toHex32(value) : value];
        }));
    } else if (section === "grid") {
        const response = await $.ajax({url: board.IP + "/grid/config", cache: false, timeout: Math.max(board.timeout, 5000)});
        values = Object.fromEntries(backupGridKeys.map((key) => [key, response[key]]));
    } else if (section === "history") {
        const response = await $.ajax({url: board.IP + "/history/config", cache: false, timeout: Math.max(board.timeout, 5000)});
        values = {enabled: response.enabled, groupConfig: historyOptionsToGroupConfig(response),
            samplePeriodDeciseconds: Math.round(Number(response.samplePeriod) * 10)};
    } else if (section === "energy") {
        const response = await $.ajax({url: board.IP + "/meter?energy=1", cache: false, timeout: Math.max(board.timeout, 5000)});
        values = {
            rWattH: response.wattH.r, rVarH: response.varH.r, rVaH: response.vaH.r,
            sWattH: response.wattH.s, sVarH: response.varH.s, sVaH: response.vaH.s,
            tWattH: response.wattH.t, tVarH: response.varH.t, tVaH: response.vaH.t,
            WattH: response.wattH.tot, VarH: response.varH.tot, VaH: response.vaH.tot,
        };
    } else throw new Error("Sección desconocida.");
    validateBackupSection(section, values);
    return {values, defaultsUsed};
}

// Descarga un respaldo con las secciones seleccionadas en la interfaz.
async function exportSettingsBackup() {
    const selected = $(".backupExportSection:checked").map((_, input) => input.value).get();
    if (!selected.length) { $("#settingsBackupStatus").text("Seleccioná al menos una sección."); return; }
    const button = $("#btnExportSettingsBackup").prop("disabled", true);
    const status = $("#settingsBackupStatus").empty();
    const sections = {};
    try {
        for (const key of selected) {
            const label = settingsBackupSections.find((item) => item.key === key).label;
            const row = $("<li>").text(label + ": leyendo...").appendTo(status);
            const result = await readBackupSection(key);
            sections[key] = result.values;
            const fallbackNames = result.defaultsUsed.map((field) => field === "eventPrevSamples" ? "muestras previas (100)" : field === "eventPostSamples" ? "muestras posteriores (100)" : "muestras máximas (300)");
            row.text(label + ": lista." + (fallbackNames.length ? " Se usaron valores predeterminados no guardados: " + fallbackNames.join(", ") + "." : ""));
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
        button.prop("disabled", false);
    }
}

// Carga y valida un archivo JSON para restaurar.
// file: archivo elegido por el usuario.
async function loadSettingsBackup(file) {
    loadedSettingsBackup = null;
    $("#settingsBackupImportSections").empty();
    $("#btnImportSettingsBackup").prop("disabled", true);
    $("#btnRestartAfterImport").prop("hidden", true);
    if (!file) return;
    try {
        const backup = JSON.parse(await file.text());
        if (backup.format !== "pm9000-config" || backup.version !== 1 || !backup.sections || typeof backup.sections !== "object")
            throw new Error("El archivo no es un respaldo Power Meter compatible.");
        const available = settingsBackupSections.filter(({key}) => Object.prototype.hasOwnProperty.call(backup.sections, key));
        if (!available.length) throw new Error("El archivo no contiene secciones conocidas.");
        available.forEach(({key, label}) => {
            validateBackupSection(key, backup.sections[key]);
            $("#settingsBackupImportSections").append('<div class="form-check"><input class="form-check-input backupImportSection" type="checkbox" id="backupImport_' + key + '" value="' + key + '" checked><label class="form-check-label" for="backupImport_' + key + '">' + label + '</label></div>');
        });
        loadedSettingsBackup = backup;
        $("#settingsBackupWarning").removeClass("text-danger").text("Respaldo válido. " + available.length + " secciones; todas seleccionadas.");
        $("#btnImportSettingsBackup").prop("disabled", false);
    } catch (error) {
        $("#settingsBackupWarning").text("No se puede usar el archivo: " + error.message).addClass("text-danger");
    }
}

// Aplica una seccion usando su endpoint especifico.
// section: identificador. values: configuracion validada de esa seccion.
async function applyBackupSection(section, values) {
    if (section === "network") return $.ajax({type: "POST", url: board.IP + "/network/config", data: values, timeout: Math.max(board.timeout, 5000)});
    if (section === "grid") return $.ajax({type: "POST", url: board.IP + "/grid/config", data: values, timeout: Math.max(board.timeout, 5000)});
    if (section === "history") return $.ajax({type: "POST", url: board.IP + "/history/config", data: historyBackupToOptions(values), timeout: Math.max(board.timeout, 5000)});
    if (section === "capture") {
        await writeOptions("meter", values);
        return $.ajax({type: "POST", url: board.IP + "/options/reload", timeout: Math.max(board.timeout, 5000)});
    }
    if (section === "energy") return writeOptions("meter", values);
    if (section === "calibration") return writeOptions("ADE9000", values);
    throw new Error("Sección desconocida.");
}

// Restaura las secciones marcadas, una por una, y deja visible el resultado de cada una.
async function importSettingsBackup() {
    if (!loadedSettingsBackup) return;
    let selected = $(".backupImportSection:checked").map((_, input) => input.value).get();
    if (!selected.length) { $("#settingsBackupWarning").text("Seleccioná al menos una sección."); return; }
    if (!confirm("Se van a sobrescribir " + selected.length + " secciones del medidor. ¿Continuar?")) return;
    selected = selected.filter((key) => key !== "network").concat(selected.includes("network") ? ["network"] : []);
    settingsBackupRestartRequired = false;
    $("#btnImportSettingsBackup").prop("disabled", true);
    $("#btnRestartAfterImport").prop("hidden", true);
    const status = $("#settingsBackupStatus").empty();
    let failed = false;
    for (const key of selected) {
        const label = settingsBackupSections.find((item) => item.key === key).label;
        const row = $("<li>").text(label + ": aplicando...").appendTo(status);
        try {
            await applyBackupSection(key, loadedSettingsBackup.sections[key]);
            if (["network", "energy", "calibration"].includes(key)) settingsBackupRestartRequired = true;
            row.text(label + (["network", "energy", "calibration"].includes(key) ? ": guardada; requiere reinicio." : ": aplicada."));
        } catch (error) {
            failed = true;
            row.addClass("text-danger").text(label + ": error; puede haber quedado parcialmente escrita. " + (error.responseText || error.message || ""));
        }
    }
    if (settingsBackupRestartRequired) $("#btnRestartAfterImport").prop("hidden", false);
    $("#settingsBackupWarning").text(failed ? "La importación terminó con errores. Revisá cada sección; no hay rollback automático." : "Importación terminada." + (settingsBackupRestartRequired ? " Reiniciá para aplicar los cambios pendientes." : ""));
    $("#btnImportSettingsBackup").prop("disabled", false);
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
            else input.val(values[field.key]);
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
    setSettingsStatus("Guardando opciones de captura...", false, "capture");
    $("#btnSaveMeterOptions, #btnLoadSettings, #captureFields").prop("disabled", true);
    writeOptions("meter", collectMeterOptionValues())
        .then(() => $.ajax({type: "POST", url: board.IP + "/options/reload", timeout: Math.max(board.timeout, 3000)}))
        .done(() => {
            setSettingsStatus("Opciones de captura guardadas y aplicadas.", false, "capture");
        })
        .fail(() => setSettingsStatus("No se pudo confirmar el guardado o la aplicación. Recargue las opciones.", true, "capture"))
        .always(() => $("#btnSaveMeterOptions, #btnLoadSettings, #captureFields").prop("disabled", false));
}

// Guarda los valores de calibración.
function saveCalibrationOptions() {
    let values = collectCalibrationValues();
    if (values == null) return;
    setSettingsStatus("Guardando calibración...", false, "calibration");
    $("#btnSaveCalibration, #btnLoadCalibration, #btnRestoreCalibration, #calibrationFields").prop("disabled", true);
    writeOptions("ADE9000", values)
        .done(() => setSettingsStatus("Calibración guardada. Reinicie para aplicarla.", false, "calibration"))
        .fail(() => setSettingsStatus("No se pudo guardar la calibración.", true, "calibration"))
        .always(() => $("#btnSaveCalibration, #btnLoadCalibration, #btnRestoreCalibration, #calibrationFields").prop("disabled", false));
}

// Restaura la calibración por defecto.
function restoreDefaultCalibration() {
    if (!confirm("¿Restaurar la calibración por defecto? Se reemplazarán los valores guardados.")) return;
    setSettingsStatus("Restaurando calibración por defecto...", false, "calibration");
    $.ajax({type: "POST", url: board.IP + "/calibration/restore", timeout: board.timeout})
        .done(() => {
            loadSettings("calibration", true).then(() => setSettingsStatus("Calibración por defecto restaurada. Reinicie para aplicarla.", false, "calibration"));
        })
        .fail(() => setSettingsStatus("No se pudo restaurar la calibración.", true, "calibration"));
}

// Recolecta los valores de opciones del medidor.
function collectMeterOptionValues() {
    let values = {};
    meterOptionGroups.forEach((group) => {
        group.fields.forEach((field) => {
            const input = $("#" + getOptionInputId("meter", field.key));
            values[field.key] = field.type == "bool" ? (input.prop("checked") ? "1" : "0") : input.val();
        });
    });
    return values;
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

// Inicializa el grafico de eventos cuando sus librerias estan disponibles.
function initEventChart() {
    if (typeof Scope == "undefined" || typeof CanvasJS == "undefined") {
        showEventError("No se pudo cargar el modulo de eventos.");
        return false;
    }
    Scope.stop();
    Scope.begin("eventChartContainer", null, "event");
    return true;
}

function meterLoadEventsList() {
    if (!initEventChart()) return;
    $("#eventList").html('<div class="text-muted p-2">Cargando eventos...</div>');
    board.loadNodeFileList(function (res) {
        console.log("Lista de archivos cargada!");
        res = parseNodeFileList(res);
        if (!Array.isArray(res)) {
            showEventError("Node-RED no devolvió una lista de eventos válida.");
            return;
        }

        let events = [];
        res.forEach((path) => {
            //Path: /share/PowerMeterLogs/ACevent_1970-7-21_13-29-36.csv
            let data = path.replaceAll("/share/PowerMeterLogs/", "");
            let fase = "",
                type = "",
                time,
                format = "csv";
            if (data.endsWith(".json")) {
                data = data.split(".");
                time = data[0];
                fase = data[1];
                type = data[2];
                format = "json";
            } else if (data.endsWith(".csv")) {
                data = data.replaceAll("ACevent_", "");
                time = data.split(".")[0];
            }
            else 
                return; //No es un archivo de evento válido (se rompía con archivos que no eran .json o .csv)

            let [datePart, timePart] = time.split(".")[0].split("_");
            if (!datePart || !timePart) return;
            let [year, month, day] = datePart.split("-").map(Number); // "7" → 7
            let [hours, minutes, seconds] = timePart.split("-").map(Number);
            const date = new Date(year, month - 1, day, hours, minutes, seconds);
            if (isNaN(date.getTime())) return;

            events.push({path: path, date: date, phase: fase, type: type});
        });
        events.sort((a, b) => b.date - a.date);

        if (events.length == 0) {
            showEventError("No se encontraron archivos de eventos válidos.");
            return;
        }

        let list = "";
        events.forEach((event) => {
            let detail = event.phase.length > 0 ? " | " + event.phase : "";
            if (detail.length > 0 && event.type.length > 0) detail += " | ";
            detail += event.type;
            list += '<a href="' + event.path + '" class="list-group-item">' + event.date.toLocaleString() + detail + "</a>";
        });

        $("#eventList").html("<ul class='list-group'>" + list + "</ul>");

        $("#eventList").off("click", ".list-group-item");
        $("#eventList").on("click", ".list-group-item", function (e) {
            e.preventDefault();
            const path = $(this).attr("href"); // Si el href tenía la ruta completa
            board.loadNodeFile(
                path,
                function (res) {
                    try {
                        if (typeof Scope == "undefined" || !Scope.chart) throw new Error("gráfico no inicializado");
                        let event = meterParseEventFile(res);
                        Scope.loadEventData(event);
                    }
                    catch (error) {
                        showEventError("No se pudo interpretar el archivo de evento: " + error.message);
                    }
                },
                function () {
                    showEventError("Fallo al leer el archivo de evento.");
                }
            );
        });
    }, function (error) {
        showEventError("No se pudo conectar con Node-RED para cargar la lista de eventos." + getAjaxErrorText(error));
    });
}

// Actualiza los eventos persistidos en Node-RED y los eventos temporales de RAM.
function refreshEventLists() {
    loadRamEventsList();
    meterLoadEventsList();
}

// Genera un evento virtual en el firmware para probar el flujo Manager.
function createVirtualEvent() {
    $("#eventRamStatus").text("Iniciando captura de evento...").removeClass("text-danger");
    $.ajax({type: "POST", url: board.IP + "/events/test", timeout: board.timeout})
        .done((event) => {
            renderRamEventStatus("Captura iniciada. Esperando cierre...");
            setTimeout(loadRamEventsList, 1800);
        })
        .fail((error) => {
            renderRamEventStatus("No se pudo generar el evento virtual." + getAjaxErrorText(error), true);
        });
}

// Genera buffers sinteticos sin capturar el ADE; sin parametros.
function createRampTestEvent() {
    $("#eventRampBtn").prop("disabled", true);
    renderRamEventStatus("Generando rampa de prueba en RAM...");
    $.ajax({type: "POST", url: board.IP + "/events/test", data: {pattern: "ramp"}, cache: false, timeout: board.timeout})
        .done((data) => {
            renderRamEventStatus("Rampa generada: " + data.eventId);
            setTimeout(loadRamEventsList, 500);
        })
        .fail((error) => {
            const reason = error.responseJSON && error.responseJSON.reason;
            renderRamEventStatus("No se pudo generar la rampa." + (reason ? " " + reason : getAjaxErrorText(error)), true);
        })
        .always(() => { $("#eventRampBtn").prop("disabled", false); });
}

// Carga la lista de eventos retenidos en RAM por el firmware.
function loadRamEventsList() {
    $("#eventRamStatus").text("Cargando eventos RAM...").removeClass("text-danger");
    $.ajax({type: "GET", url: board.IP + "/events", timeout: board.timeout})
        .done((data) => {
            renderRamEventsList(data);
        })
        .fail((error) => {
            renderRamEventStatus("No se pudo leer /events." + getAjaxErrorText(error), true);
        });
}

// Muestra un estado corto del bloque de eventos RAM.
// message: texto a mostrar. isError: indica si debe mostrarse como error.
function renderRamEventStatus(message, isError = false) {
    $("#eventRamStatus").text(message).toggleClass("text-danger", isError).toggleClass("text-muted", !isError);
}

// Renderiza los eventos RAM disponibles para descarga local.
// data: respuesta de /events.
function renderRamEventsList(data) {
    if (!data || !Array.isArray(data.events)) {
        renderRamEventStatus("La placa no devolvio una lista valida.", true);
        return;
    }
    renderRamEventStatus(data.events.length + " evento(s) RAM disponible(s).");
    if (data.events.length == 0) {
        $("#eventRamList").html('<div class="small text-muted">No hay eventos RAM.</div>');
        return;
    }

    let html = '<div class="list-group small">';
    data.events.forEach((event) => {
        const id = escapeHtml(event.eventId || "");
        const fastRms = event.fastRms || {};
        const wave = event.wave || {};
        const links = event.links || {};
        const fastRmsUrl = links.fastRms ? board.IP + links.fastRms : "";
        const waveUrl = links.wave ? board.IP + links.wave : "";
        html += '<div class="list-group-item p-2">';
        html += '<div class="fw-semibold">' + id + " - " + escapeHtml(event.type || "evento") + "</div>";
        html += '<div>Fast RMS: ' + (fastRms.available ? "si" : "no") + (fastRms.available ? " (" + (fastRms.sampleCount || 0) + " muestras)" : " (" + escapeHtml(fastRms.reason || "sin detalle") + ")") + "</div>";
        html += '<div>Wave: ' + (wave.available ? "si" : "no") + (wave.available ? " (" + (wave.samplesPerChannel || 0) + " muestras/canal)" : " (" + escapeHtml(wave.reason || "sin detalle") + ")") + "</div>";
        html += '<div class="d-flex flex-wrap gap-1 mt-1">';
        if (fastRms.available && fastRmsUrl) html += '<a class="btn btn-sm btn-outline-primary py-0" href="' + escapeHtml(fastRmsUrl) + '" target="_blank">Fast RMS</a>';
        if (wave.available && waveUrl) html += '<a class="btn btn-sm btn-outline-primary py-0" href="' + escapeHtml(waveUrl) + '" target="_blank">Wave</a>';
        html += '<button class="btn btn-sm btn-outline-danger py-0 event-clear-btn" data-event-id="' + id + '">Borrar</button>';
        html += "</div></div>";
    });
    html += "</div>";
    $("#eventRamList").html(html);
    $("#eventRamList").off("click", ".event-clear-btn");
    $("#eventRamList").on("click", ".event-clear-btn", function () {
        clearRamEvent($(this).data("event-id"));
    });
}

// Borra de RAM un evento descargado o de prueba.
// eventId: id del evento a borrar.
function clearRamEvent(eventId) {
    if (!eventId) return;
    renderRamEventStatus("Borrando evento " + eventId + "...");
    $.ajax({type: "POST", url: board.IP + "/events/clear", data: {eventId}, timeout: board.timeout})
        .done(() => {
            renderRamEventStatus("Evento borrado: " + eventId);
            loadRamEventsList();
        })
        .fail((error) => {
            renderRamEventStatus("No se pudo borrar el evento." + getAjaxErrorText(error), true);
        });
}

// Convierte la respuesta de Node-RED en una lista de archivos.
// value: respuesta recibida desde Node-RED.
function parseNodeFileList(value) {
    if (Array.isArray(value)) return value;
    if (typeof value != "string") return value;
    try {
        return JSON.parse(value);
    } catch (error) {
        return value;
    }
}

// Devuelve un detalle corto de error Ajax para mostrar al usuario.
// error: objeto de error recibido desde jQuery.
function getAjaxErrorText(error) {
    if (!error || !error.status) return "";
    return " Código: " + error.status + ".";
}

// Muestra un error en la pestaña de eventos.
// message: texto que se muestra al usuario.
function showEventError(message) {
    $("#eventList").html('<div class="alert alert-warning m-2">' + escapeHtml(message) + "</div>");
    $("#eventChartContainer").html('<div class="text-muted p-3">Sin evento cargado.</div>');
}

// Escapa texto para insertarlo como HTML seguro.
// text: valor a convertir en texto seguro.
function escapeHtml(text) {
    return String(text).replace(/[&<>"']/g, (char) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#039;" }[char]));
}

function meterParseEventFile(data) {
    data = JSON.parse(data);
    if (!data || !Array.isArray(data.datos)) throw new Error("formato inválido");
    let resp = {};
    resp.eventInfo = data.eventInfo || {};
    resp.waves = {t: [], vr: [], vs: [], vt: [], ir: [], is: [], it: [], in: []};

    data.datos.forEach((s) => {
        resp.waves.t.push(s[0]);
        resp.waves.vr.push(s[1]);
        resp.waves.vs.push(s[2]);
        resp.waves.vt.push(s[3]);
        resp.waves.ir.push(s[4]);
        resp.waves.is.push(s[5]);
        resp.waves.it.push(s[6]);
        resp.waves.in.push(s[7]);
    });
    return resp;
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
