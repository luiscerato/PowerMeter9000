const captureCacheState = {db: null, opening: null, memory: new Map(), memoryBytes: 0, limit: 32 * 1024 * 1024,
    memoryLimit: 1024 * 1024, maxEntries: 3000, queue: Promise.resolve(), hits: 0, downloads: 0};

// Abre IndexedDB sin bloquear indefinidamente; no recibe parametros y admite navegadores sin almacenamiento.
function openCaptureCache() {
    if (captureCacheState.db) return Promise.resolve(captureCacheState.db);
    if (captureCacheState.opening) return captureCacheState.opening;
    captureCacheState.opening = new Promise(resolve => {
        if (typeof indexedDB === "undefined") return resolve(null);
        let request, settled = false;
        const finish = db => { if (!settled) { settled = true; clearTimeout(timer); captureCacheState.db = db; resolve(db); } else db?.close(); };
        const timer = setTimeout(() => finish(null), 1500);
        try { request = indexedDB.open("powerMeterCaptures", 1); } catch (_) { finish(null); return; }
        request.onupgradeneeded = () => {
            const db = request.result;
            db.createObjectStore("records", {keyPath: "key"}); db.createObjectStore("metadata", {keyPath: "key"});
        };
        request.onsuccess = () => {
            const db = request.result;
            db.onversionchange = () => { db.close(); captureCacheState.db = null; captureCacheState.opening = null; };
            finish(db);
        };
        request.onerror = request.onblocked = () => finish(null);
    });
    return captureCacheState.opening;
}

// Actualiza la cache RAM de respaldo con limite LRU; key identifica la captura y buffer contiene el binario completo.
function rememberCaptureBuffer(key, buffer) {
    const state = captureCacheState, previous = state.memory.get(key);
    if (previous) { state.memoryBytes -= previous.byteLength; state.memory.delete(key); }
    if (buffer.byteLength > state.memoryLimit) return;
    state.memory.set(key, buffer); state.memoryBytes += buffer.byteLength;
    while (state.memoryBytes > state.memoryLimit || state.memory.size > state.maxEntries) {
        const oldest = state.memory.keys().next().value;
        state.memoryBytes -= state.memory.get(oldest).byteLength; state.memory.delete(oldest);
    }
}

// Lee un binario y actualiza su antiguedad; key es una clave de dispositivo/generacion/captura.
async function readCaptureBuffer(key) {
    const memory = captureCacheState.memory.get(key);
    if (memory) { rememberCaptureBuffer(key, memory); return memory; }
    const db = await openCaptureCache();
    if (!db) return null;
    return new Promise(resolve => {
        let result = null;
        try {
            const transaction = db.transaction(["records", "metadata"], "readwrite"), request = transaction.objectStore("records").get(key);
            request.onsuccess = () => {
                result = request.result?.buffer || null;
                if (result) transaction.objectStore("metadata").put({key, bytes: result.byteLength, usedAt: Date.now()});
            };
            transaction.oncomplete = () => resolve(result);
            transaction.onabort = transaction.onerror = () => resolve(null);
        } catch (_) { resolve(null); }
    });
}

// Borra una entrada invalida tanto en disco como RAM; key es su identificador completo.
async function removeCaptureBuffer(key) {
    const state = captureCacheState, previous = state.memory.get(key);
    if (previous) { state.memoryBytes -= previous.byteLength; state.memory.delete(key); }
    const db = await openCaptureCache();
    if (!db) return;
    await new Promise(resolve => {
        try {
            const transaction = db.transaction(["records", "metadata"], "readwrite");
            transaction.objectStore("records").delete(key); transaction.objectStore("metadata").delete(key);
            transaction.oncomplete = transaction.onabort = transaction.onerror = resolve;
        } catch (_) { resolve(); }
    });
}

// Persiste y elimina las mas antiguas sin leer sus binarios; key y buffer identifican la captura validada.
async function writeCaptureBuffer(key, buffer) {
    const state = captureCacheState, db = await openCaptureCache();
    if (!db) { rememberCaptureBuffer(key, buffer); return; }
    if (buffer.byteLength > state.limit) return;
    const stored = await new Promise(resolve => {
        try {
            const transaction = db.transaction(["records", "metadata"], "readwrite"), records = transaction.objectStore("records"), metadata = transaction.objectStore("metadata");
            const request = metadata.getAll();
            request.onsuccess = () => {
                const entries = request.result.filter(entry => entry.key !== key).sort((a, b) => a.usedAt - b.usedAt);
                let size = entries.reduce((sum, entry) => sum + entry.bytes, buffer.byteLength), count = entries.length + 1;
                for (const entry of entries) {
                    if (size <= state.limit && count <= state.maxEntries) break;
                    records.delete(entry.key); metadata.delete(entry.key); size -= entry.bytes; --count;
                }
                records.put({key, buffer}); metadata.put({key, bytes: buffer.byteLength, usedAt: Date.now()});
            };
            transaction.oncomplete = () => resolve(true);
            transaction.onabort = transaction.onerror = () => resolve(false);
        } catch (_) { resolve(false); }
    });
    if (!stored) rememberCaptureBuffer(key, buffer);
}

// Comprueba una identidad del firmware; value contiene MAC y generaciones en hexadecimal, sin convertir uint64 a Number.
function validCaptureCacheScope(value) {
    return value?.ready === true && /^[0-9a-f]{12}$/i.test(value.deviceId) &&
        /^[0-9a-f]{16}$/i.test(value.fastEpoch) && !/^0+$/.test(value.fastEpoch) &&
        /^[0-9a-f]{16}$/i.test(value.waveEpoch) && !/^0+$/.test(value.waveEpoch);
}

// Refresca la identidad al empezar una accion; signal permite cancelar, sin cachear estados de formato.
async function refreshCaptureCacheScope(signal) {
    try {
        const scope = await fetchEventHistory("/capture-cache", signal);
        return validCaptureCacheScope(scope) ? scope : null;
    } catch (error) { if (signal.aborted) throw error; return null; }
}

// Forma una clave independiente de IP y bloques fisicos; scope identifica el dispositivo, kind el formato y parts su firma.
function captureCacheKey(scope, kind, parts) {
    if (!validCaptureCacheScope(scope)) return null;
    return JSON.stringify([1, scope.deviceId.toLowerCase(), kind, kind === "fast" ? scope.fastEpoch : scope.waveEpoch, ...parts]);
}

// Refleja reutilizacion y descargas en Historial; no recibe parametros.
function renderCaptureCacheStatus() {
    if (typeof document === "undefined") return;
    const target = document.getElementById("eventHistoryCacheStatus");
    if (target) target.textContent = "Cache local: " + captureCacheState.hits + " lecturas reutilizadas · " + captureCacheState.downloads + " descargas binarias";
}

// Serializa lectura/validacion/escritura; key identifica el binario, identity su origen, path su URL y decode/complete validan el contenido.
function cachedCapture(key, identity, path, signal, decode, complete) {
    const job = async () => {
        if (signal.aborted) throw new DOMException("Cancelado", "AbortError");
        const cached = key ? await readCaptureBuffer(key) : null;
        if (signal.aborted) throw new DOMException("Cancelado", "AbortError");
        if (cached) {
            try {
                const decoded = decode(cached);
                if (complete(decoded)) { ++captureCacheState.hits; renderCaptureCacheStatus(); return decoded; }
            } catch (_) { /* El CRC o la referencia no coinciden: recuperar el original. */ }
            await removeCaptureBuffer(key);
        }
        ++captureCacheState.downloads; renderCaptureCacheStatus();
        const response = await fetchEventHistory(path, signal, "capture");
        if (identity && response.identity && response.identity !== identity) throw new Error("El almacenamiento cambio durante la lectura. Actualice el historial.");
        const decoded = decode(response.buffer);
        if (signal.aborted) throw new DOMException("Cancelado", "AbortError");
        if (key && identity === response.identity && complete(decoded)) await writeCaptureBuffer(key, response.buffer);
        return decoded;
    };
    const pending = captureCacheState.queue.then(job);
    captureCacheState.queue = pending.catch(() => {});
    return pending;
}

// Lee un FAST completo reutilizable; id/timestamp provienen del indice, scope del firmware y signal cancela la accion.
function loadCachedFast(id, timestamp, scope, signal) {
    const stamp = Number(timestamp), knownTime = Number.isFinite(stamp) && stamp > 0;
    const key = knownTime ? captureCacheKey(scope, "fast", [4, id, stamp.toFixed(1)]) : null;
    const identity = scope ? scope.deviceId + ":" + scope.fastEpoch : null;
    return cachedCapture(key, identity, "/grid-events/data?eventId=" + id, signal, buffer => {
        const event = decodeGridEvent(buffer);
        if (event.id !== id || knownTime && Math.abs(event.timestamp - stamp) > 0.2) throw new Error("La captura no coincide con el indice.");
        event.cacheIdentity = identity;
        return event;
    }, event => !event.partial);
}

// Consulta las Wave actuales y comprueba su propietario; event es el FAST, scope su identidad y signal cancela.
async function loadCachedWaveIndex(event, scope, signal) {
    const deadline = Date.now() + 20000;
    const unixUs = Math.round(event.timestamp * 1000);
    if (!Number.isSafeInteger(unixUs) || unixUs < 0) throw Error("La captura no tiene una fecha válida para consultar su oscilografía.");
    let info;
    for (;;) {
        try { info = await fetchEventHistory("/wave-events?eventId=" + event.id + "&unixUs=" + unixUs, signal); }
        catch (error) {
            if (error.status !== 503 || error.reason !== "wave_index_busy") throw error;
            info = {ready: false, reason: error.reason};
        }
        if (info.ready !== false) break;
        if (!["wave_index_pending", "wave_index_busy"].includes(info.reason)) throw Error("No se pudo consultar el índice de oscilografías.");
        if (Date.now() >= deadline) throw Error("La consulta de oscilografías sigue ocupada. Intente nuevamente.");
        await waitWaveIndexRetry(signal, info.reason === "wave_index_busy" ? 1000 : 200);
    }
    if (scope && (info.fastIdentity !== scope.deviceId + ":" + scope.fastEpoch || info.waveIdentity !== scope.deviceId + ":" + scope.waveEpoch) ||
        Number.isFinite(info.eventTimestamp) && Math.abs(info.eventTimestamp - event.timestamp) > 0.2) {
        throw new Error("El registro cambio. Actualice la captura antes de leer su oscilografia.");
    }
    return info;
}

// Espera delayMs sin retener HTTP; signal cancela inmediatamente el reintento.
function waitWaveIndexRetry(signal, delayMs) {
    return new Promise((resolve, reject) => {
        const cancel = () => { clearTimeout(timer); signal.removeEventListener("abort", cancel); reject(new DOMException("Cancelado", "AbortError")); };
        const timer = setTimeout(() => { signal.removeEventListener("abort", cancel); resolve(); }, delayMs);
        signal.addEventListener("abort", cancel, {once: true});
        if (signal.aborted) cancel();
    });
}

// Lee una Wave enlazada al FAST; event es su cabecera, capture la ubicacion indexada, scope su generacion y signal cancela.
function loadCachedWave(event, capture, scope, signal) {
    const key = captureCacheKey(scope, "wave", [scope?.fastEpoch, event.id, event.timestamp.toFixed(1), capture.version, capture.id, capture.triggerUs, capture.endUs]);
    const identity = scope ? scope.deviceId + ":" + scope.waveEpoch : null;
    const path = "/wave-events/data?block=" + capture.block + "&page=" + capture.page + "&version=" + capture.version + "&waveId=" + capture.id;
    return cachedCapture(key, identity, path, signal, buffer => {
        const wave = decodeWaveEvent(buffer, event.id);
        if (wave.id !== capture.id) throw new Error("La oscilografia no coincide con el indice.");
        if (Number.isFinite(capture.triggerUs) && Math.abs(wave.rootUs + wave.triggerMs * 1000 - capture.triggerUs) > 1) throw new Error("El disparo de la oscilografia cambio.");
        return wave;
    }, wave => !wave.invalidPackets);
}

if (typeof module !== "undefined") module.exports = {validCaptureCacheScope, captureCacheKey};
