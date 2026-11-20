const logsViewer = {
    active: false,
    busy: false,
    initialized: false,
    timer: 0,
    statsTimer: 0,
    statsBusy: false,
    recentCursor: 0,
    recentUptimeUs: 0,
    ramFirstSequence: 0,
    ramOldestSequence: 0,
    nandCursor: null,
    nandDone: false,
    autoLoading: false,
    backfillMode: "",
    generation: 0,
    items: [],
    seen: new Set(),
    bootMarkers: 0,
    todayStartUs: 0,
    maxRows: 2000,
};

const logLevelOptions = [
    {value: "inherit", label: "Heredar"},
    {value: "off", label: "OFF"},
    {value: "error", label: "ERROR"},
    {value: "warn", label: "WARNING"},
    {value: "info", label: "INFO"},
    {value: "verbose", label: "VERBOSE"},
    {value: "debug", label: "DEBUG"},
];

// Ejecuta una consulta HTTP con timeout; url y options describen la petición al ESP.
async function fetchLogJson(url, options = {}) {
    const controller = new AbortController();
    const timeout = setTimeout(abortLogFetch.bind(null, controller), 12000);
    try {
        return await fetch(url, {...options, signal: controller.signal});
    } finally {
        clearTimeout(timeout);
    }
}

// Interrumpe una petición que excede el plazo; controller pertenece a esa consulta.
function abortLogFetch(controller) {
    controller.abort();
}

// Conecta los controles de historial, exportación, formateo y configuración.
function initLogsViewer() {
    document.getElementById("logsPrevious").addEventListener("click", loadMoreLogs);
    document.getElementById("logsPreviousBoot").addEventListener("click", loadPreviousBoot);
    document.getElementById("logsExport").addEventListener("click", exportLogs);
    document.getElementById("logsFormat").addEventListener("click", formatLogStorage);
    document.getElementById("logsClear").addEventListener("click", clearLogsView);
    document.getElementById("logsFollow").addEventListener("change", handleLogsFollowChange);
    document.getElementById("logsConfigSave").addEventListener("click", saveLoggerConfig);
}

// Activa las consultas al mostrar la pestaña; active indica si está visible.
function setLogsViewerActive(active) {
    if (logsViewer.active === active) return;
    logsViewer.active = active;
    clearTimeout(logsViewer.timer);
    clearTimeout(logsViewer.statsTimer);
    if (!active) return;
    if (!logsViewer.initialized) loadInitialLogs().then(refreshLogStorageStats);
    else {
        if (document.getElementById("logsFollow").checked) pollRecentLogs().then(refreshLogStorageStats);
        else refreshLogStorageStats();
        if (!logsViewer.nandDone && !logsViewer.autoLoading && !logsViewer.backfillMode) startLogBackfill("today");
    }
}

// Revisa si se deben pedir o detener nuevos mensajes según el switch.
function handleLogsFollowChange() {
    if (logsViewer.active && document.getElementById("logsFollow").checked) pollRecentLogs();
    else clearTimeout(logsViewer.timer);
}

// Consulta uso y configuración en una sola llamada periódica al endpoint /debug.
async function refreshLogStorageStats() {
    if (!logsViewer.active) return;
    clearTimeout(logsViewer.statsTimer);
    if (logsViewer.statsBusy) {
        logsViewer.statsTimer = setTimeout(refreshLogStorageStats, 1000);
        return;
    }
    if (logsViewer.busy) {
        logsViewer.statsTimer = setTimeout(refreshLogStorageStats, 700);
        return;
    }
    logsViewer.statsBusy = true;
    logsViewer.busy = true;
    try {
        const response = await fetchLogJson(board.IP + "/debug", {cache: "no-store"});
        if (!response.ok) throw new Error("HTTP " + response.status);
        const data = await response.json();
        renderLogStorageStats(data);
        renderLoggerConfig(data);
        logsViewer.statsTimer = setTimeout(refreshLogStorageStats, data.storage?.formatting ? 1200 : 10000);
    } catch (error) {
        setLogText("logsStorageStats", "No se pudo consultar estado y configuración: " + error.message);
        document.getElementById("logsStorageStats").classList.add("text-danger");
        logsViewer.statsTimer = setTimeout(refreshLogStorageStats, 10000);
    } finally {
        logsViewer.busy = false;
        logsViewer.statsBusy = false;
        updateLogButtons();
    }
}

// Muestra uso, cola y errores; data reúne el estado devuelto por el firmware.
function renderLogStorageStats(data) {
    const storage = data.storage || {};
    const used = Number(storage.usedBytes) || 0;
    const capacity = Number(storage.capacityBytes) || 0;
    const percent = Number(storage.usedPercent) || 0;
    const queue = data.queue || {};
    let status = (Number(storage.recordsThisRun) || 0) + " registros en este arranque · " +
        formatLogBytes(used) + " / " + formatLogBytes(capacity) + " · " + percent + "% · " +
        (Number(storage.usedBlocks) || 0) + " bloques · cola " + (Number(queue.used) || 0) + "/" +
        (Number(queue.capacity) || 0) + " · pérdidas serie/NAND " + (Number(data.serialDrops) || 0) + "/" +
        (Number(data.nandDrops) || 0) + " · escrituras fallidas " + (Number(storage.failedWrites) || 0);
    if (storage.formatting) {
        status = "Formateando: " + Number(storage.formatPercent || 0) + "% · " +
            formatLogBytes(Number(storage.formatBytesDone) || 0) + " / " +
            formatLogBytes(Number(storage.formatBytesTotal) || 0) + " · " + status;
    } else if (storage.formatDone) {
        status = (storage.formatSuccess ? "Formateo terminado · " : "Falló el formateo · ") + status;
    }
    const stats = document.getElementById("logsStorageStats");
    stats.textContent = status;
    stats.classList.toggle("text-danger", storage.formatDone && !storage.formatSuccess);
    document.getElementById("logsFormat").disabled = !!storage.formatting || !storage.ready;
}

// Carga los mensajes recientes de RAM y luego completa hacia atrás hasta medianoche.
async function loadInitialLogs() {
    logsViewer.todayStartUs = getTodayStartUs();
    const data = await requestRecentLogs({count: 16}, false);
    if (!data) return;
    logsViewer.initialized = true;
    startLogBackfill("today");
}

// Pide fragmentos recientes; params define el cursor y append decide si se agregan al final.
async function requestRecentLogs(params, append = true) {
    if (logsViewer.busy) return null;
    logsViewer.busy = true;
    updateLogButtons();
    try {
        const query = new URLSearchParams(params);
        const response = await fetchLogJson(board.IP + "/debug/recent?" + query.toString(), {cache: "no-store"});
        if (!response.ok) throw new Error("HTTP " + response.status);
        const data = await response.json();
        const items = Array.isArray(data.logs) ? data.logs : [];
        if (data.gap) addLogNotice("La ventana reciente de RAM perdió fragmentos antes de esta lectura.");
        logsViewer.ramOldestSequence = Number(data.oldest) || 0;
        if (items.length) {
            addLogItems(items, !append);
            logsViewer.recentCursor = Math.max(logsViewer.recentCursor, Number(data.newest) || 0,
                Number(items[items.length - 1].i) || 0);
            if (!params.before) logsViewer.recentUptimeUs = Number(items[items.length - 1].u) || logsViewer.recentUptimeUs;
            const first = Number(items[0].i) || 0;
            if (params.before) logsViewer.ramFirstSequence = first || logsViewer.ramOldestSequence;
            else if (!logsViewer.ramFirstSequence) logsViewer.ramFirstSequence = first;
        } else if (params.before && data.gap) {
            logsViewer.ramFirstSequence = logsViewer.ramOldestSequence;
        } else if (params.before) {
            logsViewer.ramFirstSequence = logsViewer.ramOldestSequence;
        }
        if (!logsViewer.initialized && append) logsViewer.initialized = true;
        if (items.length) setLogsStatus("RAM reciente y registro histórico · " + logsViewer.items.length + " mensajes visibles");
        else if (!logsViewer.items.length) setLogsStatus("Todavía no hay mensajes de diagnóstico.");
        return data;
    } catch (error) {
        setLogsStatus("No se pudieron leer los mensajes recientes: " + error.message, true);
        return null;
    } finally {
        logsViewer.busy = false;
        updateLogButtons();
    }
}

// Actualiza mensajes nuevos en serie y vuelve a consultar mientras la vista siga activa.
async function pollRecentLogs() {
    if (!logsViewer.active || !document.getElementById("logsFollow").checked) return;
    if (logsViewer.busy) {
        logsViewer.timer = setTimeout(pollRecentLogs, 700);
        return;
    }
    const previousCursor = logsViewer.recentCursor;
    const previousUptime = logsViewer.recentUptimeUs;
    const params = logsViewer.recentCursor ? {after: logsViewer.recentCursor, count: 16} : {count: 16};
    const data = await requestRecentLogs(params, true);
    const resetDetected = data && (Number(data.newest) < previousCursor ||
        (data.logs?.length && Number(data.logs[0].u) < previousUptime));
    if (resetDetected) {
        resetLogsForNewBoot();
        const reset = await requestRecentLogs({count: 16}, true);
        if (reset) {
            logsViewer.recentCursor = Number(reset.newest) || 0;
            logsViewer.ramFirstSequence = Number(reset.logs?.[0]?.i) || 0;
            logsViewer.ramOldestSequence = Number(reset.oldest) || 0;
            startLogBackfill("today");
        }
    }
    if (logsViewer.active && document.getElementById("logsFollow").checked)
        logsViewer.timer = setTimeout(pollRecentLogs, 1800);
}

// Carga una tanda NAND anterior; count se limita a las filas libres de la vista.
async function requestNandPage() {
    while (logsViewer.busy && logsViewer.active) await delayLogRequest(100);
    if (logsViewer.busy || logsViewer.nandDone || logsViewer.items.length >= logsViewer.maxRows) return null;
    logsViewer.busy = true;
    updateLogButtons();
    try {
        const params = {count: Math.min(16, logsViewer.maxRows - logsViewer.items.length)};
        if (logsViewer.nandCursor) Object.assign(params, logsViewer.nandCursor);
        const response = await fetchLogJson(board.IP + "/debug/nand?" + new URLSearchParams(params), {cache: "no-store"});
        if (!response.ok) throw new Error("HTTP " + response.status);
        const data = await response.json();
        addLogItems(Array.isArray(data.logs) ? data.logs : [], true);
        logsViewer.nandCursor = data.next || null;
        logsViewer.nandDone = !!data.done || !data.next;
        return data;
    } catch (error) {
        setLogsStatus("No se pudo leer la tanda NAND: " + error.message, true);
        return null;
    } finally {
        logsViewer.busy = false;
        updateLogButtons();
    }
}

// Lee la tanda RAM anterior a ramFirstSequence, antes de continuar con NAND.
async function requestOlderRamPage() {
    if (!hasOlderRamLogs()) return null;
    const data = await requestRecentLogs({before: logsViewer.ramFirstSequence, count: 16}, false);
    if (data?.gap && !data.logs?.length)
        logsViewer.ramFirstSequence = logsViewer.ramOldestSequence;
    return data;
}

// Carga de a un lote o continúa hasta hoy/otro arranque; mode selecciona el límite.
async function startLogBackfill(mode) {
    if (!logsViewer.active || logsViewer.autoLoading || logsViewer.nandDone) return;
    logsViewer.autoLoading = true;
    logsViewer.backfillMode = mode;
    logsViewer.generation++;
    const generation = logsViewer.generation;
    const startingBoots = countBootMarkers();
    let batches = 0;
    try {
        do {
            if (!logsViewer.active || generation !== logsViewer.generation || logsViewer.nandDone) break;
            if (mode === "today" && hasReachedToday()) break;
            if (mode === "boot" && countBootMarkers() > startingBoots) break;
            if (logsViewer.items.length >= logsViewer.maxRows) {
                setLogsStatus("Límite de " + logsViewer.maxRows + " mensajes visibles alcanzado.");
                break;
            }
            const readingRam = hasOlderRamLogs();
            setLogsStatus(mode === "boot" ? "Buscando el arranque anterior..." : readingRam
                ? "Cargando mensajes pendientes de RAM..."
                : "Cargando historial NAND en segundo plano · " + logsViewer.items.length + " mensajes...");
            const data = readingRam ? await requestOlderRamPage() : await requestNandPage();
            if (!data) break;
            batches++;
            if (mode === "page" || (!readingRam && data.done) || hasReachedToday() ||
                (mode === "boot" && countBootMarkers() > startingBoots)) break;
            if (batches % 5 === 0 && document.getElementById("logsFollow").checked) {
                const params = logsViewer.recentCursor ? {after: logsViewer.recentCursor, count: 16} : {count: 16};
                await requestRecentLogs(params, true);
            }
            await delayLogRequest(300);
        } while (true);
        if (logsViewer.nandDone) setLogsStatus("Inicio de los logs alcanzado · " + logsViewer.items.length + " mensajes visibles");
        else if (mode === "today" && hasReachedToday()) setLogsStatus("Historial cargado desde medianoche · " + logsViewer.items.length + " mensajes visibles");
        else if (mode === "boot" && countBootMarkers() > startingBoots) setLogsStatus("Arranque anterior encontrado · " + logsViewer.items.length + " mensajes visibles");
        else if (mode === "page") setLogsStatus("Tanda cargada · " + logsViewer.items.length + " mensajes visibles");
    } finally {
        logsViewer.autoLoading = false;
        logsViewer.backfillMode = "";
        updateLogButtons();
        refreshLogStorageStats();
        if (logsViewer.active && document.getElementById("logsFollow").checked)
            logsViewer.timer = setTimeout(pollRecentLogs, 300);
    }
}

// Espera entre lecturas NAND para no saturar la red; milliseconds es la pausa solicitada.
function delayLogRequest(milliseconds) {
    return new Promise(
        // resolve libera la secuencia cuando vence el temporizador.
        function(resolve) { setTimeout(resolve, milliseconds); }
    );
}

// Pide solo una tanda NAND de hasta 16 mensajes.
function loadMoreLogs() {
    if (logsViewer.busy || logsViewer.autoLoading) return;
    startLogBackfill("page");
}

// Lee tandas espaciadas hasta encontrar el siguiente marcador de inicio.
function loadPreviousBoot() {
    if (logsViewer.busy || logsViewer.autoLoading) return;
    startLogBackfill("boot");
}

// Inserta mensajes, conserva la posición de lectura y elimina duplicados por contenido.
function addLogItems(items, prepend) {
    const output = document.getElementById("logsOutput");
    const fresh = [];
    for (const item of items) {
        const key = getLogKey(item);
        if (logsViewer.seen.has(key)) continue;
        logsViewer.seen.add(key);
        const entry = {...item, _key: key};
        if (isBootMarker(entry)) logsViewer.bootMarkers++;
        fresh.push(entry);
    }
    if (!fresh.length) return;
    const wasAtBottom = output.scrollHeight - output.clientHeight - output.scrollTop < 28;
    const oldHeight = output.scrollHeight;
    const fragment = document.createDocumentFragment();
    for (const item of fresh) fragment.appendChild(createLogLine(item));
    if (prepend) {
        output.insertBefore(fragment, output.firstChild);
        logsViewer.items = fresh.concat(logsViewer.items);
        output.scrollTop += output.scrollHeight - oldHeight;
    } else {
        output.appendChild(fragment);
        logsViewer.items.push(...fresh);
        if (wasAtBottom && document.getElementById("logsFollow").checked) output.scrollTop = output.scrollHeight;
    }
    trimLogRows();
    updateLogButtons();
}

// Crea una fila con texto seguro y clase de color; item contiene un fragmento del logger.
function createLogLine(item) {
    const level = normalizeLogLevel(item.l);
    const row = document.createElement("div");
    row.className = "logLine logLine--" + level.toLowerCase();
    row.dataset.key = item._key;
    const time = document.createElement("span");
    time.className = "logTime";
    time.textContent = formatLogTime(item);
    const severity = document.createElement("span");
    severity.className = "logLevel";
    severity.textContent = level;
    const module = document.createElement("span");
    module.className = "logModule";
    module.textContent = item.n || "global";
    const message = document.createElement("span");
    message.className = "logMessage";
    message.textContent = (Number(item.p) > 1 ? "↳ " : "") + (item.x || "");
    row.append(time, severity, module, message);
    return row;
}

// Calcula una clave común para un fragmento recibido desde RAM o NAND; item aporta sus campos.
function getLogKey(item) {
    return [Number(item.u) || 0, Number(item.m) || 0, Number(item.s) || 0,
        Number(item.p) || 0, item.l || "", item.x || ""].join("|");
}

// Devuelve el nivel normalizado que usa CSS; value es la etiqueta guardada por el logger.
function normalizeLogLevel(value) {
    const level = String(value || "OFF").toUpperCase();
    return ({ERROR: "ERR", WARNING: "WRN", WARN: "WRN", INFO: "INF", VERBOSE: "VRB", DEBUG: "DBG"})[level] || level;
}

// Formatea el timestamp UTC/local o el uptime cuando todavía no hay hora válida; item es el log.
function formatLogTime(item) {
    const unixUs = Number(item.t) || 0;
    if (unixUs > 0) {
        const date = new Date(Math.floor(unixUs / 1000));
        return date.toLocaleString("es-AR", {hour12: false}) + "." + String(unixUs % 1000000).padStart(6, "0");
    }
    return "+" + ((Number(item.u) || 0) / 1000000).toFixed(3) + " s";
}

// Inserta un aviso local en la lista; message describe una pérdida o un cambio de estado.
function addLogNotice(message) {
    const row = document.createElement("div");
    row.className = "logLine logLine--notice";
    row.textContent = "[visor] " + message;
    document.getElementById("logsOutput").appendChild(row);
}

// Mantiene como máximo maxRows elementos y olvida sus claves al quitar filas antiguas.
function trimLogRows() {
    const output = document.getElementById("logsOutput");
    while (logsViewer.items.length > logsViewer.maxRows) {
        const removed = logsViewer.items.shift();
        logsViewer.seen.delete(removed._key);
        if (output.firstElementChild?.dataset.key === removed._key) output.firstElementChild.remove();
    }
}

// Comprueba si el historial ya llegó al inicio del día local.
function hasReachedToday() {
    let oldest = Infinity;
    for (const item of logsViewer.items) {
        const timestamp = Number(item.t) || 0;
        if (timestamp > 0 && timestamp < oldest) oldest = timestamp;
    }
    return oldest <= logsViewer.todayStartUs;
}

// Cuenta los marcadores de arranque visibles.
function countBootMarkers() {
    return logsViewer.bootMarkers;
}

// Indica si quedan mensajes en el anillo RAM anteriores al primer mensaje visible.
function hasOlderRamLogs() {
    return logsViewer.ramFirstSequence > logsViewer.ramOldestSequence && logsViewer.ramFirstSequence > 0;
}

// Identifica un mensaje que marca el inicio de la placa; item contiene el mensaje completo.
function isBootMarker(item) {
    return item.n === "main" && String(item.x || "").includes("Inicio, reset=");
}

// Calcula medianoche local como microsegundos Unix.
function getTodayStartUs() {
    const start = new Date();
    start.setHours(0, 0, 0, 0);
    return start.getTime() * 1000;
}

// Actualiza estado visible y estilo; error distingue fallas de consulta.
function setLogsStatus(message, error = false) {
    const status = document.getElementById("logsStatus");
    status.textContent = message;
    status.classList.toggle("text-danger", error);
    status.classList.toggle("text-muted", !error);
}

// Habilita acciones solo cuando no hay lecturas pendientes ni se alcanzó el inicio.
function updateLogButtons() {
    const locked = logsViewer.busy || logsViewer.autoLoading;
    document.getElementById("logsPrevious").disabled = locked || logsViewer.nandDone || logsViewer.items.length >= logsViewer.maxRows;
    document.getElementById("logsPreviousBoot").disabled = locked || logsViewer.nandDone || logsViewer.items.length >= logsViewer.maxRows;
    document.getElementById("logsExport").disabled = !logsViewer.items.length;
}

// Borra solo las filas de pantalla y conserva los cursores para seguir desde el punto actual.
function clearLogsView() {
    logsViewer.generation++;
    logsViewer.items = [];
    logsViewer.seen.clear();
    logsViewer.bootMarkers = 0;
    document.getElementById("logsOutput").replaceChildren();
    setLogsStatus("Vista vacía; los siguientes mensajes se agregarán aquí.");
    updateLogButtons();
}

// Limpia cursores de la sesión anterior cuando el contador RAM confirma un reinicio.
function resetLogsForNewBoot() {
    logsViewer.recentCursor = 0;
    logsViewer.recentUptimeUs = 0;
    logsViewer.ramFirstSequence = 0;
    logsViewer.ramOldestSequence = 0;
    logsViewer.nandCursor = null;
    logsViewer.nandDone = false;
    logsViewer.items = [];
    logsViewer.seen.clear();
    logsViewer.bootMarkers = 0;
    document.getElementById("logsOutput").replaceChildren();
}

// Descarga los mensajes visibles como un archivo de texto local.
function exportLogs() {
    if (!logsViewer.items.length) return;
    const lines = [];
    for (const item of logsViewer.items)
        lines.push("[" + formatLogTime(item) + "] [" + normalizeLogLevel(item.l) + "] [" +
            (item.n || "global") + "] " + (Number(item.p) > 1 ? "↳ " : "") + (item.x || ""));
    const text = lines.join("\n");
    const blob = new Blob([text], {type: "text/plain;charset=utf-8"});
    const url = URL.createObjectURL(blob);
    const link = document.createElement("a");
    link.href = url;
    link.download = "power-meter-logs-" + new Date().toISOString().replace(/[:.]/g, "-") + ".txt";
    link.click();
    URL.revokeObjectURL(url);
}

// Inicia el formateo de la partición de logs tras confirmar; no toca históricos ni eventos.
async function formatLogStorage() {
    if (logsViewer.busy || !window.confirm("Se borrarán todos los logs de diagnóstico. El histórico y los eventos no se borran. ¿Continuar?")) return;
    try {
        const response = await fetchLogJson(board.IP + "/debug/format", {method: "POST", cache: "no-store"});
        if (!response.ok) throw new Error("HTTP " + response.status);
        logsViewer.generation++;
        logsViewer.items = [];
        logsViewer.seen.clear();
        logsViewer.bootMarkers = 0;
        logsViewer.nandCursor = null;
        logsViewer.nandDone = false;
        document.getElementById("logsOutput").replaceChildren();
        setLogsStatus("Formateo iniciado; la lectura histórica se reanudará al terminar.");
        refreshLogStorageStats();
    } catch (error) {
        setLogsStatus("No se pudo iniciar el formateo: " + error.message, true);
    }
}

// Muestra la configuración global y por módulo; data es la respuesta de /debug.
function renderLoggerConfig(data) {
    const globalLevel = document.getElementById("logsGlobalLevel");
    globalLevel.value = diagnosticLevelValue(data.globalLevel);
    document.getElementById("logsOutputSerial").checked = !!data.serial;
    document.getElementById("logsOutputNand").checked = !!data.nand;
    const container = document.getElementById("logsModuleLevels");
    const modules = Array.isArray(data.modules) ? data.modules : [];
    const moduleIds = getLoggerModuleIds(modules);
    if (container.dataset.ids === moduleIds) {
        for (const module of modules) {
            const select = container.querySelector('[data-module-id="' + module.id + '"]');
            if (select && document.activeElement !== select) select.value = diagnosticLevelValue(module.level, true);
        }
        return;
    }
    container.replaceChildren();
    container.dataset.ids = moduleIds;
    for (const module of modules) {
        const row = document.createElement("div");
        row.className = "logsModuleRow";
        const name = document.createElement("span");
        name.className = "logsModuleName";
        name.textContent = module.name || ("Módulo " + module.id);
        const select = document.createElement("select");
        select.className = "form-select form-select-sm";
        select.dataset.moduleId = String(module.id);
        select.setAttribute("aria-label", "Nivel para " + name.textContent);
        for (const option of logLevelOptions) {
            const element = document.createElement("option");
            element.value = option.value;
            element.textContent = option.label;
            select.appendChild(element);
        }
        select.value = diagnosticLevelValue(module.level, true);
        const button = document.createElement("button");
        button.type = "button";
        button.className = "btn btn-sm btn-outline-primary";
        button.textContent = "Guardar";
        button.addEventListener("click", saveModuleLoggerLevel);
        row.append(name, select, button);
        container.appendChild(row);
    }
}

// Devuelve IDs en orden de respuesta; modules contiene la lista enviada por /debug.
function getLoggerModuleIds(modules) {
    const ids = [];
    for (const module of modules) ids.push(module.id);
    return ids.join(",");
}

// Convierte el enum del firmware a una opción del formulario; inherit permite herencia.
function diagnosticLevelValue(value, inherit = false) {
    const number = Number(value);
    if (inherit && number === 255) return "inherit";
    return ["off", "error", "warn", "info", "verbose", "debug"][number] || "off";
}

// Guarda nivel global y destinos serie/NAND en una única solicitud URL-encoded.
async function saveLoggerConfig() {
    const params = new URLSearchParams({
        globalLevel: document.getElementById("logsGlobalLevel").value,
        serial: String(document.getElementById("logsOutputSerial").checked),
        nand: String(document.getElementById("logsOutputNand").checked),
    });
    await postLoggerConfig(params, "Configuración general guardada.");
}

// Guarda el nivel seleccionado para un módulo; event identifica el botón y su fila.
async function saveModuleLoggerLevel(event) {
    const row = event.currentTarget.closest(".logsModuleRow");
    const select = row.querySelector("select");
    const params = new URLSearchParams({moduleId: select.dataset.moduleId, level: select.value});
    await postLoggerConfig(params, "Nivel guardado para " + row.querySelector(".logsModuleName").textContent + ".");
}

// Envía opciones del logger y confirma resultado; params contiene solo claves soportadas.
async function postLoggerConfig(params, successMessage) {
    const status = document.getElementById("logsConfigStatus");
    status.textContent = "Guardando...";
    try {
        const response = await fetchLogJson(board.IP + "/debug/config", {
            method: "POST", headers: {"Content-Type": "application/x-www-form-urlencoded"},
            body: params.toString(), cache: "no-store",
        });
        if (!response.ok) {
            let details = "HTTP " + response.status;
            try { const body = await response.json(); if (body.reason) details += " · " + body.reason; } catch (_) {}
            throw new Error(details);
        }
        status.textContent = successMessage;
        status.classList.remove("text-danger");
        refreshLogStorageStats();
    } catch (error) {
        status.textContent = "No se pudo guardar: " + error.message;
        status.classList.add("text-danger");
    }
}

// Formatea el tamaño de almacenamiento en KiB o MiB; bytes es el valor recibido del ESP.
function formatLogBytes(bytes) {
    if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + " MiB";
    return (bytes / 1024).toFixed(1) + " KiB";
}

// Decodifica un bloque binario heredado; buffer contiene los bytes completos recibidos.
function parseDiagnosticBlock(buffer) {
    const bytes = new Uint8Array(buffer);
    if (bytes.length < 20 || String.fromCharCode(...bytes.subarray(0, 4)) !== "DLOG" || bytes[4] !== 1)
        throw new Error("cabecera binaria inválida");
    const view = new DataView(buffer);
    const ramCount = bytes[5], nandCount = bytes[6];
    if (!nandCount || bytes.length !== 20 + (ramCount + nandCount) * 2048)
        throw new Error("longitud de bloque incorrecta");
    const names = readDiagnosticNames(bytes, view, 20 + (ramCount + nandCount - 1) * 2048);
    const pages = [];
    let invalidPages = 0;
    const decoder = new TextDecoder();
    for (let index = 0; index < ramCount + nandCount; ++index) {
        const offset = 20 + index * 2048;
        if (view.getUint32(offset, true) !== 0x47415044) continue;
        const count = view.getUint16(offset + 6, true), used = view.getUint16(offset + 16, true);
        const unit = view.getUint16(offset + 18, true);
        if (bytes[offset + 4] !== 1 || bytes[offset + 5] !== 32 || !count || used > 2016 || unit > 1 ||
            diagnosticCrc(bytes, offset + 32, used, diagnosticCrc(bytes, offset, 28)) !== view.getUint32(offset + 28, true)) {
            invalidPages++;
            continue;
        }
        const records = [];
        let position = 32;
        for (let record = 0; record < count; ++record) {
            if (position + 27 > 32 + used) break;
            const length = view.getUint16(offset + position, true);
            if (length < 27 || length > 107 || position + length > 32 + used) break;
            const moduleId = view.getUint32(offset + position + 2, true), level = bytes[offset + position + 6];
            const scale = unit ? 1 : 1000;
            records.push({g: view.getUint32(offset + 8, true), r: record,
                u: Number(view.getBigUint64(offset + position + 11, true)) * scale,
                t: Number(view.getBigUint64(offset + position + 19, true)) * scale,
                m: moduleId, n: names.get(moduleId) || String(moduleId),
                l: ["OFF", "ERR", "WRN", "INF", "VRB", "DBG"][level] || "?",
                s: view.getUint16(offset + position + 9, true), f: bytes[offset + position + 7],
                p: bytes[offset + position + 8], x: decoder.decode(bytes.subarray(offset + position + 27, offset + position + length))});
            position += length;
        }
        if (records.length !== count || position !== 32 + used) { invalidPages++; continue; }
        pages.push(records);
    }
    return {logs: pages.reverse().flat(), next: {session: view.getUint32(8, true), before: view.getUint16(12, true)}, invalidPages};
}

// Lee el diccionario de módulos de la cabecera NAND; bytes y view contienen el bloque.
function readDiagnosticNames(bytes, view, offset) {
    const names = new Map();
    if (view.getUint32(offset, true) !== 0x474F4C44 || bytes[offset + 4] !== 1) return names;
    const count = bytes[offset + 5], length = view.getUint16(offset + 6, true);
    if (length !== 20 + count * 20 || length > 2048 ||
        diagnosticCrc(bytes, offset + 20, length - 20, diagnosticCrc(bytes, offset, 16)) !== view.getUint32(offset + 16, true)) return names;
    const decoder = new TextDecoder();
    for (let index = 0; index < count; ++index) {
        const entry = offset + 20 + index * 20, nameBytes = bytes.subarray(entry + 4, entry + 20);
        const end = nameBytes.indexOf(0);
        names.set(view.getUint32(entry, true), decoder.decode(end < 0 ? nameBytes : nameBytes.subarray(0, end)));
    }
    return names;
}

// Calcula CRC32 IEEE sobre un tramo; previous permite encadenar cabecera y payload.
function diagnosticCrc(bytes, offset, length, previous = 0) {
    let crc = (previous ^ 0xffffffff) >>> 0;
    for (let index = 0; index < length; ++index) {
        crc ^= bytes[offset + index];
        for (let bit = 0; bit < 8; ++bit) crc = (crc >>> 1) ^ (crc & 1 ? 0xedb88320 : 0);
    }
    return (crc ^ 0xffffffff) >>> 0;
}
