let gridEventsViewer = {events: [], voltageChart: null, currentChart: null, voltageVisible: true, currentVisible: false, loading: false};

// Inicializa los controles del visor de capturas persistentes de red; sin parámetros.
function initGridEventsViewer() {
    $("#gridEventsRefresh").on("click", loadGridEventsViewer); $("#gridEventsTest").on("click", createGridEventTest);
    $("#gridEventVoltageToggle").on("click", () => setGridChartVisibility("voltage", !gridEventsViewer.voltageVisible));
    $("#gridEventCurrentToggle").on("click", () => setGridChartVisibility("current", !gridEventsViewer.currentVisible));
}

// Descarga el índice reciente y actualiza la lista de eventos disponibles; sin parámetros.
async function loadGridEventsViewer() {
    if (gridEventsViewer.loading) return;
    gridEventsViewer.loading = true; setGridEventsStatus("Consultando eventos guardados...");
    try {
        const infoResponse = await fetch(board.IP + "/grid-events", {cache: "no-store"});
        if (!infoResponse.ok) throw new Error("No se pudo leer el registro de eventos.");
        const info = await infoResponse.json();
        if (!info.eventCount) { gridEventsViewer.events = []; renderGridEventsList(); return setGridEventsStatus("No hay eventos persistidos todavía."); }
        gridEventsViewer.events = []; let to = info.newestId;
        while (to >= info.oldestId) {
            const from = Math.max(info.oldestId, to - 49), listResponse = await fetch(board.IP + "/grid-events/list?from=" + from + "&to=" + to, {cache: "no-store"});
            if (!listResponse.ok) throw new Error("No se pudo leer el listado de eventos.");
            const list = await listResponse.json();
            gridEventsViewer.events.push(...(list.events || [])); renderGridEventsList();
            setGridEventsStatus("Cargando " + gridEventsViewer.events.length + " de " + info.eventCount + " eventos...");
            to = from - 1; await new Promise((resolve) => setTimeout(resolve, 20));
        }
        setGridEventsStatus(gridEventsViewer.events.length + " eventos cargados; " + formatGridBytes(info.partitionBytes || 0) + " disponibles.");
    } catch (error) { gridEventsViewer.events = []; renderGridEventsList(); setGridEventsStatus(error.message || "No se pudo cargar el registro.", true); }
    gridEventsViewer.loading = false;
}

// Renderiza las capturas Grid recientes; sin parámetros.
function renderGridEventsList() {
    const list = $("#gridEventsList").empty();
    if (!gridEventsViewer.events.length) return list.append('<div class="text-muted small p-2">No hay eventos persistidos todavía.</div>');
    gridEventsViewer.events.forEach((event) => {
        const when = formatGridTime(event.t);
        list.append($("<button></button>").addClass("list-group-item list-group-item-action py-2").attr("type", "button")
            .html('<strong>#' + event.i + " · " + gridSubjectName(event.y) + '</strong><br><span class="small">' + gridStateName(event.p) + " → " + gridStateName(event.s) + " · " + when + '</span>').on("click", () => loadGridEvent(event)));
    });
}

// Descarga y decodifica una captura Grid seleccionada; event contiene la cabecera indexada.
async function loadGridEvent(event) {
    setGridEventsStatus("Descargando evento #" + event.i + "...");
    try {
        const response = await fetch(board.IP + "/grid-events/data?eventId=" + event.i, {cache: "no-store"});
        if (!response.ok) throw new Error("No se pudo descargar el evento.");
        renderGridEvent(decodeGridEvent(await response.arrayBuffer())); setGridEventsStatus("Evento #" + event.i + " cargado.");
    } catch (error) { setGridEventsStatus(error.message || "No se pudo cargar el evento.", true); }
}

// Decodifica el formato Grid V1 completo; buffer contiene las páginas de una captura.
function decodeGridEvent(buffer) {
    const bytes = new Uint8Array(buffer);
    if (bytes.length < 2048) throw new Error("La descarga no contiene la cabecera completa.");
    const root = new DataView(bytes.buffer, bytes.byteOffset, 2048);
    if (bytes.length < 2048 || root.getUint8(0) !== 1) throw new Error("Formato de evento no reconocido.");
    const timeStatus = root.getUint8(13);
    if (timeStatus & 16) throw new Error("Formato de evento de 18 bytes no admitido.");
    const hasNeutral = !!(timeStatus & 32), sampleBytes = 16;
    const event = {id: root.getUint32(1, true), timestamp: root.getUint32(5, true) * 1000 + root.getUint16(9, true), states: decodeGridStates(root.getUint16(11, true)), timeStatus: timeStatus, estimatedTime: !!(timeStatus & 1), startup: !!(timeStatus & 14), previousDurationMs: root.getUint32(14, true) * 10, previousVoltage: decodeGridVoltage(root.getUint16(18, true)), currentVoltage: decodeGridVoltage(root.getUint16(20, true)), firstOffset: root.getInt32(22, true), samples: [], transitions: [], partial: false, invalidPages: 0, timingEstimated: false};
    const rootSamples = root.getUint8(26), pageCount = root.getUint8(27);
    event.hasNeutral = hasNeutral;
    event.snapshot = null;
    if (rootSamples > 113 || pageCount < 3 || pageCount > 6 || bytes.length < pageCount * 2048) throw new Error("Cabecera de evento inválida.");
    if (gridCrc16(bytes, 0, 28) !== root.getUint16(28, true)) throw new Error("El CRC de la cabecera del evento no es válido.");
    let lastSampleTimestamp = null, sampleRelativeMs = event.firstOffset;
    // Agrega count muestras de view desde offset y conserva sus deltas temporales.
    const appendSamples = (view, offset, count) => {
        for (let index = 0; index < count; ++index) {
            const sample = decodeGridFastSample(view, offset + index * sampleBytes, hasNeutral);
            if (lastSampleTimestamp !== null) {
                let deltaMs = (sample.timestamp - lastSampleTimestamp + 65536) & 0xffff;
                if (!deltaMs || deltaMs > 5000) { deltaMs = 10; event.timingEstimated = true; }
                sampleRelativeMs += deltaMs;
            }
            sample.relativeMs = sampleRelativeMs;
            lastSampleTimestamp = sample.timestamp;
            event.samples.push(sample);
        }
    };
    const rootCrcOffset = 234 + rootSamples * sampleBytes;
    if (gridCrc16(bytes, 30, rootCrcOffset - 30) === root.getUint16(rootCrcOffset, true)) {
        event.snapshot = decodeGridSnapshot(root);
        appendSamples(root, 234, rootSamples);
    }
    else { event.partial = true; event.invalidPages = 1; sampleRelativeMs += rootSamples * 10; }
    for (let pageIndex = 1; pageIndex < pageCount && pageIndex * 2048 < bytes.length; ++pageIndex) {
        const pageOffset = pageIndex * 2048, pageBytes = bytes.subarray(pageOffset, pageOffset + 2048);
        const page = new DataView(bytes.buffer, bytes.byteOffset + pageOffset, 2048), transitions = page.getUint8(7), samples = page.getUint8(6);
        const usedBytes = 154 + samples * sampleBytes;
        if (page.getUint32(0, true) !== event.id || page.getUint8(4) !== pageIndex || page.getUint8(5) !== pageCount || transitions > 12 || samples > 118 ||
            gridCrc16(bytes, pageOffset, usedBytes, pageOffset + 8) !== page.getUint16(8, true)) {
            event.partial = true; event.invalidPages = (event.invalidPages || 0) + 1; continue;
        }
        for (let index = 0; index < transitions; ++index) event.transitions.push(decodeGridTransition(page, 10 + index * 12));
        appendSamples(page, 154, samples);
    }
    return event;
}

// Lee los 156 bytes de contexto RMS; view contiene la pagina raiz con CRC validado.
function decodeGridSnapshot(view) {
    let offset = 78;
    const snapshot = {};
    for (const [name, count] of [["voltage", 4], ["current", 4], ["activePower", 3], ["reactivePower", 3], ["apparentPower", 3], ["powerFactor", 3], ["totalPower", 3], ["frequency", 1], ["activeEnergy", 3], ["reactiveEnergy", 3], ["apparentEnergy", 3], ["totalEnergy", 3]]) {
        const wide = name === "totalEnergy", size = wide ? 8 : 4;
        snapshot[name] = Array.from({length: count}, () => {
            const value = wide ? view.getFloat64(offset, true) : view.getFloat32(offset, true);
            offset += size;
            return Number.isFinite(value) ? value : null;
        });
    }
    return snapshot;
}

// Formatea un valor del contexto; value puede ser null y digits indica los decimales.
function formatGridSnapshotValue(value, digits = 2) {
    return value == null ? "—" : value.toLocaleString("es-AR", {minimumFractionDigits: digits, maximumFractionDigits: digits});
}

// Presenta el contexto por magnitudes; snapshot es null si la pagina raiz no es valida.
function renderGridSnapshot(snapshot) {
    const target = $("#gridEventContext");
    if (!snapshot) return target.text("Contexto no disponible: CRC de la página raíz inválido.");
    const groups = [
        ["Voltajes y corrientes", [["Voltaje RMS (V)", "voltage"], ["Corriente RMS (A)", "current"]]],
        ["Potencias", [["Activa (W)", "activePower", 0], ["Reactiva (var)", "reactivePower", 1], ["Aparente (VA)", "apparentPower", 2], ["Factor de potencia", "powerFactor"]]],
        ["Energías", [["Activa (Wh)", "activeEnergy", 0], ["Reactiva (varh)", "reactiveEnergy", 1], ["Aparente (VAh)", "apparentEnergy", 2]]]
    ];
    let html = '<p class="small text-muted mb-2">Últimos valores RMS disponibles al detectar el cambio inicial; no son muestras FAST ni una lectura simultánea de todos los registros.</p>';
    for (const [title, rows] of groups) {
        html += '<h6 class="mt-3 mb-1">' + title + '</h6><div class="table-responsive"><table class="table table-sm small mb-0"><thead><tr><th>Magnitud</th><th>R</th><th>S</th><th>T</th><th>N</th><th>Total</th></tr></thead><tbody>';
        for (const [label, key, total] of rows) {
            html += '<tr><th scope="row">' + label + '</th>';
            for (let index = 0; index < 4; ++index) html += '<td class="text-nowrap">' + formatGridSnapshotValue(snapshot[key][index]) + '</td>';
            const value = total == null ? null : snapshot[key.endsWith("Energy") ? "totalEnergy" : "totalPower"][total];
            html += '<td class="text-nowrap">' + formatGridSnapshotValue(value) + '</td></tr>';
        }
        html += '</tbody></table></div>';
    }
    target.html(html + '<p class="small mt-3 mb-0"><strong>Frecuencia:</strong> ' + formatGridSnapshotValue(snapshot.frequency[0], 3) + ' Hz</p>');
}

// Calcula CRC-16/CCITT-FALSE; data/start/length delimitan el rango y zeroOffset enmascara el campo CRC.
function gridCrc16(data, start, length, zeroOffset = -1) {
    let crc = 0xffff;
    for (let index = start; index < start + length; ++index) {
        const value = zeroOffset >= 0 && (index === zeroOffset || index === zeroOffset + 1) ? 0 : data[index];
        crc ^= value << 8;
        for (let bit = 0; bit < 8; ++bit) crc = crc & 0x8000 ? (crc << 1 ^ 0x1021) & 0xffff : crc << 1 & 0xffff;
    }
    return crc;
}

// Lee una muestra de view en offset; packed selecciona ocho canales de 14 bits o el formato original.
function decodeGridFastSample(view, offset, packed = false) {
    const sample = {timestamp: view.getUint16(offset, true), vn: null};
    const keys = ["vr", "vs", "vt", "ir", "is", "it", "inn", "vn"];
    for (let channel = 0; channel < (packed ? 8 : 7); ++channel) {
        if (!packed) { sample[keys[channel]] = view.getUint16(offset + 2 + channel * 2, true) / 100; continue; }
        const bit = channel * 14, byte = offset + 2 + Math.floor(bit / 8), shift = bit % 8;
        let value = 0;
        for (let i = 0; i < Math.ceil((shift + 14) / 8); ++i) value |= view.getUint8(byte + i) << (i * 8);
        const raw = value >>> shift & 16383;
        sample[keys[channel]] = raw === 16383 ? null : raw * (channel < 3 || channel === 7 ? 0.025 : 0.01);
    }
    return sample;
}

// Decodifica una transición secundaria del evento; view contiene la página y offset su posición.
function decodeGridTransition(view, offset) { return {offsetMs: view.getInt16(offset, true), previousDurationMs: view.getUint32(offset + 2, true) * 10, states: decodeGridStates(view.getUint16(offset + 6, true)), previousVoltage: decodeGridVoltage(view.getUint16(offset + 8, true)), currentVoltage: decodeGridVoltage(view.getUint16(offset + 10, true))}; }

// Decodifica centésimas de voltio; raw reserva 0xFFFF para una lectura no disponible.
function decodeGridVoltage(raw) { return raw === 65535 ? null : raw / 100; }

// Presenta una tensión opcional; voltage es null cuando no hubo lectura anterior.
function formatGridVoltage(voltage, digits = 1) { return voltage == null ? "—" : voltage.toFixed(digits); }

// Presenta las gráficas Fast RMS y la secuencia de transiciones; event contiene la captura decodificada.
function renderGridEvent(event) {
    const subject = gridSubjectName(event.states.subject), previous = gridStateName(event.states.previous, event.states.subject), current = gridStateName(event.states.current, event.states.subject), when = event.estimatedTime ? "Hora estimada" : formatGridTime(event.timestamp), voltage = event.states.subject === 4 ? "" : " " + formatGridVoltage(event.previousVoltage, 2) + " → " + formatGridVoltage(event.currentVoltage, 2) + " V.", duration = event.startup ? "Duración previa no disponible" : "Estado previo durante " + formatGridDuration(event.previousDurationMs);
    const startup = event.startup ? " Estado reconstruido al iniciar" + (gridStartupReason(event) ? " (" + gridStartupReason(event) + ")" : "") + "." : "";
    const partial = event.partial ? " Captura incompleta: " + event.invalidPages + " página(s) con CRC o cabecera inválidos." : "";
    const timing = event.timingEstimated ? " La escala temporal incluye intervalos estimados." : "";
    $("#gridEventSummary").text("Evento #" + event.id + ": " + subject + " " + previous + " → " + current + ". " + when + ". " + duration + "." + voltage + " " + event.samples.length + " muestras Fast RMS." + startup + partial + timing);
    renderGridTransitionList(event);
    renderGridSnapshot(event.snapshot);
    const voltageSeries = [["R", "vr", "#d9534f"], ["S", "vs", "#5cb85c"], ["T", "vt", "#337ab7"]];
    if (event.hasNeutral) voltageSeries.push(["Neutro-tierra", "vn", "#00838f"]);
    gridEventsViewer.voltageChart = createGridChart("gridEventVoltageChart", "Voltaje RMS", "V", voltageSeries, event.samples);
    gridEventsViewer.currentChart = createGridChart("gridEventCurrentChart", "Corriente RMS", "A", [["R", "ir", "#d9534f"], ["S", "is", "#5cb85c"], ["T", "it", "#337ab7"], ["N", "inn", "#6c757d"]], event.samples);
    refreshGridCharts();
}

// Describe el motivo de arranque codificado en event.timeStatus.
function gridStartupReason(event) {
    const flags = event.timeStatus;
    if (flags & 4) return "brownout";
    if (flags & 2) return "encendido";
    if (flags & 8) return "reinicio";
    return "";
}

// Renderiza la transicion raíz y las posteriores; event contiene la captura Grid ya decodificada.
function renderGridTransitionList(event) {
    const transitions = [{offsetMs: 0, previousDurationMs: event.previousDurationMs, states: event.states, previousVoltage: event.previousVoltage, currentVoltage: event.currentVoltage, root: true}, ...event.transitions];
    let html = '<div class="table-responsive"><table class="table table-sm align-middle mb-0"><thead><tr><th>t</th><th>Cambio</th><th>Previo</th><th>V</th></tr></thead><tbody>';
    transitions.forEach((item) => {
        const subject = gridSubjectName(item.states.subject), previous = gridStateName(item.states.previous, item.states.subject), current = gridStateName(item.states.current, item.states.subject), offset = item.offsetMs ? (item.offsetMs > 0 ? "+" : "") + item.offsetMs + " ms" : "Inicio", voltage = item.states.subject === 4 ? "—" : formatGridVoltage(item.previousVoltage) + " → " + formatGridVoltage(item.currentVoltage) + " V";
        html += '<tr' + (item.root ? ' class="table-primary"' : "") + '><td class="text-nowrap">' + offset + '</td><td>' + subject + '<br><span class="small">' + previous + ' → ' + current + '</span></td><td class="text-nowrap">' + (event.startup && !item.previousDurationMs ? "—" : formatGridDuration(item.previousDurationMs)) + '</td><td class="text-nowrap">' + voltage + '</td></tr>';
    });
    $("#gridEventTransitions").html(html + "</tbody></table></div>");
}

// Construye un gráfico Fast RMS; elementId identifica el contenedor y series define nombre, campo y color.
function createGridChart(elementId, title, unit, series, samples) {
    const data = series.map(([name, key, color]) => ({type: "line", name: name, showInLegend: true, markerSize: 0, color: color, dataPoints: samples.map((sample) => ({x: sample.relativeMs, y: sample[key]}))}));
    return new CanvasJS.Chart(elementId, {animationEnabled: false, zoomEnabled: true, axisX: {title: "Tiempo relativo (ms)"}, axisY: {title: title, suffix: " " + unit, includeZero: false, gridDashType: "dot"}, legend: {cursor: "pointer", itemclick: toggleGridSeries}, data: data});
}

// Alterna la visibilidad de una serie; event identifica la serie y su grafico CanvasJS.
function toggleGridSeries(event) { event.dataSeries.visible = event.dataSeries.visible === false; event.chart.render(); }

// Muestra u oculta un gráfico Grid; type acepta voltage o current y visible define su estado final.
function setGridChartVisibility(type, visible) {
    const isVoltage = type === "voltage", chartId = isVoltage ? "#gridEventVoltageChart" : "#gridEventCurrentChart", buttonId = isVoltage ? "#gridEventVoltageToggle" : "#gridEventCurrentToggle";
    gridEventsViewer[isVoltage ? "voltageVisible" : "currentVisible"] = visible;
    $(chartId).toggleClass("d-none", !visible); $(buttonId).toggleClass("active", visible).attr("aria-pressed", visible);
    if (visible) { const chart = isVoltage ? gridEventsViewer.voltageChart : gridEventsViewer.currentChart; if (chart) chart.render(); }
}

// Aplica las preferencias visibles a ambas gráficas después de cargar una captura.
function refreshGridCharts() {
    setGridChartVisibility("voltage", gridEventsViewer.voltageVisible);
    setGridChartVisibility("current", gridEventsViewer.currentVisible);
}

// Desempaqueta sujeto y estados Grid del campo persistente de 16 bits.
function decodeGridStates(value) { return {subject: value & 7, previous: value >> 3 & 7, current: value >> 6 & 7}; }

// Convierte el sujeto compacto en una etiqueta visible; subject identifica fase, neutro o red.
function gridSubjectName(subject) { return ({0: "Fase R", 1: "Fase S", 2: "Fase T", 3: "Neutro-tierra", 4: "Red", R: "Fase R", S: "Fase S", T: "Fase T", N: "Neutro-tierra", G: "Red"})[subject] || "Desconocido"; }

// Convierte el estado compactado en una etiqueta visible; state corresponde a fase o red.
function gridStateName(state, subject) {
    if (typeof state === "string") return ({N: "Normal", L: "Bajo voltaje", H: "Alto voltaje", X: "Sin tensión", A: "Anormal", O: "Corte"})[state] || "Desconocido";
    return subject === 4 ? ({0: "Desconocido", 1: "Normal", 2: "Anormal", 3: "Corte"})[state] || "Desconocido" : ({0: "Desconocido", 1: "Normal", 2: "Bajo voltaje", 3: "Alto voltaje", 4: "Sin tensión"})[state] || "Desconocido";
}

// Formatea fecha Unix para el listado; seconds y milliseconds proceden de la cabecera Grid.
function formatGridTime(timestamp) { return timestamp ? new Date(timestamp).toLocaleString() : "Hora estimada"; }

// Formatea una duración de evento; milliseconds es el tiempo transcurrido en el estado previo.
function formatGridDuration(milliseconds) {
    if (milliseconds < 1000) return milliseconds + " ms";
    if (milliseconds < 60000) return (milliseconds / 1000).toFixed(1) + " s";
    const seconds = Math.floor(milliseconds / 1000);
    if (seconds < 3600) return `${Math.floor(seconds / 60)} min ${seconds % 60} s`;
    return `${Math.floor(seconds / 3600)} h ${Math.floor(seconds % 3600 / 60)} min`;
}

// Actualiza el mensaje del visor; message describe el resultado e isError indica error.
function setGridEventsStatus(message, isError) { $("#gridEventsStatus").text(message).toggleClass("text-danger", !!isError).toggleClass("text-muted", !isError); }

// Convierte bytes a una etiqueta compacta; value indica la capacidad disponible.
function formatGridBytes(value) { return value < 1048576 ? (value / 1024).toFixed(0) + " KiB" : (value / 1048576).toFixed(1) + " MiB"; }

// Solicita una captura artificial para comprobar el recorrido completo de la persistencia.
async function createGridEventTest() {
    try { const response = await fetch(board.IP + "/grid-events/test", {method: "POST", cache: "no-store"}); if (!response.ok) throw new Error(); setGridEventsStatus("Prueba solicitada; espere unos segundos y recargue."); }
    catch (_) { setGridEventsStatus("No se pudo solicitar la captura de prueba.", true); }
}
