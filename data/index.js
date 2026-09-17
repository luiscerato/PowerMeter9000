let tableRows = [
    {name: "Voltaje", unit: "vrms", visible: true, decimals: 2, source: "meter"},
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
    $("#gaugeVoltaje").simpleGauge(getGaugeOptions("Voltaje"));
    $("#gaugeVoltajeF").simpleGauge(getGaugeOptions("Voltaje Fases"));
    $("#gaugeCorriente").simpleGauge(getGaugeOptions("Corriente"));
    $("#gaugePotencia").simpleGauge(getGaugeOptions("Potencia"));

    createTable();
    initSettingsUI();

    board.getResponseRepeat("meter", meterOk, meterFail, "fast");

    board.getResponseRepeat("meter?energy", energyOk, energyFail, "slow");

    board.getResponseRepeat("meter?angles", angleOk, angleFail, "slow");

    board.getResponseRepeat("meter?thd", thdOk, thdFail, "slow");


    $("#eventList").html('<div class="text-muted p-2">Seleccione la pestaña Eventos para cargar la lista.</div>');
    $("#eventRamList").html("");
    $("#eventChartContainer").html('<div class="text-muted p-3">Sin evento cargado.</div>');
    $("#eventTestBtn").on("click", createVirtualEvent);
    $("#eventRampBtn").on("click", createRampTestEvent);
    $("#eventRefreshBtn").on("click", refreshEventLists);

    $(window).on("resize", function () {
        let width = $(window).width();
        let height = $(window).height();
        let size = 200;

        if (width > height) {
            //landscape
            size = width / 4.5;
            if (size > height / 1.5) size = height / 1.5;
        } else {
            size = height / 2.5;
            if (size > width / 1.5) size = width / 1.5;
        }

        size = Math.round(size / 25.0);
        size *= 25;
        if (size < 180) size = 180;
        if (size > 320) size = 320;

        console.log(width, height, size);
    });

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

// Inicializa la pestaña de opciones.
function initSettingsUI() {
    $("#settings").html(createSettingsHtml());
    $("#btnLoadSettings").click(() => loadSettings("capture", true));
    $("#btnLoadCalibration").click(() => loadSettings("calibration", true));
    $("#btnSaveMeterOptions").click(saveMeterOptions);
    $("#btnSaveCalibration").click(saveCalibrationOptions);
    $("#btnRestoreCalibration").click(restoreDefaultCalibration);
    $("#gridSettingsForm").on("submit", saveGridSettings);
    $("#btnLoadGridSettings").on("click", () => loadSettings("grid", true));
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
    const sections = [["grid", "Estados de red"], ["capture", "Captura de eventos"], ["calibration", "Calibración"]];
    sections.forEach(([key, label], i) => {
        html += '<li class="nav-item" role="presentation"><button class="nav-link' + (i ? '' : ' active') + '" id="settingsTab_' + key + '" data-section="' + key + '" data-bs-toggle="tab" data-bs-target="#settings_' + key + '" type="button" role="tab" aria-controls="settings_' + key + '" aria-selected="' + !i + '">' + label + '</button></li>';
    });
    html += '</ul><div class="tab-content">';
    const contents = [createGridSettingsHtml(), createMeterSettingsHtml(), createCalibrationSettingsHtml()];
    sections.forEach(([key], i) => {
        html += '<section class="tab-pane settingsPane' + (i ? '' : ' active') + '" id="settings_' + key + '" role="tabpanel" aria-labelledby="settingsTab_' + key + '">';
        html += contents[i] + '</section>';
    });
    html += '</div></div>';
    return html;
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
    ];
    fields.forEach(([key, label, min, max, step]) => {
        html += '<div class="settingsField"><label for="grid_' + key + '">' + label + '</label><input class="form-control form-control-sm" required type="number" id="grid_' + key + '" name="' + key + '" min="' + min + '" max="' + max + '" step="' + step + '"></div>';
    });
    html += '</div></div></fieldset></form>';
    return html;
}

// Crea el encabezado comun de una seccion; title y description son textos, actions contiene sus botones.
function createSettingsHeaderHtml(title, description, actions) {
    return '<header class="settingsSectionHeader"><div><h2>' + title + '</h2><p>' + description +
        '</p></div><div class="settingsActions">' + actions + '</div></header>';
}

// Completa el formulario con values recibidos del medidor.
function fillGridSettings(values) {
    $("#gridSettingsForm input[name]").each(function () { $(this).val(values[this.name]); });
}

// Lee ajustes una sola vez por solicitud, sin consultas periodicas; sin parametros.
function loadGridSettings() {
    $("#gridSettingsFields").prop("disabled", true);
    $("#gridSettingsStatus").text("Cargando...");
    return $.ajax({url: board.IP + "/gridopts", cache: false, timeout: Math.max(board.timeout, 3000)})
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
    const absence = Number(values.noVoltage), hysteresis = Number(values.hysteresis);
    if (["R", "S", "T"].some((phase) => Number(values["low" + phase]) <= absence + hysteresis ||
        Number(values["high" + phase]) <= Number(values["low" + phase]) + 2 * hysteresis)) {
        $("#gridSettingsStatus").text("Los límites deben dejar espacio para la histéresis: sin tensión < bajo < alto.");
        return;
    }
    $("#gridSettingsFields, #btnLoadGridSettings").prop("disabled", true);
    $("#gridSettingsStatus").text("Guardando...");
    $.ajax({type: "POST", url: board.IP + "/gridopts", data: values, timeout: Math.max(board.timeout, 5000)})
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
    if (!["grid", "capture", "calibration"].includes(section)) return;
    const panel = $("#settings_" + section);
    if (panel.data("loading") || (!force && panel.data("loaded"))) return settingsLoadQueue;
    panel.data("loading", true);
    settingsLoadQueue = settingsLoadQueue.then(async () => {
        try {
            if (section === "grid") await loadGridSettings();
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
            if (section !== "grid") setSettingsStatus("No se pudieron cargar las opciones.", true, section);
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
    return $.ajax({type: "GET", url: board.IP + "/opts?" + params.toString(), cache: false, timeout: Math.max(board.timeout, 3000)});
}

// Escribe opciones en un namespace.
// namespace: nombre del namespace. values: objeto clave valor.
function writeOptions(namespace, values) {
    let params = new URLSearchParams();
    params.append("ns", namespace);
    Object.keys(values).forEach((key) => params.append(key, values[key]));
    return $.ajax({type: "GET", url: board.IP + "/opts?" + params.toString(), cache: false, timeout: Math.max(board.timeout, 3000)});
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
        .then(() => $.ajax({type: "GET", url: board.IP + "/reloadopts", timeout: Math.max(board.timeout, 3000)}))
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
    $.ajax({type: "GET", url: board.IP + "/restorecal", timeout: board.timeout})
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
    $.ajax({type: "GET", url: board.IP + "/api/v1/events/test", timeout: board.timeout})
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
    $.ajax({type: "GET", url: board.IP + "/api/v1/events/test?pattern=ramp", cache: false, timeout: board.timeout})
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
    $.ajax({type: "GET", url: board.IP + "/api/v1/events", timeout: board.timeout})
        .done((data) => {
            renderRamEventsList(data);
        })
        .fail((error) => {
            renderRamEventStatus("No se pudo leer /api/v1/events." + getAjaxErrorText(error), true);
        });
}

// Muestra un estado corto del bloque de eventos RAM.
// message: texto a mostrar. isError: indica si debe mostrarse como error.
function renderRamEventStatus(message, isError = false) {
    $("#eventRamStatus").text(message).toggleClass("text-danger", isError).toggleClass("text-muted", !isError);
}

// Renderiza los eventos RAM disponibles para descarga local.
// data: respuesta de /api/v1/events.
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
    $.ajax({type: "POST", url: board.IP + "/api/v1/events/clear?eventId=" + encodeURIComponent(eventId), timeout: board.timeout})
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

// Muestra estados confirmados; states ausente indica que no hay informacion disponible.
function showGridStates(states = {}) {
    const labels = {normal: "Normal", undervoltage: "Bajo voltaje", overvoltage: "Alto voltaje",
        phase_loss: "Sin tensión", abnormal: "Fuera de rango", outage: "Corte total"};
    ["r", "s", "t", "overall"].forEach((key) => {
        const state = states[key];
        const color = state === "normal" ? "text-success" :
            state === "phase_loss" || state === "outage" ? "text-danger" : labels[state] ? "text-warning" : "text-muted";
        $("#gridState_" + key).attr("class", "fw-bold " + color).text(labels[state] || "Sin datos");
    });
}

function meterOk(resp) {
    showGridStates(resp.gridState);
    if (meterOk.fail) $(".meter").removeClass("notLoading");
    meterOk.fail = false;

    //Actualizar los gauge
    $("#gaugeVoltaje").simpleGauge("setValue", resp.vrms.avg);
    $("#gaugeVoltajeF").simpleGauge("setValue", resp.vvrms.avg);
    $("#gaugeCorriente").simpleGauge("setValue", resp.irms.avg);
    $("#gaugePotencia").simpleGauge("setValue", (resp.watt.avg / 1000).toFixed(1));

    // //Llenar las celdas de voltaje
    $(".vvrmsR").text(formatNumber(resp.vvrms.r, "V"));
    $(".vvrmsS").text(formatNumber(resp.vvrms.s, "V"));
    $(".vvrmsT").text(formatNumber(resp.vvrms.t, "V"));
    $(".vvrmsAvg").text(formatNumber(resp.vvrms.avg, "V"));

    $(".vrmsR").text(formatNumber(resp.vrms.r, "V"));
    $(".vrmsS").text(formatNumber(resp.vrms.s, "V"));
    $(".vrmsT").text(formatNumber(resp.vrms.t, "V"));
    $(".vrmsN").text(formatNumber(resp.vrms.n, "V"));
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

function getGaugeOptions(signal) {
    let options = {
        min: 0,
        max: 100,
        value: 0,
        type: "analog digital",
        container: {
            scale: 95, // scale of gauge, in percent
            style: "background: #ddd; background: linear-gradient(335deg, #ddd 0%, #fff 30%, #fff 50%, #bbb 100%); border-radius: 1000px; border: 8px solid #bbb;",
        },
        title: {
            text: signal,
            style: "color: #555; font-size: 20px; padding: 5px;",
        },
        digital: {
            text: "{value.2} V", // value with number of decimals
            style: "color: auto; font-size: 30px; z-index=10; ",
        },
        analog: {
            minAngle: -150,
            maxAngle: 150,
        },
        labels: {
            text: "{value}",
            count: 10,
            scale: 72,
            style: "font-size: 20px;",
        },
        ticks: {
            count: 14,
            scale1: 84,
            scale2: 93,
            style: "width: 2px; color: #999;",
        },
        subTicks: {
            count: 5,
            scale1: 93,
            scale2: 96,
            style: "width: 1px; color: #bbb;",
        },
        bars: {
            scale1: 88,
            scale2: 94,
        },
        pointer: {
            scale: 95,
            style: "color: #000000; opacity: 0.8; filter: drop-shadow(-3px 3px 2px rgba(0, 0, 0, .7));",
        },
    };

    options.title.text = "";

    if (signal.toLowerCase() == "voltaje") {
        options.digital.text = "{value.1} V";
        options.min = 180;
        options.max = 260;
        options.labels.count = 8;
        options.ticks.count = 8;
        options.bars.colors = [
            [180, "#dd2222", 0, 0],
            [190, "#ffa500", 0, 0],
            [200, "#378618", 0, 0],
            [240, "#ffa500", 0, 0],
            [250, "#dd2222", 0, 0],
        ];
    } else if (signal.toLowerCase() == "voltaje fases") {
        options.digital.text = "{value.1} V";
        options.min = 340;
        options.max = 420;
        options.labels.count = 8;
        options.ticks.count = 16;
        options.bars.colors = [
            [340, "#dd2222", 0, 0],
            [350, "#ffa500", 0, 0],
            [360, "#378618", 0, 0],
            [400, "#ffa500", 0, 0],
            [410, "#dd2222", 0, 0],
        ];
    } else if (signal.toLowerCase() == "corriente") {
        options.digital.text = "{value.3} A";
        options.min = 0;
        options.max = 100;
        options.labels.count = 10;
        options.bars.colors = [
            [0, "#378618", 0, 0],
            [70, "#ffa500", 0, 0],
            [85, "#dd2222", 0, 0],
        ];
    } else if (signal.toLowerCase() == "potencia") {
        options.digital.text = "{value.3} kW";
        options.min = 0;
        options.max = 20;
        options.labels.count = 10;
        options.bars.colors = [
            [0, "#378618", 0, 0],
            [14, "#ffa500", 0, 0],
            [17, "#dd2222", 0, 0],
        ];
    }

    return options;
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
                    else row += `<td id="pt_${id}" class="valueTable ${rowItem.source} ${id}"></td>`;
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
