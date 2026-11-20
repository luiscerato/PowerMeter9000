const ScopeFft = {enabled: true, maxHz: 1000, channels: [], coherent: true, currentDiv: 1, voltageDiv: 100};
const scopeFftDivisions = [0.01, 0.1, 1, 2, 5, 10, 20, 50, 100];

const powerQuality = {active: false, quality: false, timer: 0, meter: null, meterAt: 0};

// Prepara la vista espectral reutilizando su grafico y la navegacion existente; sin parametros.
powerQuality.init = function () {
    const link = document.createElement("a"), scopeLink = document.getElementById("navscope");
    link.className = "nav-link app-page-link";
    link.href = "#qualityPage";
    link.dataset.page = "qualityPage";
    link.title = "Calidad de energía";
    link.innerHTML = scopeLink.querySelector("svg").outerHTML + '<span class="navLabel">Calidad de energía</span>';
    scopeLink.after(link);
    document.getElementById("qualitySpectrum").append(document.querySelector(".scopeFft"));
    document.querySelector(".scopeFftToolbar").append(document.getElementById("qualityRanges"));
    document.querySelector(".scopeFft").append(document.getElementById("qualityClipping"));
    document.getElementById("qualityVoltageControls").append(document.getElementById("scopeFftVoltage"));
    document.getElementById("qualityCurrentControls").append(document.getElementById("scopeFftCurrent"));
    const toolbar = document.getElementById("qualityChannels");
    toolbar.innerHTML = [["R", 3], ["S", 12], ["T", 48], ["N", 64], ["Voltaje", 42], ["Corriente", 85], ["Todas", 127]]
        .map(([label, mask]) => `<button class="btn btn-sm btn-outline-primary" data-mask="${mask}" type="button">${label}</button>`).join("") +
        Scope.Data.waves.map((wave, index) => `<button class="btn btn-sm btn-outline-secondary" type="button" data-channel="${index}" title="${wave.name}">${wave.unit}${wave.id.slice(1).toUpperCase()}</button>`).join("");
    toolbar.onclick = event => {
        const button = event.target.closest("button");
        if (!button || !Scope.Data.configSynced) return;
        if (button.dataset.channel !== undefined) Scope.toggleWave(Number(button.dataset.channel));
        else {
            const mask = Number(button.dataset.mask), options = {};
            ["corrR", "voltR", "corrS", "voltS", "corrT", "voltT", "corrN"].forEach((key, index) => { options[key] = Boolean(mask & (1 << index)); });
            Scope.setOptions(options);
        }
        powerQuality.syncControls();
    };
    const main = document.getElementById("mainContent"), footer = document.querySelector("#divFooter .fixed-bottom");
    if (main && footer && typeof ResizeObserver !== "undefined") {
        powerQuality.footerObserver = new ResizeObserver(() => main.style.setProperty("--qualityFooterHeight", footer.getBoundingClientRect().height + "px"));
        powerQuality.footerObserver.observe(footer);
    }
};

// Calcula el pico de captura para cinco divisiones RMS y 10% de margen senoidal; division es V/A por division.
powerQuality.capturePeak = function (division) {
    return division * 5 * Math.SQRT2 * 1.1;
};

// Publica rangos automaticos segun las escalas FFT; mask selecciona canales, sin cambiar el osciloscopio guardado.
powerQuality.sendSettings = function (mask) {
    if (Scope.view !== "quality" || !Scope.Data.configSynced || !Scope.websocket || Scope.websocket.readyState !== WebSocket.OPEN) return;
    const voltage = powerQuality.capturePeak(ScopeFft.voltageDiv), current = powerQuality.capturePeak(ScopeFft.currentDiv);
    if (![voltage, current].every(value => Number.isFinite(value) && value >= 0.05 && value <= 1000)) return;
    Scope.websocket.send(`quality_set,${voltage},${current},${mask}`);
};

// Selecciona el consumidor sin duplicar conexiones; active habilita medidas y quality selecciona el espectro.
powerQuality.setActive = function (active, quality) {
    powerQuality.active = active;
    powerQuality.quality = quality;
    powerQuality.autoRangeReady = false;
    Scope.view = quality ? "quality" : "scope";
    document.getElementById("mainContent")?.classList.toggle("qualityLayout", active && quality);
    clearTimeout(powerQuality.timer);
    if (powerQuality.controller) powerQuality.controller.abort();
    powerQuality.controller = null;
    powerQuality.meter = null;
    powerQuality.renderMeter();
    if (active) powerQuality.timer = setTimeout(powerQuality.pollMeter, 300);
};

// Refleja la seleccion compartida y el estado de conexion en los accesos rapidos; sin parametros.
powerQuality.syncControls = function () {
    const toolbar = document.getElementById("qualityChannels");
    if (!toolbar) return;
    const mask = ScopeFft.getMask();
    toolbar.querySelectorAll("button").forEach(button => {
        const selected = button.dataset.channel !== undefined ? Boolean(mask & (1 << Number(button.dataset.channel))) : mask === Number(button.dataset.mask);
        button.classList.toggle("active", selected);
        button.setAttribute("aria-pressed", String(selected));
        button.disabled = !Scope.Data.configSynced;
    });
    if (powerQuality.active && powerQuality.quality && Scope.Data.configSynced && Scope.qualityLocked && !powerQuality.autoRangeReady) {
        powerQuality.autoRangeReady = true;
        powerQuality.sendSettings(mask);
    }
    powerQuality.renderMeter();
    ScopeFft.draw();
};

// Consulta la medicion en RAM de forma secuencial mientras se usa una vista; sin parametros.
powerQuality.pollMeter = async function () {
    if (!powerQuality.active || powerQuality.controller) return;
    if (!Scope.shouldReconnect) {
        powerQuality.timer = setTimeout(powerQuality.pollMeter, 1000);
        powerQuality.renderMeter();
        return;
    }
    const controller = new AbortController();
    powerQuality.controller = controller;
    const timeout = setTimeout(() => controller.abort(), 5000);
    try {
        const response = await fetch(board.IP + "/meter", {cache: "no-store", signal: controller.signal});
        if (!response.ok) throw new Error("HTTP " + response.status);
        const meter = await response.json();
        if (!controller.signal.aborted && powerQuality.active) {
            powerQuality.meter = meter;
            powerQuality.meterAt = Date.now();
        }
        if (!controller.signal.aborted && powerQuality.active && powerQuality.quality) {
            try {
                const thdResponse = await fetch(board.IP + "/meter?thd", {cache: "no-store", signal: controller.signal});
                if (!thdResponse.ok) throw new Error("HTTP " + thdResponse.status);
                const thd = await thdResponse.json();
                if (!controller.signal.aborted && powerQuality.controller === controller) {
                    meter.voltageTHD = thd.voltageTHD;
                    meter.currentTHD = thd.currentTHD;
                }
            } catch (_) { /* La referencia RMS sigue siendo valida si falla solo la consulta THD. */ }
        }
    } catch (_) { if (powerQuality.controller === controller) powerQuality.meter = null; }
    finally {
        clearTimeout(timeout);
        if (powerQuality.controller !== controller) return;
        powerQuality.controller = null;
        powerQuality.renderMeter();
        powerQuality.renderAnalysis();
        if (powerQuality.active) powerQuality.timer = setTimeout(powerQuality.pollMeter, 2000);
    }
};

// Formatea numeros finitos sin convertir null en una medicion cero; value es el numero y digits sus decimales.
powerQuality.format = function (value, digits = 2) {
    return typeof value === "number" && Number.isFinite(value) ? value.toFixed(digits) : "—";
};

// Devuelve solo la ultima lectura vigente del equipo; sin parametros.
powerQuality.getMeter = function () {
    return Scope.shouldReconnect && Date.now() - powerQuality.meterAt < 6000 ? powerQuality.meter : null;
};

// Dibuja la misma referencia en ambas vistas sin consultas adicionales; sin parametros.
powerQuality.renderMeter = function () {
    const meter = powerQuality.getMeter(), format = powerQuality.format;
    for (const prefix of ["scope", "quality"]) {
        const rows = ["r", "s", "t", "n"].map((phase, index) => {
            const visible = index === 3 ? Scope.Data.waves[6].visible : Scope.Data.waves[index * 2].visible || Scope.Data.waves[index * 2 + 1].visible;
            if (!visible) return "";
            const thd = prefix === "quality" ? `<td>${format(index < 3 ? meter?.voltageTHD?.[phase] : null)} %</td><td>${format(index < 3 ? meter?.currentTHD?.[phase] : null)} %</td>` : "";
            return `<tr><th>${phase.toUpperCase()}${index === 3 ? " (V: BL)" : ""}</th><td>${format(meter?.vrms?.[phase])} V</td><td>${format(meter?.irms?.[phase], 3)} A</td><td>${format(meter?.watt?.[phase], 1)} W</td><td>${format(meter?.pf?.[phase], 3)}</td>${thd}</tr>`;
        }).join("");
        const target = document.getElementById(prefix + "AdeRows"), status = document.getElementById(prefix + "AdeStatus");
        if (target) target.innerHTML = rows || `<tr><td colspan="${prefix === "quality" ? 7 : 5}">Sin canales seleccionados</td></tr>`;
        if (status) status.textContent = meter ? `${format(meter.freq)} Hz · actualización cada 2 s` : "Sin lectura vigente";
    }
};

// Resume amplitudes coherentes H1-H40 sin confundir ruido con armonicos; record es una captura fisica y unit V/A.
powerQuality.summarize = function (record, unit) {
    const samples = record.samples, analysis = record.analysis;
    const rms = Number.isFinite(analysis.rms) ? analysis.rms : ScopeFft.acRms(samples);
    const invalid = record.clipped ? "Saturada" : !analysis.cycles ? "Sin sincronismo" : "";
    if (invalid) return {rms, valid: false, reason: invalid, harmonics: []};
    const harmonics = Array.from({length: 40}, (_, index) => {
        const order = index + 1, point = analysis.points[analysis.cycles * order];
        return {order, hz: order * analysis.frequency, rms: point ? point.y : NaN};
    });
    const fundamental = harmonics[0].rms;
    if (!(fundamental > Math.max(unit === "V" ? 0.1 : 0.001, (record.step || 0) * 4)))
        return {rms, valid: false, reason: "Fundamental pequeña", harmonics: []};
    if (harmonics.some(harmonic => !Number.isFinite(harmonic.rms))) return {rms, valid: false, reason: "Banda insuficiente", harmonics: []};
    const harmonicRms = Math.sqrt(harmonics.slice(1).reduce((sum, harmonic) => sum + harmonic.rms ** 2, 0));
    const dominant = harmonics.slice(1).reduce((peak, harmonic) => harmonic.rms > peak.rms ? harmonic : peak);
    harmonics.forEach(harmonic => { harmonic.percent = 100 * harmonic.rms / fundamental; });
    return {rms, fundamental, harmonicRms, thd: 100 * harmonicRms / fundamental, dominant, harmonics, valid: true, reason: "Estimada"};
};

// Actualiza calculos y el detalle expandible de la captura activa; sin parametros.
powerQuality.renderAnalysis = function () {
    const target = document.getElementById("qualityAnalysisRows");
    if (!target || !powerQuality.quality) return;
    const format = powerQuality.format;
    const active = Scope.Data.waves.map((wave, index) => ({wave, index, record: ScopeFft.channels[index]})).filter(item => item.wave.visible);
    target.innerHTML = active.map(({wave, index, record}) => {
        const fresh = record?.analysis && Date.now() - record.receivedAt < 6000 && Scope.shouldReconnect;
        const summary = fresh ? powerQuality.summarize(record, wave.unit) : null;
        return `<tr><th>${wave.name}</th><td>${format(summary?.rms)} ${wave.unit}</td>` +
            `<td>${format(summary?.fundamental)}</td><td>${format(summary?.harmonicRms)}</td><td>${format(summary?.thd)} %</td>` +
            `<td>${summary?.valid ? "H" + summary.dominant.order + " · " + format(summary.dominant.percent) + " %" : "—"}</td>` +
            `<td>${format(fresh ? record.analysis.frequency || null : null)}</td><td>${summary?.reason || "Sin captura vigente"}</td></tr>`;
    }).join("") || '<tr><td colspan="8">Sin canales seleccionados</td></tr>';
    powerQuality.renderHarmonics(active);
};

// Agrupa RMS y porcentaje de todos los canales activos; active contiene señal y captura, sin modificar sus calculos.
powerQuality.renderHarmonics = function (active) {
    const head = document.getElementById("qualityHarmonicHead"), rows = document.getElementById("qualityHarmonicRows");
    if (!head || !rows) return;
    const format = powerQuality.format;
    const signals = active.map(item => ({...item, summary: item.record?.analysis &&
        Date.now() - item.record.receivedAt < 6000 && Scope.shouldReconnect ? powerQuality.summarize(item.record, item.wave.unit) : null}));
    const reference = signals.find(item => item.wave.unit === "V" && item.summary?.valid) || signals.find(item => item.summary?.valid);
    const frequency = reference?.record.analysis.frequency;
    head.innerHTML = '<tr><th rowspan="2" scope="col">Orden</th><th rowspan="2" scope="col">Hz</th>' +
        signals.map(({wave}) => `<th colspan="2" scope="colgroup" style="border-bottom:2px solid ${wave.color}" title="${wave.name}">${wave.id.toUpperCase()} (${wave.unit})</th>`).join("") + '</tr><tr>' +
        signals.map(() => '<th scope="col">RMS</th><th scope="col">% H1</th>').join("") + '</tr>';
    rows.innerHTML = signals.length ? Array.from({length: 40}, (_, index) => {
        const order = index + 1;
        return `<tr><th scope="row">H${order}</th><td>${format(frequency ? order * frequency : null)}</td>` + signals.map(({summary}) => {
            const harmonic = summary?.valid ? summary.harmonics[index] : null;
            return `<td>${format(harmonic?.rms, 3)}</td><td>${format(harmonic?.percent)}</td>`;
        }).join("") + '</tr>';
    }).join("") : '<tr><td colspan="2">Sin canales seleccionados</td></tr>';
    const status = document.getElementById("qualityHarmonicStatus");
    if (status) {
        status.textContent = frequency ? `${format(frequency)} Hz · H1–H40` : "Sin estimación vigente";
        status.title = reference ? "Frecuencia de referencia: " + reference.wave.name : "";
    }
};

// Inicializa el espectro y sus controles; container es el id del grafico debajo del osciloscopio.
ScopeFft.begin = function (container) {
    if (!document.getElementById(container)) return;
    if (ScopeFft.chart) ScopeFft.chart.destroy();
    ScopeFft.chart = new CanvasJS.Chart(container, {
        animationEnabled: false, zoomEnabled: true, zoomType: "xy",
        axisX: {title: "Frecuencia (Hz)", titleFontSize: 14, labelFontSize: 11, minimum: 0, maximum: ScopeFft.maxHz},
        axisY: {title: "Corriente (A RMS)", titleFontSize: 14, labelFontSize: 11, minimum: 0},
        axisY2: {title: "Voltaje (V RMS)", titleFontSize: 14, labelFontSize: 11, minimum: 0},
        legend: {fontSize: 11, verticalAlign: "bottom", cursor: "pointer", itemclick: ScopeFft.toggleChannel},
        toolTip: {...chartTooltip.options(), contentFormatter: ScopeFft.tooltip}, data: []
    });
    ScopeFft.coherent = true;
    const range = document.getElementById("scopeFftRange");
    range.value = String(ScopeFft.maxHz);
    range.onchange = () => {
        ScopeFft.maxHz = Number(range.value);
        ScopeFft.chart.options.axisX.viewportMinimum = null;
        ScopeFft.chart.options.axisX.viewportMaximum = null;
        ScopeFft.draw();
    };
    ScopeFft.loadScales();
    for (const [axis, unit] of [["current", "A"], ["voltage", "V"]]) {
        const select = document.getElementById("scopeFft" + (axis === "current" ? "Current" : "Voltage"));
        select.innerHTML = scopeFftDivisions.map(value => `<option value="${value}">${value} ${unit}/div</option>`).join("");
        select.value = String(ScopeFft[axis + "Div"]);
        select.onchange = () => ScopeFft.setScale(axis, Number(select.value));
    }
    document.getElementById("scopeFftReset").onclick = ScopeFft.resetZoom;
    document.getElementById("scopeFftExpand").onclick = () => ScopeFft.setExpanded(!ScopeFft.expanded);
    document.removeEventListener("keydown", ScopeFft.onKeyDown);
    document.addEventListener("keydown", ScopeFft.onKeyDown);
    ScopeFft.channels = [];
    ScopeFft.pending = null;
    ScopeFft.captureNote = "";
    ScopeFft.scopeSignature = null;
    ScopeFft.syncScope();
};

// Recupera escalas del navegador; sin parametros, valores anteriores no admitidos usan los predeterminados.
ScopeFft.loadScales = function () {
    ScopeFft.currentDiv = 1;
    ScopeFft.voltageDiv = 100;
    try {
        const saved = JSON.parse(localStorage.getItem("scopeFftScales") || "{}");
        for (const axis of ["current", "voltage"]) {
            if (saved && scopeFftDivisions.includes(saved[axis])) ScopeFft[axis + "Div"] = saved[axis];
        }
    } catch (_) { /* La vista sigue disponible aunque el navegador bloquee el almacenamiento. */ }
};

// Fija la escala visual y el rango de captura automatico; axis es current/voltage y value unidades RMS por division.
ScopeFft.setScale = function (axis, value) {
    if (!["current", "voltage"].includes(axis) || !scopeFftDivisions.includes(value)) return;
    ScopeFft[axis + "Div"] = value;
    if (ScopeFft.chart) {
        const options = ScopeFft.chart.options[axis === "current" ? "axisY" : "axisY2"];
        options.viewportMinimum = options.viewportMaximum = null;
    }
    try {
        localStorage.setItem("scopeFftScales", JSON.stringify({current: ScopeFft.currentDiv, voltage: ScopeFft.voltageDiv}));
    } catch (_) { /* No se requiere almacenamiento local para ajustar la escala. */ }
    ScopeFft.draw();
    powerQuality.sendSettings(ScopeFft.getMask());
};

// Amplia la grafica dentro de la pagina, incluso en HTTP o moviles sin Fullscreen API; expanded indica si debe ocupar la pantalla.
ScopeFft.setExpanded = function (expanded) {
    const panel = document.querySelector(".scopeFft"), button = document.getElementById("scopeFftExpand");
    if (!panel || !button) return;
    expanded = Boolean(expanded);
    if (expanded && !ScopeFft.expanded) {
        ScopeFft.previousOverflow = document.body.style.overflow;
        ScopeFft.previousFocus = document.activeElement;
    }
    if (!expanded && ScopeFft.expanded) document.body.style.overflow = ScopeFft.previousOverflow;
    if (expanded) document.body.style.overflow = "hidden";
    ScopeFft.expanded = expanded;
    panel.classList.toggle("isExpanded", expanded);
    button.setAttribute("aria-pressed", String(expanded));
    button.title = expanded ? "Reducir FFT (Esc)" : "Ampliar FFT";
    button.setAttribute("aria-label", button.title);
    if (!expanded && ScopeFft.previousFocus) ScopeFft.previousFocus.focus();
    requestAnimationFrame(() => { if (ScopeFft.chart) ScopeFft.chart.render(); });
};

// Sale de la vista ampliada con Escape; event es la pulsacion del teclado.
ScopeFft.onKeyDown = function (event) {
    if (event.key === "Escape" && ScopeFft.expanded) {
        event.preventDefault();
        ScopeFft.setExpanded(false);
    }
};

// Comparte la seleccion al pulsar una leyenda FFT; event es el clic de CanvasJS sobre una serie.
ScopeFft.toggleChannel = function (event) {
    Scope.toggleWave(Scope.Data.waves.findIndex(wave => wave.name === event.dataSeries.name));
};

// Refleja el estado del ESP y mantiene FFT activa mientras se usa Calidad; enabled es el flag 0/1 recibido.
ScopeFft.setEnabled = function (enabled) {
    if (enabled !== 0 && enabled !== 1) return;
    if (!enabled && powerQuality.active && powerQuality.quality) {
        if (Scope.websocket?.readyState === WebSocket.OPEN && Scope.Data.configSynced) Scope.websocket.send("scope_fft_on,1");
        enabled = 1;
    }
    ScopeFft.enabled = Boolean(enabled);
    if (!enabled) ScopeFft.channels = [];
    ScopeFft.draw();
};

// Describe los puntos del tooltip compartido; event contiene las entradas CanvasJS de las series visibles.
ScopeFft.tooltip = function (event) {
    const entries = (event.entries || []).filter(entry => entry.dataSeries.visible !== false);
    if (!entries.length) return "";
    const format = (value, digits = 2) => powerQuality.format(value, digits).replace(".", ",");
    const rows = entries.map(entry => {
        const index = Scope.Data.waves.findIndex(wave => wave.name === entry.dataSeries.name);
        const wave = Scope.Data.waves[index], record = ScopeFft.channels[index];
        if (!wave || !record?.analysis || !Number.isFinite(entry.dataPoint.y)) return null;
        const analysis = record.analysis, point = entry.dataPoint;
        const order = analysis.frequency ? Math.round(point.x / analysis.frequency) : 0;
        const harmonic = order > 0 && Math.abs(point.x - order * analysis.frequency) <= analysis.resolution / 2 + 1e-6;
        const label = point.x === 0 ? "Continua eliminada" : harmonic ? "H" + order : "Entre armonicos";
        const summary = powerQuality.summarize(record, wave.unit);
        const percent = summary.valid ? 100 * point.y / summary.fundamental : null;
        return {name: wave.name + " · " + label, shortName: (wave.unit === "V" ? "V " : "I ") + wave.id.slice(1).toUpperCase() + " · " + label,
            color: wave.color, value: `${format(point.y, 3)} ${wave.unit} RMS`,
            detail: `${format(percent)} % H1 · THD-F ${format(summary.thd)} %${summary.valid ? "" : " · " + summary.reason}`};
    }).filter(Boolean);
    return chartTooltip.wrap(`${format(entries[0].dataPoint.x)} Hz`, rows, ["THD-F: H2–H40"]);
};

// Devuelve la mascara de los canales visibles del osciloscopio; sin parametros.
ScopeFft.getMask = function () {
    return Scope.Data.waves.reduce((mask, wave, index) => mask | (wave.visible ? 1 << index : 0), 0);
};

// Quita el zoom manual manteniendo las escalas FFT seleccionadas; sin parametros.
ScopeFft.resetZoom = function () {
    if (!ScopeFft.chart) return;
    for (const name of ["axisX", "axisY", "axisY2"]) {
        ScopeFft.chart.options[name].viewportMinimum = null;
        ScopeFft.chart.options[name].viewportMaximum = null;
    }
    ScopeFft.draw();
};

// Refleja cambios de seleccion o escala sin redibujar cada trama temporal; sin parametros.
ScopeFft.syncScope = function () {
    const scales = Scope.Data.Scale;
    const signature = `${ScopeFft.getMask()},${scales.Current},${scales.Voltage}`;
    if (signature === ScopeFft.scopeSignature) return;
    if (ScopeFft.scopeSignature && ScopeFft.scopeSignature.split(",").slice(1).join(",") !== `${scales.Current},${scales.Voltage}`) {
        for (const [name, scale] of [["axisY", "currentDiv"], ["axisY2", "voltageDiv"]]) {
            if (ScopeFft.chart && !ScopeFft[scale]) {
                ScopeFft.chart.options[name].viewportMinimum = null;
                ScopeFft.chart.options[name].viewportMaximum = null;
            }
        }
    }
    ScopeFft.scopeSignature = signature;
    ScopeFft.draw();
};

// Calcula amplitudes RMS sin continua; samples son valores fisicos, rate su frecuencia y hann habilita ventana Hann.
ScopeFft.calculate = function (samples, rate, hann = true) {
    const size = samples.length;
    if (!ScopeFft.transform || ScopeFft.transform.size !== size) {
        ScopeFft.transform = new FFT(size);
        ScopeFft.input = new Float64Array(size);
        ScopeFft.output = ScopeFft.transform.createComplexArray();
    }
    const mean = samples.reduce((sum, value) => sum + value, 0) / size;
    let weightSum = 0;
    for (let index = 0; index < size; ++index) {
        const weight = hann ? 0.5 - 0.5 * Math.cos(2 * Math.PI * index / size) : 1;
        ScopeFft.input[index] = (samples[index] - mean) * weight;
        weightSum += weight;
    }
    ScopeFft.transform.realTransform(ScopeFft.output, ScopeFft.input);
    return Array.from({length: size / 2 + 1}, (_, bin) => ({
        x: bin * rate / size,
        y: Math.hypot(ScopeFft.output[bin * 2], ScopeFft.output[bin * 2 + 1]) *
            (bin > 0 && bin < size / 2 ? Math.SQRT2 : 1) / weightSum
    }));
};

// Interpola para comparar periodos sin introducir cruces falsos; samples es la onda y position su indice fraccional interior.
ScopeFft.interpolate = function (samples, position) {
    const index = Math.floor(position), t = position - index;
    const a = samples[index - 1], b = samples[index], c = samples[index + 1], d = samples[index + 2];
    return b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
};

// Estima la frecuencia por repeticion de la onda; samples/rate describen la captura y reference es la frecuencia ADE o cero.
ScopeFft.findFrequency = function (samples, rate, reference) {
    const mean = samples.reduce((sum, value) => sum + value, 0) / samples.length;
    const energy = samples.reduce((sum, value) => sum + (value - mean) ** 2, 0) / samples.length;
    if (energy < 1e-16) return 0;
    if (!Number.isFinite(reference) || reference < 40 || reference > 70) {
        const spectrum = ScopeFft.calculate(samples, rate);
        let peak = 0;
        for (let bin = 1; bin < spectrum.length - 1; ++bin) {
            if (spectrum[bin].x >= 40 && spectrum[bin].x <= 70 && spectrum[bin].y > spectrum[peak].y) peak = bin;
        }
        if (!peak || spectrum[peak].y < Math.sqrt(energy) * 0.05) return 0;
        const left = Math.log(Math.max(1e-20, spectrum[peak - 1].y));
        const center = Math.log(Math.max(1e-20, spectrum[peak].y));
        const right = Math.log(Math.max(1e-20, spectrum[peak + 1].y));
        const curvature = left - 2 * center + right;
        const offset = curvature ? Math.max(-0.5, Math.min(0.5, 0.5 * (left - right) / curvature)) : 0;
        reference = (peak + offset) * rate / samples.length;
    }
    // Mide coincidencia a un periodo de distancia; hz es la frecuencia candidata.
    const error = (hz) => {
        const period = rate / hz;
        let difference = 0, total = 0;
        for (let index = 1; index < samples.length - Math.ceil(period) - 2; ++index) {
            const a = samples[index] - mean, b = ScopeFft.interpolate(samples, index + period) - mean;
            difference += (a - b) ** 2;
            total += a * a + b * b;
        }
        const compared = samples.length - Math.ceil(period) - 3;
        return total > energy * compared * 0.2 ? difference / total : Infinity;
    };
    let low = Math.max(40, reference - 2), high = Math.min(70, reference + 2);
    for (let step = 0; step < 28; ++step) {
        const a = low + (high - low) / 3, b = high - (high - low) / 3;
        if (error(a) < error(b)) high = b;
        else low = a;
    }
    const frequency = (low + high) / 2;
    return error(frequency) < 0.01 ? frequency : 0;
};

// Remuestrea ciclos completos con sinc Lanczos sin extrapolar; samples/rate son la captura y frequency el fundamental validado.
ScopeFft.resample = function (samples, rate, frequency) {
    const radius = 8, size = samples.length;
    const cycles = size === 2048 ? (frequency < 55 ? 10 : 12) : Math.floor((size - 1 - 2 * radius) * frequency / rate);
    if (cycles < 2) return null;
    const span = cycles * rate / frequency;
    if (span > size - 1 - 2 * radius) return null;
    const first = (size - 1 - span) / 2;
    const output = new Float64Array(size);
    for (let index = 0; index < size; ++index) {
        const position = first + index * span / size;
        const start = Math.floor(position) - radius + 1;
        let sum = 0, weightSum = 0;
        for (let tap = start; tap < start + 2 * radius; ++tap) {
            const distance = position - tap;
            const angle = Math.PI * distance;
            const weight = Math.abs(distance) < 1e-12 ? 1 : Math.sin(angle) / angle * Math.sin(angle / radius) / (angle / radius);
            sum += samples[tap] * weight;
            weightSum += weight;
        }
        output[index] = sum / weightSum;
    }
    return {samples: output, rate: size * frequency / cycles, cycles, frequency};
};

// Genera el espectro sincronizado o Hann si la onda no es periodica; samples/rate son la captura y reference la frecuencia ADE.
ScopeFft.analyze = function (samples, rate, reference = 0) {
    const frequency = ScopeFft.coherent ? ScopeFft.findFrequency(samples, rate, reference) : 0;
    const coherent = frequency ? ScopeFft.resample(samples, rate, frequency) : null;
    const points = coherent ? ScopeFft.calculate(coherent.samples, coherent.rate, false).filter(point => point.x <= rate / 2) :
        ScopeFft.calculate(samples, rate);
    return {points, frequency: coherent ? frequency : 0, cycles: coherent ? coherent.cycles : 0,
        rms: ScopeFft.acRms(coherent ? coherent.samples : samples),
        windowMs: coherent ? 1000 * coherent.cycles / frequency : 1000 * samples.length / rate,
        resolution: coherent ? coherent.rate / samples.length : rate / samples.length};
};

// Calcula RMS de alterna de samples, sin componente continua y sin cambiar los datos originales.
ScopeFft.acRms = function (samples) {
    if (!samples.length) return NaN;
    const mean = samples.reduce((sum, value) => sum + value, 0) / samples.length;
    return Math.sqrt(samples.reduce((sum, value) => sum + (value - mean) ** 2, 0) / samples.length);
};

// Recalcula con los controles locales sin consultar al ESP; sin parametros.
ScopeFft.updateSpectrum = function () {
    ScopeFft.channels.forEach((record, index) => {
        if (record && Scope.Data.waves[index].visible) record.analysis = ScopeFft.analyze(record.samples, 8000, record.referenceHz);
    });
    ScopeFft.draw();
};

// Valida una captura SF v2 simultanea; data contiene cabecera y muestras intercaladas de los canales activos.
ScopeFft.receive = function (data) {
    if (data.length >= 3 && data[0] === 83 && data[1] === 70 && data[2] === 3) return ScopeFft.receivePart(data);
    if (data.length < 36 || data[0] !== 83 || data[1] !== 70 || data[2] !== 2) return false;
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const mask = data[3], count = view.getUint16(4, true), period = data[6], clipped = data[7];
    const voltageGain = view.getFloat32(8, true), currentGain = view.getFloat32(12, true);
    const selected = Scope.Data.waves.map((_, index) => index).filter(index => mask & (1 << index));
    if (!mask || mask > 127 || count !== 1024 || period !== 125 || (clipped & ~mask) ||
        !Number.isFinite(voltageGain) || voltageGain <= 0 || !Number.isFinite(currentGain) || currentGain <= 0 ||
        data.length !== 36 + count * selected.length * 3 / 2) return false;
    if (!ScopeFft.enabled || mask !== ScopeFft.getMask()) return true;
    const channels = [];
    selected.forEach(index => {
        channels[index] = {samples: new Float64Array(count), clipped: Boolean(clipped & (1 << index)), receivedAt: Date.now(),
            step: index % 2 ? voltageGain : currentGain,
            referenceHz: view.getFloat32(24 + (index < 6 ? Math.floor(index / 2) : 0) * 4, true)};
    });
    for (let index = 0; index < count * selected.length; ++index) {
        const offset = 36 + Math.floor(index * 3 / 2);
        let value = (data[offset] | (data[offset + 1] << 8)) >>> (index % 2 ? 4 : 0) & 4095;
        if (value & 2048) value -= 4096;
        const channel = selected[index % selected.length];
        channels[channel].samples[Math.floor(index / selected.length)] = value * (channel % 2 ? voltageGain : currentGain);
    }
    channels.forEach((record, index) => { ScopeFft.channels[index] = record; });
    ScopeFft.updateSpectrum();
    return true;
};

// Vence un armado incompleto aunque no llegue su cancelacion; sin parametros.
ScopeFft.expireCapture = function () {
    if (ScopeFft.pending && Date.now() - ScopeFft.pending.startedAt > 3000) {
        ScopeFft.dropCapture("Captura incompleta descartada");
    }
};

// Descarta el armado y actualiza ambos indicadores de recepcion; note describe el motivo al usuario.
ScopeFft.dropCapture = function (note) {
    ScopeFft.pending = null;
    ScopeFft.captureNote = note;
    const status = document.getElementById("qualityCaptureStatus");
    if (status) status.textContent = note;
    ScopeFft.draw();
};

// Reensambla SF v3 sin mezclar IDs, canales, factores o tiempos; data es un mensaje binario completo.
ScopeFft.receivePart = function (data) {
    if (data.length < 10) return false;
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength), id = view.getUint32(4, true);
    if (data[3] === 1) {
        if (data.length !== 10 || data[8] < 1 || data[8] > 4) return false;
        if (ScopeFft.pending?.id === id) {
            ScopeFft.dropCapture(["", "Captura descartada: hueco de adquisición", "Captura descartada: memoria", "Captura descartada: cola ocupada", "Captura descartada: cambio de ajustes"][data[8]]);
        }
        return true;
    }
    if (data[3] !== 0 || data.length < 52) return false;
    const part = data[8], mask = data[10], clipped = data[11], count = view.getUint16(12, true);
    const trigger = view.getUint16(16, true), voltageGain = view.getFloat32(28, true), currentGain = view.getFloat32(32, true);
    const generation = view.getUint32(48, true);
    const firstUs = view.getUint32(20, true) + view.getUint32(24, true) * 4294967296;
    const selected = Scope.Data.waves.map((_, index) => index).filter(index => mask & (1 << index));
    if (data[9] !== 4 || part > 3 || count !== 512 || view.getUint16(14, true) !== 125 || !mask || mask > 127 ||
        clipped & ~mask || (trigger !== 0xffff && (part !== 0 || trigger !== 256)) ||
        ![voltageGain, currentGain].every(value => Number.isFinite(value) && value >= 0.000024 && value <= 0.4884) ||
        data.length !== 52 + 768 * selected.length) return false;
    if (Scope.qualityGeneration !== undefined && generation !== Scope.qualityGeneration) return true;
    if (mask !== ScopeFft.getMask()) return true;
    ScopeFft.expireCapture();
    const signature = Array.from(data.subarray(28, 52)).join(",");
    const raw = part === 0 && Scope.view === "scope" ? Scope.Data.waves.map(() => []) : null;
    if (part === 0 && Scope.view !== "scope" && ScopeFft.enabled) {
        const channels = [];
        selected.forEach(index => {
            channels[index] = {samples: new Float64Array(2048), clipped: false, step: index % 2 ? voltageGain : currentGain,
                referenceHz: view.getFloat32(36 + (index < 6 ? Math.floor(index / 2) : 0) * 4, true)};
        });
        ScopeFft.pending = {id, generation, signature, firstUs, mask, next: 0, startedAt: Date.now(), channels};
    }
    const pending = ScopeFft.pending;
    if (Scope.view !== "scope" && (!pending || pending.id !== id || pending.next !== part || pending.mask !== mask ||
        pending.signature !== signature || firstUs !== pending.firstUs + part * 64000)) {
        ScopeFft.dropCapture("Fragmento discontinuo descartado");
        return true;
    }
    if (!raw && Scope.view === "scope") return true;
    for (let index = 0; index < count * selected.length; ++index) {
        const offset = 52 + Math.floor(index * 3 / 2);
        let value = (data[offset] | data[offset + 1] << 8) >>> (index % 2 ? 4 : 0) & 4095;
        if (value & 2048) value -= 4096;
        const channel = selected[index % selected.length], point = Math.floor(index / selected.length);
        if (raw) raw[channel][point] = value;
        else pending.channels[channel].samples[part * 512 + point] = value * (channel % 2 ? voltageGain : currentGain);
    }
    if (raw) Scope.showQualityFrame(raw, {trigger, clipped, voltageGain, currentGain});
    else {
        selected.forEach(index => { pending.channels[index].clipped ||= Boolean(clipped & 1 << index); });
        ++pending.next;
        if (part === 3) {
            pending.channels.forEach(record => { record.receivedAt = Date.now(); });
            pending.channels.forEach((record, index) => { ScopeFft.channels[index] = record; });
            ScopeFft.pending = null;
            ScopeFft.captureNote = "";
            ScopeFft.updateSpectrum();
        }
    }
    const status = document.getElementById("qualityCaptureStatus");
    if (status) status.textContent = part === 3 ? "Captura completa" : `Recibiendo ${part + 1}/4`;
    return true;
};

// Dibuja el ultimo espectro y su estado con la escala elegida; sin parametros.
ScopeFft.draw = function () {
    if (!ScopeFft.chart) return;
    if (Scope.view === "scope") return;
    const scales = Scope.Data.Scale;
    ScopeFft.chart.options.axisX.maximum = ScopeFft.maxHz;
    const currentDiv = ScopeFft.currentDiv || scales.Current, voltageDiv = ScopeFft.voltageDiv || scales.Voltage;
    Object.assign(ScopeFft.chart.options.axisY, {minimum: 0, maximum: currentDiv * 5, interval: currentDiv});
    Object.assign(ScopeFft.chart.options.axisY2, {minimum: 0, maximum: voltageDiv * 5, interval: voltageDiv});
    const active = ScopeFft.enabled ? Scope.Data.waves.map((wave, index) => ({wave, record: ScopeFft.channels[index]}))
        .filter(item => item.wave.visible && item.record && item.record.analysis) : [];
    const series = ScopeFft.chart.options.data ||= [];
    Scope.Data.waves.forEach((wave, index) => {
        const record = ScopeFft.channels[index];
        if (!series[index]) series[index] = {type: "line", color: wave.color,
            appPhase:wave.id.slice(1).toUpperCase(),appVariant:wave.unit === 'A' ? .22 : 0,signalUnit:wave.unit,
            name: wave.name, axisYType: wave.unit === "A" ? "primary" : "secondary",
            markerSize: 0, lineThickness: 1, xValueFormatString: "0.00",
            yValueFormatString: "0.000 '" + wave.unit + " RMS'", dataPoints: []};
        series[index].visible = Boolean(ScopeFft.enabled && wave.visible && record?.analysis);
        series[index].color = wave.color;
        series[index].showInLegend = series[index].visible;
        series[index].dataPoints = record?.analysis?.points || [];
    });
    ScopeFft.chart.render();
    const status = document.getElementById("scopeFftStatus");
    const synced = active.filter(item => item.record.analysis.cycles);
    const clipped = active.filter(item => item.record.clipped);
    const clipping = document.getElementById("qualityClipping");
    if (clipping) {
        clipping.classList.toggle("d-none", !clipped.length);
        clipping.textContent = clipped.length ? "Saturacion en " + clipped.map(item => item.wave.name).join(", ") +
            ": muestras recortadas; espectro y RMS pueden ser incorrectos. Aumente la escala por division. THD no valida." : "";
    }
    const detail = ScopeFft.coherent ? `${synced.length}/${active.length} sincronizados` : "Hann";
    const window = (synced[0] || active[0])?.record.analysis;
    const windowDetail = window?.windowMs ? ` · ${window.cycles ? window.cycles + " ciclos · " : ""}${window.windowMs.toFixed(1)} ms · Δf ${window.resolution.toFixed(2)} Hz` : "";
    if (status) {
        status.textContent = !ScopeFft.enabled ? "Desactivada" : !ScopeFft.getMask() ? "Sin canales seleccionados" : !active.length ?
            "Esperando muestras a 8 kHz..." : `${active[0].record.samples.length} muestras · 8 kHz · ${active.length} canales · ${detail}` + windowDetail + (clipped.length ? " · Señal saturada" : "");
        if (ScopeFft.captureNote) status.textContent += " · " + ScopeFft.captureNote;
        status.title = clipped.map(item => item.wave.name).join(", ");
    }
    if (typeof powerQuality !== "undefined") powerQuality.renderAnalysis();
};
