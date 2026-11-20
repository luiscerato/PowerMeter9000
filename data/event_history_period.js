// Devuelve un periodo calendario local; preset elige el atajo y now fija la fecha de referencia.
function eventHistoryCalendarRange(preset, now = new Date()) {
    const from = new Date(now.getFullYear(), now.getMonth(), now.getDate());
    let unit = "day";
    if (preset === "yesterday") from.setDate(from.getDate() - 1);
    if (preset === "thisWeek" || preset === "lastWeek") {
        unit = "week"; from.setDate(from.getDate() - (from.getDay() + 6) % 7 - (preset === "lastWeek" ? 7 : 0));
    }
    if (preset === "thisMonth" || preset === "lastMonth") {
        unit = "month"; from.setDate(1); if (preset === "lastMonth") from.setMonth(from.getMonth() - 1);
    }
    if (preset === "thisYear") { unit = "year"; from.setMonth(0, 1); }
    const next = new Date(from);
    if (unit === "month") next.setMonth(next.getMonth() + 1);
    else if (unit === "year") next.setFullYear(next.getFullYear() + 1);
    else next.setDate(next.getDate() + (unit === "week" ? 7 : 1));
    return {from: from.getTime(), to: next.getTime() - 1000, unit};
}

// Desplaza un periodo sin perder meses ni cambios horarios; range tiene limites inclusivos y direction vale -1/1.
function shiftEventHistoryPeriod(range, direction) {
    if (range.unit === "custom") {
        const delta = (range.to - range.from + 1000) * direction;
        return {...range, from: range.from + delta, to: range.to + delta};
    }
    const date = new Date(range.from);
    if (range.unit === "month") date.setMonth(date.getMonth() + direction);
    else if (range.unit === "year") date.setFullYear(date.getFullYear() + direction);
    else date.setDate(date.getDate() + direction * (range.unit === "week" ? 7 : 1));
    return eventHistoryCalendarRange(({day: "today", week: "thisWeek", month: "thisMonth", year: "thisYear"})[range.unit], date);
}

// Completa ambos campos sin consultar; range contiene desde/hasta inclusivos y unidad calendario.
function setEventHistoryPeriod(range) {
    eventHistory.periodUnit = range.unit;
    $("#eventHistoryFrom").val(eventHistoryInputTime(new Date(range.from)));
    $("#eventHistoryTo").val(eventHistoryInputTime(new Date(range.to)));
    syncEventHistoryDates();
}

// Actualiza las etiquetas cortas sin alterar fecha, hora ni segundos de los controles; sin parametros.
function syncEventHistoryDates() {
    if (typeof document === "undefined") return;
    for (const id of ["eventHistoryFrom", "eventHistoryTo"]) {
        const input = document.getElementById(id), label = document.getElementById(id + "Label");
        if (!input || !label) continue;
        const date = new Date(input.value), valid = Number.isFinite(date.getTime());
        label.textContent = valid ? String(date.getDate()).padStart(2, "0") + "/" + String(date.getMonth() + 1).padStart(2, "0") + " " +
            String(date.getHours()).padStart(2, "0") + ":" + String(date.getMinutes()).padStart(2, "0") : "—";
        input.parentElement.title = valid ? date.toLocaleString("es-AR", {hour12: false}) : "Seleccionar fecha y hora";
    }
}

// Aplica un atajo y carga el periodo; preset identifica hoy/ayer/semana/mes/anio.
function applyEventHistoryPreset(preset) {
    if (!preset) return;
    setEventHistoryPeriod(eventHistoryCalendarRange(preset)); loadEventHistory();
}

// Mueve la seleccion por calendario o vuelve a su periodo actual; direction es -1, 1 o 0.
function moveEventHistoryPeriod(direction) {
    const range = {from: new Date($("#eventHistoryFrom").val()).getTime(), to: new Date($("#eventHistoryTo").val()).getTime(), unit: eventHistory.periodUnit || "custom"};
    if (!Number.isFinite(range.from) || !Number.isFinite(range.to) || range.from >= range.to) return;
    if (direction) { setEventHistoryPeriod(shiftEventHistoryPeriod(range, direction)); $("#eventHistoryPreset").val(""); }
    else if (range.unit === "custom") {
        const to = Math.floor(Date.now() / 1000) * 1000;
        setEventHistoryPeriod({...range, from: to - (range.to - range.from), to});
    } else {
        const preset = ({day: "today", week: "thisWeek", month: "thisMonth", year: "thisYear"})[range.unit];
        $("#eventHistoryPreset").val(preset); setEventHistoryPeriod(eventHistoryCalendarRange(preset));
    }
    loadEventHistory();
}

if (typeof module !== "undefined") module.exports = {eventHistoryCalendarRange, shiftEventHistoryPeriod};

// Guarda periodo y filtros de Historial en este navegador; no recibe parametros.
function saveEventHistoryPreferences() {
    try {
        localStorage.setItem("powerMeter.eventHistory.v1", JSON.stringify({preset: $("#eventHistoryPreset").val(),
            from: $("#eventHistoryFrom").val(), to: $("#eventHistoryTo").val(), unit: eventHistory.periodUnit,
            lane: eventHistory.lane, types: [...eventHistory.types], subjects: [...eventStatisticsSubjects],
            grouping: $("#eventStatisticsUnit").val()}));
    } catch (_) {}
}

// Recupera filtros y recalcula los atajos relativos como Hoy; no recibe parametros.
function restoreEventHistoryPreferences() {
    try {
        const saved = JSON.parse(localStorage.getItem("powerMeter.eventHistory.v1"));
        if (!saved || typeof saved !== "object") return;
        const presets = ["today", "yesterday", "thisWeek", "lastWeek", "thisMonth", "lastMonth", "thisYear"];
        if (presets.includes(saved.preset)) {
            $("#eventHistoryPreset").val(saved.preset); setEventHistoryPeriod(eventHistoryCalendarRange(saved.preset));
        } else if (Number.isFinite(Date.parse(saved.from)) && Date.parse(saved.from) < Date.parse(saved.to)) {
            $("#eventHistoryPreset").val("");
            setEventHistoryPeriod({from: Date.parse(saved.from), to: Date.parse(saved.to),
                unit: ["day", "week", "month", "year", "custom"].includes(saved.unit) ? saved.unit : "custom"});
        }
        if (["all", "0:V", "1:V", "2:V", "3:V", "4:V"].includes(saved.lane)) {
            eventHistory.lane = saved.lane; $("#eventHistoryLane").val(saved.lane);
        }
        if (Array.isArray(saved.types)) eventHistory.types = new Set(saved.types.filter(key => eventHistoryTypes.some(type => type.key === key)));
        if (Array.isArray(saved.subjects)) {
            const subjects = saved.subjects.filter(subject => Number.isInteger(subject) && subject >= 0 && subject <= 4);
            eventStatisticsSubjects = new Set(subjects.includes(4) ? [4] : subjects);
        }
        if (["auto", "hour", "day", "week"].includes(saved.grouping)) $("#eventStatisticsUnit").val(saved.grouping);
        updateEventHistoryTypeControls(); updateEventStatisticsSubjectControls();
    } catch (_) {}
}
