let gridEventsViewer = {events: [], cache: new Map(), selectedId: null, request: 0, records: [], selectedRecord: null, showMarkers: true, linkViews: true, panMode: false, voltageChart: null, currentChart: null, voltageVisible: true, currentVisible: false, loading: false};

// Inicializa los controles del visor de capturas persistentes de red; sin parámetros.
function initGridEventsViewer() {
    $("#gridEventsRefresh").on("click", loadGridEventsViewer); $("#gridEventsTest").on("click", createGridEventTest);
    $("#gridEventVoltageToggle").on("click", () => setGridChartVisibility("voltage", !gridEventsViewer.voltageVisible));
    $("#gridEventCurrentToggle").on("click", () => setGridChartVisibility("current", !gridEventsViewer.currentVisible));
    $("#gridEventMarkers").on("change", function () { gridEventsViewer.showMarkers = this.checked; refreshGridMarkers(); });
    $("#gridEventLinkViews").on("change", function () {
        gridEventsViewer.linkViews = this.checked;
        if (!this.checked) return;
        const waveOpen = $("#gridEventWaveDetails").prop("open");
        const source = waveOpen && gridWaveViewer.chart ? gridWaveViewer.chart :
            (gridEventsViewer.currentVisible ? gridEventsViewer.currentChart : gridEventsViewer.voltageChart);
        syncGridChartRange(source);
    });
    $("#gridEventZoomToggle").on("click", () => setGridChartNavigationMode(false));
    $("#gridEventPanToggle").on("click", () => setGridChartNavigationMode(true));
    $("#gridEventResetZoom").on("click", resetGridChartRange);
    $("#gridEventWaveDetails").on("toggle", function () { if (this.open) loadGridWaves(); });
    $("#gridWaveRefresh").on("click", loadGridWaves);
    $("#gridWaveSelect").on("change", loadSelectedGridWave);
}

// Descarga el índice reciente y actualiza la lista de eventos disponibles; sin parámetros.
async function loadGridEventsViewer() {
    if (gridEventsViewer.loading) return;
    gridEventsViewer.loading = true; setGridEventsStatus("Consultando eventos guardados...");
    try {
        const scope = await refreshCaptureCacheScope(new AbortController().signal);
        const identity = scope ? scope.deviceId + ":" + scope.fastEpoch : null;
        if (identity !== gridEventsViewer.cacheIdentity) gridEventsViewer.cache.clear();
        gridEventsViewer.cacheIdentity = identity;
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
    finally { gridEventsViewer.loading = false; }
}

// Renderiza las capturas Grid recientes; sin parámetros.
function renderGridEventsList() {
    const list = $("#gridEventsList").empty();
    if (!gridEventsViewer.events.length) return list.append('<div class="text-muted small p-2">No hay eventos persistidos todavía.</div>');
    gridEventsViewer.events.forEach((event) => {
        const when = formatGridTime(event.t);
        const saved = gridEventsViewer.cache.get(event.i), cached = saved && saved.timestamp === event.t ? saved : null, description = Object.prototype.hasOwnProperty.call(event, "k") ? gridPointName(event.k) + (event.l ? " · limitado" : "") : gridStateName(event.p) + " → " + gridStateName(event.s);
        const details = cached ? gridEventRecords(cached).length + " registros · " + buildGridOverview(cached).map(group => group.name).join(", ") : "Disparador: " + gridRecordSubject(event.y, event.c ? 4 : event.k) + " · " + description;
        list.append($("<button></button>").addClass("list-group-item list-group-item-action py-2").toggleClass("active", gridEventsViewer.selectedId === event.i).attr({type: "button", "aria-current": gridEventsViewer.selectedId === event.i ? "true" : "false"})
            .html('<strong>Captura #' + event.i + '</strong><br><span class="small">' + when + '</span><br><span class="small">' + details + '</span>').on("click", () => loadGridEvent(event)));
    });
}

// Descarga y decodifica una captura Grid seleccionada; event contiene la cabecera indexada.
async function loadGridEvent(event) {
    const request = ++gridEventsViewer.request;
    gridEventsViewer.controller?.abort();
    const controller = new AbortController();
    gridEventsViewer.controller = controller;
    setGridEventsStatus("Cargando captura #" + event.i + "...");
    try {
        const scope = await refreshCaptureCacheScope(controller.signal);
        const identity = scope ? scope.deviceId + ":" + scope.fastEpoch : null;
        if (identity !== gridEventsViewer.cacheIdentity) gridEventsViewer.cache.clear();
        gridEventsViewer.cacheIdentity = identity;
        const decoded = await loadCachedFast(event.i, event.t, scope, controller.signal);
        if (request !== gridEventsViewer.request) return;
        if (!decoded.partial) {
            gridEventsViewer.cache.set(event.i, decoded);
            if (gridEventsViewer.cache.size > 12) gridEventsViewer.cache.delete(gridEventsViewer.cache.keys().next().value);
        }
        gridEventsViewer.selectedId = event.i;
        resetGridWaves(decoded, scope);
        renderGridEvent(decoded); renderGridEventsList(); setGridEventsStatus("Captura #" + event.i + " cargada.");
        if ($("#gridEventWaveDetails").prop("open")) loadGridWaves();
    } catch (error) { if (request === gridEventsViewer.request) setGridEventsStatus(error.message || "No se pudo cargar la captura.", true); }
}

// Ordena cabecera y transiciones; event es una captura decodificada, sin inferir registros faltantes.
function gridEventRecords(event) {
    return [{offsetMs: 0, previousDurationMs: event.previousDurationMs, states: event.states, previousVoltage: event.previousVoltage, currentVoltage: event.currentVoltage, root: true}, ...event.transitions]
        .sort((a, b) => a.offsetMs - b.offsetMs).map((record, index) => ({...record, number: index + 1}));
}

// Agrupa los registros por sujeto y magnitud; event conserva separados estados y puntuales.
function buildGridOverview(event) {
    const groups = new Map();
    for (const record of gridEventRecords(event)) {
        const state = record.states, key = state.subject + ":" + (state.isCurrent || state.isPoint && state.pointType >= 3 ? "A" : "V");
        if (!groups.has(key)) groups.set(key, {key, name: gridRecordSubject(state.subject, state.pointType), states: [], points: [], returns: [], discontinuous: false});
        const group = groups.get(key);
        if (state.isPoint) { group.points.push(record); continue; }
        const last = group.states[group.states.length - 1];
        if (last && last.states.current !== state.previous) group.discontinuous = true;
        group.states.push(record);
        if (state.current === 1 && state.previous > 1) group.returns.push(record);
    }
    return Array.from(groups.values());
}

// Resume lo observado sin reclasificar eventos; event contiene registros y avisos de integridad.
function renderGridOverview(event) {
    const target = $("#gridEventOverview").empty();
    for (const group of buildGridOverview(event)) {
        const row = $("<button></button>").addClass("gridEventOverviewRow").attr({type: "button", "data-subject": group.key, "aria-pressed": "false", title: "Ampliar los cambios de " + group.name}).prop("disabled", !event.samples.length), sequence = [];
        group.states.forEach((record, index) => {
            if (!index || group.states[index - 1].states.current !== record.states.previous) sequence.push(gridStateName(record.states.previous, record.states.subject));
            sequence.push(gridStateName(record.states.current, record.states.subject));
        });
        group.points.forEach(record => sequence.push(gridPointName(record.states.pointType) + " (" + formatGridDuration(record.previousDurationMs) + (record.states.limited ? ", limitado" : "") + ")"));
        let outcome = "";
        if (group.returns.length === 1) outcome = "Retorno registrado · " + formatGridDuration(group.returns[0].previousDurationMs) + " en estado previo";
        else if (group.returns.length) outcome = group.returns.length + " retornos registrados";
        if (group.states.length && group.states[group.states.length - 1].states.current !== 1) outcome = (outcome ? outcome + " · " : "") + "Sin retorno registrado en esta captura";
        if (group.discontinuous) outcome += (outcome ? " · " : "") + "Secuencia incompleta";
        row.append($("<strong></strong>").text(group.name), $("<div></div>").append($("<div></div>").text(sequence.join(" → ")), $("<div></div>").addClass("small text-muted").text(outcome)));
        row.on("click", () => selectGridOverview(group));
        target.append(row);
    }
}

// Decodifica el unico formato vigente; buffer contiene paginas completas con CRC.
function decodeGridEvent(buffer) {
    const bytes = new Uint8Array(buffer);
    if (bytes.length < 2048) throw new Error("La descarga no contiene la cabecera completa.");
    const root = new DataView(bytes.buffer, bytes.byteOffset, 2048);
    const format = root.getUint8(0);
    if (format !== 4) throw new Error("Formato de evento no reconocido; no se admiten capturas anteriores.");
    const timeStatus = root.getUint8(13);
    const hasNeutral = true, sampleBytes = 16;
    if (!(timeStatus & 32)) throw new Error("Empaquetado FAST no válido.");
    const event = {format, id: root.getUint32(1, true), timestamp: root.getUint32(5, true) * 1000 + root.getUint16(9, true) / 10, states: decodeGridStates(root.getUint16(11, true), format), timeStatus: timeStatus, estimatedTime: !!(timeStatus & 1), startup: !!(timeStatus & 14), previousDurationMs: root.getUint32(14, true) * 10, previousVoltage: decodeGridVoltage(root.getUint16(18, true)), currentVoltage: decodeGridVoltage(root.getUint16(20, true)), firstOffset: root.getInt32(22, true) / 1000, samples: [], transitions: [], partial: false, invalidPages: 0, timingEstimated: false};
    const rootSamples = root.getUint8(26), pageCount = root.getUint8(27);
    event.preSampleCount = rootSamples;
    event.hasNeutral = hasNeutral;
    event.snapshot = null;
    event.settings = null;
    if (rootSamples > 113 || pageCount < 3 || pageCount > 5 || bytes.length < pageCount * 2048) throw new Error("Cabecera de evento inválida.");
    if (gridCrc16(bytes, 0, 28) !== root.getUint16(28, true)) throw new Error("El CRC de la cabecera del evento no es válido.");
    let lastSampleTimestamp = null, sampleRelativeMs = event.firstOffset;
    // Agrega count muestras de view desde offset y conserva sus deltas temporales.
    const appendSamples = (view, offset, count) => {
        for (let index = 0; index < count; ++index) {
            const sample = decodeGridFastSample(view, offset + index * sampleBytes, hasNeutral);
            if (lastSampleTimestamp !== null) {
                let deltaMs = ((sample.timestamp - lastSampleTimestamp + 65536) & 0xffff) / 10;
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
        event.settings = decodeGridSettings(root);
        appendSamples(root, 234, rootSamples);
    }
    else { event.partial = true; event.invalidPages = 1; sampleRelativeMs += rootSamples * 10; }
    for (let pageIndex = 1; pageIndex < pageCount && pageIndex * 2048 < bytes.length; ++pageIndex) {
        const pageOffset = pageIndex * 2048, pageBytes = bytes.subarray(pageOffset, pageOffset + 2048);
        const page = new DataView(bytes.buffer, bytes.byteOffset + pageOffset, 2048), recordFlags = page.getUint8(7), transitions = recordFlags & 127, samples = page.getUint8(6);
        const usedBytes = 154 + samples * sampleBytes;
        if (page.getUint32(0, true) !== event.id || page.getUint8(4) !== pageIndex || page.getUint8(5) !== pageCount || transitions > 12 || samples > 118 ||
            gridCrc16(bytes, pageOffset, usedBytes, pageOffset + 8) !== page.getUint16(8, true)) {
            event.partial = true; event.invalidPages = (event.invalidPages || 0) + 1; continue;
        }
        if (recordFlags & 128) event.timeStatus |= 64;
        for (let index = 0; index < transitions; ++index) event.transitions.push(decodeGridTransition(page, 10 + index * 12, format));
        appendSamples(page, 154, samples);
    }
    event.transitions.sort((a, b) => a.offsetMs - b.offsetMs);
    return event;
}

// Lee los 48 bytes de ajustes; view contiene la pagina raiz del unico formato vigente.
function decodeGridSettings(view) {
    const value = at => view.getUint16(at, true) / 10, time = at => view.getUint16(at, true) * 10;
    return {low: [30, 32, 34].map(value), high: [36, 38, 40].map(value), absent: value(42),
        hysteresis: value(44), lossMs: time(46), recoveryMs: time(48), pointLimitMs: time(50),
        overCurrent: value(52), currentHysteresis: view.getUint16(54, true) / 100,
        dipMs: view.getUint8(56) * 10, swellMs: view.getUint8(57) * 10, ocMs: view.getUint8(58) * 10, flags: view.getUint8(59),
        neutralHigh: value(60), neutralHysteresis: value(62), neutralMs: time(64), neutralRecoveryMs: time(66),
        preMs: time(68), postMs: time(70), pages: view.getUint8(72), windowMs: time(74)};
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

// Lee ocho canales de 14 bits; view contiene la pagina y offset indica la muestra.
function decodeGridFastSample(view, offset) {
    const sample = {timestamp: view.getUint16(offset, true), vn: null};
    const keys = ["vr", "vs", "vt", "ir", "is", "it", "inn", "vn"];
    for (let channel = 0; channel < 8; ++channel) {
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

// Revisa el previo de event con tolerancia de una muestra/20 ms, incluso en capturas ya guardadas.
function gridPreHistoryMissing(event) {
    if (!(event.timeStatus & 16)) return false;
    if (!event.settings || event.preSampleCount == null || !event.samples.length) return true;
    const expectedMs = event.settings.preMs;
    return event.preSampleCount + 1 < expectedMs / 10 || -event.samples[0].relativeMs + 20 < expectedMs;
}

// Presenta las gráficas Fast RMS y la secuencia de transiciones; event contiene la captura decodificada.
function renderGridEvent(event) {
    const subject = gridRecordSubject(event.states.subject, event.states.pointType), previous = gridStateName(event.states.previous, event.states.subject), current = gridStateName(event.states.current, event.states.subject), when = event.estimatedTime ? "Hora estimada" : formatGridTime(event.timestamp), voltage = event.states.subject === 4 ? "" : " " + formatGridVoltage(event.previousVoltage, 2) + " → " + formatGridVoltage(event.currentVoltage, 2) + " V.", duration = event.startup ? "Duración previa no disponible" : "Estado previo durante " + formatGridDuration(event.previousDurationMs);
    const startup = event.startup ? " Estado reconstruido al iniciar" + (gridStartupReason(event) ? " (" + gridStartupReason(event) + ")" : "") + "." : "";
    const partial = event.partial ? " Captura incompleta: " + event.invalidPages + " página(s) con CRC o cabecera inválidos." : "";
    const timing = event.timingEstimated ? " La escala temporal incluye intervalos estimados." : "";
    const description = event.states.isPoint ? gridPointName(event.states.pointType) + ". Duración observada: " + formatGridDuration(event.previousDurationMs) + (event.states.limited ? " (captura limitada)." : ".") : previous + " → " + current + ". " + duration + "." + (event.states.isCurrent ? voltage.replace(" V.", " A.") : voltage);
    const dropped = event.timeStatus & 64 ? " Se agotó el espacio de marcas: faltan entradas." : "";
    const synthetic = (event.timeStatus & 128 ? " Prueba artificial." : "") + (gridPreHistoryMissing(event) ? " Historial FAST anterior al disparo insuficiente." : "");
    const first = event.samples[0], last = event.samples[event.samples.length - 1];
    $("#gridEventSummary").text("Captura #" + event.id + " · " + when + " · " + event.samples.length + " muestras" + (first && last ? " · " + formatGridDuration(last.relativeMs - first.relativeMs) + " cubiertos" : ""));
    $("#gridEventTrigger").text("Disparador: " + subject + " · " + description);
    $("#gridEventWarnings").text((startup + partial + timing + dropped + synthetic).trim()).toggleClass("d-none", !(startup + partial + timing + dropped + synthetic).trim());
    gridEventsViewer.records = gridEventRecords(event);
    gridEventsViewer.selectedRecord = null;
    renderGridOverview(event);
    renderGridTransitionList(event);
    renderGridSnapshot(event.snapshot);
    renderGridSettings(event.settings);
    const voltageSeries = [["R", "vr", "#d9534f"], ["S", "vs", "#5cb85c"], ["T", "vt", "#337ab7"]];
    if (event.hasNeutral) voltageSeries.push(["Neutro-tierra", "vn", "#00838f"]);
    if (gridEventsViewer.voltageChart) gridEventsViewer.voltageChart.destroy();
    if (gridEventsViewer.currentChart) gridEventsViewer.currentChart.destroy();
    gridEventsViewer.voltageChart = createGridChart("gridEventVoltageChart", "Voltaje RMS", "V", voltageSeries, event.samples);
    gridEventsViewer.currentChart = createGridChart("gridEventCurrentChart", "Corriente RMS", "A", [["R", "ir", "#d9534f"], ["S", "is", "#5cb85c"], ["T", "it", "#337ab7"], ["N", "inn", "#6c757d"]], event.samples);
    setGridChartNavigationMode(gridEventsViewer.panMode);
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
    let html = '<div class="table-responsive"><table class="table table-sm align-middle mb-0"><thead><tr><th># / t</th><th>Estado / evento</th><th>Duración</th><th>Valores</th></tr></thead><tbody>';
    gridEventRecords(event).forEach((item) => {
        const subject = gridRecordSubject(item.states.subject, item.states.pointType), previous = gridStateName(item.states.previous, item.states.subject), current = gridStateName(item.states.current, item.states.subject), offset = (item.offsetMs > 0 ? "+" : "") + item.offsetMs + " ms", voltage = item.states.subject === 4 ? "—" : formatGridVoltage(item.previousVoltage) + " → " + formatGridVoltage(item.currentVoltage) + " V";
        const point = item.states.isPoint, unit = item.states.pointType >= 3 ? "A" : "V";
        const change = point ? gridPointName(item.states.pointType) + (item.states.limited ? " · limitado" : "") : previous + ' → ' + current;
        const values = point || item.states.isCurrent ? formatGridVoltage(item.previousVoltage) + ' → ' + formatGridVoltage(item.currentVoltage) + ' ' + unit : voltage;
        const first = event.samples[0], last = event.samples[event.samples.length - 1], outside = !first || item.offsetMs < first.relativeMs || item.offsetMs > last.relativeMs;
        html += '<tr data-record="' + item.number + '"><td class="text-nowrap"><span class="text-muted">' + item.number + '</span><br><button type="button" class="btn btn-link btn-sm p-0 gridEventRecord" data-record="' + item.number + '" aria-pressed="false" title="' + (outside ? "Fuera del tramo FAST disponible" : "Ver este cambio en la gráfica") + '"' + (outside ? " disabled" : "") + '>' + offset + '</button></td><td>' + subject + (item.root ? '<span class="badge bg-light text-dark ms-1">Disparador</span>' : "") + '<br><span class="small">' + change + '</span></td><td>' + (point ? "Evento: " : "Estado previo: ") + (event.startup && !item.previousDurationMs ? "—" : formatGridDuration(item.previousDurationMs)) + '</td><td class="text-nowrap" title="' + (point ? "RMS previo y extremo FAST observado" : "Valores de la transición") + '">' + values + '</td></tr>';
    });
    $("#gridEventTransitions").html(html + "</tbody></table></div>");
    $("#gridEventTransitions tr[data-record]").on("click", function () { selectGridRecord(Number(this.dataset.record)); });
}

// Construye marcas simultaneas agrupadas; records contiene entradas numeradas y selected la elegida.
function gridRecordMarkers(records, selected, minimum, maximum) {
    const groups = new Map();
    for (const record of records) {
        if (record.offsetMs < minimum || record.offsetMs > maximum) continue;
        if (!groups.has(record.offsetMs)) groups.set(record.offsetMs, []);
        groups.get(record.offsetMs).push(record.number);
    }
    return Array.from(groups, ([value, numbers], index) => {
        const active = numbers.includes(selected);
        return {value, label: active ? "#" + selected : "#" + numbers[0] + (numbers.length > 1 ? "–" + numbers[numbers.length - 1] : ""),
            color: active ? "#212529" : "#8b949e", thickness: active ? 2 : 1, lineDashType: active ? "solid" : "dash",
            labelFontSize: 11, labelFontColor: "#fff", labelBackgroundColor: active ? "#212529" : "#667078", labelAlign: index % 2 ? "center" : "far"};
    });
}

// Actualiza las marcas de ambos graficos segun la seleccion y el interruptor; sin parametros.
function refreshGridMarkers() {
    for (const chart of [gridEventsViewer.voltageChart, gridEventsViewer.currentChart]) {
        if (!chart) continue;
        const samples = chart.options.data[0].dataPoints, first = samples[0], last = samples[samples.length - 1];
        chart.options.axisX.stripLines = gridEventsViewer.showMarkers && first ? gridRecordMarkers(gridEventsViewer.records, gridEventsViewer.selectedRecord, first.x, last.x) : [];
        chart.render();
    }
}

// Selecciona number y amplia su cambio o puntual completo; number es el indice visible del registro.
function selectGridRecord(number) {
    const record = gridEventsViewer.records.find(item => item.number === number);
    if (!record) return;
    gridEventsViewer.selectedRecord = number;
    $("#gridEventOverview .gridEventOverviewRow").attr("aria-pressed", "false");
    $("#gridEventTransitions tr[data-record]").each(function () { $(this).toggleClass("table-primary", Number(this.dataset.record) === number); });
    $("#gridEventTransitions .gridEventRecord").each(function () { $(this).attr("aria-pressed", Number(this.dataset.record) === number); });
    zoomGridRecords([record]);
}

// Amplia todos los cambios guardados de un sujeto; group es el resumen de tension o corriente elegido.
function selectGridOverview(group) {
    const records = [...group.states, ...group.points], numbers = records.map(record => record.number);
    gridEventsViewer.selectedRecord = null;
    $("#gridEventOverview .gridEventOverviewRow").each(function () { $(this).attr("aria-pressed", this.dataset.subject === group.key); });
    $("#gridEventTransitions tr[data-record]").each(function () { $(this).toggleClass("table-primary", numbers.includes(Number(this.dataset.record))); });
    $("#gridEventTransitions .gridEventRecord").each(function () { $(this).attr("aria-pressed", numbers.includes(Number(this.dataset.record))); });
    zoomGridRecords(records);
}

// Obtiene inicio/fin guardados; record es la entrada elegida y records permite emparejar cambios del mismo sujeto.
function gridRecordInterval(record, records) {
    const start = record.offsetMs, state = record.states;
    if (state.isPoint) return {start, end: start + record.previousDurationMs};
    const changes = records.filter(item => !item.states.isPoint && item.states.subject === state.subject && item.states.isCurrent === state.isCurrent);
    const index = changes.findIndex(item => item === record || record.number != null && item.number === record.number);
    const previous = index > 0 ? changes[index - 1] : null, next = index >= 0 ? changes[index + 1] : null;
    if (state.current === 1 && state.previous > 1) {
        const began = previous && previous.states.current === state.previous ? previous.offsetMs : start - record.previousDurationMs;
        return {start: began, end: start};
    }
    if (state.current > 1 && next && next.states.previous === state.current) return {start, end: next.offsetMs};
    return {start, end: start};
}

// Calcula el intervalo completo con 50 ms por lado; records selecciona entradas, context permite emparejarlas y los limites recortan al FAST disponible.
function gridSelectionRange(records, minimum, maximum, context = records) {
    if (!records.length || maximum <= minimum) return null;
    const intervals = records.map(record => gridRecordInterval(record, context));
    const start = Math.min(...intervals.map(interval => interval.start));
    const end = Math.max(...intervals.map(interval => interval.end));
    if (end < minimum || start > maximum) return null;
    return {minimum: Math.max(minimum, start - 50), maximum: Math.min(maximum, end + 50)};
}

// Aplica el zoom temporal a voltajes y corrientes; records define el tramo seleccionado.
function zoomGridRecords(records) {
    for (const chart of [gridEventsViewer.voltageChart, gridEventsViewer.currentChart]) {
        if (!chart) continue;
        const samples = chart.options.data[0].dataPoints;
        if (!samples.length) continue;
        const range = gridSelectionRange(records, samples[0].x, samples[samples.length - 1].x, gridEventsViewer.records);
        if (!range) continue;
        chart.options.axisX.viewportMinimum = range.minimum;
        chart.options.axisX.viewportMaximum = range.maximum;
    }
    refreshGridMarkers();
    if (typeof syncGridWaveRange === "function") syncGridWaveRange();
}

// Restaura el rango temporal completo en ambos graficos; sin parametros.
function resetGridChartRange() {
    for (const chart of [gridEventsViewer.voltageChart, gridEventsViewer.currentChart]) {
        if (!chart) continue;
        setGridChartRange(chart, null, null);
    }
    setGridChartNavigationMode(false);
    if (typeof syncGridWaveRange === "function") syncGridWaveRange(null, null, true);
}

// Construye un gráfico Fast RMS; elementId identifica el contenedor y series define nombre, campo y color.
function createGridChart(elementId, title, unit, series, samples) {
    const data = series.map(([name, key, color]) => ({type: "line", name: name, showInLegend: true, markerSize: 0, color: color, dataPoints: samples.map((sample) => ({x: sample.relativeMs, y: sample[key]}))}));
    let chart;
    chart = new CanvasJS.Chart(elementId, {animationEnabled: false, zoomEnabled: true, axisX: {title: "Tiempo relativo (ms)", stripLines: gridEventsViewer.showMarkers && samples.length ? gridRecordMarkers(gridEventsViewer.records, null, samples[0].relativeMs, samples[samples.length - 1].relativeMs) : []}, axisY: {title: title, suffix: " " + unit, includeZero: false, gridDashType: "dot"}, legend: {cursor: "pointer", itemclick: toggleGridSeries}, rangeChanged: () => syncGridChartRange(chart), data: data});
    chart.zoomEnabled = !gridEventsViewer.panMode;
    chart.panEnabled = gridEventsViewer.panMode;
    return chart;
}

// Aplica modo de zoom o paneo a todos los gráficos; panEnabled elige arrastrar para mover.
function setGridChartNavigationMode(panEnabled) {
    gridEventsViewer.panMode = panEnabled;
    for (const chart of [gridEventsViewer.voltageChart, gridEventsViewer.currentChart, gridWaveViewer.chart]) {
        if (!chart) continue;
        chart.zoomEnabled = !panEnabled;
        chart.panEnabled = panEnabled;
        chart.render();
    }
    $("#gridEventZoomToggle").toggleClass("active", !panEnabled).attr("aria-pressed", !panEnabled);
    $("#gridEventPanToggle").toggleClass("active", panEnabled).attr("aria-pressed", panEnabled);
}

// Cambia el rango X sin perder el estado interno que CanvasJS usa para zoom y paneo.
function setGridChartRange(chart, minimum, maximum) {
    const axis = chart.axisX && chart.axisX[0];
    if (!axis) return;
    axis.set("viewportMinimum", minimum, false);
    axis.set("viewportMaximum", maximum, false);
}

// Propaga el rango cambiado por el usuario a las otras vistas; source es el gráfico manipulado.
function syncGridChartRange(source) {
    if (!source || !source.axisX || !source.axisX[0]) return;
    const waveSource = typeof gridWaveViewer !== "undefined" && source === gridWaveViewer.chart;
    if (waveSource && !gridEventsViewer.linkViews) return;
    const axis = source.axisX[0], minimum = axis.viewportMinimum, maximum = axis.viewportMaximum;
    for (const chart of [gridEventsViewer.voltageChart, gridEventsViewer.currentChart]) {
        if (!chart || chart === source) continue;
        setGridChartRange(chart, minimum, maximum);
        chart.render();
    }
    if (!waveSource && gridEventsViewer.linkViews && typeof syncGridWaveRange === "function")
        syncGridWaveRange(minimum, maximum);
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
function decodeGridStates(value) { const isPoint = !!(value & 512), isCurrent = !isPoint && !!(value & 1024);
    return {subject: value & 7, previous: value >> 3 & 7, current: value >> 6 & 7, isPoint, isCurrent,
        pointType: isPoint ? value >>> 10 & 31 : isCurrent ? 4 : 0, limited: !!(value & 32768)}; }

// Devuelve la etiqueta de un evento puntual; type es el codigo persistido de cinco bits.
function gridPointName(type) { return ({1: "Dip", 2: "Swell", 3: "Sobrecorriente"})[type] || "Evento desconocido"; }

// Distingue corriente de neutro de tension neutro-tierra; subject y type identifican la entrada.
function gridRecordSubject(subject, type) { return type >= 3 && (subject === 3 || subject === "N") ? "Neutro" : gridSubjectName(subject); }

// Muestra los ajustes guardados y sus limites de precision; settings es null cuando falla el CRC.
function renderGridSettings(settings) {
    const target = $("#gridEventSettings");
    if (!settings) return target.text("Configuración no disponible: página raíz inválida.");
    const values = items => items.map(value => formatGridSnapshotValue(Number.isFinite(value) ? value : null, 1)).join(" / ");
    const rows = [["DIP / bajo voltaje R/S/T", values(settings.low) + " V"], ["Swell / alto voltaje R/S/T", values(settings.high) + " V"],
        ["Ausencia / histéresis", values([settings.absent, settings.hysteresis]) + " V"],
        ["Confirmación pérdida / retorno", [settings.lossMs, settings.recoveryMs].join(" / ") + " ms"],
        ["DIP: mínimo / guardar puntual", settings.dipMs + " ms · " + (settings.flags & 1 ? "Sí" : "No")],
        ["Swell: mínimo / guardar puntual", settings.swellMs + " ms · " + (settings.flags & 2 ? "Sí" : "No")],
        ["Corriente alta / histéresis", values([settings.overCurrent, settings.currentHysteresis]) + " A"],
        ["Sobrecorriente: mínimo / guardar puntual", settings.ocMs + " ms · " + (settings.flags & 4 ? "Sí" : "No")],
        ["Puntual → sostenido", settings.pointLimitMs + " ms"],
        ["Captura: previo / posterior / máximo", settings.preMs + " / " + settings.postMs + " / " + settings.windowMs + " ms · " + settings.pages + " páginas"],
        ["Neutro alto / histéresis", values([settings.neutralHigh, settings.neutralHysteresis]) + " V · " + (settings.flags & 8 ? "Activo" : "Desactivado")],
        ["Confirmación / retorno neutro", settings.neutralMs + " / " + settings.neutralRecoveryMs + " ms"]];
    target.html('<div class="table-responsive"><table class="table table-sm small mb-0"><tbody>' + rows.map(([label, value]) => '<tr><th>' + label + '</th><td>' + value + '</td></tr>').join("") + '</tbody></table></div>' +
        '<p class="small text-muted mt-2">Los estados sostenidos se guardan siempre.' + (settings.flags & 32 ? " Algunos ajustes están redondeados." : "") + (settings.flags & 64 ? " Hay valores saturados." : "") + '</p>');
}

// Convierte el sujeto compacto en una etiqueta visible; subject identifica fase, neutro o red.
function gridSubjectName(subject) { return ({0: "Fase R", 1: "Fase S", 2: "Fase T", 3: "Neutro-tierra", 4: "Red", R: "Fase R", S: "Fase S", T: "Fase T", N: "Neutro-tierra", G: "Red"})[subject] || "Desconocido"; }

// Convierte el estado compactado en una etiqueta visible; state corresponde a fase o red.
function gridStateName(state, subject) {
    if (typeof state === "string") return ({N: "Normal", L: "Bajo voltaje", H: "Alto voltaje", X: "Sin tensión", A: "Anormal", O: "Corte", C: "Alta corriente"})[state] || "Desconocido";
    return subject === 4 ? ({0: "Desconocido", 1: "Normal", 2: "Anormal", 3: "Corte"})[state] || "Desconocido" : ({0: "Desconocido", 1: "Normal", 2: "Bajo voltaje", 3: "Alto voltaje", 4: "Sin tensión", 5: "Alta corriente"})[state] || "Desconocido";
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
