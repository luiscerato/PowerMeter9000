let gridWaveViewer = {event: null, captures: [], request: 0, chart: null};

// Descarta resultados de otro evento; event es el FAST y scope identifica su almacenamiento.
function resetGridWaves(event, scope = null) {
    ++gridWaveViewer.request;
    gridWaveViewer.event = event; gridWaveViewer.captures = [];
    gridWaveViewer.cacheScope = scope;
    gridWaveViewer.controller?.abort();
    if (gridWaveViewer.chart) { gridWaveViewer.chart.destroy(); gridWaveViewer.chart = null; }
    $("#gridWaveSelect").empty(); $("#gridWaveStatus").text("Sin oscilografia cargada.");
}

// Consulta las Wave confirmadas del FAST seleccionado; sin parametros y sin descargar bloques todavia.
async function loadGridWaves() {
    const event = gridWaveViewer.event;
    if (!event) return;
    const token = ++gridWaveViewer.request;
    gridWaveViewer.controller?.abort();
    const controller = new AbortController(); gridWaveViewer.controller = controller;
    $("#gridWaveStatus").text("Consultando oscilografias...");
    try {
        const scope = await refreshCaptureCacheScope(controller.signal);
        if (event.cacheIdentity && (!scope || event.cacheIdentity !== scope.deviceId + ":" + scope.fastEpoch))
            throw Error("El registro cambio. Vuelva a seleccionar la captura.");
        const result = await loadCachedWaveIndex(event, scope, controller.signal);
        if (token !== gridWaveViewer.request) return;
        gridWaveViewer.cacheScope = scope;
        gridWaveViewer.captures = result.captures || [];
        const select = $("#gridWaveSelect").empty();
        for (const capture of gridWaveViewer.captures) select.append($("<option>").val(capture.id).text("Wave #" + capture.id));
        if (!gridWaveViewer.captures.length) return $("#gridWaveStatus").text("Sin Wave confirmada. El cierre puede tardar unos segundos despues del FAST.");
        await loadSelectedGridWave();
    } catch (error) { if (token === gridWaveViewer.request) $("#gridWaveStatus").text(error.message); }
}

// Descarga un bloque fisico completo del selector actual; los cambios de seleccion invalidan respuestas viejas.
async function loadSelectedGridWave() {
    const capture = gridWaveViewer.captures.find(item => item.id === Number($("#gridWaveSelect").val()));
    if (!capture) return;
    const token = ++gridWaveViewer.request, event = gridWaveViewer.event;
    gridWaveViewer.controller?.abort();
    const controller = new AbortController(); gridWaveViewer.controller = controller;
    $("#gridWaveStatus").text("Descargando Wave #" + capture.id + "...");
    try {
        const wave = await loadCachedWave(event, capture, gridWaveViewer.cacheScope, controller.signal);
        if (token !== gridWaveViewer.request) return;
        renderGridWave(wave, event);
    } catch (error) { if (token === gridWaveViewer.request) $("#gridWaveStatus").text(error.message); }
}

// Calcula CRC32 IEEE de bytes; start/length delimitan el rango y zeroOffset anula cuatro bytes de CRC.
function waveCrc32(bytes, start, length, zeroOffset = -1) {
    let crc = -1;
    for (let at = start; at < start + length; ++at) {
        crc ^= zeroOffset >= 0 && at >= zeroOffset && at < zeroOffset + 4 ? 0 : bytes[at];
        for (let bit = 0; bit < 8; ++bit) crc = crc >>> 1 ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (~crc) >>> 0;
}

// Selecciona el lector por tamaño; buffer es la captura binaria y eventId su referencia FAST.
function decodeWaveEvent(buffer, eventId) {
    if (buffer.byteLength === 65536) return decodeCompactWaveEvent(buffer, eventId);
    if (buffer.byteLength === 131072) return decodeLegacyWaveEvent(buffer, eventId);
    throw Error("Longitud Wave incorrecta.");
}

// Decodifica una captura Wave v1 ya existente; buffer contiene 128 KiB y eventId el FAST enlazado.
function decodeLegacyWaveEvent(buffer, eventId) {
    const bytes = new Uint8Array(buffer), view = new DataView(buffer);
    if (bytes.length !== 131072) throw Error("Longitud Wave incorrecta.");
    const footer = 31 * 4096;
    // uint64 en microsegundos cabe exactamente en Number durante mas de 285 anos de uptime.
    const uint64 = at => view.getUint32(at, true) + view.getUint32(at + 4, true) * 4294967296;
    if (view.getUint32(footer, true) !== 0x31435657 || view.getUint8(footer + 24) !== 1 ||
        view.getUint8(footer + 25) !== 31 || view.getUint8(footer + 26) < 1 || view.getUint8(footer + 26) > 4 ||
        waveCrc32(bytes, footer, 108) !== view.getUint32(footer + 108, true)) throw Error("Wave sin cierre valido.");
    for (let at = 0; at < 112; ++at) if (bytes[footer + at] !== bytes[footer + 2048 + at]) throw Error("Cierre Wave incompleto.");
    const waveId = view.getUint32(footer + 4, true);
    let rootUs = null;
    for (let index = 0; index < view.getUint8(footer + 26); ++index) {
        const at = footer + 28 + index * 20;
        if (view.getUint32(at, true) === eventId) rootUs = uint64(at + 4);
    }
    if (rootUs === null) throw Error("La Wave no pertenece a este evento.");
    const keys = ["ir", "vr", "is", "vs", "it", "vt", "inn"];
    const wave = {id: waveId, rootUs, samples: [], invalidPackets: 0, flags: 0, triggerMs: (uint64(footer + 8) - rootUs) / 1000};
    let lastUs = null;
    for (let packet = 0; packet < 31; ++packet) {
        const at = packet * 4096;
        if (view.getUint32(at, true) !== 0x31564157 || view.getUint32(at + 4, true) !== waveId || view.getUint8(at + 56) !== 1 ||
            view.getUint8(at + 57) !== packet || view.getUint8(at + 58) !== 7 || view.getUint8(at + 59) !== 127 ||
            view.getUint16(at + 52, true) !== 8000 || view.getUint16(at + 54, true) !== 384 ||
            waveCrc32(bytes, at, 64, at + 48) !== view.getUint32(at + 48, true) ||
            waveCrc32(bytes, at + 64, 4032) !== view.getUint32(at + 44, true)) { ++wave.invalidPackets; continue; }
        wave.flags |= view.getUint16(at + 60, true);
        const scales = [view.getFloat32(at + 40, true), view.getFloat32(at + 36, true)];
        if (!(scales[0] > 0) || !(scales[1] > 0) || !scales.every(Number.isFinite)) { ++wave.invalidPackets; continue; }
        for (let slice = 0; slice < 3; ++slice) {
            const firstUs = uint64(at + 8) + (slice ? view.getInt32(at + 16 + (slice - 1) * 4, true) : 0);
            if (lastUs !== null && firstUs <= lastUs) { wave.flags |= 2; continue; }
            if (lastUs !== null && firstUs - lastUs > 250) {
                wave.samples.push({relativeMs: (lastUs + 125 - rootUs) / 1000, ir: null, vr: null, is: null, vs: null, it: null, vt: null, inn: null});
            }
            for (let sample = 0; sample < 128; ++sample) {
                const point = {relativeMs: (firstUs + sample * 125 - rootUs) / 1000};
                for (let channel = 0; channel < 7; ++channel) {
                    const rawIndex = sample * 7 + channel, offset = at + 64 + slice * 1344 + Math.floor(rawIndex / 2) * 3;
                    let raw = rawIndex % 2 ? bytes[offset + 1] >> 4 | bytes[offset + 2] << 4 : bytes[offset] | (bytes[offset + 1] & 15) << 8;
                    if (raw & 2048) raw -= 4096;
                    point[keys[channel]] = raw * scales[channel % 2];
                }
                wave.samples.push(point);
            }
            lastUs = firstUs + 127 * 125;
        }
    }
    return wave;
}

// Decodifica 31 paginas v2; conserva muestras signed12 y las escalas historicas.
function decodeCompactWaveEvent(buffer, eventId) {
    const bytes = new Uint8Array(buffer), view = new DataView(buffer), footer = 31 * 2048;
    const uint64 = at => view.getUint32(at, true) + view.getUint32(at + 4, true) * 4294967296;
    if (view.getUint32(footer, true) !== 0x31435657 || view.getUint8(footer + 24) !== 2 ||
        view.getUint8(footer + 25) !== 31 || view.getUint8(footer + 26) < 1 || view.getUint8(footer + 26) > 4 ||
        waveCrc32(bytes, footer, 108) !== view.getUint32(footer + 108, true)) throw Error("Wave sin cierre valido.");
    const waveId = view.getUint32(footer + 4, true);
    let rootUs = null;
    for (let index = 0; index < view.getUint8(footer + 26); ++index) {
        const at = footer + 28 + index * 20;
        if (view.getUint32(at, true) === eventId) rootUs = uint64(at + 4);
    }
    if (rootUs === null) throw Error("La Wave no pertenece a este evento.");
    const keys = ["ir", "vr", "is", "vs", "it", "vt", "inn"];
    const wave = {id: waveId, rootUs, samples: [], invalidPackets: 0, flags: 0,
        triggerMs: (uint64(footer + 8) - rootUs) / 1000};
    let lastUs = null;
    for (let packet = 0; packet < 31; ++packet) {
        const at = packet * 2048;
        if (view.getUint32(at, true) !== 0x32564157 || view.getUint32(at + 4, true) !== waveId ||
            view.getUint8(at + 28) !== 2 || view.getUint8(at + 29) !== packet || view.getUint16(at + 30, true) !== 192 ||
            waveCrc32(bytes, at, 32, at + 24) !== view.getUint32(at + 24, true) ||
            waveCrc32(bytes, at + 32, 2016) !== view.getUint32(at + 20, true)) {
            ++wave.invalidPackets; continue;
        }
        wave.flags |= view.getUint16(at + 18, true);
        const firstUs = uint64(at + 8);
        if (lastUs !== null && firstUs <= lastUs) { wave.flags |= 2; continue; }
        if (lastUs !== null && firstUs - lastUs > 250)
            wave.samples.push({relativeMs: (lastUs + 125 - rootUs) / 1000, ir: null, vr: null, is: null, vs: null, it: null, vt: null, inn: null});
        for (let sample = 0; sample < 192; ++sample) {
            const point = {relativeMs: (firstUs + sample * 125 - rootUs) / 1000};
            for (let channel = 0; channel < 7; ++channel) {
                const rawIndex = sample * 7 + channel, offset = at + 32 + Math.floor(rawIndex / 2) * 3;
                let raw = rawIndex % 2 ? bytes[offset + 1] >> 4 | bytes[offset + 2] << 4 : bytes[offset] | (bytes[offset + 1] & 15) << 8;
                if (raw & 2048) raw -= 4096;
                point[keys[channel]] = raw * (channel % 2 ? 0.25 : 0.1);
            }
            wave.samples.push(point);
        }
        lastUs = firstUs + 191 * 125;
    }
    return wave;
}

// Grafica wave con referencia temporal FAST de event; las corrientes usan un eje independiente.
function renderGridWave(wave, event) {
    if (gridWaveViewer.chart) gridWaveViewer.chart.destroy();
    const definitions = [["R", "vr", "#d9534f"], ["S", "vs", "#5cb85c"], ["T", "vt", "#337ab7"],
        ["IR", "ir", "#d9534f"], ["IS", "is", "#5cb85c"], ["IT", "it", "#337ab7"], ["IN", "inn", "#6c757d"]];
    const data = definitions.map(([name, key, color], index) => ({type: "line", name, color, markerSize: 0,
        axisYType: index < 3 ? "primary" : "secondary", visible: index < 3, showInLegend: true,
        connectNullData: false, dataPoints: wave.samples.map(point => ({x: point.relativeMs, y: point[key]}))}));
    // La leyenda permite superponer RMS de referencia sin modificar la captura almacenada.
    for (const [name, key, color] of [["R RMS", "vr", "#d9534f"], ["S RMS", "vs", "#5cb85c"], ["T RMS", "vt", "#337ab7"]])
        data.push({type: "line", name, color, visible: false, showInLegend: true, markerSize: 0, lineDashType: "dash",
            dataPoints: event.samples.map(point => ({x: point.relativeMs, y: point[key]}))});
    gridWaveViewer.chart = new CanvasJS.Chart("gridWaveChart", {animationEnabled: false, zoomEnabled: true,
        axisX: {title: "Tiempo relativo al FAST (ms)", stripLines: [{value: wave.triggerMs, label: "Disparo Wave"}]},
        axisY: {title: "Voltaje", suffix: " V"}, axisY2: {title: "Corriente", suffix: " A"},
        legend: {cursor: "pointer", itemclick: toggleGridSeries}, rangeChanged: () => syncGridChartRange(gridWaveViewer.chart), data});
    gridWaveViewer.chart.zoomEnabled = !gridEventsViewer.panMode;
    gridWaveViewer.chart.panEnabled = gridEventsViewer.panMode;
    gridWaveViewer.chart.render();
    if (gridEventsViewer.linkViews) syncGridWaveRange();
    const storedPreMs = wave.samples.length ? Math.max(0, wave.triggerMs - wave.samples[0].relativeMs) : 0;
    const warnings = [wave.invalidPackets ? wave.invalidPackets + " paquetes invalidos" : "",
        wave.flags & 1 ? "saturacion" : "", wave.flags & 2 ? "perdida de muestras" : "",
        wave.flags & 4 ? "referencia IRQ estimada" : "",
        wave.flags & 8 ? "Previo insuficiente: " + storedPreMs.toFixed(1) + " ms guardados antes del disparo; no se alcanzo el minimo esperado" : ""].filter(Boolean);
    $("#gridWaveStatus").text("Wave #" + wave.id + " · " + wave.samples.length + " muestras" + (warnings.length ? " · " + warnings.join(", ") : ""));
}

// Sincroniza el rango de la Wave; minimum y maximum son límites X o null, force ignora el check.
function syncGridWaveRange(minimum, maximum, force = false) {
    const source = gridEventsViewer.voltageChart, target = gridWaveViewer.chart;
    if (!source || !target || !force && !gridEventsViewer.linkViews) return;
    if (arguments.length === 0) {
        minimum = source.axisX[0].viewportMinimum;
        maximum = source.axisX[0].viewportMaximum;
    }
    setGridChartRange(target, minimum, maximum);
    target.render();
}
