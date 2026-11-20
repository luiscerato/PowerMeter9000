const chartTooltip = {};

// Escapa texto antes de incluirlo en HTML; value es una etiqueta o valor de la grafica.
chartTooltip.escape = function (value) {
    return String(value ?? "").replace(/[&<>"']/g, char => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"}[char]));
};

// Formatea numeros sin convertir datos ausentes en cero; value es el numero y digits sus decimales.
chartTooltip.number = function (value, digits = 2) {
    return typeof value === "number" && Number.isFinite(value) ? value.toLocaleString("es-AR", {maximumFractionDigits: digits, minimumFractionDigits: digits}) : "—";
};

// Arma contenido compacto y adaptable; title encabeza, rows contiene name/shortName/value/color/detail y notes da contexto.
chartTooltip.wrap = function (title, rows, notes = []) {
    if (!rows.length) return "";
    const escape = chartTooltip.escape;
    return `<div class="meterTooltip${rows.length > 4 ? " isMany" : ""}"><div class="meterTooltipTitle">${escape(title)}</div>` +
        '<div class="meterTooltipRows">' + rows.map(row => {
            const color = /^(?:#[\da-f]{3,8}|rgba\(\d{1,3},\d{1,3},\d{1,3},(?:0(?:\.\d+)?|1)\))$/i.test(row.color || "") ? row.color : "#555";
            return `<div class="meterTooltipRow"><span class="meterTooltipName" style="border-color:${color}"><span class="meterTooltipFull">${escape(row.name)}</span>` +
                `<span class="meterTooltipShort">${escape(row.shortName || row.name)}</span></span><strong>${escape(row.value)}</strong>` +
                (row.detail ? `<small>${escape(row.detail)}</small>` : "") + '</div>';
        }).join("") + '</div>' + notes.filter(Boolean).map(note => `<div class="meterTooltipNote">${escape(note)}</div>`).join("") + '</div>';
};

// Describe fecha u hora local con segundos; value es un timestamp Unix en ms o Date.
chartTooltip.date = function (value) {
    const date = new Date(Number(value));
    return Number.isFinite(date.getTime()) ? date.toLocaleString("es-AR", {day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false}) : "—";
};

// Construye un tooltip compartido; event contiene entradas CanvasJS y config define titulo, tiempo, unidad y marcas cercanas.
chartTooltip.time = function (event, config = {}) {
    const entries = (event.entries || []).filter(entry => entry.dataSeries.visible !== false && Number.isFinite(entry.dataPoint.y));
    if (!entries.length) return "";
    const point = entries[0].dataPoint, x = Number(point.x);
    const when = config.mode === "date" ? chartTooltip.date(x) : config.mode === "category" ? point.label || "Intervalo" :
        `t=${x > 0 ? "+" : ""}${chartTooltip.number(x, config.digits ?? 2)} ms`;
    const rows = entries.map(({dataSeries: series, dataPoint: sample}) => {
        const unit = series.signalUnit ?? series.options?.signalUnit ?? config.unit ?? "";
        const value = sample.tooltipValue !== undefined ? sample.tooltipValue : sample.y;
        return {name: series.name, shortName: series.shortName ?? series.options?.shortName, color: series.color,
            value: sample.tooltipText ?? `${chartTooltip.number(value, config.valueDigits ?? (unit === "A" ? 3 : 2))}${unit ? " " + unit : ""}`};
    });
    const markers = typeof config.markers === "function" ? config.markers() : config.markers || [];
    const nearby = markers.filter(marker => Number.isFinite(marker.at) && Math.abs(marker.at - x) <= (config.tolerance ?? 15))
        .sort((a, b) => Math.abs(a.at - x) - Math.abs(b.at - x) || (a.priority || 0) - (b.priority || 0)).slice(0, 2);
    const notes = nearby.map(marker => "Marca: " + marker.label + (marker.durationMs > 0 ? ` · ${chartTooltip.number(marker.durationMs, 0)} ms` : ""));
    if (config.origin && Number.isFinite(x)) notes.push(chartTooltip.date(config.origin + x));
    return chartTooltip.wrap([config.title, when].filter(Boolean).join(" · "), rows, notes);
};

// Crea opciones CanvasJS compartidas; config selecciona contexto, unidades y referencia temporal, sin consultar al ESP.
chartTooltip.options = function (config = {}) {
    return {shared: true, animationEnabled: false, fontSize: 12, fontStyle: "normal", borderThickness: 1, cornerRadius: 4,
        contentFormatter: event => chartTooltip.time(event, config)};
};
