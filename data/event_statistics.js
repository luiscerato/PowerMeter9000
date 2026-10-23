const eventStatisticsTypes = [
    {key: "outage", name: "Sin tension", timed: true}, {key: "low", name: "Baja tension", timed: true},
    {key: "high", name: "Alta tension", timed: true}, {key: "current", name: "Corriente alta", timed: true},
    {key: "abnormal", name: "Red anormal", timed: true}, {key: "dip", name: "Dips"},
    {key: "swell", name: "Swells"}, {key: "pulse", name: "Picos de corriente"}
];
const eventStatisticsCharts = [];

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
        const metric = values[type], key = type + ":" + Math.round(item.start * 10);
        if (item.startKnown && item.start >= from && !starts.has(key)) { ++metric.starts; starts.add(key); }
        if (item.point) continue;
        if (!item.startKnown || !item.endKnown || item.uncertain) {
            if (item.end > from && !partials.has(key)) { ++metric.partial; partials.add(key); }
            continue;
        }
        const start = Math.max(from, item.start), end = Math.min(to, item.end);
        if (end > start) ranges[type].push([start, end]);
    }
    for (const type of eventStatisticsTypes) values[type.key].durationMs = eventStatisticsDuration(ranges[type.key]);
    return values;
}

// Resume todas las entradas y desglosa subject por periodos; intervals son tramos y from/to el rango solicitado.
function buildEventStatistics(intervals, from, to, requested = "auto", subject = 4) {
    const unit = eventStatisticsGranularity(from, to, requested), groups = new Map();
    for (const item of intervals) {
        if (!eventStatisticsType(item) || item.subject < 0 || item.subject > 4) continue;
        if (!groups.has(item.subject)) groups.set(item.subject, []);
        groups.get(item.subject).push(item);
    }
    const summary = [4, 0, 1, 2, 3].map(subject => ({subject, values: calculateEventStatistics(groups.get(subject) || [], from, to)}));
    const rows = [], items = groups.get(subject) || [];
    for (let at = eventStatisticsFloor(from, unit); at < to;) {
        const next = eventStatisticsNext(at, unit);
        if (next <= at) break;
        if (next > from) {
            if (rows.length >= 1000) return {unit, summary, rows: [], limited: true};
            const start = Math.max(from, at), end = Math.min(to, next);
            rows.push({start, end, values: calculateEventStatistics(items, start, end)});
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
    $("#eventStatisticsUnit, #eventStatisticsSubject").on("change", renderEventStatistics);
    $("#eventHistoryStats-tab").on("shown.bs.tab", renderEventStatistics);
    $("#eventStatisticsCharts-tab").on("shown.bs.tab", renderEventStatistics);
}

// Construye columnas por intervalo; result es el desglose calculado y duration elige minutos en lugar de cantidades.
function eventStatisticsChartData(result, duration) {
    const colors = ["#d63d48", "#c58b16", "#c94d83", "#475cba", "#59666c", "#16859b", "#278447", "#d46922"];
    return eventStatisticsTypes.filter(type => !duration || type.timed).map(type => ({type: "column", name: type.name,
        color: colors[eventStatisticsTypes.indexOf(type)], showInLegend: true,
        yValueFormatString: duration ? "#,##0.000 min" : "#,##0",
        dataPoints: result.rows.map((row, index) => ({x: index, label: (result.unit === "week" ? "Semana " + (index + 1) + " · " : "") +
            new Date(row.start).toLocaleDateString("es-AR", {day: "2-digit", month: "2-digit"}) +
            (result.unit === "hour" ? " " + new Date(row.start).toLocaleTimeString("es-AR", {hour: "2-digit", minute: "2-digit", hour12: false}) : ""),
            y: duration ? row.values[type.key].durationMs / 60000 : row.values[type.key].starts}))}));
}

// Alterna una serie desde la leyenda; event contiene la serie y su grafica CanvasJS.
function toggleEventStatisticsSeries(event) {
    event.dataSeries.visible = event.dataSeries.visible === false;
    event.chart.render();
}

// Actualiza graficas solo cuando su pestaña es visible; result contiene filas y unidad del desglose.
function renderEventStatisticsCharts(result) {
    if (!$("#eventHistoryStatsPane").hasClass("active") || !$("#eventStatisticsChartsPane").hasClass("active")) return;
    const targets = ["eventStatisticsCountChart", "eventStatisticsDurationChart"];
    for (let index = 0; index < targets.length; ++index) {
        const duration = index === 1, data = eventStatisticsChartData(result, duration);
        const maximum = data.reduce((max, series) => series.dataPoints.reduce((value, point) => Math.max(value, point.y), max), 0);
        if (!eventStatisticsCharts[index]) eventStatisticsCharts[index] = new CanvasJS.Chart(targets[index], {
            animationEnabled: false, zoomEnabled: true, title: {text: duration ? "Duracion verificada" : "Inicios y eventos puntuales", fontSize: 15},
            axisX: {labelFontSize: 10, labelAngle: -30}, axisY: {minimum: 0, title: duration ? "Minutos" : "Cantidad", includeZero: true},
            toolTip: {shared: true}, legend: {fontSize: 11, cursor: "pointer", itemclick: toggleEventStatisticsSeries}, data});
        else eventStatisticsCharts[index].options.data = data;
        eventStatisticsCharts[index].options.axisY.interval = duration ? null : Math.max(1, Math.ceil(maximum / 5));
        eventStatisticsCharts[index].render();
    }
    $("#eventStatisticsChartNotice").text(result.limited ? "Seleccione una agrupacion mayor para graficar este periodo." : "");
}

// Muestra duracion e inicios o solo cantidad; metric es el acumulador y timed distingue estados de puntuales.
function eventStatisticsCell(metric, timed) {
    if (!timed) return String(metric.starts);
    const time = metric.partial && !metric.durationMs ? "Sin cierre verificable" : formatGridDuration(Math.round(metric.durationMs));
    return time + " · " + metric.starts + (metric.starts === 1 ? " inicio" : " inicios") + (metric.partial ? " *" : "");
}

// Pinta resumen y detalle con los datos ya reconstruidos; no recibe parametros.
function renderEventStatistics() {
    if (!eventHistory.range || !eventHistory.result) return;
    const {from, to} = eventHistory.range;
    const result = buildEventStatistics(eventHistory.result.intervals, from, to, $("#eventStatisticsUnit").val(), Number($("#eventStatisticsSubject").val()));
    const unitName = {hour: "hora", day: "dia", week: "semana"}[result.unit];
    $("#eventStatisticsStatus").text((eventHistory.controller ? "Resultados provisionales" : "Resultados de los registros recibidos") + " · agrupado por " + unitName);
    const partial = result.summary.reduce((total, row) => total + Object.values(row.values).reduce((sum, metric) => sum + metric.partial, 0), 0);
    $("#eventStatisticsNote").text("Duracion verificada dentro del periodo · inicios ocurridos dentro del periodo. Red y fases no se suman entre si." +
        (partial ? " * " + partial + " tramos parciales sin sumar su duracion; sus inicios conocidos si se cuentan." : ""));
    const summary = $("#eventStatisticsSummary").empty(), rows = $("#eventStatisticsRows").empty();
    for (const row of result.summary) {
        const tr = $("<tr>").append($("<th>").attr("scope", "row").text(gridSubjectName(row.subject)));
        for (const type of eventStatisticsTypes) tr.append($("<td>").text(eventStatisticsCell(row.values[type.key], type.timed)));
        summary.append(tr);
    }
    for (const row of result.rows) {
        const tr = $("<tr>").append($("<th>").attr("scope", "row").text(formatEventHistoryTime(row.start) + " → " + formatEventHistoryTime(row.end)));
        for (const type of eventStatisticsTypes) tr.append($("<td>").text(eventStatisticsCell(row.values[type.key], type.timed)));
        rows.append(tr);
    }
    if (result.limited) rows.append($("<tr>").append($("<td>").attr("colspan", 9).addClass("text-danger").text("El rango supera 1000 intervalos. Seleccione una agrupacion mayor.")));
    renderEventStatisticsCharts(result);
}

if (typeof module !== "undefined") module.exports = {buildEventStatistics, calculateEventStatistics, eventStatisticsGranularity, eventStatisticsChartData};
