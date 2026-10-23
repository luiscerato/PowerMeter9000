const eventHistory = {captures: [], failures: [], controller: null, run: 0, loaded: false, selected: null, range: null, result: null, periodUnit: "week", readQueue: Promise.resolve()};

// Reduce una captura a sus registros; event es el resultado del decodificador Grid con CRC.
function summarizeEventHistory(event) {
    return {id: event.id, timestamp: event.timestamp, estimated: event.estimatedTime, startup: event.startup,
        partial: event.partial, dropped: !!(event.timeStatus & 64), synthetic: !!(event.timeStatus & 128),
        records: gridEventRecords(event).map(record => ({...record, states: {...record.states}}))};
}

// Reconstruye tramos por entrada; captures son resumenes, from/to delimitan fechas Unix en ms.
function buildEventHistory(captures, from, to) {
    const lanes = new Map(), seen = new Map(), barriers = [], intervals = [];
    let excluded = 0;
    for (const capture of captures) {
        if (capture.estimated || !Number.isFinite(capture.timestamp) || capture.timestamp < 946684800000 || capture.synthetic) { ++excluded; continue; }
        if (capture.startup) barriers.push(capture.timestamp);
        for (const record of capture.records) {
            const state = record.states, at = capture.timestamp + record.offsetMs;
            if (state.subject > 4 || !Number.isFinite(at)) continue;
            const current = state.isCurrent || state.isPoint && state.pointType === 3;
            const lane = state.subject + (current ? ":A" : ":V");
            const key = [lane, Math.round(at * 10), state.isPoint ? state.pointType : 0, state.previous, state.current, record.previousDurationMs].join(":");
            const existing = seen.get(key);
            if (existing) { existing.ids.add(capture.id); continue; }
            const observation = {...record, at, lane, ids: new Set([capture.id]), startup: !!(capture.startup && record.root),
                uncertain: capture.partial || capture.dropped};
            seen.set(key, observation);
            if (!lanes.has(lane)) lanes.set(lane, []);
            lanes.get(lane).push(observation);
        }
    }
    barriers.sort((a, b) => a - b);
    for (const [lane, observations] of lanes) {
        observations.sort((a, b) => a.at - b.at);
        let pending = null;
        for (const observation of observations) {
            const state = observation.states, duration = observation.previousDurationMs;
            if (state.isPoint) {
                intervals.push({lane, subject: state.subject, current: state.pointType === 3, point: state.pointType, state: 0,
                    start: observation.at, end: observation.at + duration, startKnown: true, endKnown: !state.limited,
                    uncertain: observation.uncertain || state.limited, ids: [...observation.ids]});
                continue;
            }
            let previous = null;
            if (state.previous > 0 && duration > 0 && !observation.startup) {
                previous = {lane, subject: state.subject, current: !!state.isCurrent, state: state.previous, point: 0,
                    start: observation.at - duration, end: observation.at, startKnown: true, endKnown: true,
                    uncertain: observation.uncertain, ids: [...observation.ids]};
                const restart = barriers.filter(at => at > previous.start + 10 && at <= previous.end).pop();
                if (restart != null) { previous.start = restart; previous.startKnown = false; previous.uncertain = true; }
            }
            if (pending) {
                const continuous = previous && previous.state === pending.state && Math.abs(previous.start - pending.start) <= 30 && previous.startKnown;
                if (continuous) {
                    previous.ids = [...new Set([...pending.ids, ...previous.ids])];
                    previous.uncertain ||= pending.uncertain;
                } else {
                    pending.end = Math.max(pending.start, Math.min(observation.at, previous ? previous.start : observation.at));
                    pending.uncertain = true;
                    appendEventHistoryState(intervals, pending, barriers);
                }
                pending = null;
            }
            if (previous) intervals.push(previous);
            if (state.current > 0) pending = {lane, subject: state.subject, current: !!state.isCurrent, state: state.current, point: 0,
                start: observation.at, end: to, startKnown: !observation.startup, endKnown: false,
                uncertain: observation.uncertain || observation.startup, ids: [...observation.ids]};
        }
        if (pending) appendEventHistoryState(intervals, pending, barriers);
    }
    const visible = intervals.filter(item => item.end >= from && item.start <= to).map(item => ({...item,
        key: [item.lane, item.point, item.state, Math.round(item.start * 10)].join(":"),
        ids: item.ids.sort((a, b) => b - a)}));
    visible.sort((a, b) => b.start - a.start || a.lane.localeCompare(b.lane));
    return {intervals: visible, excluded, records: seen.size};
}

// Agrega state a intervals sin prolongarlo mas alla del primer reinicio registrado en barriers.
function appendEventHistoryState(intervals, state, barriers) {
    const restart = barriers.find(at => at > state.start && at <= state.end);
    if (restart != null) { state.end = restart; state.endKnown = false; state.uncertain = true; }
    intervals.push(state);
}

// Inicializa controles y periodo por defecto de siete dias; no recibe parametros.
function initEventHistory() {
    initEventStatistics();
    initEventHistoryPreview();
    $("#eventHistoryWarning").on("close.bs.alert", function (event) {
        event.preventDefault();
        eventHistory.dismissedWarning = eventHistory.warningKey;
        $(this).addClass("d-none");
    });
    setEventHistoryPeriod(eventHistoryCalendarRange("thisWeek"));
    $("#eventHistoryPreset").val("thisWeek").on("change", function () { applyEventHistoryPreset(this.value); });
    $("#eventHistoryPrevious").on("click", () => moveEventHistoryPeriod(-1));
    $("#eventHistoryNext").on("click", () => moveEventHistoryPeriod(1));
    $("#eventHistoryCurrent").on("click", () => moveEventHistoryPeriod(0));
    $("#eventHistoryFrom, #eventHistoryTo").on("input change", () => { eventHistory.periodUnit = "custom"; $("#eventHistoryPreset").val(""); });
    $("#eventHistoryLoad").on("click", loadEventHistory);
    $("#eventHistoryStop").on("click", () => stopEventHistory());
    $("#eventHistoryLane").on("change", renderEventHistory);
    initEventHistoryTimelineLayout();
}

// Formatea date como fecha local para el control datetime-local.
function eventHistoryInputTime(date) {
    return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 19);
}

// Muestra fecha y hora local sin ambiguedad AM/PM; at es tiempo Unix en milisegundos.
function formatEventHistoryTime(at) {
    return new Date(at).toLocaleString("es-AR", {hour12: false});
}

// Detiene solicitudes pendientes conservando lo recibido; no recibe parametros.
function stopEventHistory() {
    if (eventHistory.controller) eventHistory.controller.abort();
}

// Activa o pausa el analisis al cambiar de seccion; active indica si Historial esta visible.
function setEventHistoryActive(active) {
    if (!active) { stopEventHistory(); closeEventHistoryPreview(); return; }
    if (!eventHistory.controller || eventHistory.controller.signal.aborted) loadEventHistory();
}

// Lee JSON o binario con plazo y cancelacion; path es la ruta, signal cancela y binary elige bytes.
async function fetchEventHistory(path, signal, binary = false) {
    const previous = eventHistory.readQueue;
    let release;
    eventHistory.readQueue = new Promise(resolve => { release = resolve; });
    await previous;
    try { return await readEventHistoryResponse(path, signal, binary); }
    finally { release(); }
}

// Ejecuta una lectura ya serializada; path es la ruta, signal cancela y binary elige bytes.
async function readEventHistoryResponse(path, signal, binary) {
    if (signal.aborted) throw new DOMException("Cancelado", "AbortError");
    const controller = new AbortController(), cancel = () => controller.abort();
    const timer = setTimeout(cancel, 20000);
    signal.addEventListener("abort", cancel, {once: true});
    if (signal.aborted) cancel();
    try {
        const response = await fetch(board.IP + path, {cache: "no-store", signal: controller.signal});
        if (!response.ok) {
            const error = new Error("HTTP " + response.status);
            error.status = response.status;
            try { error.reason = (await response.json()).reason; } catch (_) {}
            throw error;
        }
        if (binary === "capture") return {buffer: await response.arrayBuffer(), identity: response.headers.get("X-Capture-Identity")};
        return await (binary ? response.arrayBuffer() : response.json());
    } finally { clearTimeout(timer); signal.removeEventListener("abort", cancel); }
}

// Agrega un resumen sin conservar las muestras; entry es el indice, signal cancela y run evita respuestas antiguas.
async function appendEventHistoryCapture(entry, signal, run) {
    $("#eventHistoryStatus").text("Leyendo captura #" + entry.i + "...");
    try {
        const decoded = await loadCachedFast(entry.i, entry.t, eventHistory.cacheScope, signal);
        if (run !== eventHistory.run) return;
        if (decoded.id !== entry.i) throw new Error("Identificador de captura incorrecto");
        eventHistory.captures.push(summarizeEventHistory(decoded));
    } catch (error) {
        if (signal.aborted) throw error;
        if (run === eventHistory.run) eventHistory.failures.push(entry.i);
    }
    if (run === eventHistory.run && Date.now() - (eventHistory.renderAt || 0) >= 150) renderEventHistory();
}

// Descarga las capturas de una en una, desde la mas reciente, y reconstruye tras cada respuesta; sin parametros.
async function loadEventHistory() {
    stopEventHistory();
    closeEventHistoryPreview();
    const from = new Date($("#eventHistoryFrom").val()).getTime(), to = new Date($("#eventHistoryTo").val()).getTime() + 1000;
    if (!Number.isFinite(from) || !Number.isFinite(to) || from >= to) return $("#eventHistoryStatus").text("Seleccione un intervalo valido.");
    const run = ++eventHistory.run, controller = new AbortController();
    eventHistory.controller = controller; eventHistory.loaded = true; eventHistory.range = {from, to};
    eventHistory.captures = []; eventHistory.failures = []; eventHistory.selected = null;
    eventHistory.dismissedWarning = null;
    $("#eventHistoryLoad").prop("disabled", true); $("#eventHistoryStop").prop("disabled", false);
    $("#eventHistoryStatus").text("Consultando indice..."); $("#eventHistoryProgress").prop("value", 0).removeAttr("max");
    renderEventHistory();
    let examined = 0, boundaryLoaded = false, followingBoundary = null;
    try {
        const cacheScope = await refreshCaptureCacheScope(controller.signal);
        if (run !== eventHistory.run) return;
        eventHistory.cacheScope = cacheScope;
        const info = await fetchEventHistory("/grid-events", controller.signal);
        if (run !== eventHistory.run) return;
        $("#eventHistoryProgress").attr("max", Math.max(1, info.eventCount));
        for (let last = info.newestId; info.eventCount && last >= info.oldestId && last > 0;) {
            const first = Math.max(info.oldestId, last - 63);
            const list = await fetchEventHistory("/grid-events/list?from=" + first + "&to=" + last, controller.signal);
            if (run !== eventHistory.run) return;
            const entries = (list.events || []).sort((a, b) => b.i - a.i);
            for (const entry of entries) {
                if (controller.signal.aborted) throw new DOMException("Cancelado", "AbortError");
                ++examined;
                if (!entry.e && entry.t > to + 32768 && (!followingBoundary || entry.t < followingBoundary.t)) followingBoundary = entry;
                // Una captura anterior aporta contexto del limite, sin descargar todo el archivo historico.
                const boundary = !entry.e && entry.t < from - 32768 && !boundaryLoaded;
                if (boundary || entry.e || entry.t >= from - 32768 && entry.t <= to + 32768) {
                    await appendEventHistoryCapture(entry, controller.signal, run);
                    if (boundary) boundaryLoaded = true;
                    if (run !== eventHistory.run) return;
                }
                $("#eventHistoryProgress").val(examined);
                $("#eventHistoryStatus").text(eventHistory.captures.length + " capturas analizadas · " + examined + " / " + info.eventCount + " revisadas");
            }
            last = first - 1;
        }
        // El primer registro posterior puede fechar un retorno que cruza el final del periodo.
        if (followingBoundary) await appendEventHistoryCapture(followingBoundary, controller.signal, run);
        if (run !== eventHistory.run) return;
        $("#eventHistoryProgress").attr("max", 1).val(1);
        $("#eventHistoryStatus").text((eventHistory.failures.length ? "Analisis finalizado con lecturas pendientes" : "Analisis completo") + " · " + eventHistory.captures.length + " capturas analizadas");
    } catch (error) {
        if (run === eventHistory.run) $("#eventHistoryStatus").text(controller.signal.aborted ? "Analisis detenido; se conserva lo recibido." : "No se pudo completar el analisis: " + error.message);
    } finally {
        if (run === eventHistory.run) {
            eventHistory.controller = null;
            $("#eventHistoryLoad").prop("disabled", false); $("#eventHistoryStop").prop("disabled", true);
            renderEventHistory();
        }
    }
}

// Devuelve un nombre por estado o puntual; item es un tramo reconstruido.
function eventHistoryName(item) {
    return item.point ? gridPointName(item.point) : item.subject === 3 && !item.current && item.state === 3 ? "Neutro alto" : gridStateName(item.state, item.subject);
}

// Nombra una entrada sin mezclar corriente y tension; lane identifica sujeto y magnitud.
function eventHistoryLaneName(lane) {
    const [subject, unit] = lane.split(":"), name = unit === "A" && subject === "3" ? "Neutro" : gridSubjectName(Number(subject));
    return name + (unit === "A" ? " · corriente" : "");
}

// Asigna color segun estado, sin depender de la fase; item identifica el tramo.
function eventHistoryColor(item) {
    if (item.point) return item.point === 1 ? "#c58b16" : item.point === 2 ? "#c94d83" : "#16859b";
    if (item.state === 1) return "#b9ddcb";
    if (item.subject === 4 && item.state === 3 || item.subject !== 4 && item.state === 4) return "#d63d48";
    return item.state === 2 ? "#c58b16" : "#c94d83";
}

// Agrupa marcas cercanas en una sola fila; items son puntuales, from/to el periodo y width el ancho disponible en px.
function layoutEventHistoryPoints(items, from, to, width) {
    const markers = [];
    for (const item of [...items].sort((a, b) => a.start - b.start || a.ids[0] - b.ids[0])) {
        const anchor = Math.max(0, Math.min(width, (item.start - from) / (to - from) * width));
        const previous = markers[markers.length - 1];
        if (previous && anchor - previous.anchor < 18) { previous.items.push(item); continue; }
        const size = Math.min(width, 18);
        const left = Math.max(0, Math.min(width - size, anchor - size / 2));
        markers.push({items: [item], left, size, anchor});
    }
    return markers;
}

// Recoloca flechas al cambiar el ancho sin consultar al ESP; no recibe parametros.
function initEventHistoryTimelineLayout() {
    if (typeof ResizeObserver === "undefined") return;
    let lastWidth = 0;
    const observer = new ResizeObserver(entries => {
        const width = Math.round(entries[0].contentRect.width);
        if (width > 0 && width !== lastWidth) {
            lastWidth = width;
            if (eventHistory.range) renderEventHistory();
        }
    });
    observer.observe(document.getElementById("eventHistoryTimeline"));
}

// Pinta cronologia y lista con los resumenes actuales; no recibe parametros.
function renderEventHistory() {
    if (!eventHistory.range) return;
    eventHistory.renderAt = Date.now();
    disposeEventHistoryTooltips();
    const {from, to} = eventHistory.range;
    const result = eventHistory.result = buildEventHistory(eventHistory.captures, from, to);
    const filter = $("#eventHistoryLane").val() || "all";
    const entries = result.intervals.filter(item => filter === "all" || item.lane === filter);
    const episodes = entries.filter(item => item.point || item.state > 1);
    const quality = eventHistory.captures.filter(item => item.partial || item.dropped).length;
    const startups = eventHistory.captures.filter(item => item.startup).length;
    $("#eventHistorySummary").text(episodes.length + " tramos / eventos · " + result.records + " registros" + (startups ? " · " + startups + " arranques registrados" : ""));
    $("#eventHistoryNote").text(result.excluded ? result.excluded + " capturas de prueba o sin hora fiable excluidas del calendario." : "");
    const warnings = [];
    if (eventHistory.failures.length) warnings.push("No se pudieron leer: #" + eventHistory.failures.join(", #"));
    if (quality) warnings.push(quality + " capturas con paginas o registros incompletos");
    eventHistory.warningKey = JSON.stringify([eventHistory.failures, eventHistory.captures.filter(item => item.partial || item.dropped).map(item => item.id).sort((a,b) => a-b)]);
    $("#eventHistoryWarningText").text(warnings.join(" · "));
    $("#eventHistoryWarning").toggleClass("d-none", !warnings.length || eventHistory.dismissedWarning === eventHistory.warningKey);
    const timeline = $("#eventHistoryTimeline").empty();
    const laneNames = ["4:V", "0:V", "1:V", "2:V", "3:V", ...[...new Set(entries.map(item => item.lane))].filter(lane => lane.endsWith(":A"))];
    for (const lane of laneNames.filter(lane => filter === "all" || lane === filter)) {
        const row = $("<div>").addClass("eventHistoryLane"), track = $("<div>").addClass("eventHistoryTrack");
        const plot = $("<div>").addClass("eventHistoryLanePlot"), points = $("<div>").addClass("eventHistoryPoints");
        row.append($("<span>").addClass("eventHistoryLaneLabel").text(eventHistoryLaneName(lane)), plot.append(points, track)); timeline.append(row);
        for (const item of entries.filter(item => item.lane === lane && !item.point).reverse()) {
            const left = Math.max(from, item.start), right = Math.min(to, item.end);
            const label = eventHistoryTooltip(item);
            const button = $("<button>").attr({type: "button", title: label, "aria-label": label, "aria-pressed": item.key === eventHistory.selected ? "true" : "false"})
                .css({left: (left - from) / (to - from) * 100 + "%", width: Math.max(0, right - left) / (to - from) * 100 + "%", backgroundColor: eventHistoryColor(item)})
                .toggleClass("uncertain", !item.startKnown || !item.endKnown || item.uncertain)
                .attr("data-history-tooltip", "true").on("click", () => openEventHistoryPreview(item.key));
            track.append(button);
        }
        const markers = layoutEventHistoryPoints(entries.filter(item => item.lane === lane && item.point), from, to, Math.max(1, track[0].clientWidth));
        points.css("height", markers.length ? 14 : 0);
        for (const marker of markers) {
            const ids = [...new Set(marker.items.flatMap(item => item.ids))], colors = [...new Set(marker.items.map(eventHistoryColor))];
            const label = marker.items.length === 1 ? eventHistoryTooltip(marker.items[0]) :
                marker.items.length + " eventos · " + ids.map(id => "#" + id).join(", ") + "\nClic para elegir un evento";
            const group = $("<div>").addClass("eventHistoryPointGroup").css({left: marker.left, width: marker.size});
            const button = $("<button>").addClass("eventHistoryPoint").attr({type: "button", title: label, "aria-label": label,
                "aria-pressed": marker.items.some(item => item.key === eventHistory.selected) ? "true" : "false",
                "data-history-tooltip": "true", "data-event-ids": ids.join(" ")})
                .css({"--event-arrow-x": (marker.anchor - marker.left) + "px", "--event-marker-fill":
                    colors.length === 1 ? colors[0] : "linear-gradient(90deg," + colors.join(",") + ")"});
            group.append(button); points.append(group);
            if (marker.items.length === 1) button.on("click", () => openEventHistoryPreview(marker.items[0].key));
            else {
                button.attr({"data-bs-toggle": "dropdown", "aria-expanded": "false"});
                const menu = $("<ul>").addClass("dropdown-menu eventHistoryPointMenu");
                for (const item of marker.items) menu.append($("<li>").append($("<button type='button' class='dropdown-item'>")
                    .text(item.ids.map(id => "#" + id).join(", ") + " · " + eventHistoryName(item) + " · " + formatEventHistoryTime(item.start))
                    .on("click", () => openEventHistoryPreview(item.key))));
                group.append(menu);
            }
        }
    }
    $("#eventHistoryAxis").empty().append(...[from, from + (to - from) / 2, to - 1].map(at => $("<span>").text(new Date(at).toLocaleDateString())));
    const table = $("#eventHistoryRows").empty();
    for (const item of episodes) {
        const row = $("<tr>").attr("data-history-key", item.key).toggleClass("table-active", item.key === eventHistory.selected);
        const button = $("<button>").addClass("btn btn-link btn-sm p-0 text-start").attr("type", "button").text(eventHistoryName(item)).on("click", () => selectEventHistory(item.key));
        const start = $("<button>").attr({type: "button", title: eventHistoryTooltip(item), "data-history-tooltip": "true"}).addClass("btn btn-link btn-sm p-0 text-start").text(formatEventHistoryTime(item.start) + (item.startKnown ? "" : " · inicio desconocido")).on("click", () => openEventHistoryPreview(item.key, "start"));
        const end = item.endKnown ? $("<button>").attr({type: "button", title: eventHistoryTooltip(item), "data-history-tooltip": "true"}).addClass("btn btn-link btn-sm p-0 text-start").text(formatEventHistoryTime(item.end)).on("click", () => openEventHistoryPreview(item.key, "end")) : "Sin retorno registrado";
        row.append($("<td>").append(start), $("<td>").text(eventHistoryLaneName(item.lane)), $("<td>").append(button),
            $("<td>").append(end),
            $("<td>").text(item.startKnown && item.endKnown ? formatGridDuration(item.end - item.start) : "Sin cierre completo"));
        table.append(row);
    }
    if (!episodes.length) table.append($("<tr>").append($("<td>").attr("colspan", 5).addClass("text-muted").text(eventHistory.controller ? "Analizando registros..." : "No hay eventos reconstruidos en este intervalo.")));
    renderEventHistoryDetail();
    renderEventStatistics();
    initEventHistoryTooltips();
}

// Selecciona un tramo desde la linea o la lista; key es su identificador local.
function selectEventHistory(key) {
    eventHistory.selected = key; renderEventHistory();
    document.getElementById("eventHistoryDetail").scrollIntoView({behavior: "smooth", block: "nearest"});
}

// Muestra las capturas fuente del tramo seleccionado; no recibe parametros.
function renderEventHistoryDetail() {
    const item = eventHistory.result.intervals.find(item => item.key === eventHistory.selected), target = $("#eventHistoryDetail").empty();
    if (!item) { target.addClass("d-none"); return; }
    target.removeClass("d-none").append($("<strong>").text(eventHistoryLaneName(item.lane) + " · " + eventHistoryName(item)),
        $("<div>").addClass("small text-muted").text(item.uncertain || !item.startKnown || !item.endKnown ? "Tramo parcial: no se pudo verificar toda la continuidad." : "Inicio y final reconstruidos con las transiciones y su duracion registrada."));
    const links = $("<div>").addClass("d-flex flex-wrap gap-2 mt-2");
    for (const id of item.ids) links.append($("<button>").addClass("btn btn-sm btn-outline-secondary").attr("type", "button").text("Ver captura #" + id).on("click", () => {
        const capture = eventHistory.captures.find(capture => capture.id === id);
        showAppPage("gridEvents"); loadGridEvent({i: id, t: capture.timestamp});
    }));
    target.append(links);
}

if (typeof module !== "undefined") module.exports = {buildEventHistory, layoutEventHistoryPoints};
