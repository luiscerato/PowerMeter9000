const historyChartNames = {voltage: 'Voltajes', current: 'Corrientes', lineVoltage: 'V entre fases', activePower: 'P activa', reactivePower: 'P reactiva', apparentPower: 'P aparente', energy: 'Energia', powerFactor: 'FP', frequency: 'Frecuencia', voltageThd: 'THD V', currentThd: 'THD I', voltageAngle: 'Angulos', batteryVoltage: 'Vbat', batteryInputVoltage: 'Vbus', batteryCurrent: 'I carga', batteryLevel: 'Nivel bateria', connectivity: 'Estados'};
let historyViewer = {charts: {}, records: [], seconds: 3600, loading: false, invalidPages: 0,
    visible: Object.fromEntries(Object.keys(historyChartNames).map(key => [key,key === 'voltage'])),
    order: Object.keys(historyChartNames), scales: {}, ranges: {}, linked: true, syncing: false, lastRange: null, seriesVisibility: {}, pan:{}};
let historyKiloUnits = false;

// Guarda orden, visibilidad, escalas y enlace localmente; sin parametros, no guarda muestras ni consulta el ESP.
function saveHistoryViewOptions() {
    try {localStorage.setItem('powerMeterHistoryView',JSON.stringify({order:historyViewer.order,visible:historyViewer.visible,scales:historyViewer.scales,linked:historyViewer.linked,kilo:historyKiloUnits}));}
    catch (_) { /* El visor funciona tambien cuando el navegador no permite persistencia. */ }
}

// Recupera solo claves y limites conocidos; sin parametros, ignora preferencias locales invalidas.
function loadHistoryViewOptions() {
    try {
        const saved = JSON.parse(localStorage.getItem('powerMeterHistoryView'));
        if (!saved || typeof saved !== 'object') return;
        if (typeof saved.kilo === 'boolean') historyKiloUnits = saved.kilo;
        if (Array.isArray(saved.order)) historyViewer.order = [...new Set([...saved.order.filter(key => Object.hasOwn(historyChartNames,key)),...Object.keys(historyChartNames)])];
        for (const key of Object.keys(historyChartNames)) {
            if (typeof saved.visible?.[key] === 'boolean') historyViewer.visible[key] = saved.visible[key];
            const scale = saved.scales?.[key];
            if (scale && Number.isFinite(scale.min) && Number.isFinite(scale.max) && scale.min < scale.max) historyViewer.scales[key] = {min:scale.min,max:scale.max};
        }
        if (typeof saved.linked === 'boolean') historyViewer.linked = saved.linked;
    } catch (_) { /* Una preferencia danada no debe impedir abrir el historico. */ }
}

// Crea barras compactas y contenedores independientes; sin parametros, conserva los IDs usados por CanvasJS.
function createHistoryChartPanels() {
    const selectors = $('#historyChartSelectors').empty(), list = $('#historyChartList').empty();
    for (const key of historyViewer.order) {
        const title = getHistoryChartConfig(key).title, visible = historyViewer.visible[key];
        $('<button type="button" class="btn btn-sm btn-outline-primary historyChartToggle">').text(historyChartNames[key]).attr('data-chart',key).attr('aria-pressed',visible).toggleClass('active',visible).appendTo(selectors);
        const panel = $('<section class="historyChartPanel">').attr('id','historyPanel'+capitalizeHistoryChart(key)).attr('data-chart',key).toggleClass('d-none',!visible).appendTo(list);
        const bar = $('<div class="historyChartBar">').appendTo(panel);
        $('<h3>').text(title + (getHistoryChartConfig(key).unit ? ' ('+getHistoryChartConfig(key).unit+')' : '')).appendTo(bar);
        for (const [action,label,icon] of [['up','Subir grafica','&#8593;'],['down','Bajar grafica','&#8595;'],['pan','Arrastrar o seleccionar zoom','&#8596;'],['reset','Restablecer zoom','&#8634;'],['scale','Configurar escala','&#9881;']])
            $('<button type="button" class="btn btn-sm btn-outline-secondary">').attr({'data-action':action,title:label,'aria-label':label}).html(icon).appendTo(bar);
        const scale = historyViewer.scales[key], form = $('<form class="historyScaleForm" hidden>').appendTo(panel);
        form.html('<label><input type="checkbox" name="auto"> Auto</label><label>Min <input type="number" name="min" step="any" required></label><label>Max <input type="number" name="max" step="any" required></label><button class="btn btn-sm btn-primary" type="submit">Aplicar</button><span class="small text-danger" role="status"></span>');
        form.find('[name=auto]').prop('checked',!scale);
        form.find('[name=min]').val(scale?.min ?? '').prop('disabled',!scale);
        form.find('[name=max]').val(scale?.max ?? '').prop('disabled',!scale);
        $('<div class="historyChart">').attr('id','historyChart'+capitalizeHistoryChart(key)).html('<div class="text-muted p-3">Sin muestras para mostrar.</div>').appendTo(panel);
    }
    selectors.append($('<label class="historyLinkViews ms-auto">').append($('<input id="historyLinkViews" type="checkbox">').prop('checked',historyViewer.linked),' Sincronizar vistas'));
    updateHistoryOrderButtons();
}

// Actualiza botones de movimiento sobre las graficas visibles; sin parametros.
function updateHistoryOrderButtons() {
    const visible = historyViewer.order.filter(key => historyViewer.visible[key]);
    for (const key of historyViewer.order) {
        const panel = $('#historyPanel'+capitalizeHistoryChart(key)), index = visible.indexOf(key);
        panel.find('[data-action=up]').prop('disabled',index <= 0);
        panel.find('[data-action=down]').prop('disabled',index < 0 || index === visible.length-1);
    }
}

// Mueve key una posicion visible en direction (-1 o 1), sin recrear graficas ni descargar datos.
function moveHistoryChart(key,direction) {
    const visible = historyViewer.order.filter(name => historyViewer.visible[name]), neighbor = visible[visible.indexOf(key)+direction];
    if (!neighbor) return;
    const a = historyViewer.order.indexOf(key), b = historyViewer.order.indexOf(neighbor);
    [historyViewer.order[a],historyViewer.order[b]] = [historyViewer.order[b],historyViewer.order[a]];
    for (const name of historyViewer.order) $('#historyPanel'+capitalizeHistoryChart(name)).appendTo('#historyChartList');
    updateHistoryOrderButtons(); saveHistoryViewOptions();
}

// Aplica la escala del form a key; rechaza limites no finitos o invertidos sin alterar la grafica.
function applyHistoryScale(key,form) {
    const auto = form.find('[name=auto]').prop('checked'), minText = form.find('[name=min]').val(), maxText = form.find('[name=max]').val(), min = Number(minText), max = Number(maxText);
    if (!auto && (!minText || !maxText || !Number.isFinite(min) || !Number.isFinite(max) || min >= max)) {form.find('[role=status]').text('Min debe ser menor que Max.'); return false;}
    if (auto) delete historyViewer.scales[key]; else historyViewer.scales[key] = {min,max};
    form.find('[role=status]').text(''); form.prop('hidden',true);
    saveHistoryViewOptions(); renderHistoryChart(key); return true;
}

// Inicializa los controles del visor histórico; sin parámetros.
function initHistoryViewer() {
    loadHistoryViewOptions(); createHistoryChartPanels();
    loadSharedHistoryUnits();
    const params = new URLSearchParams(location.search), from = params.get("from"), to = params.get("to");
    if (from && to && Number.isFinite(new Date(from).getTime()) && Number.isFinite(new Date(to).getTime())) {
        $("#historyFrom").val(formatHistoryDateInput(new Date(from)));
        $("#historyTo").val(formatHistoryDateInput(new Date(to)));
    } else {
        setHistoryDateRange(3600);
        $("#historyPreset").val("last-3600");
    }
    $("#historyPreset").on("change", function () { applyHistoryPreset($(this).val()); });
    $("#historyFrom,#historyTo").on("input change", () => {$("#historyPreset").val(""); historyViewer.calendarUnit = null;});
    $("#historyLoadButton").on("click", () => loadHistoryViewer());
    $("#historyPrevButton").on("click", () => moveHistoryRange(-1));
    $("#historyNextButton").on("click", () => moveHistoryRange(1));
    $(".historyChartToggle").on("click", function () { const chart = $(this).data("chart"); setHistoryChartVisibility(chart, !historyViewer.visible[chart]); });
    $('#historyLinkViews').on('change',function () {
        historyViewer.linked = this.checked; saveHistoryViewOptions();
        if (this.checked) setHistoryViewport(null,historyViewer.lastRange);
    });
    $('#historyChartList').on('click','[data-action]',function () {
        const key = $(this).closest('[data-chart]').attr('data-chart'), action = $(this).attr('data-action');
        if (action === 'up' || action === 'down') moveHistoryChart(key,action === 'up' ? -1 : 1);
        if (action === 'scale') {const form = $(this).closest('section').find('form'); form.prop('hidden',!form.prop('hidden'));}
        if (action === 'reset') setHistoryViewport(key,null);
        if (action === 'pan') {
            historyViewer.pan[key] = !historyViewer.pan[key];
            $(this).toggleClass('active',historyViewer.pan[key]).attr('aria-pressed',historyViewer.pan[key]);
            const chart = historyViewer.charts[key];
            if (chart) {chart.options.zoomEnabled = chart.zoomEnabled = !historyViewer.pan[key]; chart.options.panEnabled = chart.panEnabled = historyViewer.pan[key]; chart.render(); alignHistoryPlot(chart);}
        }
    }).on('change','[name=auto]',function () {$(this).closest('form').find('[name=min],[name=max]').prop('disabled',this.checked);})
        .on('submit','form',function (event) {event.preventDefault(); applyHistoryScale($(this).closest('[data-chart]').attr('data-chart'),$(this));});
    let resizeTimer;
    window.addEventListener('resize',() => {clearTimeout(resizeTimer); resizeTimer = setTimeout(() => {
        for (const [key,chart] of Object.entries(historyViewer.charts)) if (chart && historyViewer.visible[key]) {chart.render(); alignHistoryPlot(chart);}
    },100);});
}

// Actualiza la unidad visual sin modificar muestras; kilo indica si se usan multiplos de mil.
function setSharedHistoryUnits(kilo) {
    if (historyKiloUnits === kilo) return;
    const factor = kilo ? 0.001 : 1000;
    for (const key of ['activePower','reactivePower','apparentPower','energy']) {
        const scale = historyViewer.scales[key];
        if (scale) { scale.min *= factor; scale.max *= factor; }
    }
    historyKiloUnits = kilo; saveHistoryViewOptions();
    for (const key of ['activePower','reactivePower','apparentPower','energy']) {
        const config = getHistoryChartConfig(key);
        $('#historyPanel'+capitalizeHistoryChart(key)+' h3').text(config.title + ' (' + config.unit + ')');
        const scale = historyViewer.scales[key];
        if (scale) { $('#historyPanel'+capitalizeHistoryChart(key)+' [name=min]').val(scale.min); $('#historyPanel'+capitalizeHistoryChart(key)+' [name=max]').val(scale.max); }
        if (historyViewer.visible[key]) renderHistoryChart(key);
    }
}

// Lee la preferencia compartida en la placa; sin parametros, conserva vatios si aun no existe.
async function loadSharedHistoryUnits() {
    try {
        const values = await readOptions('web',['kilo']);
        if (values.kilo === 0 || values.kilo === 1) setSharedHistoryUnits(values.kilo === 1);
        else if (values.kilo === 'key no existe') setSharedHistoryUnits(false);
        const input = document.getElementById('appHistoryUnits');
        if (input) input.value = historyKiloUnits ? '1' : '0';
    } catch (_) { /* Una falla de configuracion no impide visualizar el historico. */ }
}

// Convierte una fecha local al formato que usa un control datetime-local; date es la fecha a mostrar.
function formatHistoryDateInput(date) { return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 19); }

// Completa las fechas con un intervalo hacia atrás desde el momento actual; seconds es la duración del intervalo.
function setHistoryDateRange(seconds) {
    const to = new Date();
    $("#historyFrom").val(formatHistoryDateInput(new Date(to.getTime() - seconds * 1000)));
    $("#historyTo").val(formatHistoryDateInput(to));
}

// Aplica un atajo de intervalo y rellena los controles; preset identifica una duración o período calendario.
function applyHistoryPreset(preset) {
    const now = new Date();
    historyViewer.calendarUnit = ['thisWeek','lastWeek'].includes(preset) ? 'week' : ['thisMonth','lastMonth'].includes(preset) ? 'month' : ['today','yesterday','dayBeforeYesterday'].includes(preset) ? 'day' : null;
    if (preset === 'lastMonthRolling') {
        const from = new Date(now), day = from.getDate();
        from.setDate(1); from.setMonth(from.getMonth()-1);
        from.setDate(Math.min(day,new Date(from.getFullYear(),from.getMonth()+1,0).getDate()));
        $('#historyFrom').val(formatHistoryDateInput(from)); $('#historyTo').val(formatHistoryDateInput(now));
    } else if (preset.startsWith("last-")) {
        setHistoryDateRange(Number(preset.slice(5)));
    } else {
        const today = new Date(now.getFullYear(), now.getMonth(), now.getDate());
        let from = today, to = new Date(today);
        to.setDate(to.getDate() + 1);
        if (preset === "yesterday") { from = new Date(today); from.setDate(from.getDate() - 1); to = today; }
        if (preset === "dayBeforeYesterday") { from = new Date(today); from.setDate(from.getDate() - 2); to = new Date(today); to.setDate(to.getDate() - 1); }
        if (preset === 'thisWeek' || preset === "lastWeek") {
            from = new Date(today); from.setDate(from.getDate()-today.getDay()-(preset === 'lastWeek' ? 7 : 0));
            to = new Date(from); to.setDate(to.getDate()+7);
        }
        if (preset === 'thisMonth' || preset === 'lastMonth') {from = new Date(now.getFullYear(),now.getMonth()-(preset === 'lastMonth' ? 1 : 0),1); to = new Date(from.getFullYear(),from.getMonth()+1,1);}
        $("#historyFrom").val(formatHistoryDateInput(from));
        $("#historyTo").val(formatHistoryDateInput(new Date(to.getTime()-1000)));
    }
    loadHistoryViewer();
}

// Desplaza el intervalo actual y consulta el nuevo período; direction vale -1 o 1.
function moveHistoryRange(direction) {
    const from = new Date($("#historyFrom").val()), to = new Date($("#historyTo").val());
    const duration = to.getTime() - from.getTime();
    if (!Number.isFinite(duration) || duration <= 0) return setHistoryStatus("Seleccione primero un intervalo válido.", true);
    if (historyViewer.calendarUnit) {
        if (historyViewer.calendarUnit === 'month') {from.setMonth(from.getMonth()+direction); to.setTime(new Date(from.getFullYear(),from.getMonth()+1,1).getTime()-1000);}
        else {const days = (historyViewer.calendarUnit === 'week' ? 7 : 1)*direction; from.setDate(from.getDate()+days); to.setDate(to.getDate()+days);}
        $('#historyFrom').val(formatHistoryDateInput(from)); $('#historyTo').val(formatHistoryDateInput(to));
    } else {
        $("#historyFrom").val(formatHistoryDateInput(new Date(from.getTime() + duration * direction)));
        $("#historyTo").val(formatHistoryDateInput(new Date(to.getTime() + duration * direction)));
    }
    $("#historyPreset").val("");
    loadHistoryViewer();
}

// Ejecuta una consulta histórica con un límite de tiempo; url identifica el recurso y timeoutMs el máximo de espera.
async function fetchHistory(url, timeoutMs) {
    const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), timeoutMs);
    try { return await fetch(url, {cache: "no-store", signal: controller.signal}); }
    finally { clearTimeout(timeout); }
}

// Calcula una espera proporcional a la descarga; bytes es el tamaño binario estimado.
function getHistoryDownloadTimeout(bytes) { return Math.min(120000, Math.max(30000, Math.ceil(bytes / 4096) * 1000)); }

// Construye la consulta histórica a partir de las dos fechas locales seleccionadas; sin parámetros.
function getHistoryRangeQuery() {
    const fromValue = $("#historyFrom").val(), toValue = $("#historyTo").val();
    if (!fromValue || !toValue) throw new Error("Seleccione fecha y hora de inicio y fin.");
    const from = Math.floor(new Date(fromValue).getTime() / 1000), to = Math.floor(new Date(toValue).getTime() / 1000);
    if (!Number.isFinite(from) || !Number.isFinite(to) || from >= to) throw new Error("El período seleccionado no es válido.");
    updateHistoryUrl(fromValue, toValue);
    return {query: "?from=" + from + "&to=" + to, seconds: to - from};
}

// Guarda el período consultado en la dirección para poder volver a abrirlo; fromValue y toValue son fechas locales.
function updateHistoryUrl(fromValue, toValue) {
    const url = new URL(location.href);
    url.searchParams.set("from", new Date(fromValue).toISOString());
    url.searchParams.set("to", new Date(toValue).toISOString());
    url.hash = "history";
    history.replaceState(null, "", url);
}

// Muestra el porcentaje de transferencia; received y totalBytes están expresados en bytes.
function updateHistoryProgress(received, totalBytes) {
    const percent = totalBytes > 0 ? Math.min(100, Math.floor(received * 100 / totalBytes)) : 0;
    setHistoryStatus("Descargando histórico: " + percent + "%");
}

// Descarga url mostrando avance; timeoutMs limita la espera, expectedBytes informa el progreso y devuelve buffer más bytes/s.
async function fetchHistoryDownload(url, timeoutMs, expectedBytes) {
    const controller = new AbortController(), timeout = setTimeout(() => controller.abort(), timeoutMs);
    const startedAt = performance.now();
    try {
        const response = await fetch(url, {cache: "no-store", signal: controller.signal});
        if (!response.ok) throw new Error(await getHistoryError(response));
        if (!response.body || !response.body.getReader) {
            const buffer = await response.arrayBuffer();
            updateHistoryProgress(buffer.byteLength, expectedBytes);
            return {buffer, bytesPerSecond: buffer.byteLength * 1000 / Math.max(1, performance.now() - startedAt)};
        }
        const reader = response.body.getReader(), chunks = [];
        let received = 0;
        while (true) {
            const result = await reader.read();
            if (result.done) break;
            chunks.push(result.value);
            received += result.value.byteLength;
            updateHistoryProgress(received, expectedBytes);
        }
        const bytes = new Uint8Array(received);
        let offset = 0;
        chunks.forEach((chunk) => { bytes.set(chunk, offset); offset += chunk.byteLength; });
        return {buffer: bytes.buffer, bytesPerSecond: received * 1000 / Math.max(1, performance.now() - startedAt)};
    } finally {
        clearTimeout(timeout);
    }
}

// Descarga y decodifica el intervalo seleccionado; sin parámetros.
async function loadHistoryViewer() {
    if (historyViewer.loading || typeof CanvasJS === "undefined") return;
    historyViewer.loading = true; $("#historyLoadButton,#historyPrevButton,#historyNextButton,#historyPreset,#historyFrom,#historyTo").prop("disabled", true); setHistoryStatus("Calculando rango disponible...");
    try {
        if (typeof loadCachedHistoryRange === "function" && await openHistoryCache()) {
            const range = getHistoryRangeQuery(), params = new URLSearchParams(range.query);
            const from = Number(params.get("from")), to = Number(params.get("to"));
            const buffer = await loadCachedHistoryRange(from,to);
            historyViewer.records = decodeHistoryPages(buffer).filter(record => record.timestamp.getTime() >= from * 1000 && record.timestamp.getTime() <= to * 1000);
            historyViewer.seconds = range.seconds;
            renderHistoryCharts(); setHistoryStatus("Intervalo cargado: " + historyViewer.records.length + " muestras · cache local compartida.");
        } else {
        const range = getHistoryRangeQuery(), infoResponse = await fetchHistory(board.IP + "/history" + range.query, 30000);
        if (!infoResponse.ok) throw new Error(await getHistoryError(infoResponse));
        const info = await infoResponse.json(); updateHistoryProgress(0, info.downloadBytes);
        const download = await fetchHistoryDownload(board.IP + "/history/data" + range.query, getHistoryDownloadTimeout(info.downloadBytes), info.downloadBytes);
        historyViewer.records = decodeHistoryPages(download.buffer); historyViewer.seconds = range.seconds;
        const invalidText = historyViewer.invalidPages ? ", " + historyViewer.invalidPages + " páginas con CRC inválido omitidas." : info.hasGaps ? ", con páginas inválidas omitidas." : ".";
        const rateKiB = download.bytesPerSecond / 1024, isSlow = download.bytesPerSecond < 150 * 1024;
        const slowText = isSlow ? " Descarga lenta; si el resto de la web también responde lento, reinicie router/switch." : "";
        renderHistoryCharts(); setHistoryStatus("Intervalo cargado: " + historyViewer.records.length + " muestras decodificadas, " + info.validPages + " páginas · " + rateKiB.toFixed(0) + " KiB/s" + invalidText + slowText, false, isSlow);
        }
    } catch (error) { historyViewer.records = []; clearHistoryCharts(); setHistoryStatus(error.name === "AbortError" ? "La descarga demoró demasiado. Intente nuevamente." : error.message || "No se pudo cargar el histórico.", true); }
    historyViewer.loading = false; $("#historyLoadButton,#historyPrevButton,#historyNextButton,#historyPreset,#historyFrom,#historyTo").prop("disabled", false);
}

// Obtiene el motivo de un error HTTP; response contiene la respuesta fallida.
async function getHistoryError(response) { try { const data = await response.json(); return data.reason || "Error HTTP " + response.status; } catch (_) { return "Error HTTP " + response.status; } }

// Decodifica páginas de 2048 bytes y devuelve muestras cronológicas; buffer contiene el archivo binario.
function decodeHistoryPages(buffer) {
    const bytes = new Uint8Array(buffer), records = []; historyViewer.invalidPages = 0;
    for (let offset = 0; offset + 2048 <= bytes.length; offset += 2048) {
        const page = bytes.subarray(offset, offset + 2048), view = new DataView(page.buffer, page.byteOffset, 2048), config = readHistory24(page, 7), period = view.getUint16(10, true), count = view.getUint16(12, true), formatVersion = page[14], size = getHistorySampleSize(config, formatVersion);
        if (!view.getUint32(0, true) || !period || !count || !size || !isValidHistoryConfig(config, formatVersion) || 20 + count * size > 2048) { ++historyViewer.invalidPages; continue; }
        if (view.getUint16(16, true) !== historyCrc16(page, 16) || view.getUint16(18, true) !== historyCrc16(page.subarray(20), count * size)) { ++historyViewer.invalidPages; continue; }
        for (let index = 0; index < count; ++index) { const record = decodeHistorySample(page.subarray(20 + index * size, 20 + (index + 1) * size), config, formatVersion); record.timestamp = new Date((view.getUint32(0, true) + index * period / 10) * 1000); records.push(record); }
    }
    return records;
}

// Calcula CRC-16/CCITT-FALSE para validar una cabecera o payload recibido; data y length delimitan el rango.
function historyCrc16(data, length) {
    let crc = 0xFFFF;
    for (let index = 0; index < length; ++index) { crc ^= data[index] << 8; for (let bit = 0; bit < 8; ++bit) crc = crc & 0x8000 ? (crc << 1 ^ 0x1021) & 0xFFFF : crc << 1 & 0xFFFF; }
    return crc;
}

// Lee un entero little-endian de 24 bits; data contiene los bytes y offset indica el inicio.
function readHistory24(data, offset) { return data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16; }

// Calcula el tamaño de muestra; config es el bitmap y formatVersion identifica su mapa.
function getHistorySampleSize(config, formatVersion) {
    if (formatVersion === 1) return getHistoryV1SampleSize(config);
    if (formatVersion === 2) return getHistoryV2SampleSize(config);
    return 0;
}

// Calcula el tamaño de muestra V1; config usa el bitmap compacto unificado.
function getHistoryV1SampleSize(config) {
    const mode = (shift) => config >> shift & 3, energyMode = config >> 15 & 7;
    let size = 1 + (config & 1 ? 14 : 8) + 9;
    size += [1, 3, 5].reduce((total, shift) => total + (mode(shift) === 2 ? 8 : mode(shift) === 1 ? 3 : 0), 0);
    if (config & 128) size += 4;
    if (config & 256) size += 4;
    if (config & 512) size += 4;
    size += mode(10) === 2 ? 2 : mode(10) === 1 ? 1 : 0;
    if (config & 4096) size += 5;
    if (config & 8192) size += 3;
    if (config & 16384) size += 9 + [0, 3, 3, 6, 9, 9, 12, 18][energyMode];
    return size;
}

// Calcula el tamaño de muestra V2 temporal; config conserva el bitmap que se escribió en esa versión.
function getHistoryV2SampleSize(config) {
    const voltageMode = config & 3, energyMode = config >> 17 & 7;
    const mode = (shift) => config >> shift & 3;
    let size = 1 + (voltageMode === 1 ? 8 : voltageMode === 2 ? 14 : 0) + (config & 4 ? 9 : 0);
    size += [3, 5, 7].reduce((total, shift) => total + (mode(shift) === 2 ? 8 : mode(shift) === 1 ? 3 : 0), 0);
    if (config & 512) size += 4;
    if (config & 1024) size += 4;
    if (config & 2048) size += 4;
    size += mode(12) === 2 ? 2 : mode(12) === 1 ? 1 : 0;
    if (config & 16384) size += 5;
    if (config & 32768) size += 3;
    if (config & 65536) size += 9 + [0, 3, 3, 6, 9, 9, 12, 18][energyMode];
    return size;
}

// Valida config según formatVersion y rechaza bits reservados o modos indefinidos.
function isValidHistoryConfig(config, formatVersion) {
    if (formatVersion === 1) {
        const modes = [1, 3, 5, 10].map((shift) => config >> shift & 3);
        return !(config & 0xFC0000) && modes.every((mode) => mode !== 3) && ((config & 16384) || !(config >> 15 & 7));
    }
    if (formatVersion !== 2 || (config & 0xF00000)) return false;
    const modes = [0, 3, 5, 7, 12].map((shift) => config >> shift & 3);
    return modes.every((mode) => mode !== 3) && ((config & 65536) || !(config >> 17 & 7));
}

// Lee bits menos significativos primero; data contiene el stream, offset y count delimitan el campo.
function readHistoryBits(data, offset, count) { let value = 0; for (let bit = 0; bit < count; ++bit) value |= ((data[(offset + bit) >> 3] >> ((offset + bit) & 7)) & 1) << bit; return value >>> 0; }

// Lee un entero con signo en complemento a dos; data contiene el stream y count define sus bits útiles.
function readHistorySignedBits(data, offset, count) { const value = readHistoryBits(data, offset, count); return value & (1 << (count - 1)) ? value - (1 << count) : value; }

// Decodifica una muestra usando el mapa de bits de su versión; sample contiene bytes y config su bitmap.
function decodeHistorySample(sample, config, formatVersion) {
    return formatVersion === 1 ? decodeHistoryV1Sample(sample, config) : decodeHistoryV2Sample(sample, config);
}

// Decodifica una muestra V1 unificada; sample contiene los datos y config los grupos activos.
function decodeHistoryV1Sample(sample, config) {
    const record = {status: sample[0]}, phases = ["R", "S", "T", "N"];
    let cursor = 8;
    const read = (bits) => { const value = readHistoryBits(sample, cursor, bits); cursor += bits; return value; };
    const readSigned = (bits) => { const value = readHistorySignedBits(sample, cursor, bits); cursor += bits; return value; };
    phases.forEach((phase) => { record["voltage" + phase] = read(16) / 100; });
    if (config & 1) ["RS", "ST", "TR"].forEach((pair) => { record["voltage" + pair] = read(16) / 100; });
    phases.forEach((phase) => { record["current" + phase] = read(18) / 1000; });
    const decodePower = (mode, prefix, signed) => {
        if (mode === 2) ["R", "S", "T"].forEach((phase) => { record[prefix + phase] = (signed ? readSigned(21) : read(21)) / 10; });
        else if (mode === 1) record[prefix + "Total"] = (signed ? readSigned(24) : read(24)) / 10;
    };
    decodePower(config >> 1 & 3, "activePower", true);
    decodePower(config >> 3 & 3, "reactivePower", true);
    decodePower(config >> 5 & 3, "apparentPower", false);
    if (config & 128) ["R", "S", "T"].forEach((phase) => { const value = read(10); record["powerFactor" + phase] = (value & 512 ? -1 : 1) * (value & 511) / 500; });
    if (config & 256) ["R", "S", "T"].forEach((phase) => { record["voltageThd" + phase] = read(10); });
    if (config & 512) ["R", "S", "T"].forEach((phase) => { record["currentThd" + phase] = read(10); });
    const frequencyMode = config >> 10 & 3;
    if (frequencyMode === 2) record.frequency = read(16) / 100;
    else if (frequencyMode === 1) { const value = read(8); record.frequency = value ? 40 + (value - 1) / 10 : null; }
    if (config & 4096) ["RS", "ST", "TR"].forEach((pair) => { record["voltageAngle" + pair] = read(13) / 20; });
    if (config & 16384) decodeHistoryEnergy(record, read, config >> 15 & 7);
    if (config & 8192) decodeHistoryBattery(record, read(24));
    return record;
}

// Decodifica una muestra V2 temporal; sample contiene los datos y config su bitmap original.
function decodeHistoryV2Sample(sample, config) {
    const record = {status: sample[0]}, phases = ["R", "S", "T", "N"]; let cursor = 8;
    // Lee el siguiente campo sin signo y avanza cursor en bits.
    const read = (bits) => { const value = readHistoryBits(sample, cursor, bits); cursor += bits; return value; };
    // Lee el siguiente campo con signo y avanza cursor en bits.
    const readSigned = (bits) => { const value = readHistorySignedBits(sample, cursor, bits); cursor += bits; return value; };
    const voltageMode = config & 3;
    if (voltageMode) phases.forEach((phase) => { record["voltage" + phase] = read(16) / 100; });
    if (voltageMode === 2) ["RS", "ST", "TR"].forEach((pair) => { record["voltage" + pair] = read(16) / 100; });
    if (config & 4) phases.forEach((phase) => { record["current" + phase] = read(18) / 1000; });
    // Decodifica potencia por fase o compacta; mode elige tamaño, prefix nombra y unit escala.
    const decodePower = (mode, prefix, unit, signed) => {
        if (mode === 2) ["R", "S", "T"].forEach((phase) => { record[prefix + phase] = (signed ? readSigned(21) : read(21)) / unit; });
        else if (mode === 1) { const value = signed ? readSigned(24) : read(24); record[prefix + "Total"] = value / unit; }
    };
    decodePower(config >> 3 & 3, "activePower", 10, true);
    decodePower(config >> 5 & 3, "reactivePower", 10, true);
    decodePower(config >> 7 & 3, "apparentPower", 10, false);
    if (config & 512) ["R", "S", "T"].forEach((phase) => { const value = read(10); record["powerFactor" + phase] = (value & 512 ? -1 : 1) * (value & 511) / 500; });
    if (config & 1024) ["R", "S", "T"].forEach((phase) => { record["voltageThd" + phase] = read(10); });
    if (config & 2048) ["R", "S", "T"].forEach((phase) => { record["currentThd" + phase] = read(10); });
    const frequencyMode = config >> 12 & 3;
    if (frequencyMode === 2) record.frequency = read(16) / 100;
    else if (frequencyMode === 1) { const value = read(8); record.frequency = value ? 40 + (value - 1) / 10 : null; }
    if (config & 16384) ["RS", "ST", "TR"].forEach((pair) => { record["voltageAngle" + pair] = read(13) / 20; });
    if (config & 32768) decodeHistoryBattery(record, read(24));
    if (config & 65536) decodeHistoryEnergy(record, read, config >> 17 & 7);
    return record;
}

// Decodifica energia activa y sus extras; read extrae cada valor de 24 bits.
function decodeHistoryEnergy(record, read, energyMode) {
    ["R", "S", "T"].forEach((phase) => { record["activeEnergy" + phase] = read(24) * 10; });
    if (energyMode === 1 || energyMode === 3) record.reactiveEnergyTotal = read(24) * 100;
    if (energyMode === 2 || energyMode === 3) record.apparentEnergyTotal = read(24) * 100;
    if (energyMode === 4 || energyMode === 6 || energyMode === 7)
        ["R", "S", "T"].forEach((phase) => { record["reactiveEnergy" + phase] = read(24) * 10; });
    if (energyMode === 5 || energyMode === 7)
        ["R", "S", "T"].forEach((phase) => { record["apparentEnergy" + phase] = read(24) * 10; });
    if (energyMode === 6) record.apparentEnergyTotal = read(24) * 100;
}

// Decodifica los campos BQ25895; value contiene sus 24 bits empaquetados.
function decodeHistoryBattery(record, value) {
    const batteryLevel = value >> 21 & 7;
    record.batteryVoltage = batteryLevel <= 5 ? 2.304 + (value & 127) * 0.02 : null;
    record.batteryInputVoltage = 2.6 + (value >> 7 & 127) / 10;
    record.batteryCurrent = batteryLevel <= 5 ? (value >> 14 & 127) / 20 : null;
    record.batteryLevel = batteryLevel <= 5 ? batteryLevel * 20 : null;
}

// Muestra u oculta una gráfica histórica; chart identifica el grupo y visible indica su estado final.
function setHistoryChartVisibility(chart, visible) {
    historyViewer.visible[chart] = visible;
    $("#historyPanel" + capitalizeHistoryChart(chart)).toggleClass("d-none", !visible);
    $(".historyChartToggle[data-chart='" + chart + "']").toggleClass("active", visible).attr("aria-pressed", visible);
    if (visible) renderHistoryChart(chart);
    updateHistoryOrderButtons(); saveHistoryViewOptions();
}

// Renderiza los grupos que el usuario dejó visibles; sin parámetros.
function renderHistoryCharts() {
    historyViewer.ranges = {}; historyViewer.lastRange = null;
    Object.keys(historyViewer.visible).forEach((chart) => { if (historyViewer.visible[chart]) renderHistoryChart(chart); });
}

// Aplica range temporal a source o todas las vistas enlazadas; null restablece el intervalo completo.
function setHistoryViewport(source,range) {
    if (range && (!range.every(Number.isFinite) || range[0] >= range[1])) return;
    historyViewer.lastRange = range;
    historyViewer.syncing = true;
    try {
        for (const key of historyViewer.order) {
            if (source && !historyViewer.linked && key !== source) continue;
            historyViewer.ranges[key] = range;
            const chart = historyViewer.charts[key];
            if (!chart) continue;
            chart.options.axisX.viewportMinimum = range?.[0] ?? null;
            chart.options.axisX.viewportMaximum = range?.[1] ?? null;
            if (historyViewer.visible[key]) {chart.render(); alignHistoryPlot(chart);}
        }
    } finally {historyViewer.syncing = false;}
}

// Recibe zoom o desplazamiento de key; event conserva el mismo rango absoluto para las otras graficas.
function historyRangeChanged(key,event) {
    if (historyViewer.syncing) return;
    const axis = event.axisX?.[0];
    setHistoryViewport(key,event.trigger === 'reset' ? null : axis ? [axis.viewportMinimum,axis.viewportMaximum] : null);
}

// Formatea event con ancho tipografico fijo; config identifica estados o valores numericos.
function historyAxisLabel(event,config) {
    const labels = ['WiFi','MQTT','Bateria','Cargando','Completa','Falla','Reloj'];
    const text = config.step ? labels[Math.floor(event.value/2)]+' '+(Math.round(event.value)%2) :
        Number(event.value).toLocaleString('es-AR',{maximumFractionDigits:3,notation:Math.abs(event.value)>=100000 ? 'compact' : 'standard'});
    return text.slice(0,10).padStart(10,'\u00a0');
}

// Reserva el mismo margen izquierdo real en chart; usa conversion publica de coordenadas, tambien con zoom.
function alignHistoryPlot(chart) {
    const axis = chart.axisX?.[0]; if (!axis) return;
    const left = axis.convertValueToPixel(axis.get('viewportMinimum') ?? axis.get('minimum'));
    if (!Number.isFinite(left)) return;
    const correction = 82-left;
    if (Math.abs(correction) < .5) return;
    chart.options.axisY.margin = Math.max(0,(chart.options.axisY.margin || 0)+correction);
    chart.render();
}

// Dibuja un grupo de series sobre su contenedor; chart identifica el tipo de gráfica.
function renderHistoryChart(chart) {
    if (!historyViewer.records.length || typeof CanvasJS === "undefined") return clearHistoryChart(chart);
    const config = getHistoryChartConfig(chart), divisor = historyKiloUnits && ['activePower','reactivePower','apparentPower','energy'].includes(chart) ? 1000 : 1, data = config.series.map((series, lane) => ({type: config.step ? "stepLine" : "line", name: series.name, appPhase:series.phase, appVariant:series.variant || 0,
        signalUnit: series.unit ?? config.unit, showInLegend: true, markerSize: 0, color: series.color, connectNullData:false,
        lineDashType:series.dash || 'solid', visible:historyViewer.seriesVisibility[chart+'|'+series.name] !== false,
        dataPoints: historyViewer.records.map((record) => { const value = series.value(record); return {x: record.timestamp, y: Number.isFinite(value) ? config.step ? lane * 2 + value : value / divisor : null, tooltipText: config.step ? (value ? "Sí" : "No") : undefined}; })})).filter((series) => series.dataPoints.some(point => Number.isFinite(point.y)));
    if (!data.length) return clearHistoryChart(chart, "La variable no está incluida en este perfil.");
    const axisY = {includeZero: false, gridDashType: "dot", interval: config.step ? 1 : undefined,
        labelFontFamily:'monospace', labelFontSize:10, labelAutoFit:false, labelWrap:false, margin:5, labelFormatter:event => historyAxisLabel(event,config)};
    if (config.step) {
        axisY.minimum = 0;
        axisY.maximum = config.series.length * 2 - 1;
    }
    const scale = historyViewer.scales[chart]; if (scale) {axisY.minimum = scale.min; axisY.maximum = scale.max;}
    const records = historyViewer.records, range = historyViewer.ranges[chart] || (historyViewer.linked ? historyViewer.lastRange : null);
    const minimum = Number(records[0].timestamp), maximum = Math.max(minimum+1,Number(records[records.length-1].timestamp));
    const options = {animationEnabled:false,zoomEnabled:!historyViewer.pan[chart],panEnabled:!!historyViewer.pan[chart],zoomType:'x',theme:'light1',
        toolTip:chartTooltip.options({title:config.title,mode:'date'}),
        axisX:{minimum,maximum,viewportMinimum:range?.[0] ?? null,viewportMaximum:range?.[1] ?? null,
            labelFontSize:10,valueFormatString:historyViewer.seconds <= 21600 ? 'HH:mm' : 'DD MMM HH:mm'},
        axisY,legend:{fontSize:10,cursor:'pointer',itemclick:event => {toggleHistorySeries(event); alignHistoryPlot(event.chart); historyViewer.seriesVisibility[chart+'|'+event.dataSeries.name] = event.dataSeries.visible;}},
        rangeChanged:event => historyRangeChanged(chart,event),data};
    if (historyViewer.charts[chart]) Object.assign(historyViewer.charts[chart].options,options);
    else historyViewer.charts[chart] = new CanvasJS.Chart('historyChart'+capitalizeHistoryChart(chart),options);
    historyViewer.charts[chart].render(); alignHistoryPlot(historyViewer.charts[chart]);
}

// Devuelve el total guardado o la suma completa de fases; record es la muestra y prefix la magnitud.
function historyTotal(record, prefix) {
    const total = record[prefix + 'Total'];
    if (Number.isFinite(total)) return total;
    const phases = ['R', 'S', 'T'].map(phase => record[prefix + phase]);
    return phases.every(Number.isFinite) ? phases.reduce((sum, value) => sum + value, 0) : null;
}

// Define las series visuales disponibles; chart identifica el grupo solicitado.
function getHistoryChartConfig(chart) {
    const phaseSeries = (prefix, names, variant = 0, dash = 'solid') => names.map(([name, color]) => ({name: ['activeEnergy','reactiveEnergy','apparentEnergy'].includes(prefix) ? {activeEnergy:'Activa ',reactiveEnergy:'Reactiva ',apparentEnergy:'Aparente '}[prefix]+name : name, color: typeof appPhaseColor === 'function' ? appPhaseColor(name,variant) : color, phase:name, variant,dash,
        unit: {activeEnergy: historyKiloUnits?'kWh':'Wh', reactiveEnergy: historyKiloUnits?'kvarh':'varh', apparentEnergy: historyKiloUnits?'kVAh':'VAh'}[prefix], value: (record) => record[prefix + name]}));
    const colors = [["R", "#d9534f"], ["S", "#5cb85c"], ["T", "#337ab7"], ["N", "#6c757d"]];
    if (chart === "voltage") return {title: "Voltaje RMS", unit: "V", includeZero: false, series: phaseSeries("voltage", colors)};
    if (chart === "current") return {title: "Corriente RMS", unit: "A", includeZero: false, series: phaseSeries("current", colors)};
    if (chart === 'lineVoltage') return {title:'Tension entre fases',unit:'V',series:['RS','ST','TR'].map((pair,index) => ({name:pair,phase:['R','S','T'][index],color:typeof appPhaseColor === 'function' ? appPhaseColor(['R','S','T'][index]) : colors[index][1],value:record => record['voltage'+pair]}))};
    if (chart === 'voltageThd' || chart === 'currentThd') return {title:chart === 'voltageThd' ? 'THD de tension' : 'THD de corriente',unit:'%',series:phaseSeries(chart,colors.slice(0,3))};
    if (chart === 'voltageAngle') return {title:'Angulos entre fases',unit:'grados',series:['RS','ST','TR'].map((pair,index) => ({name:pair,phase:['R','S','T'][index],color:typeof appPhaseColor === 'function' ? appPhaseColor(['R','S','T'][index]) : colors[index][1],value:record => record['voltageAngle'+pair]}))};
    const batteries = {batteryVoltage:['Tension de bateria','V'],batteryInputVoltage:['Tension de entrada (Vbus)','V'],batteryCurrent:['Corriente de carga','A'],batteryLevel:['Nivel de bateria','%']};
    if (batteries[chart]) return {title:batteries[chart][0],unit:batteries[chart][1],series:[{name:historyChartNames[chart],color:'#087e83',value:record => record[chart]}]};
    const powers = {activePower:['Potencia activa',historyKiloUnits?'kW':'W'],reactivePower:['Potencia reactiva',historyKiloUnits?'kvar':'var'],apparentPower:['Potencia aparente',historyKiloUnits?'kVA':'VA']};
    if (powers[chart]) return {title:powers[chart][0],unit:powers[chart][1],series:[...phaseSeries(chart,colors.slice(0,3)),{name:'Total',color:'#087e83',value:record => historyTotal(record,chart)}]};
    if (chart === "energy") return {title: "Energía", unit: historyKiloUnits ? "kWh / kvarh / kVAh" : "Wh / varh / VAh", includeZero: false, series: [
        ...phaseSeries("activeEnergy", colors.slice(0, 3)),
        {name: "Activa total", color: "#087e83", unit: historyKiloUnits?'kWh':'Wh', value: record => historyTotal(record,'activeEnergy')},
        {name: "Reactiva total", color: "#6f42c1", unit: historyKiloUnits?'kvarh':'varh', value: record => historyTotal(record,'reactiveEnergy')},
        {name: "Aparente total", color: "#fd7e14", unit: historyKiloUnits?'kVAh':'VAh', value: record => historyTotal(record,'apparentEnergy')},
        ...phaseSeries("reactiveEnergy", colors.slice(0,3),.2,'dash'),
        ...phaseSeries("apparentEnergy", colors.slice(0,3),.4,'dot'),
    ]};
    if (chart === "powerFactor") return {title: "Factor de potencia", unit: "", includeZero: false, series: phaseSeries("powerFactor", colors.slice(0, 3))};
    if (chart === "frequency") return {title: "Frecuencia", unit: "Hz", includeZero: false, series: [{name: "Frecuencia", color: "#6f42c1", value: (record) => record.frequency}]};
    return {title: "Estados de conectividad y alimentación", unit: "", includeZero: true, step: true, series: [["Wi-Fi", "#0d6efd", 0], ["MQTT", "#198754", 1], ["Batería", "#fd7e14", 2], ["Cargando", "#20c997", 3], ["Carga completa", "#6f42c1", 4], ["Falla batería", "#dc3545", 5], ["Reloj", "#6c757d", 6]].map(([name, color, bit]) => ({name: name, color: color, value: (record) => record.status >> bit & 1}))};
}

// Alterna una serie desde la leyenda CanvasJS; event contiene la serie seleccionada.
function toggleHistorySeries(event) { event.dataSeries.visible = event.dataSeries.visible === false; event.chart.render(); }

// Convierte una clave de gráfica en el sufijo usado por el identificador HTML.
function capitalizeHistoryChart(chart) { return chart.charAt(0).toUpperCase() + chart.slice(1); }

// Limpia una gráfica sin modificar las demás; chart identifica el contenedor y message el texto visible.
function clearHistoryChart(chart, message) { historyViewer.charts[chart]?.destroy(); historyViewer.charts[chart] = null; $("#historyChart" + capitalizeHistoryChart(chart)).html('<div class="text-muted p-3">' + (message || "Sin muestras para mostrar.") + "</div>"); }

// Limpia todos los contenedores históricos; sin parámetros.
function clearHistoryCharts() { Object.keys(historyViewer.visible).forEach((chart) => clearHistoryChart(chart)); }

// Actualiza el estado visible; message describe el resultado e isError indica error.
function setHistoryStatus(message, isError, isWarning) { $("#historyViewerStatus").text(message).toggleClass("text-danger", !!isError).toggleClass("historySlowWarning", !!isWarning).toggleClass("text-muted", !isError && !isWarning); }

// Convierte bytes a una etiqueta compacta; value es la cantidad de bytes.
