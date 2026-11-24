let gridEventsViewer = {events: [], cache: new Map(), selectedId: null, request: 0, records: [], selectedRecord: null, showMarkers: true, linkViews: true, panMode: false, voltageChart: null, currentChart: null, voltageVisible: true, currentVisible: true, loading: false};

// Inicializa los controles del visor de capturas persistentes de red; sin parámetros.
function initGridEventsViewer() {
    initGridCaptureFilters();
    initGridEventsLayout();
    try {
        const saved = JSON.parse(localStorage.getItem('powerMeter.captureView') || '{}');
        for (const key of ['voltageVisible', 'currentVisible', 'showMarkers', 'linkViews']) if (typeof saved[key] === 'boolean') gridEventsViewer[key] = saved[key];
        for (const id of ['gridEventWaveDetails', 'gridEventContextDetails', 'gridEventSettingsDetails']) if (typeof saved[id] === 'boolean') document.getElementById(id).open = saved[id];
    } catch (_) {}
    $('#gridEventMarkers').prop('checked', gridEventsViewer.showMarkers);
    $('#gridEventLinkViews').prop('checked', gridEventsViewer.linkViews);
    refreshGridCharts();
    $('#gridEventWaveDetails, #gridEventContextDetails, #gridEventSettingsDetails').on('toggle', saveGridCaptureView);
    $("#gridEventsRefresh").on("click", loadGridEventsViewer);
    $("#gridEventVoltageToggle").on("click", () => setGridChartVisibility("voltage", !gridEventsViewer.voltageVisible));
    $("#gridEventCurrentToggle").on("click", () => setGridChartVisibility("current", !gridEventsViewer.currentVisible));
    $("#gridEventMarkers").on("change", function () { gridEventsViewer.showMarkers = this.checked; saveGridCaptureView(); refreshGridMarkers(); });
    $("#gridEventLinkViews").on("change", function () {
        gridEventsViewer.linkViews = this.checked;
        saveGridCaptureView();
        if (!this.checked) return;
        const waveOpen = $("#gridEventWaveDetails").prop("open");
        const source = waveOpen && gridWaveViewer.chart ? gridWaveViewer.chart :
            gridEventsViewer.voltageChart;
        syncGridChartRange(source);
    });
    $("#gridEventZoomToggle").on("click", () => setGridChartNavigationMode(false));
    $("#gridEventPanToggle").on("click", () => setGridChartNavigationMode(true));
    $("#gridEventResetZoom").on("click", resetGridChartRange);
    $("#gridEventExpand").on("click", () => setGridEventExpanded(!$('#gridEventWorkspace').hasClass('gridEventExpanded')));
    $(document).on('keydown', event => { if (event.key === 'Escape' && $('#gridEventWorkspace').hasClass('gridEventExpanded')) setGridEventExpanded(false); });
    $("#gridEventWaveDetails").on("toggle", function () { if (this.open) loadGridWaves(); });
    $("#gridWaveRefresh").on("click", loadGridWaves);
    $("#gridWaveSelect").on("click", "button", function () { selectGridWave(Number(this.dataset.waveId)); });
}

// Adapta paneles y controles al ancho de pantalla; no recibe parametros.
function initGridEventsLayout() {
    const layout = document.querySelector('.gridEventsLayout'), chartPanel = document.getElementById('gridEventChartPanel');
    const desktop = matchMedia('(min-width: 1100px)');
    let timer;
    // Ajusta altura disponible y reubica la barra segun el modo actual; no recibe parametros.
    function updateLayout() {
        const expanded = $('#gridEventWorkspace').hasClass('gridEventExpanded');
        if (!expanded) $('#gridChartViewTools').appendTo(desktop.matches ? '#gridCaptureViewTools' : '#gridEventChartPanel');
        if (!desktop.matches && !expanded) $('#gridChartViewTools').prependTo(chartPanel);
        if (desktop.matches && layout.getBoundingClientRect().top > 0)
            layout.style.setProperty('--gridEventsHeight', Math.max(260, innerHeight - layout.getBoundingClientRect().top - 45) + 'px');
    }
    $('.gridFilterMobileToggle').on('click', function () {
        const group = $(this).parent(), open = !group.hasClass('gridFilterOpen');
        $('.gridFilterMobileToggle').attr('aria-expanded', 'false').parent().removeClass('gridFilterOpen');
        group.toggleClass('gridFilterOpen', open); $(this).attr('aria-expanded', open);
    });
    $('.gridPanelDivider').each(function () {
        const divider = this, left = divider.dataset.panel === 'captures';
        const panel = document.querySelector(left ? '.gridEventsCaptures' : '.gridEventSide');
        const container = document.querySelector(left ? '.gridEventsLayout' : '.gridEventsPanels');
        const className = left ? 'capturesCollapsed' : 'detailsCollapsed', button = divider.querySelector('button');
        button.addEventListener('click', () => {
            const collapsed = container.classList.toggle(className);
            button.textContent = left ? (collapsed ? '›' : '‹') : (collapsed ? '‹' : '›');
            const label = (collapsed ? 'Mostrar ' : 'Ocultar ') + (left ? 'capturas' : 'detalles');
            button.setAttribute('aria-label', label); button.title = label;
        });
        divider.addEventListener('pointerdown', event => {
            if (event.target === button || container.classList.contains(className)) return;
            const startX = event.clientX, startWidth = panel.getBoundingClientRect().width;
            divider.setPointerCapture(event.pointerId);
            // Ajusta el panel segun el desplazamiento horizontal del puntero; move es el evento activo.
            const move = move => { panel.style.width = Math.max(left ? 150 : 180, startWidth + (move.clientX - startX) * (left ? 1 : -1)) + 'px'; };
            // Finaliza el arrastre y retira los escuchadores; no recibe parametros.
            const end = () => { divider.removeEventListener('pointermove', move); divider.removeEventListener('pointerup', end); divider.removeEventListener('pointercancel', end); };
            divider.addEventListener('pointermove', move); divider.addEventListener('pointerup', end); divider.addEventListener('pointercancel', end);
        });
    });
    $(document).on('click', event => {
        if (!$(event.target).closest('.gridFilterOpen').length)
            $('.gridFilterMobileToggle').attr('aria-expanded', 'false').parent().removeClass('gridFilterOpen');
    });
    $(window).on('resize', updateLayout);
    if (typeof ResizeObserver !== 'undefined') new ResizeObserver(() => {
        clearTimeout(timer); timer = setTimeout(() => {
            updateLayout();
            for (const chart of [gridEventsViewer.voltageChart, gridWaveViewer.chart]) if (chart) chart.render();
        }, 150);
    }).observe(chartPanel);
    updateLayout();
}

// Guarda la visibilidad y los paneles del visor en este navegador; no recibe parametros.
function saveGridCaptureView() {
    const saved = {};
    for (const key of ['voltageVisible', 'currentVisible', 'showMarkers', 'linkViews']) saved[key] = gridEventsViewer[key];
    for (const id of ['gridEventWaveDetails', 'gridEventContextDetails', 'gridEventSettingsDetails']) saved[id] = document.getElementById(id).open;
    try { localStorage.setItem('powerMeter.captureView', JSON.stringify(saved)); } catch (_) {}
}

// Descarga el índice reciente y actualiza la lista de eventos disponibles; sin parámetros.
async function loadGridEventsViewer() {
    if (gridEventsViewer.loading) return;
    ++gridCaptureFilters.run; gridCaptureFilters.controller?.abort(); clearTimeout(gridCaptureFilters.timer);
    gridEventsViewer.loading = true; setGridEventsStatus("Consultando eventos guardados...");
    try {
        const scope = await refreshCaptureCacheScope(new AbortController().signal);
        const identity = scope ? scope.deviceId + ":" + scope.fastEpoch : null;
        if (identity !== gridEventsViewer.cacheIdentity) { gridEventsViewer.cache.clear(); gridCaptureFilters.records.clear(); }
        gridEventsViewer.cacheIdentity = identity;
        gridEventsViewer.cacheScope = scope;
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
        await applyGridCaptureFilters();
        setGridEventsStatus(gridEventsViewer.events.length + " eventos cargados; " + formatGridBytes(info.partitionBytes || 0) + " disponibles.");
    } catch (error) { gridEventsViewer.events = []; renderGridEventsList(); setGridEventsStatus(error.message || "No se pudo cargar el registro.", true); }
    finally { gridEventsViewer.loading = false; renderGridEventsList(); $('#gridEventsStatus').toggleClass('d-none', !$('#gridEventsStatus').hasClass('text-danger')); }
}

// Renderiza las capturas Grid recientes; sin parámetros.
function renderGridEventsList() {
    const list = $("#gridEventsList").empty();
    if (!gridEventsViewer.events.length) return list.append('<div class="text-muted small p-2">' + (gridEventsViewer.loading ? 'Cargando capturas...' : 'No hay eventos persistidos todavía.') + '</div>');
    const visible = gridCaptureVisibleEvents();
    if (!visible.length) list.append('<div class="text-muted small p-2">' + (gridEventsViewer.loading || gridCaptureFilters.checking ? 'Buscando capturas para estos filtros...' : 'Sin capturas para estos filtros.') + '</div>');
    visible.forEach((event) => {
        const when = formatGridTime(event.t);
        const saved = gridEventsViewer.cache.get(event.i), cached = saved && saved.timestamp === event.t ? saved : null, description = Object.prototype.hasOwnProperty.call(event, "k") ? gridPointName(event.k) + (event.l ? " · limitado" : "") : gridStateName(event.p) + " → " + gridStateName(event.s);
        const summary = gridCaptureFilters.records.get(event.i);
        const matches = summary?.timestamp === event.t ? summary.records.filter(record => gridCaptureFilters.phases.has(record.states.subject))
            .flatMap(record => gridCaptureRecordTypes(record, summary.startup).filter(type => gridCaptureFilters.types.has(type))
                .map(type => gridSubjectName(record.states.subject) + ' · ' + gridCaptureTypeNames[type])) : [];
        const descriptions = [...new Set(matches)];
        const details = descriptions.length ? descriptions.slice(0, 3).join('; ') + (descriptions.length > 3 ? ' · +' + (descriptions.length - 3) : '') : cached ? gridEventRecords(cached).length + " registros · " + buildGridOverview(cached).map(group => group.name).join(", ") : "Disparador: " + gridRecordSubject(event.y, event.c ? 4 : event.k) + " · " + description;
        list.append($("<button></button>").addClass("list-group-item list-group-item-action py-2").toggleClass("active", gridEventsViewer.selectedId === event.i).attr({type: "button", "aria-current": gridEventsViewer.selectedId === event.i ? "true" : "false"})
            .append($('<strong>').text(details), $('<div>').addClass('d-flex justify-content-between gap-1 small text-muted').append($('<span>').text('#' + event.i), $('<span>').text(when))).on("click", () => loadGridEvent(event)));
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
        if (identity !== gridEventsViewer.cacheIdentity) { gridEventsViewer.cache.clear(); gridCaptureFilters.records.clear(); }
        gridEventsViewer.cacheIdentity = identity;
        const decoded = await loadCachedFast(event.i, event.t, scope, controller.signal);
        if (request !== gridEventsViewer.request) return;
        if (!decoded.partial) {
            rememberGridCaptureRecords(decoded);
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
        preMs: time(68), postMs: time(70), pages: view.getUint8(72), windowMs: time(74),
        resetReason: (view.getUint16(76, true) & 0xff00) === 0xb000 ? view.getUint16(76, true) & 255 : null};
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

// Crea puntos visuales periodicos; samples conserva los originales y periodMs indica la cadencia nominal.
function gridRegularSamples(samples, periodMs) {
    let anchor = null, count = 0, previous = null;
    return samples.map(sample => {
        const valid = ["vr", "vs", "vt", "vn", "ir", "is", "it", "inn"].some(key => Number.isFinite(sample[key]));
        if (!valid) { anchor = null; previous = null; return {...sample}; }
        // Un hueco amplio o un separador inicia otro tramo; no comprimir datos ausentes.
        if (anchor === null || previous !== null && sample.relativeMs - previous > periodMs * 3) {
            anchor = sample.relativeMs; count = 0;
        }
        previous = sample.relativeMs;
        return {...sample, relativeMs: anchor + count++ * periodMs};
    });
}

// Prepara FAST nominal por transicion; event conserva muestras, referencias y frecuencia del contexto.
function gridFastDisplaySamples(event) {
    const frequency = event.snapshot?.frequency?.[0];
    const period = frequency >= 55 && frequency <= 70 ? 1000 / 120 : 10;
    const records = [{offsetMs: 0, states: event.states, previousDurationMs: event.previousDurationMs}, ...(event.transitions || [])];
    const times = new Set();
    for (const record of records) {
        times.add(record.offsetMs);
        if (record.states?.isPoint && record.previousDurationMs > 0)
            times.add(record.offsetMs + record.previousDurationMs);
    }
    const valid = event.samples.filter(sample => Number.isFinite(sample.relativeMs) &&
        ["vr", "vs", "vt", "vn", "ir", "is", "it", "inn"].some(key => Number.isFinite(sample[key])));
    const anchors = [...times].filter(Number.isFinite).sort((a, b) => a - b).map(time => {
        let closest = null;
        for (const sample of valid)
            if (!closest || Math.abs(sample.relativeMs - time) < Math.abs(closest.relativeMs - time)) closest = sample;
        return closest && Math.abs(closest.relativeMs - time) <= period * 1.5 ? {time, raw: closest.relativeMs} : null;
    }).filter(Boolean);
    if (!anchors.length) return gridRegularSamples(event.samples, period);
    const points = [], slots = new Map(), validSamples = new Set(valid);
    let previous = null;
    for (const sample of event.samples) {
        if (!validSamples.has(sample)) { previous = null; continue; }
        let anchor = anchors[0];
        for (const candidate of anchors)
            if (Math.abs(sample.relativeMs - candidate.raw) < Math.abs(sample.relativeMs - anchor.raw)) anchor = candidate;
        const slot = Math.round((sample.relativeMs - anchor.raw) / period);
        const at = anchor.time + slot * period, distance = Math.abs(sample.relativeMs - anchor.raw - slot * period);
        // Las marcas guardadas solo ubican la lectura en una casilla nominal; no estiran su intervalo.
        const key = anchor.time + ":" + slot;
        const saved = slots.get(key);
        if (saved) {
            if (distance < saved.distance) { saved.sample = {...sample, relativeMs: at}; saved.distance = distance; }
            continue;
        }
        // Cambiar la referencia de sincronismo no implica perder datos entre dos lecturas validas.
        const entry = {sample: {...sample, relativeMs: at}, distance,
            split: previous === null || sample.relativeMs - previous > period * 3};
        points.push(entry); slots.set(key, entry);
        previous = sample.relativeMs;
    }
    points.sort((a, b) => a.sample.relativeMs - b.sample.relativeMs);
    const result = [];
    for (const entry of points) {
        const last = result[result.length - 1];
        if (last && entry.split)
            result.push({relativeMs: (last.relativeMs + entry.sample.relativeMs) / 2});
        result.push(entry.sample);
    }
    return result;
}

// Presenta las gráficas Fast RMS y la secuencia de transiciones; event contiene la captura decodificada.
function renderGridEvent(event) {
    const subject = gridRecordSubject(event.states.subject, event.states.pointType), previous = gridStateName(event.states.previous, event.states.subject), current = gridStateName(event.states.current, event.states.subject), when = event.estimatedTime ? "Hora estimada" : formatGridTime(event.timestamp), voltage = event.states.subject === 4 ? "" : " " + formatGridVoltage(event.previousVoltage, 2) + " → " + formatGridVoltage(event.currentVoltage, 2) + " V.", duration = event.startup ? "Duración previa no disponible" : "Estado previo durante " + formatGridDuration(event.previousDurationMs);
    const startup = event.startup ? " Registro de arranque; no implica un corte previo" + (gridStartupReason(event) ? " (" + gridStartupReason(event) + ")" : "") + "." : "";
    const partial = event.partial ? " Captura incompleta: " + event.invalidPages + " página(s) con CRC o cabecera inválidos." : "";
    const timing = event.timingEstimated ? " La escala temporal incluye intervalos estimados." : "";
    const description = event.states.isPoint ? gridPointName(event.states.pointType) + ". Duración observada: " + formatGridDuration(event.previousDurationMs) + (event.states.limited ? " (captura limitada)." : ".") : previous + " → " + current + ". " + duration + "." + (event.states.isCurrent ? voltage.replace(" V.", " A.") : voltage);
    const dropped = event.timeStatus & 64 ? " Se agotó el espacio de marcas: faltan entradas." : "";
    const synthetic = (event.timeStatus & 128 ? " Prueba artificial." : "") + (gridPreHistoryMissing(event) ? " Historial FAST anterior al disparo insuficiente." : "");
    const displaySamples = gridFastDisplaySamples(event), first = displaySamples[0], last = displaySamples[displaySamples.length - 1];
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
    if (typeof appPhaseColor === 'function') for (const signal of voltageSeries) signal[2] = appPhaseColor(signal[1].slice(1));
    if (gridEventsViewer.voltageChart) gridEventsViewer.voltageChart.destroy();
    if (gridEventsViewer.currentChart) gridEventsViewer.currentChart.destroy();
    const currentSeries = [["R", "ir", "#d9534f"], ["S", "is", "#5cb85c"], ["T", "it", "#337ab7"], ["N", "inn", "#6c757d"]];
    gridEventsViewer.voltageChart = createGridChart("gridEventVoltageChart", voltageSeries, currentSeries, displaySamples);
    gridEventsViewer.currentChart = null;
    setGridChartNavigationMode(gridEventsViewer.panMode);
    refreshGridCharts();
}

// Describe el motivo de arranque codificado en event.timeStatus.
function gridStartupReason(event) {
    if (event.startup && event.settings?.resetReason != null) {
        const reasons = {0: "causa desconocida", 1: "encendido", 2: "reset externo", 3: "reinicio por software", 4: "fallo / panic",
            5: "watchdog de interrupciones", 6: "watchdog de tarea", 7: "watchdog", 8: "salida de reposo", 9: "brownout", 10: "reset SDIO"};
        return reasons[event.settings.resetReason] || "reset " + event.settings.resetReason;
    }
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
    selectGridWaveForTime(record.offsetMs);
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
        const range = records.length === 1 ? gridRecordFocusRange(records[0], samples[0].x, samples[samples.length - 1].x) : gridSelectionRange(records, samples[0].x, samples[samples.length - 1].x, gridEventsViewer.records);
        if (!range) continue;
        setGridChartRange(chart, range.minimum, range.maximum);
    }
    refreshGridMarkers();
    if (typeof syncGridWaveRange === "function") syncGridWaveRange();
}

// Centra una transicion en hasta 250 ms; record identifica el instante y minimum/maximum limitan la captura.
function gridRecordFocusRange(record, minimum, maximum) {
    const at = record.offsetMs;
    if (!Number.isFinite(at) || at < minimum || at > maximum || maximum <= minimum) return null;
    const width = Math.min(250, maximum - minimum), start = Math.max(minimum, Math.min(at - width / 2, maximum - width));
    return {minimum:start, maximum:start + width};
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

// Construye el gráfico FAST con dos ejes; elementId es el contenedor, voltageSeries y currentSeries definen señales y samples aporta lecturas.
function createGridChart(elementId, voltageSeries, currentSeries, samples) {
    // Convierte series y unidad a opciones CanvasJS conservando el eje y la unidad correspondientes.
    const mobileView = window.matchMedia("(max-width: 767.98px)").matches;
    const makeSeries = (series, unit) => series.map(([name, key, color]) => ({type: "line", name: (unit === "A" ? "I " : "V ") + name, signalUnit: unit, axisYType: unit === "A" ? "secondary" : "primary", appPhase:key === 'inn' ? 'N' : key.slice(1).toUpperCase(), appVariant:unit === 'A' ? .22 : 0,
        showInLegend: true, markerSize: 0, color: typeof appPhaseColor === 'function' ? appPhaseColor(key === 'inn' ? 'N' : key.slice(1),unit === 'A' ? .22 : 0) : color, connectNullData: false, dataPoints: samples.map((sample) => ({x: sample.relativeMs, y: sample[key] ?? null}))}));
    const data = [...makeSeries(voltageSeries, "V"), ...makeSeries(currentSeries, "A")];
    let chart;
    chart = new CanvasJS.Chart(elementId, {animationEnabled: false, zoomEnabled: true,
        toolTip: chartTooltip.options({title: "FAST RMS #" + gridEventsViewer.selectedId,
            markers: () => gridEventsViewer.records.map(record => ({at: record.offsetMs, priority: record.states.subject === 4 ? 0 : 1,
                label: gridRecordSubject(record.states.subject, record.states.pointType) + " · " + (record.states.isPoint ? gridPointName(record.states.pointType) :
                    gridStateName(record.states.previous, record.states.subject) + " → " + gridStateName(record.states.current, record.states.subject)),
                durationMs: record.states.isPoint ? record.previousDurationMs : 0}))}),
        axisX: {title: mobileView ? "Tiempo (ms)" : "Tiempo relativo (ms)", titleFontSize: mobileView ? 11 : 14, labelFontSize: mobileView ? 10 : 12, stripLines: gridEventsViewer.showMarkers && samples.length ? gridRecordMarkers(gridEventsViewer.records, null, samples[0].relativeMs, samples[samples.length - 1].relativeMs) : []}, axisY: {title: mobileView ? "" : "Voltaje RMS", titleFontSize: 12, labelFontSize: mobileView ? 10 : 12, suffix: " V", includeZero: false, gridDashType: "dot"}, axisY2: {title: mobileView ? "" : "Corriente RMS", titleFontSize: 12, labelFontSize: mobileView ? 10 : 12, suffix: " A", includeZero: false, gridThickness: 0}, legend: {fontSize: mobileView ? 10 : 12, cursor: "pointer", itemclick: toggleGridSeries}, rangeChanged: () => syncGridChartRange(chart), data: data});
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

// Muestra u oculta un grupo de señales; type acepta voltage o current y visible define su estado final.
function setGridChartVisibility(type, visible) {
    const isVoltage = type === "voltage", buttonId = isVoltage ? "#gridEventVoltageToggle" : "#gridEventCurrentToggle";
    gridEventsViewer[isVoltage ? "voltageVisible" : "currentVisible"] = visible;
    saveGridCaptureView();
    $(buttonId).toggleClass("active", visible).attr("aria-pressed", visible);
    const chart = gridEventsViewer.voltageChart;
    if (chart) {
        chart.options.data.filter(series => series.signalUnit === (isVoltage ? "V" : "A")).forEach(series => { series.visible = visible; });
        chart.render();
    }
    if (gridWaveViewer.chart) {
        gridWaveViewer.chart.options.data.filter(series => series.signalGroup === (isVoltage ? 'voltage' : 'current')).forEach(series => { series.visible = visible; });
        gridWaveViewer.chart.render();
    }
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
function setGridEventsStatus(message, isError) { $("#gridEventsStatus").text(message).toggleClass("text-danger", !!isError).toggleClass("text-muted", !isError).toggleClass('d-none', !isError && !gridEventsViewer.loading); }

// Amplia el area de graficas; expanded indica si ocupa la ventana.
function setGridEventExpanded(expanded) {
    $('#gridEventWorkspace').toggleClass('gridEventExpanded', expanded);
    if (expanded) $('#gridChartViewTools').prependTo('#gridEventWorkspace');
    else if (matchMedia('(min-width: 1100px)').matches) $('#gridChartViewTools').appendTo('#gridCaptureViewTools');
    else $('#gridChartViewTools').prependTo('#gridEventChartPanel');
    $('#gridEventExpand').attr('aria-pressed', String(expanded)).attr('title', expanded ? 'Salir de pantalla completa' : 'Ampliar gráficas');
    for (const chart of [gridEventsViewer.voltageChart, gridEventsViewer.currentChart, gridWaveViewer.chart]) if (chart) chart.render();
}

// Convierte bytes a una etiqueta compacta; value indica la capacidad disponible.
function formatGridBytes(value) { return value < 1048576 ? (value / 1024).toFixed(0) + " KiB" : (value / 1048576).toFixed(1) + " MiB"; }

// Solicita una captura artificial para comprobar el recorrido completo de la persistencia.
async function createGridEventTest() {
    try { const response = await fetch(board.IP + "/grid-events/test", {method: "POST", cache: "no-store"}); if (!response.ok) throw new Error(); setGridEventsStatus("Prueba solicitada; espere unos segundos y recargue."); }
    catch (_) { setGridEventsStatus("No se pudo solicitar la captura de prueba.", true); }
}

const gridCaptureTypeNames = {outage: 'Corte', low: 'Baja V', high: 'Alta V / N', dip: 'DIP', swell: 'Swell',
    pulse: 'Pico I', current: 'Alta I', abnormal: 'Red anormal', boot: 'Reinicio', normal: 'Normalidad'};
const gridCaptureFilters = {types: new Set(Object.keys(gridCaptureTypeNames)), phases: new Set([0, 1, 2, 3, 4]),
    records: new Map(), unit: 'custom', run: 0, controller: null};

// Clasifica record sin perder los retornos; startup identifica observaciones del arranque.
function gridCaptureRecordTypes(record, startup) {
    const state = record.states, types = [];
    if (startup && state.previous === 0 && !state.isPoint) types.push('boot');
    if (state.isPoint) types.push(({1:'dip', 2:'swell', 3:'pulse'})[state.pointType]);
    else if (state.isCurrent) types.push(state.current === 5 || state.previous === 5 ? 'current' : 'normal');
    else {
        const names = state.subject === 4 ? {1:'normal', 2:'abnormal', 3:'outage'} : {1:'normal', 2:'low', 3:'high', 4:'outage'};
        const abnormal = [state.previous, state.current].filter(value => value > 1);
        types.push(...(abnormal.length ? abnormal.map(value => names[value]) : ['normal']));
    }
    return types.filter(Boolean);
}

// Evalua tipo y fase sobre el mismo registro; capture conserva fecha, arranque y transiciones resumidas.
function gridCaptureMatches(capture, types, phases, from, to) {
    return capture.records.some(record => {
        const at = capture.timestamp + record.offsetMs;
        return at >= from && at <= to && phases.has(record.states.subject) &&
            gridCaptureRecordTypes(record, capture.startup).some(type => types.has(type));
    });
}

// Comprueba solo el disparador del indice; entry es la cabecera y from/to delimitan el periodo.
function gridCaptureIndexMatches(entry, from, to) {
    const subject = ['R','S','T','N','G'].indexOf(entry.y);
    if (subject < 0 || !Number.isFinite(entry.t) || entry.t < from || entry.t > to) return false;
    const names = subject === 4 ? {'?':0,N:1,A:2,O:3} : {'?':0,N:1,L:2,H:3,X:4,C:5};
    const states = {subject, previous:names[entry.p] ?? 0, current:names[entry.s] ?? 0,
        isPoint:Object.hasOwn(entry,'k'), pointType:entry.k || 0, isCurrent:!!entry.c};
    return gridCaptureFilters.phases.has(subject) && gridCaptureRecordTypes({states}, false).some(type => gridCaptureFilters.types.has(type));
}

// Lee limites inclusivos de los controles; sin parametros, rechaza fechas invalidas.
function gridCaptureRange() {
    const fromText = $('#gridCaptureFrom').val(), toText = $('#gridCaptureTo').val();
    const from = fromText ? Date.parse(fromText) : -Infinity, to = toText ? Date.parse(toText) : Infinity;
    if (Number.isNaN(from) || Number.isNaN(to) || from >= to) throw Error('Desde debe ser anterior a Hasta.');
    return {from, to};
}

// Devuelve las capturas que cumplen los controles actuales; sin parametros ni consultas al ESP.
function gridCaptureVisibleEvents() {
    let range;
    try { range = gridCaptureRange(); } catch (_) { return []; }
    const all = gridCaptureFilters.types.size === Object.keys(gridCaptureTypeNames).length && gridCaptureFilters.phases.size === 5;
    return gridEventsViewer.events.filter(event => {
        const saved = gridCaptureFilters.records.get(event.i);
        if (saved && saved.timestamp === event.t) return gridCaptureMatches(saved, gridCaptureFilters.types, gridCaptureFilters.phases, range.from, range.to);
        return all && event.t >= range.from && event.t <= range.to || gridCaptureIndexMatches(event, range.from, range.to);
    });
}

// Conserva solo registros pequenos; event es una captura ya decodificada con su CRC.
function rememberGridCaptureRecords(event) {
    gridCaptureFilters.records.set(event.id, {timestamp:event.timestamp, startup:event.startup,
        records:gridEventRecords(event).map(record => ({offsetMs:record.offsetMs, states:{...record.states}}))});
}

// Filtra progresivamente reutilizando cache y leyendo faltantes de a uno; sin parametros.
async function applyGridCaptureFilters() {
    if (gridCaptureFilters.active === false) return;
    const token = ++gridCaptureFilters.run;
    gridCaptureFilters.controller?.abort();
    const controller = gridCaptureFilters.controller = new AbortController();
    gridCaptureFilters.checking = true;
    try {
        const range = gridCaptureRange(), all = gridCaptureFilters.types.size === Object.keys(gridCaptureTypeNames).length && gridCaptureFilters.phases.size === 5;
        renderGridEventsList();
        const candidates = gridEventsViewer.events.filter(event => event.t >= range.from - 32768 && event.t <= range.to + 32768);
        let checked = 0, failed = 0, lastRender = 0;
        for (const entry of candidates) {
            if (token !== gridCaptureFilters.run) return;
            const saved = gridCaptureFilters.records.get(entry.i);
            if ((!all || entry.t < range.from || entry.t > range.to) && (!saved || saved.timestamp !== entry.t)) {
                $('#gridCaptureFilterStatus').text('Revisando registros ' + (++checked) + ' / ' + candidates.length + '...');
                try {
                    const event = await loadCachedFast(entry.i, entry.t, gridEventsViewer.cacheScope, controller.signal);
                    if (controller.signal.aborted) return;
                    if (event.id !== entry.i || event.partial) throw Error('Captura incompleta');
                    rememberGridCaptureRecords(event);
                } catch (error) { if (controller.signal.aborted) return; ++failed; }
                if (performance.now() - lastRender > 250) { renderGridEventsList(); lastRender = performance.now(); }
            }
        }
        if (token === gridCaptureFilters.run) { gridCaptureFilters.checking = false; renderGridEventsList(); $('#gridCaptureFilterStatus').text(gridCaptureVisibleEvents().length + ' de ' + gridEventsViewer.events.length + ' capturas' + (failed ? ' · ' + failed + ' sin verificar; actualice para reintentar' : '')); }
    } catch (error) { if (token === gridCaptureFilters.run) { gridCaptureFilters.checking = false; $('#gridCaptureFilterStatus').text(error.message); renderGridEventsList(); } }
}

// Persiste los controles del visor por navegador; sin parametros, no guarda muestras.
function saveGridCaptureFilters() {
    try { localStorage.setItem('powerMeter.captureFilters', JSON.stringify({types:[...gridCaptureFilters.types], phases:[...gridCaptureFilters.phases],
        preset:$('#gridCapturePreset').val(), from:$('#gridCaptureFrom').val(), to:$('#gridCaptureTo').val(), unit:gridCaptureFilters.unit})); } catch (_) {}
}

// Aplica preset a los campos sin descargar; now permite fijar el instante de referencia.
function setGridCapturePreset(preset, now = new Date()) {
    if (!preset) return;
    if (preset === 'all') { $('#gridCaptureFrom, #gridCaptureTo').val(''); gridCaptureFilters.unit = 'custom'; return; }
    let range;
    if (preset.startsWith('last-')) range = {from:now.getTime() - Number(preset.slice(5)) * 1000, to:now.getTime(), unit:'custom'};
    else if (preset === 'lastMonthRolling') {
        const from = new Date(now), day = from.getDate();
        from.setDate(1); from.setMonth(from.getMonth() - 1); from.setDate(Math.min(day, new Date(from.getFullYear(), from.getMonth() + 1, 0).getDate()));
        range = {from:from.getTime(), to:now.getTime(), unit:'custom'};
    } else {
        const date = new Date(now);
        if (preset === 'dayBeforeYesterday') date.setDate(date.getDate() - 2);
        range = eventHistoryCalendarRange(preset === 'dayBeforeYesterday' ? 'today' : preset, date);
    }
    gridCaptureFilters.unit = range.unit;
    $('#gridCaptureFrom').val(formatHistoryDateInput(new Date(range.from)));
    $('#gridCaptureTo').val(formatHistoryDateInput(new Date(range.to)));
}

// Sincroniza botones y programa el filtrado; sin parametros, evita consultas por cada pulsacion rapida.
function updateGridCaptureFilters() {
    for (const [group, selected, count] of [['Types',gridCaptureFilters.types,Object.keys(gridCaptureTypeNames).length], ['Phases',gridCaptureFilters.phases,5]])
        $('#gridCapture' + group + ' button').each(function () {
            const key = this.dataset.captureKey;
            $(this).attr('aria-pressed', String(key === 'all' ? selected.size === count : selected.has(group === 'Phases' ? Number(key) : key)));
        });
    saveGridCaptureFilters();
    $('#gridCaptureFilterStatus').text('Filtrando...');
    gridCaptureFilters.controller?.abort();
    clearTimeout(gridCaptureFilters.timer);
    gridCaptureFilters.timer = setTimeout(applyGridCaptureFilters, 180);
}

// Crea filtros compactos con los periodos del Historico; sin parametros.
function initGridCaptureFilters() {
    $('#gridCapturePreset').append('<option value="all">Todo el registro</option><option value="">Personalizado</option>', $('#historyPreset').children().clone()).val('all');
    for (const [group, labels, selected] of [['Types',gridCaptureTypeNames,gridCaptureFilters.types], ['Phases',{'4':'Red','0':'R','1':'S','2':'T','3':'N'},gridCaptureFilters.phases]]) {
        for (const [key,label] of [['all','Todas'], ...Object.entries(labels)])
            $('<button type="button" class="btn btn-outline-secondary">').text(label).attr({'data-capture-key':key, 'aria-pressed':'true'})
                .appendTo('#gridCapture' + group).on('click', () => {
                    const value = group === 'Phases' ? Number(key) : key;
                    if (key === 'all') {
                        const enabled = selected.size === Object.keys(labels).length;
                        selected.clear(); if (!enabled) for (const name of Object.keys(labels)) selected.add(group === 'Phases' ? Number(name) : name);
                    }
                    else if (selected.has(value)) selected.delete(value); else selected.add(value);
                    updateGridCaptureFilters();
                });
    }
    try {
        const saved = JSON.parse(localStorage.getItem('powerMeter.captureFilters'));
        if (saved) {
            for (const [key,known] of [['types',Object.keys(gridCaptureTypeNames)], ['phases',[0,1,2,3,4]]]) if (Array.isArray(saved[key])) {
                gridCaptureFilters[key].clear(); for (const value of saved[key].filter(value => known.includes(value))) gridCaptureFilters[key].add(value);
            }
            if ($('#gridCapturePreset option').toArray().some(option => option.value === saved.preset)) {
                $('#gridCapturePreset').val(saved.preset);
                if (saved.preset) setGridCapturePreset(saved.preset);
                else if (Number.isFinite(Date.parse(saved.from)) && Date.parse(saved.from) < Date.parse(saved.to)) {
                    $('#gridCaptureFrom').val(saved.from); $('#gridCaptureTo').val(saved.to);
                    gridCaptureFilters.unit = ['day','week','month','year','custom'].includes(saved.unit) ? saved.unit : 'custom';
                }
            }
        }
    } catch (_) {}
    $('#gridCapturePreset').on('change', function () { setGridCapturePreset(this.value); updateGridCaptureFilters(); });
    $('#gridCaptureFrom, #gridCaptureTo').on('change', () => { $('#gridCapturePreset').val(''); gridCaptureFilters.unit = 'custom'; updateGridCaptureFilters(); });
    $('[data-capture-move]').on('click', function () {
        try {
            const range = {...gridCaptureRange(), unit:gridCaptureFilters.unit}, direction = Number(this.dataset.captureMove);
            if (!Number.isFinite(range.from) || !Number.isFinite(range.to)) return;
            const moved = direction ? shiftEventHistoryPeriod(range, direction) : range.unit === 'custom' ?
                {...range, to:Date.now(), from:Date.now() - (range.to - range.from)} : eventHistoryCalendarRange(({day:'today',week:'thisWeek',month:'thisMonth',year:'thisYear'})[range.unit]);
            $('#gridCaptureFrom').val(formatHistoryDateInput(new Date(moved.from))); $('#gridCaptureTo').val(formatHistoryDateInput(new Date(moved.to)));
            $('#gridCapturePreset').val(!direction && range.unit !== 'custom' ? ({day:'today',week:'thisWeek',month:'thisMonth',year:'thisYear'})[range.unit] : ''); updateGridCaptureFilters();
        } catch (error) { $('#gridCaptureFilterStatus').text(error.message); }
    });
    updateGridCaptureFilters();
}

// Cancela el analisis al salir de Eventos; active indica si la vista esta visible.
function setGridCaptureFiltersActive(active) {
    gridCaptureFilters.active = active;
    if (!active) { ++gridCaptureFilters.run; clearTimeout(gridCaptureFilters.timer); gridCaptureFilters.controller?.abort(); }
}

if (typeof module !== 'undefined') module.exports = {gridCaptureRecordTypes, gridCaptureMatches};
