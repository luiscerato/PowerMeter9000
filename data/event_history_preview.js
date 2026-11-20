const eventHistoryPreview = {controller: null, token: 0, event: null, item: null, edge: "start", charts: [], waves: [],
    events: [], fastSegments: [], waveSegments: [], markers: [], range: null, loading: false, pan: false, syncing: false,
    sections: [], viewMode: "auto", sectionAt: 0, signals: new Set()};

// Describe un tramo para hover y foco; item contiene entrada, limites y capturas fuente.
function eventHistoryTooltip(item) {
    return eventHistoryLaneName(item.lane) + " · " + eventHistoryName(item) + (item.startKnown ? "\nInicio: " : "\nObservado desde (inicio desconocido): ") + formatEventHistoryTime(item.start) +
        "\nFinal: " + (item.endKnown ? formatEventHistoryTime(item.end) : item.uncertain ? "Sin cierre verificable" : "Ultimo estado hasta " + formatEventHistoryTime(item.end)) +
        "\nDuracion: " + (item.startKnown && item.endKnown || item.sequence && !item.uncertain ?
            formatGridDuration(item.end - item.start) + (item.endKnown ? "" : " hasta limite consultado") : "Tramo parcial") +
        "\nCapturas: " + item.ids.map(id => "#" + id).join(", ") + "\nClic para ver informacion y graficas";
}

// Libera tooltips antes de reconstruir sus botones; no recibe parametros.
function disposeEventHistoryTooltips() {
    document.querySelectorAll("#eventHistory [data-history-tooltip]").forEach(element => bootstrap.Tooltip.getInstance(element)?.dispose());
    document.querySelectorAll("#eventHistory [data-bs-toggle='dropdown']").forEach(element => bootstrap.Dropdown.getInstance(element)?.dispose());
}

// Agrega tooltips accesibles con contenido multilínea; no recibe parametros.
function initEventHistoryTooltips() {
    document.querySelectorAll("#eventHistory [data-history-tooltip]").forEach(element => {
        new bootstrap.Tooltip(element, {container: "body", customClass: "eventHistoryTooltip", delay: {show: 250, hide: 50}});
    });
}

// Selecciona la captura mas cercana al borde pedido; item es el tramo, captures sus resumenes y edge inicio/final.
function eventHistoryPreviewSource(item, captures, edge) {
    const at = edge === "end" ? item.end : item.start;
    let best = item.ids[0], distance = Infinity;
    for (const capture of captures.filter(capture => item.ids.includes(capture.id))) {
        for (const record of capture.records) {
            if (record.states.subject !== item.subject) continue;
            const current = !!(record.states.isCurrent || record.states.isPoint && record.states.pointType === 3);
            if (current !== !!item.current) continue;
            const delta = Math.abs(capture.timestamp + record.offsetMs - at);
            if (delta < distance) { distance = delta; best = capture.id; }
        }
    }
    return best;
}

// Inicializa la ventana independiente del visor original; no recibe parametros.
function initEventHistoryPreview() {
    const modal = document.getElementById("eventHistoryPreview");
    document.body.appendChild(modal);
    $(modal).on("shown.bs.modal", () => loadEventHistoryPreviewCapture());
    $(modal).on("hide.bs.modal", () => { cancelEventHistoryPreview(); setEventHistoryPreviewFullscreen(false); });
    $("#eventHistoryPreviewExpand").on("click", () => setEventHistoryPreviewFullscreen(!modal.querySelector(".modal-dialog").classList.contains("modal-fullscreen")));
    modal.addEventListener("keydown", event => {
        if (event.key === "Escape" && modal.querySelector(".modal-fullscreen")) {
            event.preventDefault(); event.stopImmediatePropagation(); setEventHistoryPreviewFullscreen(false);
        }
    }, true);
    $("#eventHistoryPreviewCapture").on("change", () => {
        eventHistoryPreview.event = eventHistoryPreview.events.find(event => event.id === Number($("#eventHistoryPreviewCapture").val())) || null;
        $("#eventHistoryPreviewDetails").prop("disabled", !eventHistoryPreview.event);
    });
    $("#eventHistoryPreviewFull").on("click", () => {
        eventHistoryPreview.viewMode = "all"; setEventHistoryPreviewRange(null); renderEventHistoryPreviewSections();
    });
    $("#eventHistoryPreviewPan").on("change", function () {
        eventHistoryPreview.pan = this.checked;
        for (const chart of eventHistoryPreview.charts) {
            chart.options.zoomEnabled = !this.checked; chart.options.panEnabled = this.checked; chart.render();
        }
    });
    $("#eventHistoryPreviewLink").on("change", function () {
        if (this.checked) setEventHistoryPreviewRange(eventHistoryPreview.range);
    });
    initEventHistorySignalControls();
    $("#eventHistoryPreviewDetails").on("click", () => {
        const event = eventHistoryPreview.event;
        if (!event) return;
        closeEventHistoryPreview(); showAppPage("gridEvents"); loadGridEvent({i: event.id, t: event.timestamp});
    });
}

// Destruye graficas y cancela respuestas pendientes al cerrar/cambiar; no recibe parametros.
function cancelEventHistoryPreview() {
    ++eventHistoryPreview.token;
    eventHistoryPreview.controller?.abort(); eventHistoryPreview.controller = null;
    for (const chart of eventHistoryPreview.charts) chart.destroy();
    eventHistoryPreview.charts = []; eventHistoryPreview.event = null; eventHistoryPreview.waves = [];
    eventHistoryPreview.events = []; eventHistoryPreview.fastSegments = []; eventHistoryPreview.waveSegments = [];
    eventHistoryPreview.markers = []; eventHistoryPreview.range = null; eventHistoryPreview.loading = false;
    eventHistoryPreview.sections = []; eventHistoryPreview.viewMode = "auto";
    $("#eventHistoryPreviewSections, #eventHistoryPreviewSectionStatus").empty();
    $("#eventHistoryPreviewRecords").empty();
    $("#eventHistoryPreviewWarnings").empty();
    $("#eventHistoryPreviewDetails").prop("disabled", true);
}

// Cierra una vista previa si esta abierta; no recibe parametros.
function closeEventHistoryPreview() {
    const modal = document.getElementById("eventHistoryPreview");
    if (modal) bootstrap.Modal.getInstance(modal)?.hide();
    cancelEventHistoryPreview();
}

// Abre un tramo; key lo identifica, edge elige el borde y source permite inspeccionar un reinicio independiente.
function openEventHistoryPreview(key, edge = "start", source = null) {
    const item = source || eventHistory.result?.intervals.find(item => item.key === key);
    if (!item) return;
    cancelEventHistoryPreview(); disposeEventHistoryTooltips();
    eventHistory.selected = key; eventHistoryPreview.item = item; eventHistoryPreview.edge = edge;
    eventHistoryPreview.signals = new Set(eventHistorySignalDefinitions().filter(signal =>
        signal[3] === (item.current ? "current" : "voltage") && (item.subject < 4 ? signal[4] === item.subject : signal[4] < 3)).map(signal => signal[1]));
    updateEventHistorySignals();
    $("#eventHistoryPreviewTitle").text(eventHistoryLaneName(item.lane) + " · " + eventHistoryName(item));
    $("#eventHistoryPreviewInfo").text(eventHistoryTooltip(item).split("\n").slice(1, 4).join(" · "));
    const select = $("#eventHistoryPreviewCapture").empty();
    for (const id of item.ids) select.append($("<option>").val(id).text("Captura #" + id));
    select.val(eventHistoryPreviewSource(item, eventHistory.captures, edge));
    $("#eventHistoryPreviewFastStatus").text("Preparando vista previa...");
    $("#eventHistoryPreviewWaveStatus").text("");
    const modal = document.getElementById("eventHistoryPreview");
    if (modal.classList.contains("show")) loadEventHistoryPreviewCapture();
    else bootstrap.Modal.getOrCreateInstance(modal).show();
}

// Define nombre/clave/color/magnitud/fase de las senales; wave excluye el voltaje N que no se captura.
function eventHistorySignalDefinitions(wave = false) {
    return [["V R", "vr", "#d9534f", "voltage", 0], ["V S", "vs", "#278447", "voltage", 1],
        ["V T", "vt", "#337ab7", "voltage", 2], ["V N", "vn", "#c94d83", "voltage", 3],
        ["I R", "ir", "#d9534f", "current", 0], ["I S", "is", "#278447", "current", 1],
        ["I T", "it", "#337ab7", "current", 2], ["I N", "inn", "#6c757d", "current", 3]]
        .filter(signal => !wave || signal[1] !== "vn")
        .map(signal => typeof appPhaseColor === 'function' ? [signal[0],signal[1],appPhaseColor(['R','S','T','N'][signal[4]],signal[3] === 'current' ? .22 : 0),signal[3],signal[4]] : signal);
}

// Cambia senales: selection es el conjunto actual, keys las afectadas y replace selecciona solo esas claves.
function toggleEventHistorySignals(selection, keys, replace = false) {
    if (replace) return new Set(keys);
    const next = new Set(selection), enabled = !keys.every(key => next.has(key));
    for (const key of keys) { if (enabled) next.add(key); else next.delete(key); }
    return next;
}

// Crea la barra compacta por fases y magnitudes; no recibe parametros.
function initEventHistorySignalControls() {
    const definitions = eventHistorySignalDefinitions(), host = $("#eventHistoryPreviewSignals").empty();
    for (const [label, kind] of [["Fases", "phase"], ["Voltaje", "voltage"], ["Corriente", "current"]]) {
        const group = $("<div class='btn-group btn-group-sm' role='group'>").attr("aria-label", label);
        if (kind === "phase") group.append($("<span class='eventHistorySignalLabel'>").text(label));
        else group.append($("<button type='button' class='btn btn-outline-secondary'>").text(kind === "voltage" ? "V" : "I")
            .attr({"data-signals": definitions.filter(signal => signal[3] === kind).map(signal => signal[1]).join(","), "data-select-only": "true", "aria-label": "Todas las senales de " + label.toLowerCase()}));
        for (const [phase, index] of ["R", "S", "T", "N"].map((phase, index) => [phase, index])) {
            const keys = definitions.filter(signal => signal[4] === index && (kind === "phase" || signal[3] === kind)).map(signal => signal[1]);
            group.append($("<button type='button' class='btn btn-outline-secondary'>").text(phase)
                .attr({"data-signals": keys.join(","), "data-select-only": String(kind === "phase"), "aria-label": label + " " + phase}));
        }
        host.append(group);
    }
    host.on("click", "button[data-signals]", function () {
        eventHistoryPreview.signals = toggleEventHistorySignals(eventHistoryPreview.signals, this.dataset.signals.split(","), this.dataset.selectOnly === "true");
        updateEventHistorySignals();
    });
}

// Sincroniza botones, leyendas y ejes con la seleccion, sin cambiar muestras ni zoom; no recibe parametros.
function updateEventHistorySignals() {
    const selection = eventHistoryPreview.signals;
    $("#eventHistoryPreviewSignals button[data-signals]").each(function () {
        const keys = this.dataset.signals.split(","), count = keys.filter(key => selection.has(key)).length;
        const shortcut = this.dataset.selectOnly === "true", active = count === keys.length && (!shortcut || selection.size === keys.length);
        $(this).toggleClass("active", active).toggleClass("signalPartial", count > 0 && !active)
            .attr({"aria-pressed": active ? "true" : count ? "mixed" : "false", title: (shortcut ? "Mostrar solo esta seleccion. " : "") + count + " de " + keys.length + " senales visibles"});
    });
    $("#eventHistoryPreviewSignalStatus").text(!selection.size ? "Sin senales seleccionadas" :
        selection.has("vn") ? "Voltaje N (neutro-tierra): solo disponible en FAST." : "");
    for (const chart of eventHistoryPreview.charts) {
        const range = $("#eventHistoryPreviewLink").prop("checked") ? eventHistoryPreview.range : null;
        const minimum = range?.[0] ?? chart.axisX?.[0]?.get("viewportMinimum"), maximum = range?.[1] ?? chart.axisX?.[0]?.get("viewportMaximum");
        if (Number.isFinite(minimum) && Number.isFinite(maximum)) {
            chart.options.axisX.viewportMinimum = minimum; chart.options.axisX.viewportMaximum = maximum;
        }
        const data = chart.options.data, voltage = data.some(series => series.signalKind === "voltage" && selection.has(series.signalKey));
        const current = data.some(series => series.signalKind === "current" && selection.has(series.signalKey));
        chart.options.legend.maxWidth = Math.max(120, document.getElementById(chart.options.signalTarget).clientWidth - 100);
        chart.options.axisY.title = voltage ? "Tension (V)" : current ? "Corriente (A)" : "";
        chart.options.axisY2 = {title: "Corriente (A)"};
        for (const series of data) {
            series.visible = selection.has(series.signalKey);
            series.axisYType = series.signalKind === "current" && voltage ? "secondary" : "primary";
        }
        chart.render();
    }
}

// Amplia la ventana sin cambiar datos ni zoom; expanded indica si ocupa toda el area disponible.
function setEventHistoryPreviewFullscreen(expanded) {
    const modal = document.getElementById("eventHistoryPreview"), dialog = modal.querySelector(".modal-dialog");
    dialog.classList.toggle("modal-fullscreen", expanded);
    const label = expanded ? "Restaurar ventana" : "Pantalla completa";
    $("#eventHistoryPreviewExpand").attr({"aria-pressed": String(expanded), "aria-label": label, title: label}).html(expanded ? "&#x2922;" : "&#x26F6;");
    requestAnimationFrame(updateEventHistorySignals);
}

// Agrupa ventanas de muestras; segments usa timestamp/samples, origin es Unix ms y maxGap separa grupos en ms.
function groupEventHistoryPreviewSections(segments, origin, maxGap = 15000) {
    const windows = segments.map(segment => {
        const times = segment.samples.filter(sample => Number.isFinite(sample.relativeMs) &&
            ["vr", "vs", "vt", "vn", "ir", "is", "it", "inn"].some(key => Number.isFinite(sample[key])))
            .map(sample => segment.timestamp - origin + sample.relativeMs);
        return times.length ? {start: Math.min(...times), end: Math.max(...times)} : null;
    }).filter(Boolean).sort((a, b) => a.start - b.start);
    const sections = [];
    for (const window of windows) {
        const previous = sections[sections.length - 1];
        if (previous && window.start - previous.end <= maxGap) previous.end = Math.max(previous.end, window.end);
        else sections.push({...window});
    }
    return sections;
}

// Actualiza botones por grupos y seleccion automatica; no recibe parametros ni descarga informacion.
function renderEventHistoryPreviewSections() {
    const state = eventHistoryPreview;
    state.sections = groupEventHistoryPreviewSections([...state.fastSegments, ...state.waveSegments], state.item.start);
    const target = state.viewMode === "section" ? state.sectionAt : state.edge === "end" && state.item.endKnown ? state.item.end - state.item.start : 0;
    let selected = -1, distance = Infinity;
    state.sections.forEach((section, index) => {
        const delta = Math.max(section.start - target, target - section.end, 0);
        if (delta < distance) { distance = delta; selected = index; }
    });
    if (selected >= 0 && (state.viewMode === "auto" || state.viewMode === "section")) {
        const section = state.sections[selected]; state.range = [section.start - 50, section.end + 50];
    }
    const buttons = $("#eventHistoryPreviewSections").empty();
    state.sections.forEach((section, index) => {
        const label = state.sections.length === 1 ? "Tramo 1" : index === 0 ? "Inicio" : index === state.sections.length - 1 ? "Fin" : "Tramo " + (index + 1);
        const active = (state.viewMode === "auto" || state.viewMode === "section") && index === selected;
        buttons.append($("<button type='button' class='btn btn-sm btn-outline-secondary'>").text(label)
            .toggleClass("active", active).attr({"aria-pressed": String(active), title: (section.start / 1000).toFixed(3) + " a " + (section.end / 1000).toFixed(3) + " s"})
            .on("click", () => {
                state.viewMode = "section"; state.sectionAt = (section.start + section.end) / 2;
                setEventHistoryPreviewRange([section.start - 50, section.end + 50]); renderEventHistoryPreviewSections();
            }));
    });
    $("#eventHistoryPreviewFull").toggleClass("active", state.viewMode === "all").attr("aria-pressed", String(state.viewMode === "all"));
    const count = state.sections.length;
    $("#eventHistoryPreviewSectionStatus").text(!count ? "" : state.viewMode === "all" ? "Todos los tramos · " + count :
        state.viewMode === "custom" ? "Zoom personalizado · " + count + " tramos disponibles" : "Tramo " + (selected + 1) + " de " + count);
}

// Une segmentos timestamp/samples respecto de origin en ms; maxGap corta huecos sin interpolar ni duplicar muestras.
function mergeEventHistorySamples(segments, origin, maxGap) {
    const points = new Map();
    for (const segment of segments) for (const sample of segment.samples) {
        const at = Math.round((segment.timestamp - origin + sample.relativeMs) * 1000);
        if (!Number.isFinite(at)) continue;
        const previous = points.get(at), point = previous || {relativeMs: at / 1000};
        for (const key of ["vr", "vs", "vt", "vn", "ir", "is", "it", "inn"])
            if (!previous || Number.isFinite(sample[key])) point[key] = Number.isFinite(sample[key]) ? sample[key] : null;
        points.set(at, point);
    }
    const samples = [], ordered = [...points.values()].sort((a, b) => a.relativeMs - b.relativeMs);
    let previous = null;
    for (const point of ordered) {
        if (previous && point.relativeMs - previous.relativeMs > maxGap)
            samples.push({relativeMs: (previous.relativeMs + point.relativeMs) / 2});
        samples.push(point); previous = point;
    }
    return samples;
}

// Reune registros de capturas decodificadas; events conserva tiempos Unix y origin es el inicio del tramo en ms.
function collectEventHistoryMarkers(events, origin) {
    const markers = new Map();
    for (const event of events) for (const record of gridEventRecords(event)) {
        const state = record.states, at = event.timestamp - origin + record.offsetMs;
        const key = [Math.round(at * 10), state.subject, state.isCurrent, state.isPoint, state.pointType, state.previous, state.current].join(":");
        if (!markers.has(key)) markers.set(key, {at, state, durationMs: state.isPoint ? record.previousDurationMs : 0});
    }
    return [...markers.values()].sort((a, b) => a.at - b.at);
}

// Describe una marca decodificada; marker contiene sujeto, estados y tipo puntual.
function eventHistoryMarkerName(marker) {
    const state = marker.state;
    return gridRecordSubject(state.subject, state.pointType) + " · " + (state.isPoint ? gridPointName(state.pointType) :
        gridStateName(state.previous, state.subject) + " → " + gridStateName(state.current, state.subject));
}

// Calcula el rango completo de muestras y bordes conocidos; no recibe parametros.
function eventHistoryPreviewBounds() {
    const state = eventHistoryPreview, ranges = [...state.fastSegments, ...state.waveSegments].filter(segment => segment.samples.length);
    let first = -50, last = state.item.endKnown ? state.item.end - state.item.start + 50 : 200;
    for (const segment of ranges) {
        first = Math.min(first, segment.timestamp - state.item.start + segment.samples[0].relativeMs);
        last = Math.max(last, segment.timestamp - state.item.start + segment.samples[segment.samples.length - 1].relativeMs);
    }
    return [first, Math.max(first + 1, last)];
}

// Aplica zoom a ambos graficos; range contiene limites en ms o null para ver todo, source evita realimentacion.
function setEventHistoryPreviewRange(range, source = null) {
    const state = eventHistoryPreview;
    if (state.syncing) return;
    state.range = range;
    const limits = range || eventHistoryPreviewBounds();
    state.syncing = true;
    try {
        for (const chart of state.charts) {
            chart.options.axisX.viewportMinimum = limits[0]; chart.options.axisX.viewportMaximum = limits[1];
            if (chart !== source || !range) chart.render();
        }
    } finally { state.syncing = false; }
}

// Dibuja todas las capturas cargadas y sus marcas en el mismo tiempo; no recibe parametros.
function renderEventHistoryPreview() {
    const state = eventHistoryPreview, origin = state.item.start;
    renderEventHistoryPreviewSections();
    const limits = state.range || eventHistoryPreviewBounds();
    for (const chart of state.charts) chart.destroy();
    state.charts = [];
    state.markers = collectEventHistoryMarkers(state.events, origin);
    const records = $("#eventHistoryPreviewRecords").empty();
    for (const marker of state.markers) records.append($("<button type='button' class='btn btn-sm btn-outline-secondary'>")
        .text((marker.at / 1000).toFixed(3) + " s · " + eventHistoryMarkerName(marker))
        .on("click", () => {
            state.viewMode = "custom";
            setEventHistoryPreviewRange([marker.at - 50, marker.at + Math.max(0, marker.durationMs) + 50]); renderEventHistoryPreviewSections();
        }));
    const definitions = [[state.fastSegments, "eventHistoryPreviewFast", 30, false], [state.waveSegments, "eventHistoryPreviewWave", 0.25, true]];
    for (const [segments, target, gap, wave] of definitions) {
        if (!segments.length) continue;
        const samples = mergeEventHistorySamples(segments, origin, gap);
        const stripLines = [{value: 0, label: "Inicio", color: "#555", labelFontSize: 10}];
        if (state.item.endKnown) stripLines.push({value: state.item.end - origin, label: "Retorno", color: "#278447", labelFontSize: 10});
        for (const marker of state.markers) stripLines.push({value: marker.at, color: "#999", lineDashType: "dash", thickness: 1});
        const chart = new CanvasJS.Chart(target, {signalTarget: target, animationEnabled: false, zoomEnabled: !state.pan, panEnabled: state.pan,
            axisX: {title: "Tiempo desde el inicio del evento (ms)", viewportMinimum: limits[0], viewportMaximum: limits[1], stripLines},
            axisY: {title: state.item.current ? "Corriente (A)" : "Tension (V)"},
            toolTip: chartTooltip.options({title: wave ? "Oscilografía" : "FAST RMS", digits: wave ? 3 : 2, origin,
                markers: () => state.markers.map(marker => ({...marker, label: eventHistoryMarkerName(marker), priority: marker.state.subject === 4 ? 0 : 1}))}),
            legend: {fontSize: 10, verticalAlign: "top", horizontalAlign: "left", cursor: "pointer", itemclick: event => {
                state.signals = toggleEventHistorySignals(state.signals, [event.dataSeries.signalKey]); updateEventHistorySignals();
            }}, rangeChanged: event => {
                if (state.syncing) return;
                const axis = event.axisX?.[0], range = event.trigger === "reset" ? null :
                    axis && [axis.viewportMinimum, axis.viewportMaximum];
                if (range && !range.every(Number.isFinite)) return;
                state.range = range;
                state.viewMode = range ? "custom" : "all";
                renderEventHistoryPreviewSections();
                if ($("#eventHistoryPreviewLink").prop("checked")) setEventHistoryPreviewRange(range, chart);
            }, data: eventHistorySignalDefinitions(wave).map(([name, key, color, kind]) => ({type: "line", name, color,
                signalKey: key, signalKind: kind, signalUnit: kind === "current" ? "A" : "V",
                shortName: (kind === "current" ? "I " : "V ") + (key === "inn" ? "N" : key.slice(1).toUpperCase()),
                visible: state.signals.has(key), lineDashType: kind === "current" ? "dash" : "solid",
                showInLegend: true, markerSize: 0, connectNullData: false,
                dataPoints: samples.map(point => ({x: point.relativeMs, y: point[key] ?? null}))}))});
        state.charts.push(chart);
    }
    updateEventHistorySignals();
}

// Carga todos los FAST y Wave del tramo secuencialmente mediante cache; no recibe parametros.
async function loadEventHistoryPreviewCapture() {
    cancelEventHistoryPreview();
    const state = eventHistoryPreview, controller = state.controller = new AbortController(), token = state.token;
    const selected = Number($("#eventHistoryPreviewCapture").val()), failures = [], seenWaves = new Set();
    const ids = [...state.item.ids].sort((a, b) => a - b);
    state.loading = true;
    $("#eventHistoryPreviewFastStatus").text("Cargando capturas FAST...");
    $("#eventHistoryPreviewWaveStatus").text("Consultando oscilografias...");
    try {
        const scope = await refreshCaptureCacheScope(controller.signal), original = eventHistory.cacheScope;
        if (original && scope && (original.deviceId !== scope.deviceId || original.fastEpoch !== scope.fastEpoch))
            throw new Error("El almacenamiento cambio. Actualice el historial.");
        state.cacheScope = scope;
        for (const id of ids) {
            try {
                const stamp = eventHistory.captures.find(capture => capture.id === id)?.timestamp;
                const event = await loadCachedFast(id, stamp, scope, controller.signal);
                if (token !== state.token) return;
                state.events.push(event); state.fastSegments.push({timestamp: event.timestamp, samples: gridFastDisplaySamples(event)});
                state.event = state.events.find(event => event.id === selected) || state.events[0];
                $("#eventHistoryPreviewDetails").prop("disabled", false);
                renderEventHistoryPreview();
            } catch (error) {
                if (controller.signal.aborted) throw error;
                failures.push("FAST #" + id + ": " + error.message);
            }
            $("#eventHistoryPreviewFastStatus").text(state.events.length + " / " + ids.length + " capturas FAST" +
                (state.events.some(event => event.partial) ? " · hay capturas parciales" : ""));
        }
        for (const event of state.events) {
            try {
                const info = await loadCachedWaveIndex(event, scope, controller.signal);
                if (token !== state.token) return;
                for (const capture of info.captures || []) {
                    const key = [capture.id, capture.version, capture.triggerUs, capture.endUs].join(":");
                    if (seenWaves.has(key)) continue;
                    seenWaves.add(key);
                    try {
                        $("#eventHistoryPreviewWaveStatus").text("Leyendo oscilografia #" + capture.id + "...");
                        const wave = await loadCachedWave(event, capture, scope, controller.signal);
                        if (token !== state.token) return;
                        state.waves.push(wave); state.waveSegments.push({timestamp: event.timestamp, samples: gridRegularSamples(wave.samples, 0.125)});
                        renderEventHistoryPreview();
                    } catch (error) {
                        if (controller.signal.aborted) throw error;
                        failures.push("Wave #" + capture.id + ": " + error.message);
                    }
                }
            } catch (error) {
                if (controller.signal.aborted) throw error;
                failures.push("Indice Wave #" + event.id + ": " + error.message);
            }
        }
        const samples = state.waves.reduce((sum, wave) => sum + wave.samples.length, 0);
        $("#eventHistoryPreviewWaveStatus").text(state.waves.length ? state.waves.length + " oscilografias · " + samples + " muestras Wave" +
            (state.waves.some(wave => wave.invalidPackets) ? " · hay paquetes invalidos" : "") : "Sin oscilografia disponible.");
        $("#eventHistoryPreviewWarnings").text(failures.length ? "Lecturas pendientes: " + failures.join("; ") : "");
    } catch (error) {
        if (token === state.token && !controller.signal.aborted) $("#eventHistoryPreviewWarnings").text(error.message);
    } finally { if (token === state.token) state.loading = false; }
}

if (typeof module !== "undefined") module.exports = {eventHistoryPreviewSource, mergeEventHistorySamples, collectEventHistoryMarkers, groupEventHistoryPreviewSections,
    eventHistorySignalDefinitions, toggleEventHistorySignals};
