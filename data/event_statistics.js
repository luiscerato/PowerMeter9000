const eventStatisticsTypes = [
    {key: "outage", name: "Sin tension", timed: true}, {key: "low", name: "Baja tension", timed: true},
    {key: "high", name: "Alta tension", timed: true}, {key: "current", name: "Corriente alta", timed: true},
    {key: "abnormal", name: "Red anormal", timed: true}, {key: "dip", name: "Dips"},
    {key: "swell", name: "Swells"}, {key: "pulse", name: "Picos de corriente"}
];
const eventStatisticsCharts = [];
let eventStatisticsSubjects = new Set([0, 1, 2, 3]);

// Expresa durationMs en unidades de reloj, sin minutos decimales.
function formatEventStatisticsDuration(durationMs) {
    const milliseconds = Math.max(0, Math.round(Number(durationMs) || 0));
    if (milliseconds < 1000) return milliseconds + " ms";
    if (milliseconds < 60000) return (milliseconds / 1000).toFixed(2).replace(/\.?0+$/, "").replace(".", ",") + " s";
    const seconds = Math.floor(milliseconds / 1000), minutes = Math.floor(seconds / 60), hours = Math.floor(minutes / 60);
    return (hours ? hours + " h " : "") + minutes % 60 + " min " + seconds % 60 + " s";
}

// Elige divisiones legibles; maximum es la mayor duracion graficada en segundos.
function eventStatisticsTimeInterval(maximum) {
    return [0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 14400, 21600, 43200, 86400]
        .find(value => value >= maximum / 5) || Math.ceil(maximum / 432000) * 86400;
}

// Clasifica un tramo reconstruido; item conserva separados red, tension, corriente y puntuales.
function eventStatisticsType(item) {
    if (item.point) return ({1: "dip", 2: "swell", 3: "pulse"})[item.point];
    if (item.current) return item.state === 5 ? "current" : null;
    if (item.subject === 4) return ({2: "abnormal", 3: "outage"})[item.state];
    return ({2: "low", 3: "high", 4: "outage"})[item.state];
}

// Elige hora/dia/semana; from/to son ms Unix y requested permite anular la seleccion automatica.
function eventStatisticsGranularity(from, to, requested) {
    if (["hour", "day", "week"].includes(requested)) return requested;
    const days = (to - from) / 86400000;
    return days <= 2 ? "hour" : days <= 14 ? "day" : "week";
}

// Redondea at al inicio local de unit; la semana comienza el lunes.
function eventStatisticsFloor(at, unit) {
    const date = new Date(at);
    if (unit === "hour") date.setMinutes(0, 0, 0);
    else {
        date.setHours(0, 0, 0, 0);
        if (unit === "week") date.setDate(date.getDate() - (date.getDay() + 6) % 7);
    }
    return date.getTime();
}

// Avanza un intervalo local; at es su inicio y unit indica hora/dia/semana.
function eventStatisticsNext(at, unit) {
    if (unit === "hour") return at + 3600000;
    const date = new Date(at);
    date.setDate(date.getDate() + (unit === "week" ? 7 : 1));
    return date.getTime();
}

// Crea acumuladores independientes por magnitud; no recibe parametros.
function emptyEventStatistics() {
    return Object.fromEntries(eventStatisticsTypes.map(type => [type.key, {durationMs: 0, starts: 0, partial: 0}]));
}

// Suma la union temporal sin duplicar solapamientos; ranges contiene pares inicio/final.
function eventStatisticsDuration(ranges) {
    ranges.sort((a, b) => a[0] - b[0]);
    let total = 0, end = -Infinity;
    for (const [start, stop] of ranges) { total += Math.max(0, stop - Math.max(start, end)); end = Math.max(end, stop); }
    return total;
}

// Calcula una fila; items son tramos de una entrada, from/to son limites exclusivos al final.
function calculateEventStatistics(items, from, to) {
    const values = emptyEventStatistics(), starts = new Set(), partials = new Set();
    const ranges = Object.fromEntries(eventStatisticsTypes.map(type => [type.key, []]));
    for (const item of items) {
        const type = eventStatisticsType(item);
        if (!type || item.start >= to || item.end < from) continue;
        const metric = values[type], key = item.subject + ":" + type + ":" + Math.round(item.start * 10);
        if (item.startKnown && item.start >= from && !starts.has(key)) { ++metric.starts; starts.add(key); }
        if (item.point) continue;
        if (item.uncertain || !item.sequence && (!item.startKnown || !item.endKnown)) {
            if (item.end > from && !partials.has(key)) { ++metric.partial; partials.add(key); }
            continue;
        }
        const start = Math.max(from, item.start), end = Math.min(to, item.end);
        if (end > start) ranges[type].push([start, end]);
    }
    for (const type of eventStatisticsTypes) values[type.key].durationMs = eventStatisticsDuration(ranges[type.key]);
    return values;
}

// Suma estadisticas calculadas por entrada; metrics mantiene los mismos tipos y unidades.
function sumEventStatistics(metrics) {
    const total = emptyEventStatistics();
    for (const values of metrics) for (const type of eventStatisticsTypes) {
        total[type.key].durationMs += values[type.key].durationMs;
        total[type.key].starts += values[type.key].starts;
        total[type.key].partial += values[type.key].partial;
    }
    return total;
}

// Resume entradas seleccionadas y las agrupa por periodo; subject acepta un numero o una lista de entradas.
function buildEventStatistics(intervals, from, to, requested = "auto", subject = 4) {
    const unit = eventStatisticsGranularity(from, to, requested), groups = new Map();
    for (const item of intervals) {
        if (!eventStatisticsType(item) || item.subject < 0 || item.subject > 4) continue;
        if (!groups.has(item.subject)) groups.set(item.subject, []);
        groups.get(item.subject).push(item);
    }
    // Red conserva sus estados globales y cuenta los puntuales de cada fase.
    groups.set(4, [...(groups.get(4) || []), ...intervals.filter(item => item.point && item.subject >= 0 && item.subject < 4)]);
    const selected = [...new Set((Array.isArray(subject) ? subject : [subject]).filter(value => Number.isInteger(value) && value >= 0 && value <= 4))];
    const summarySubjects = Array.isArray(subject) ? selected : [4, 0, 1, 2, 3];
    const summary = summarySubjects.map(subject => ({subject, values: calculateEventStatistics(groups.get(subject) || [], from, to)}));
    const rows = [];
    const calculate = (start, end) => {
        const total = sumEventStatistics(selected.map(value => calculateEventStatistics(groups.get(value) || [], start, end)));
        const combined = calculateEventStatistics(selected.flatMap(value => groups.get(value) || []), start, end);
        for (const type of eventStatisticsTypes) total[type.key].durationMs = combined[type.key].durationMs;
        return total;
    };
    for (let at = eventStatisticsFloor(from, unit); at < to;) {
        const next = eventStatisticsNext(at, unit);
        if (next <= at) break;
        if (next > from) {
            if (rows.length >= 1000) return {unit, summary, rows: [], limited: true};
            const start = Math.max(from, at), end = Math.min(to, next);
            rows.push({start, end, values: calculate(start, end)});
        }
        at = next;
    }
    return {unit, summary, rows, limited: false};
}

// Conecta los selectores sin volver a consultar al ESP; no recibe parametros.
function initEventStatistics() {
    for (const [id, label] of [["#eventStatisticsSummaryHeader", "Entrada"], ["#eventStatisticsRowsHeader", "Intervalo"]]) {
        const header = $(id).empty().append($("<th>").attr("scope", "col").text(label));
        for (const type of eventStatisticsTypes) header.append($("<th>").attr("scope", "col").text(type.name));
    }
    $("#eventStatisticsUnit").on("change", () => renderEventStatistics());
    $("[data-stat-subject]").on("click", function () { selectEventStatisticsSubject(this.dataset.statSubject); });
    $("[data-stat-subjects='all']").on("click", () => selectEventStatisticsSubject("all"));
    updateEventStatisticsSubjectControls();
    $("#eventHistoryStats-tab").on("shown.bs.tab", () => renderEventStatistics());
    $("#eventStatisticsCharts-tab").on("shown.bs.tab", () => renderEventStatistics());
}

// Cambia el alcance de las estadisticas; subject es Red, todas las fases o el indice de fase.
function selectEventStatisticsSubject(subject) {
    if (subject === "red") eventStatisticsSubjects = new Set([4]);
    else if (subject === "all") eventStatisticsSubjects = new Set([0, 1, 2, 3]);
    else {
        const phase = Number(subject);
        if (eventStatisticsSubjects.has(4)) eventStatisticsSubjects.clear();
        if (eventStatisticsSubjects.has(phase)) eventStatisticsSubjects.delete(phase);
        else if (Number.isInteger(phase) && phase >= 0 && phase <= 3) eventStatisticsSubjects.add(phase);
    }
    updateEventStatisticsSubjectControls();
    renderEventStatistics();
}

// Actualiza el estado visual de Red, Todas y las fases sin alterar el filtro de la linea temporal.
function updateEventStatisticsSubjectControls() {
    const red = eventStatisticsSubjects.size === 1 && eventStatisticsSubjects.has(4);
    $("[data-stat-subject='red']").attr("aria-pressed", String(red));
    $("[data-stat-subjects='all']").attr("aria-pressed", String(!red && [0, 1, 2, 3].every(subject => eventStatisticsSubjects.has(subject))));
    $("[data-stat-subject]").not("[data-stat-subject='red']").each(function () {
        $(this).attr("aria-pressed", String(eventStatisticsSubjects.has(Number(this.dataset.statSubject))));
    });
}

// Clasifica los tipos estadisticos activos; types contiene las claves seleccionadas.
function eventStatisticsKind(types) {
    const timed = eventStatisticsTypes.some(type => type.timed && types.has(type.key));
    const points = eventStatisticsTypes.some(type => !type.timed && types.has(type.key));
    return timed ? points ? "all" : "states" : points ? "points" : "none";
}

// Devuelve tipos válidos para Red o para fases; subjects es el alcance estadístico elegido.
function eventStatisticsAvailableTypes(subjects) {
    return eventStatisticsTypes.filter(type => subjects.length === 1 && subjects[0] === 4 ?
        ["outage", "abnormal", "dip", "swell", "pulse"].includes(type.key) : type.key !== "abnormal");
}

// Construye columnas; result es el desglose, duration elige tiempo en segundos, kind y types filtran series.
function eventStatisticsChartData(result, duration, kind = "all", types = null) {
    const colors = ["#d63d48", "#c58b16", "#c94d83", "#475cba", "#59666c", "#16859b", "#278447", "#d46922"];
    return eventStatisticsTypes.filter(type => (!types || types.has(type.key)) && (!duration || type.timed) && (kind === "points" ? !type.timed : kind === "states" ? type.timed : true)).map(type => ({type: "column", name: type.name,
        color: colors[eventStatisticsTypes.indexOf(type)], showInLegend: true,
        yValueFormatString: duration ? "#,##0.## s" : "#,##0",
        dataPoints: result.rows.map((row, index) => ({x: index, label: (result.unit === "week" ? "Semana " + (index + 1) + " · " : "") +
            new Date(row.start).toLocaleDateString("es-AR", {day: "2-digit", month: "2-digit"}) +
            (result.unit === "hour" ? " " + new Date(row.start).toLocaleTimeString("es-AR", {hour: "2-digit", minute: "2-digit", hour12: false}) : ""),
            y: duration ? row.values[type.key].durationMs / 1000 : row.values[type.key].starts,
            ...(duration ? {tooltipText: formatEventStatisticsDuration(row.values[type.key].durationMs)} : {})}))}));
}

// Alterna una serie desde la leyenda; event contiene la serie y su grafica CanvasJS.
function toggleEventStatisticsSeries(event) {
    event.dataSeries.visible = event.dataSeries.visible === false;
    event.chart.render();
}

// Actualiza graficas visibles; result tiene filas, types limita series y hasScope indica entradas seleccionadas.
function renderEventStatisticsCharts(result, types, hasScope, kind) {
    if (!$("#eventHistoryStatsPane").hasClass("active") || !$("#eventStatisticsChartsPane").hasClass("active")) return;
    if (!hasScope) {
        $(".eventStatisticsChartGrid").addClass("d-none");
        $("#eventStatisticsChartNotice").text("Seleccione Red o una o mas fases para ver estadisticas.");
        return;
    }
    $(".eventStatisticsChartGrid").removeClass("d-none");
    const targets = ["eventStatisticsCountChart", "eventStatisticsDurationChart"];
    const pointsOnly = kind === "points" || kind === "none";
    $("#eventStatisticsDurationChart").toggleClass("d-none", pointsOnly);
    $(".eventStatisticsChartGrid").toggleClass("singleChart", pointsOnly);
    for (let index = 0; index < targets.length; ++index) {
        if (pointsOnly && index === 1) continue;
        const duration = index === 1, data = eventStatisticsChartData(result, duration, kind, types);
        const maximum = data.reduce((max, series) => series.dataPoints.reduce((value, point) => Math.max(value, point.y), max), 0);
        if (!eventStatisticsCharts[index]) eventStatisticsCharts[index] = new CanvasJS.Chart(targets[index], {
            animationEnabled: false, zoomEnabled: true, title: {text: duration ? "Tiempo por estado" : "Inicios y eventos puntuales", fontSize: 15},
            axisX: {labelFontSize: 10, labelAngle: -30}, axisY: {minimum: 0, title: duration ? "Tiempo" : "Cantidad", includeZero: true,
                ...(duration ? {labelFormatter: event => formatEventStatisticsDuration(event.value * 1000)} : {})},
            toolTip: chartTooltip.options({title: duration ? "Tiempo por estado" : "Eventos", mode: "category", unit: duration ? "s" : "eventos", valueDigits: duration ? 2 : 0}), legend: {fontSize: 11, cursor: "pointer", itemclick: toggleEventStatisticsSeries}, data});
        else eventStatisticsCharts[index].options.data = data;
        eventStatisticsCharts[index].options.title.text = duration ? "Tiempo por estado" : kind === "states" ? "Inicios de estado" : kind === "points" ? "Eventos puntuales" : "Inicios y eventos puntuales";
        eventStatisticsCharts[index].options.axisY.interval = duration ? eventStatisticsTimeInterval(maximum) : Math.max(1, Math.ceil(maximum / 5));
        const interval = eventStatisticsCharts[index].options.axisY.interval;
        eventStatisticsCharts[index].options.axisY.maximum = Math.max(interval, Math.ceil(maximum / interval) * interval);
        eventStatisticsCharts[index].render();
    }
    $("#eventStatisticsChartNotice").text(kind === "none" ? "Seleccione los tipos de registro que desea ver." : result.limited ? "Seleccione una agrupacion mayor para graficar este periodo." : "");
}

// Muestra duracion e inicios o solo cantidad; metric es el acumulador y timed distingue estados de puntuales.
function eventStatisticsCell(metric, timed) {
    if (!timed) return String(metric.starts);
    const time = metric.partial && !metric.durationMs ? "Duracion no verificable" : formatEventStatisticsDuration(metric.durationMs);
    return time + " · " + metric.starts + (metric.starts === 1 ? " inicio" : " inicios") + (metric.partial ? " *" : "");
}

// Pinta tablas y graficas usando las entradas seleccionadas en Estadisticas; no recibe parametros.
function renderEventStatistics() {
    if (!eventHistory.range || !eventHistory.result) return;
    const {from, to} = eventHistory.range;
    const subjects = [...eventStatisticsSubjects];
    const availableTypes = eventStatisticsAvailableTypes(subjects);
    const selectedTypes = new Set(availableTypes.filter(type => eventHistory.types.has(type.key)).map(type => type.key));
    const kind = eventStatisticsKind(selectedTypes);
    const types = eventStatisticsTypes.filter(type => selectedTypes.has(type.key));
    const lanes = new Set((subjects.includes(4) ? [4, 0, 1, 2, 3] : subjects).map(subject => subject + ":V"));
    const intervals = filterEventHistoryIntervals(eventHistory.result.intervals, kind, lanes, selectedTypes)
        .filter(item => !subjects.includes(4) || item.subject === 4 || item.point);
    const result = buildEventStatistics(intervals, from, to, $("#eventStatisticsUnit").val(), subjects);
    if (!subjects.length) result.rows = [];
    const unitName = {hour: "hora", day: "dia", week: "semana"}[result.unit];
    const allPhases = [0, 1, 2, 3].every(subject => subjects.includes(subject));
    const scope = subjects.length === 1 ? subjects[0] === 4 ? "Red" : ["Fase R", "Fase S", "Fase T", "Neutro"][subjects[0]] :
        allPhases ? "RSTN · tiempo sin duplicar solapamientos" : subjects.length ? subjects.map(gridSubjectName).join(" + ") + " · tiempo sin duplicar solapamientos" : "Sin entradas";
    $("#eventStatisticsStatus").text(scope + " · " + (eventHistory.controller ? "provisional" : "registros recibidos") + " · por " + unitName);
    const partial = result.summary.reduce((total, row) => total + Object.values(row.values).reduce((sum, metric) => sum + metric.partial, 0), 0);
    $("#eventStatisticsNote").text((kind === "points" ? "Eventos puntuales iniciados dentro del periodo. Los conteos se suman entre las entradas seleccionadas." :
        "Los tiempos combinados cuentan una sola vez los solapamientos del mismo estado. El detalle conserva el tiempo por fase. Los inicios se cuentan por entrada; Red usa sus estados globales y cuenta los DIP, swell y picos por fase. Los estados abiertos se extienden hasta el final del periodo o hasta ahora.") +
        (partial ? " * " + partial + " tramos con registros incompletos o contradictorios no suman duracion; sus inicios conocidos si se cuentan." : ""));
    const summary = $("#eventStatisticsSummary").empty(), rows = $("#eventStatisticsRows").empty();
    for (const [id, label] of [["#eventStatisticsSummaryHeader", "Entrada"], ["#eventStatisticsRowsHeader", "Intervalo"]]) {
        const header = $(id).empty().append($("<th>").attr("scope", "col").text(label));
        for (const type of types) header.append($("<th>").attr("scope", "col").text(type.name));
    }
    for (const row of result.summary) {
        const tr = $("<tr>").append($("<th>").attr("scope", "row").text(gridSubjectName(row.subject)));
        for (const type of types) tr.append($("<td>").text(eventStatisticsCell(row.values[type.key], type.timed)));
        summary.append(tr);
    }
    for (const row of result.rows) {
        const tr = $("<tr>").append($("<th>").attr("scope", "row").text(formatEventHistoryTime(row.start) + " → " + formatEventHistoryTime(row.end)));
        for (const type of types) tr.append($("<td>").text(eventStatisticsCell(row.values[type.key], type.timed)));
        rows.append(tr);
    }
    if (!subjects.length) {
        const message = "Seleccione Red o una o mas fases.";
        summary.append($("<tr>").append($("<td>").attr("colspan", types.length + 1).addClass("text-muted").text(message)));
        rows.append($("<tr>").append($("<td>").attr("colspan", types.length + 1).addClass("text-muted").text(message)));
    }
    if (result.limited) rows.append($("<tr>").append($("<td>").attr("colspan", types.length + 1).addClass("text-danger").text("El rango supera 1000 intervalos. Seleccione una agrupacion mayor.")));
    renderEventStatisticsCharts(result, new Set(types.map(type => type.key)), subjects.length > 0, kind);
}

if (typeof module !== "undefined") module.exports = {buildEventStatistics, calculateEventStatistics, eventStatisticsGranularity, eventStatisticsChartData, formatEventStatisticsDuration, eventStatisticsTimeInterval};
