const logsViewer = {
    active: false,
    timer: 0,
    busy: false,
    cursor: 0,
    firstSequence: 0,
    oldestSequence: 0,
    rowCount: 0,
    maxRows: 2000,
    initialized: false,
    mode: "ram",
    nandCursor: null,
    nandSessionToken: 0,
    nandDone: false,
    statsBusy: false,
    statsTimer: 0,
};

// Conecta controles y carga la ventana reciente de logs.
function initLogsViewer() {
    $("#logsPrevious").on("click", loadPreviousLogs);
    $("#logsNand").on("click", toggleLogsMode);
	$("#logsStats").on("click", refreshLogStorageStats);
	$("#logsFormat").on("click", formatLogStorage);
    $("#logsClear").on("click", clearLogsView);
    $("#logsFollow").on("change", () => {
        if (logsViewer.active && $("#logsFollow").prop("checked")) pollRecentLogs();
    });
}

// Activa consultas mientras la pestaña está visible; active indica su estado.
function setLogsViewerActive(active) {
    if (logsViewer.active === active) return;
    logsViewer.active = active;
    clearTimeout(logsViewer.timer);
    clearTimeout(logsViewer.statsTimer);
    if (!active) return;
    refreshLogStorageStats();
    if (!logsViewer.initialized) loadRecentLogs();
    else if (logsViewer.mode === "ram" && $("#logsFollow").prop("checked")) pollRecentLogs();
}

// Consulta ocupacion, registros recientes y progreso del formateo.
async function refreshLogStorageStats() {
	if (logsViewer.statsBusy) return;
	logsViewer.statsBusy = true;
	try {
		const response = await fetch(board.IP + "/debug", {cache: "no-store"});
		if (!response.ok) throw new Error("HTTP " + response.status);
		const data = await response.json();
		const storage = data.storage || {};
		const used = Number(storage.usedBytes) || 0;
		const capacity = Number(storage.capacityBytes) || 0;
		let status = (Number(storage.recordsThisRun) || 0) + " registros de esta sesión · " +
			formatLogBytes(used) + " / " + formatLogBytes(capacity) + " · " +
			(Number(storage.usedPercent) || 0) + "% · " + (Number(storage.usedBlocks) || 0) + " bloques";
		if (storage.formatting) {
			status = "Formateando logs: " + Number(storage.formatPercent || 0) + "% · " +
				formatLogBytes(storage.formatBytesDone) + " / " + formatLogBytes(storage.formatBytesTotal) +
				" · " + storage.formatBlocksDone + "/" + storage.formatBlocksTotal + " bloques · " + status;
		} else if (storage.formatDone) {
			status = (storage.formatSuccess ? "Formateo terminado · " : "Falló el formateo · ") + status;
		}
		$("#logsStorageStats").text(status).toggleClass("text-danger", storage.formatDone && !storage.formatSuccess);
		$("#logsFormat").prop("disabled", !!storage.formatting || !storage.ready);
		logsViewer.statsTimer = setTimeout(refreshLogStorageStats, storage.formatting ? 900 : 5000);
	} catch (error) {
		$("#logsStorageStats").text("No se pudo consultar el uso: " + error.message).addClass("text-danger");
		logsViewer.statsTimer = setTimeout(refreshLogStorageStats, 5000);
	} finally {
		logsViewer.statsBusy = false;
	}
}

// Inicia el borrado total de la particion de diagnostico tras confirmacion.
async function formatLogStorage() {
	if (logsViewer.busy || !window.confirm("Se borrarán todos los logs de la partición de diagnóstico. El histórico y los eventos no se borran. ¿Continuar?")) return;
	$("#logsFormat").prop("disabled", true);
	$("#logsStorageStats").text("Iniciando formateo...").removeClass("text-danger");
	try {
		const response = await fetch(board.IP + "/debug/format", {method: "POST", cache: "no-store"});
		if (!response.ok) throw new Error("HTTP " + response.status);
		logsViewer.mode = "ram";
		logsViewer.nandCursor = null;
		logsViewer.nandSessionToken = 0;
		logsViewer.nandDone = false;
		logsViewer.rowCount = 0;
		$("#logsOutput").val("");
		$("#logsTitle").text("RAM reciente");
		$("#logsNand").text("Leer RAM + NAND");
		$("#logsFollow").prop("disabled", false).prop("checked", true);
		setLogsStatus("Borrado en curso...");
		refreshLogStorageStats();
	} catch (error) {
		$("#logsStorageStats").text("No se pudo iniciar el formateo: " + error.message).addClass("text-danger");
		$("#logsFormat").prop("disabled", false);
	}
}

// Formatea bytes como KiB o MiB para el resumen del panel.
function formatLogBytes(bytes) {
	if (bytes >= 1024 * 1024) return (bytes / (1024 * 1024)).toFixed(2) + " MiB";
	return (bytes / 1024).toFixed(1) + " KiB";
}

// Alterna entre la sesión RAM reciente y los registros persistidos en NAND.
function toggleLogsMode() {
    if (logsViewer.busy) return;
    if (logsViewer.mode === "ram") {
        logsViewer.mode = "nand";
        logsViewer.nandCursor = logsViewer.nandSessionToken
            ? {refresh: logsViewer.nandSessionToken} : null;
        logsViewer.nandDone = false;
        $("#logsFollow").prop("checked", false).prop("disabled", true);
        $("#logsNand").text("Volver a RAM");
        $("#logsTitle").text("Registro RAM + NAND");
        $("#logsOutput").val("");
        logsViewer.rowCount = 0;
        requestNandLogs(false);
    } else {
        logsViewer.mode = "ram";
        $("#logsFollow").prop("disabled", false).prop("checked", true);
        $("#logsNand").text("Leer RAM + NAND");
        $("#logsTitle").text("RAM reciente");
        loadRecentLogs();
    }
    updatePreviousButton();
}

// Carga la tanda más reciente al abrir la vista.
async function loadRecentLogs() {
    logsViewer.initialized = true;
    logsViewer.cursor = 0;
    logsViewer.firstSequence = 0;
    logsViewer.rowCount = 0;
    $("#logsOutput").val("");
    await requestRecentLogs({count: 16});
}

// Consulta fragmentos nuevos después del cursor actual.
async function pollRecentLogs() {
	if (!logsViewer.active || logsViewer.mode !== "ram" || logsViewer.busy || !$("#logsFollow").prop("checked")) return;
	await requestRecentLogs({after: logsViewer.cursor, count: 16});
}

// Recupera la tanda anterior a la primera línea visible.
async function loadPreviousLogs() {
	if (logsViewer.mode === "nand") {
		if (!logsViewer.nandDone && logsViewer.nandCursor) requestNandLogs(true);
		return;
	}
	if (logsViewer.busy || !logsViewer.firstSequence || logsViewer.firstSequence <= logsViewer.oldestSequence) return;
	await requestRecentLogs({before: logsViewer.firstSequence, count: 16}, true);
}

// Lee hasta 16 registros NAND anteriores y conserva el cursor para la tanda siguiente.
async function requestNandLogs(prepend) {
	if (logsViewer.busy || logsViewer.nandDone) return;
	logsViewer.busy = true;
	setLogsStatus(prepend ? "Leyendo bloque anterior..." : "Leyendo registro...");
	try {
		const query = logsViewer.nandCursor ? "?" + new URLSearchParams(logsViewer.nandCursor) : "";
		let response = await fetch(board.IP + "/debug/binary" + query, {cache: "no-store"});
		if (response.status === 429 && !prepend && logsViewer.nandCursor?.refresh) {
			logsViewer.nandSessionToken = 0;
			logsViewer.nandCursor = null;
			response = await fetch(board.IP + "/debug/binary", {cache: "no-store"});
		}
		if (response.status === 204) {
			logsViewer.nandDone = true;
			setLogsStatus("Inicio del registro alcanzado.");
			return;
		}
		if (!response.ok) throw new Error("HTTP " + response.status);
		const data = parseDiagnosticBlock(await response.arrayBuffer());
		showNandChunks(data, prepend);
		logsViewer.nandSessionToken = data.next.session;
		logsViewer.nandCursor = data.next || null;
	} catch (error) {
		setLogsStatus("No se pudo leer el registro: " + error.message, true);
	} finally {
		logsViewer.busy = false;
		updatePreviousButton();
	}
}

// Muestra un lote persistido y deja los más antiguos accesibles con el botón.
function showNandChunks(data, prepend) {
	const output = document.getElementById("logsOutput");
	const items = Array.isArray(data.logs) ? data.logs : [];
	const lines = items.map(formatLogChunk);
	const wasAtBottom = output.scrollHeight - output.clientHeight - output.scrollTop < 24;
	if (prepend) {
		const oldHeight = output.scrollHeight;
		output.value = lines.join("\n") + (output.value && lines.length ? "\n" : "") + output.value;
		output.scrollTop += output.scrollHeight - oldHeight;
	} else {
		output.value = lines.join("\n");
		if (wasAtBottom) output.scrollTop = output.scrollHeight;
	}
	logsViewer.rowCount += lines.length;
	trimLogsView(output, prepend);
	setLogsStatus(lines.length
		? "RAM + NAND · " + logsViewer.rowCount + " fragmentos visibles" +
			(data.invalidPages ? " · " + data.invalidPages + " páginas inválidas" : "")
		: "No hay fragmentos válidos en este bloque.");
}

// Valida y decodifica un bloque binario, conservando las páginas en orden temporal.
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
		const count = view.getUint16(offset + 6, true);
		const used = view.getUint16(offset + 16, true);
		const unit = view.getUint16(offset + 18, true);
		if (bytes[offset + 4] !== 1 || bytes[offset + 5] !== 32 || !count ||
			used > 2016 || unit > 1 ||
			diagnosticCrc(bytes, offset + 32, used, diagnosticCrc(bytes, offset, 28)) !==
				view.getUint32(offset + 28, true)) {
			++invalidPages;
			continue;
		}
		const records = [];
		let position = 32;
		for (let record = 0; record < count; ++record) {
			if (position + 27 > 32 + used) break;
			const length = view.getUint16(offset + position, true);
			if (length < 27 || length > 107 || position + length > 32 + used) break;
			const moduleId = view.getUint32(offset + position + 2, true);
			const level = bytes[offset + position + 6];
			const scale = unit ? 1 : 1000;
			records.push({
				g: view.getUint32(offset + 8, true), r: record,
				u: Number(view.getBigUint64(offset + position + 11, true)) * scale,
				t: Number(view.getBigUint64(offset + position + 19, true)) * scale,
				m: moduleId, n: names.get(moduleId) || String(moduleId),
				l: ["OFF", "ERR", "WRN", "INF", "VRB", "DBG"][level] || "?",
				s: view.getUint16(offset + position + 9, true),
				f: bytes[offset + position + 7], p: bytes[offset + position + 8],
				x: decoder.decode(bytes.subarray(offset + position + 27, offset + position + length)),
			});
			position += length;
		}
		if (records.length !== count || position !== 32 + used) {
			++invalidPages;
			continue;
		}
		pages.push(records);
	}
	return {
		logs: pages.reverse().flat(),
		next: {session: view.getUint32(8, true), before: view.getUint16(12, true)},
		invalidPages,
	};
}

// Lee el diccionario de módulos almacenado en la cabecera física del bloque.
function readDiagnosticNames(bytes, view, offset) {
	const names = new Map();
	if (view.getUint32(offset, true) !== 0x474F4C44 || bytes[offset + 4] !== 1) return names;
	const count = bytes[offset + 5];
	const length = view.getUint16(offset + 6, true);
	if (length !== 20 + count * 20 || length > 2048 ||
		diagnosticCrc(bytes, offset + 20, length - 20, diagnosticCrc(bytes, offset, 16)) !==
			view.getUint32(offset + 16, true)) return names;
	const decoder = new TextDecoder();
	for (let index = 0; index < count; ++index) {
		const entry = offset + 20 + index * 20;
		const nameBytes = bytes.subarray(entry + 4, entry + 20);
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

// Lee una tanda y la agrega arriba o abajo; params define el cursor solicitado.
async function requestRecentLogs(params, prepend = false) {
    if (logsViewer.busy) return;
    logsViewer.busy = true;
    clearTimeout(logsViewer.timer);
    setLogsStatus(prepend ? "Cargando anteriores..." : "Actualizando...");
    try {
        const query = new URLSearchParams(params);
        const response = await fetch(board.IP + "/debug/recent?" + query.toString(), {cache: "no-store"});
        if (!response.ok) throw new Error("HTTP " + response.status);
        const data = await response.json();
        showLogChunks(data, prepend);
    } catch (error) {
        setLogsStatus("No se pudieron leer los logs: " + error.message, true);
    } finally {
        logsViewer.busy = false;
        updatePreviousButton();
        if (logsViewer.active && $("#logsFollow").prop("checked")) {
            logsViewer.timer = setTimeout(pollRecentLogs, 1500);
        }
    }
}

// Agrega una tanda de fragmentos sin reconstruir el textarea completo.
function showLogChunks(data, prepend) {
    const output = document.getElementById("logsOutput");
    const items = Array.isArray(data.logs) ? data.logs : [];
    const lines = items.map(formatLogChunk);
    if (data.gap) addLogNotice("Se perdió parte de la ventana reciente en RAM.", prepend);
    if (!lines.length) {
        logsViewer.oldestSequence = Number(data.oldest) || 0;
        setLogsStatus(logsViewer.cursor ? "Al día · sin mensajes nuevos" : "No hay mensajes recientes.");
        return;
    }

    const wasAtBottom = output.scrollHeight - output.clientHeight - output.scrollTop < 24;
    if (prepend) {
        const oldHeight = output.scrollHeight;
        output.value = lines.join("\n") + (output.value ? "\n" + output.value : "");
        logsViewer.firstSequence = Number(items[0].i) || logsViewer.firstSequence;
        output.scrollTop += output.scrollHeight - oldHeight;
    } else {
        output.value += (output.value ? "\n" : "") + lines.join("\n");
        if (!logsViewer.firstSequence) logsViewer.firstSequence = Number(items[0].i) || 0;
        logsViewer.cursor = Number(items[items.length - 1].i) || logsViewer.cursor;
        if (wasAtBottom && $("#logsFollow").prop("checked")) output.scrollTop = output.scrollHeight;
    }
    logsViewer.oldestSequence = Number(data.oldest) || 0;
    logsViewer.rowCount += lines.length;
    trimLogsView(output);
    setLogsStatus("Sesión actual · " + logsViewer.rowCount + " fragmentos visibles · RAM " + data.oldest + "–" + data.newest);
}

// Formatea timestamp, nivel, módulo y texto para una línea legible.
function formatLogChunk(item) {
    const timestamp = Number(item.t) > 0
        ? new Date(Math.floor(Number(item.t) / 1000)).toLocaleString("es-AR", {hour12: false}) +
            "." + String(Number(item.t) % 1000000).padStart(6, "0")
        : "+" + (Number(item.u) / 1000000).toFixed(6) + " s";
    const continuation = Number(item.p) > 1 ? "  ↳ " : "";
    return "[" + timestamp + "] [" + item.l + "] " + item.n + ": " + continuation + item.x;
}

// Agrega un aviso de pérdida visible sin alterar los registros recuperados.
function addLogNotice(message, prepend = false) {
    const output = document.getElementById("logsOutput");
    const line = "[visor] " + message;
    output.value = prepend ? line + "\n" + output.value : (output.value ? output.value + "\n" : "") + line;
}

// Limita la vista a maxRows líneas para mantener liviano el navegador.
function trimLogsView(output, prepend = false) {
	const lines = output.value.split("\n");
	if (lines.length <= logsViewer.maxRows) return;
	output.value = lines.slice(prepend ? 0 : -logsViewer.maxRows,
		prepend ? logsViewer.maxRows : undefined).join("\n");
    logsViewer.rowCount = logsViewer.maxRows;
}

// Actualiza el estado y su apariencia; error indica una falla de consulta.
function setLogsStatus(message, error = false) {
    $("#logsStatus").text(message).toggleClass("text-danger", error).toggleClass("text-muted", !error);
}

// Habilita carga anterior solo mientras queden fragmentos en la ventana RAM.
function updatePreviousButton() {
	const disabled = logsViewer.mode === "nand"
		? logsViewer.busy || logsViewer.nandDone || !logsViewer.nandCursor
		: logsViewer.busy || !logsViewer.firstSequence || logsViewer.firstSequence <= logsViewer.oldestSequence;
	$("#logsPrevious").prop("disabled", disabled);
}

// Limpia lo mostrado y deja el cursor listo para recibir solo mensajes nuevos.
function clearLogsView() {
    $("#logsOutput").val("");
    logsViewer.firstSequence = logsViewer.cursor + 1;
    logsViewer.rowCount = 0;
    updatePreviousButton();
}
