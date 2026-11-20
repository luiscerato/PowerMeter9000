const historyCache = {db: null, opening: null, scope: null, checkedAt: 0, timer: 0, controller: null,
    task: Promise.resolve(), foreground: 0, holdUntil: 0, paused: false, force: false, verified: new Map(), message: 'Preparando cache local...', bytes: 0, hits: 0};
const historyCacheDefaults = {enabled: true, days: 7, chunkKiB: 256, pauseSec: 2, limitMiB: 100, retainDays: 30};
let historyCacheOptions = {...historyCacheDefaults};

// Valida opciones locales; value contiene los ajustes y devuelve null si alguno esta fuera de rango.
function validateHistoryCacheOptions(value) {
    if (!value || typeof value.enabled !== 'boolean') return null;
    for (const [key, min, max] of [['days',1,31],['chunkKiB',2,512],['pauseSec',1,60],['limitMiB',8,512],['retainDays',1,365]])
        if (!Number.isInteger(value[key]) || value[key] < min || value[key] > max) return null;
    if (value.chunkKiB % 2) return null;
    return Object.fromEntries(Object.keys(historyCacheDefaults).map(key => [key, value[key]]));
}

// Lee ajustes locales y migra una sola vez el bloque inicial de 128 a 256 KiB; no recibe parametros.
function loadHistoryCacheOptions() {
    try {
        const saved = validateHistoryCacheOptions(JSON.parse(localStorage.getItem('powerMeterHistoryOptions')));
        if (localStorage.getItem('powerMeterHistoryBlockDefault') !== '256') {
            if (saved?.chunkKiB === 128) {
                saved.chunkKiB = 256;
                localStorage.setItem('powerMeterHistoryOptions',JSON.stringify(saved));
            }
            localStorage.setItem('powerMeterHistoryBlockDefault','256');
        }
        return saved || {...historyCacheDefaults};
    } catch (_) {return {...historyCacheDefaults};}
}

// Abre una base exclusiva de binarios historicos; sin parametros, devuelve null si no hay persistencia.
function openHistoryCache() {
    if (historyCache.db) return Promise.resolve(historyCache.db);
    if (historyCache.opening) return historyCache.opening;
    historyCache.opening = new Promise(resolve => {
        if (typeof indexedDB === 'undefined') return resolve(null);
        let settled = false;
        const finish = db => {if (!settled) {settled = true; clearTimeout(timer); historyCache.db = db; resolve(db);} else db?.close();};
        const timer = setTimeout(() => finish(null), 2000);
        try {
            const request = indexedDB.open('powerMeterHistory', 1);
            request.onupgradeneeded = () => {
                request.result.createObjectStore('chunks', {keyPath: 'key'});
                request.result.createObjectStore('metadata', {keyPath: 'key'});
            };
            request.onsuccess = () => {
                request.result.onversionchange = () => {request.result.close(); historyCache.db = null; historyCache.opening = null;};
                finish(request.result);
            };
            request.onerror = request.onblocked = () => finish(null);
        } catch (_) {finish(null);}
    });
    return historyCache.opening;
}

// Ejecuta una transaccion IDB; stores/mode definen acceso y work recibe la transaccion sin esperas externas.
async function historyCacheTransaction(stores, mode, work) {
    const db = await openHistoryCache();
    if (!db) throw Error('El navegador no permite guardar el historico localmente.');
    return new Promise((resolve, reject) => {
        const transaction = db.transaction(stores, mode);
        let result;
        transaction.oncomplete = () => resolve(result);
        transaction.onabort = transaction.onerror = () => reject(Error('No se pudo guardar o leer la cache local. Revise el espacio disponible.'));
        try {work(transaction, value => {result = value;});} catch (error) {transaction.abort(); reject(error);}
    });
}

// Lee solo metadatos de bloques sin cargar sus binarios; sin parametros.
function historyCacheMetadata() {
    return historyCacheTransaction(['metadata'], 'readonly', (tx, done) => {
        tx.objectStore('metadata').getAll().onsuccess = event => done(event.target.result);
    });
}

// Elimina bloques por edad de uso y limite LRU; extra reserva bytes para una escritura y clear borra todo.
async function trimHistoryCache(extra = 0, clear = false) {
    const entries = await historyCacheMetadata(), expired = Date.now() - historyCacheOptions.retainDays * 86400000;
    let bytes = entries.reduce((sum, entry) => sum + entry.bytes, 0);
    const remove = entries.sort((a,b) => a.usedAt - b.usedAt).filter(entry => {
        if (!clear && entry.usedAt >= expired && bytes + extra <= historyCacheOptions.limitMiB * 1048576) return false;
        bytes -= entry.bytes; return true;
    });
    if (remove.length) await historyCacheTransaction(['chunks','metadata'], 'readwrite', tx => {
        for (const entry of remove) {tx.objectStore('chunks').delete(entry.key); tx.objectStore('metadata').delete(entry.key);}
    });
    for (const entry of remove) historyCache.verified.delete(entry.key);
    historyCache.bytes = bytes; renderHistoryCacheStatus();
}

// Lee un bloque y renueva su fecha de uso, no su fecha de medicion; entry identifica el binario.
async function readHistoryCacheChunk(entry) {
    return historyCacheTransaction(['chunks','metadata'], 'readwrite', (tx, done) => {
        tx.objectStore('chunks').get(entry.key).onsuccess = event => {
            const buffer = event.target.result?.buffer;
            if (buffer) tx.objectStore('metadata').put({...entry, usedAt: Date.now()});
            done(buffer || null);
        };
    });
}

// Borra binario y metadatos de entries; no toca otras entradas ni la NAND.
async function removeHistoryCacheEntries(entries) {
    if (!entries.length) return;
    await historyCacheTransaction(['chunks','metadata'], 'readwrite', tx => {
        for (const entry of entries) {
            tx.objectStore('chunks').delete(entry.key); tx.objectStore('metadata').delete(entry.key);
        }
    });
    for (const entry of entries) historyCache.verified.delete(entry.key);
    historyCache.bytes = (await historyCacheMetadata()).reduce((sum,entry) => sum + entry.bytes,0);
}

// Identifica una version local del bloque; entry contiene su metadata, sin incluir ultimo acceso.
function historyCacheSignature(entry) {
    return JSON.stringify([entry.identity,entry.first,entry.last,entry.bytes,entry.storedAt]);
}

// Comprueba binario y continuidad completa contra metadata; entry identifica el bloque y buffer sus bytes.
function verifyHistoryCacheChunk(entry, buffer) {
    if (!buffer || buffer.byteLength !== entry.bytes || !Number.isInteger(entry.first) || !Number.isInteger(entry.last))
        throw Error('Bloque local incompleto.');
    const pages = inspectHistoryCacheChunk(buffer), count = buffer.byteLength / 2048;
    if (pages.length !== count || entry.from !== pages[0].from || entry.last !== ((entry.first + count - 1) & 0xFFFFFF) ||
        pages.some(page => page.sequence !== ((entry.first + page.offset / 2048) & 0xFFFFFF)))
        throw Error('Faltan paginas o hay secuencias incorrectas en el bloque local.');
    historyCache.verified.set(entry.key,historyCacheSignature(entry));
}

// Valida paginas, CRC y secuencias de un bloque; buffer contiene paginas fisicas alineadas de 2 KiB.
function inspectHistoryCacheChunk(buffer) {
    if (!buffer.byteLength || buffer.byteLength % 2048) throw Error('Descarga historica incompleta.');
    const pages = [], bytes = new Uint8Array(buffer);
    for (let offset = 0; offset < bytes.length; offset += 2048) {
        const page = bytes.subarray(offset, offset + 2048);
        if (page.every(value => value === 0) || page.every(value => value === 255)) continue;
        const view = new DataView(page.buffer, page.byteOffset, 2048), start = view.getUint32(0,true), period = view.getUint16(10,true), count = view.getUint16(12,true);
        const size = getHistorySampleSize(readHistory24(page,7),page[14]);
        if (!start || !period || !count || !size || !isValidHistoryConfig(readHistory24(page,7),page[14]) || 20 + count * size > 2048 ||
            view.getUint16(16,true) !== historyCrc16(page,16) || view.getUint16(18,true) !== historyCrc16(page.subarray(20),count * size)) throw Error('CRC o formato historico invalido; no se reemplazo la cache.');
        pages.push({offset, sequence: readHistory24(page,4), from: start, to: start + (count - 1) * period / 10});
    }
    if (!pages.length) throw Error('El bloque no contiene paginas historicas validas.');
    return pages;
}

// Consulta HTTP con timeout y cancelacion; path es relativo, signal cancela y binary conserva cabeceras.
async function historyCacheRequest(path, signal, binary = false) {
    const controller = new AbortController(), cancel = () => controller.abort();
    const timer = setTimeout(cancel, 45000);
    signal?.addEventListener('abort', cancel, {once: true});
    if (signal?.aborted) cancel();
    try {
        const response = await fetch(board.IP + path, {cache: 'no-store', signal: controller.signal});
        if (!response.ok) {
            const error = Error('Historico: HTTP ' + response.status); error.status = response.status;
            try {error.reason = (await response.json()).reason;} catch (_) {}
            throw error;
        }
        return binary ? {buffer: await response.arrayBuffer(), identity: response.headers.get('X-History-Identity'),
            first: Number(response.headers.get('X-History-First-Sequence')), pages: Number(response.headers.get('X-History-Pages'))} : await response.json();
    } finally {clearTimeout(timer); signal?.removeEventListener('abort', cancel);}
}

// Refresca inventario e identidad; signal cancela y force omite el breve cache de estado.
async function historyCacheScope(signal, force = false) {
    if (!force && historyCache.scope && Date.now() - historyCache.checkedAt < 60000) return historyCache.scope;
    const info = await historyCacheRequest('/history', signal);
    if (!info.ready || !/^[0-9a-f]{12}:[0-9a-f]{16}$/i.test(info.identity)) throw Error('El almacenamiento historico aun no esta listo.');
    const device = info.identity.slice(0,13).toLowerCase();
    await removeHistoryCacheEntries((await historyCacheMetadata()).filter(entry =>
        entry.identity?.slice(0,13).toLowerCase() === device && entry.identity !== info.identity));
    if (historyCache.scope && historyCache.scope.identity !== info.identity) historyCache.force = false;
    historyCache.scope = info; historyCache.checkedAt = Date.now(); return info;
}

// Obtiene un bloque reciente o cacheado; scope/from/to/before fijan rango y cursor, signal cancela, force evita lectura local.
async function getHistoryCacheChunk(scope, from, to, before, signal, force = false) {
    const entries = await historyCacheMetadata();
    const available = entries.filter(entry => entry.identity === scope.identity && entry.from <= to &&
        (before == null ? entry.to >= to : entry.last === before)).sort((a,b) => a.from - b.from);
    if (!force) for (const entry of available) {
        const buffer = await readHistoryCacheChunk(entry);
        if (buffer) {
            try {verifyHistoryCacheChunk(entry,buffer); ++historyCache.hits; return {entry, buffer};}
            catch (_) { /* Una copia local corrupta se reemplaza solo al validar la descarga. */ }
        }
    }
    let pages = historyCacheOptions.chunkKiB / 2;
    // Recorta en el limite ya guardado para no reenviar todo un bloque por unas pocas paginas nuevas.
    if (!force) for (const entry of entries.filter(item => item.identity === scope.identity)) {
        const distance = ((before ?? scope.latestSequence) - entry.last) & 0xFFFFFF;
        if (distance > 0 && distance < pages) pages = distance;
    }
    const query = new URLSearchParams({from: String(from), to: String(to), pages: String(pages), identity: scope.identity});
    if (!historyCache.foreground) query.set('bg','1');
    if (before != null) query.set('before',before);
    const result = await historyCacheRequest('/history/data?' + query, signal, true);
    if (result.identity !== scope.identity || result.buffer.byteLength !== result.pages * 2048 || result.pages > pages || !Number.isInteger(result.first))
        throw Error('El almacenamiento cambio durante la descarga.');
    const valid = inspectHistoryCacheChunk(result.buffer);
    if (valid.length !== result.pages || valid.some(page => page.sequence !== ((result.first + page.offset / 2048) & 0xFFFFFF)))
        throw Error('Las paginas historicas cambiaron durante la lectura; vuelva a consultar.');
    const entry = {key: JSON.stringify([scope.identity,result.first,result.pages]), identity: scope.identity,
        from: valid[0].from, to, first: result.first, last: (result.first + result.pages - 1) & 0xFFFFFF,
        bytes: result.buffer.byteLength, storedAt: Date.now(), usedAt: Date.now()};
    const previous = entries.find(item => item.key === entry.key);
    await trimHistoryCache(Math.max(0, entry.bytes - (previous?.bytes || 0)));
    await historyCacheTransaction(['chunks','metadata'], 'readwrite', tx => {
        tx.objectStore('chunks').put({key: entry.key, buffer: result.buffer}); tx.objectStore('metadata').put(entry);
    });
    historyCache.verified.set(entry.key,historyCacheSignature(entry));
    historyCache.bytes = (await historyCacheMetadata()).reduce((sum,item) => sum + item.bytes,0);
    renderHistoryCacheStatus(); return {entry, buffer: result.buffer};
}

// Interrumpe una descarga de fondo para dar prioridad al usuario; holdMs establece la pausa minima.
function pauseHistoryCacheRequest(holdMs = 1500) {
    historyCache.holdUntil = Math.max(historyCache.holdUntil, Date.now() + holdMs);
    historyCache.controller?.abort();
}

// Indica si el fondo debe ceder por vista, descargas o interaccion; sin parametros.
function historyCacheBusy() {
    return document.hidden || historyCache.foreground || Date.now() < historyCache.holdUntil ||
        ['scopePage','qualityPage','gridEvents','eventHistory','history','logs'].includes(typeof showAppPage === 'function' ? showAppPage.current : '') ||
        (typeof settingsBackupBusy !== 'undefined' && settingsBackupBusy);
}

// Descarga un bloque por ciclo desde hoy hacia atras; sin parametros, nunca acumula temporizadores ni peticiones.
async function syncHistoryCache() {
    clearTimeout(historyCache.timer);
    let wait = historyCacheOptions.pauseSec * 1000;
    const controller = new AbortController(); historyCache.controller = controller;
    try {
        if ((!historyCacheOptions.enabled && !historyCache.force) || historyCache.paused || historyCacheBusy()) return;
        if (!await openHistoryCache()) {historyCache.message = 'Cache no disponible en este navegador.'; wait = 60000; return;}
        const scope = await historyCacheScope(controller.signal);
        if (!scope.committedPages || !scope.committedTo) {historyCache.message = 'Todavia no hay paginas historicas completas.'; wait = 60000; return;}
        const day = new Date(scope.committedTo * 1000); day.setHours(0,0,0,0); day.setDate(day.getDate() - historyCacheOptions.days + 1);
        const from = Math.max(scope.oldestTimestamp, Math.floor(day.getTime()/1000));
        let to = scope.committedTo, before = scope.latestSequence;
        const entries = (await historyCacheMetadata()).filter(entry => entry.identity === scope.identity);
        if (!historyCache.force) {
            for (let count=0; count<=entries.length; ++count) {
                const entry = entries.find(item => item.from <= to && (before == null ? item.to >= to : item.last === before));
                if (!entry) break;
                // Al abrir, se relee un bloque local por ciclo antes de dar su cobertura por confirmada.
                if (historyCache.verified.get(entry.key) !== historyCacheSignature(entry)) {
                    try {verifyHistoryCacheChunk(entry,await readHistoryCacheChunk(entry));}
                    catch (_) {await removeHistoryCacheEntries([entry]); break;}
                    historyCache.message = 'Verificando paginas guardadas: ' + entry.first + ' a ' + entry.last;
                    return;
                }
                if (entry.from <= from) {historyCache.message = 'Historico reciente sincronizado.'; wait = 60000; return;}
                to = entry.from; before = (entry.first - 1) & 0xFFFFFF;
            }
        } else {
            to = historyCache.force.to; before = historyCache.force.before;
        }
        if (!historyCache.force && entries.some(entry => entry.to >= scope.committedTo) &&
            historyCache.bytes + historyCacheOptions.chunkKiB * 1024 > historyCacheOptions.limitMiB * 1048576) {
            historyCache.message = 'Limite local alcanzado; se conservan los datos mas recientes.'; wait = 60000; return;
        }
        historyCache.message = 'Descargando ' + new Date(to*1000).toLocaleDateString() + '...'; renderHistoryCacheStatus();
        const {entry} = await getHistoryCacheChunk(scope,from,to,before,controller.signal,!!historyCache.force);
        if (historyCache.force) historyCache.force = entry.from <= from ? false : {to: entry.from, before: (entry.first - 1) & 0xFFFFFF};
        historyCache.message = 'Guardado hasta ' + new Date(entry.from*1000).toLocaleString();
    } catch (error) {
        if (error.name !== 'AbortError') {
            historyCache.message = error.reason === 'history_background_busy' ? 'Esperando: el equipo esta atendiendo otra captura o descarga.' :
                error.status === 404 ? 'Sin mas paginas en el periodo.' : error.message;
            if (error.reason !== 'history_background_busy') historyCache.force = false;
            historyCache.checkedAt = 0; wait = error.reason === 'history_background_busy' ? 5000 : 60000;
        }
    } finally {
        if (historyCache.controller === controller) historyCache.controller = null;
        renderHistoryCacheStatus();
        historyCache.timer = setTimeout(() => {historyCache.task = syncHistoryCache();}, wait);
    }
}

// Consulta un periodo usando la misma cache del fondo; from/to son Unix segundos, los buffers se decodifican solo al visualizar.
async function loadCachedHistoryRange(from, to) {
    pauseHistoryCacheRequest(); ++historyCache.foreground;
    const pending = historyCache.task;
    try {
        await pending;
        const scope = await historyCacheScope(null,true), chunks = [], seen = new Set();
        let loadedBytes = 0;
        let cursor = Math.min(to,scope.committedTo), before = null;
        if (scope.committedPages && cursor >= from) {
            for (let count=0; count<10000; ++count) {
                let result;
                try {result = await getHistoryCacheChunk(scope,from,cursor,before,null);}
                catch (error) {if (error.status === 404) break; throw error;}
                if (seen.has(result.entry.key)) throw Error('El cursor historico no avanzo.');
                loadedBytes += result.buffer.byteLength;
                if (loadedBytes > 16 * 1048576) throw Error('El periodo es demasiado grande para graficarlo completo. Reduzca las fechas; los bloques descargados quedan en cache.');
                seen.add(result.entry.key); chunks.push(result.buffer);
                setHistoryStatus('Leyendo historico: ' + chunks.length + ' bloques; ' + historyCache.hits + ' reutilizados.');
                if (result.entry.from <= from || result.entry.from <= scope.oldestTimestamp) break;
                cursor = result.entry.from; before = (result.entry.first - 1) & 0xFFFFFF;
            }
        }
        // La pagina en curso se consulta aparte y nunca se fija como cache definitiva.
        if (to >= scope.committedTo) {
            try {chunks.push((await historyCacheRequest('/history/data?' + new URLSearchParams({from: String(Math.max(from,scope.committedTo)),to: String(to),identity: scope.identity}),null,true)).buffer);}
            catch (error) {if (error.status !== 404) throw error;}
        }
        const pages = new Map();
        for (const buffer of chunks) for (const page of inspectHistoryCacheChunk(buffer)) {
            const bytes = new Uint8Array(buffer,page.offset,2048), old = pages.get(page.sequence);
            if (!old || new DataView(bytes.buffer,bytes.byteOffset).getUint16(12,true) >= new DataView(old.buffer,old.byteOffset).getUint16(12,true)) pages.set(page.sequence,bytes);
        }
        const buffer = new Uint8Array(pages.size * 2048);
        [...pages.values()].sort((a,b) => new DataView(a.buffer,a.byteOffset).getUint32(0,true) - new DataView(b.buffer,b.byteOffset).getUint32(0,true))
            .forEach((page,index) => buffer.set(page,index*2048));
        return buffer.buffer;
    } finally {--historyCache.foreground; renderHistoryCacheStatus();}
}

// Crea la seccion de configuracion local, sin endpoints ni NVS; sin parametros.
function createApplicationSettingsHtml() {
    return '<section class="settingsSection"><div class="settingsSectionHeader settingsCompactHeader"><div><h2>Cache del historico</h2><p>Ajustes de este navegador. Descarga reciente de a un bloque; las consultas y el osciloscopio tienen prioridad.</p></div><div class="settingsActions"><button type="button" id="appCacheReload" class="btn btn-sm btn-outline-secondary" title="Recargar ajustes locales" aria-label="Recargar ajustes locales">&#8635;</button><button type="submit" form="appCacheForm" class="btn btn-sm btn-primary">Guardar</button></div></div>' +
        '<form id="appCacheForm"><div class="appCacheFields"><label><input type="checkbox" id="appCache_enabled"> Sincronizacion automatica</label>' +
        [['days','Dias recientes',1,31,'dias'],['chunkKiB','Bloque',2,512,'KiB'],['pauseSec','Pausa entre bloques',1,60,'s'],['limitMiB','Limite local',8,512,'MiB'],['retainDays','Eliminar sin uso',1,365,'dias']].map(([key,label,min,max,unit]) =>
            '<label>' + label + ' <input id="appCache_' + key + '" type="number" min="' + min + '" max="' + max + '" step="' + (key==='chunkKiB'?2:1) + '" required> <span>' + unit + '</span></label>').join('') + '</div></form>' +
        '<div class="appCacheActions"><button id="appCachePause" type="button" class="btn btn-sm btn-outline-secondary">Pausar</button><button id="appCacheForce" type="button" class="btn btn-sm btn-outline-primary">Forzar sincronizacion</button><button id="appCacheClear" type="button" class="btn btn-sm btn-outline-danger">Borrar cache historica</button></div>' +
        '<div id="appCacheStatus" class="small" role="status"></div><div id="appCacheUsage" class="small text-muted"></div><p class="small text-muted mt-2 mb-0">Se elimina por tiempo sin consultar, no por la fecha de la medicion. Una consulta antigua tambien queda guardada. Cada direccion y navegador conserva su propia cache; no es una copia de seguridad permanente.</p></section>';
}

// Rellena campos con opciones locales; sin parametros.
function fillHistoryCacheSettings() {
    for (const key of Object.keys(historyCacheDefaults)) {
        const field = document.getElementById('appCache_' + key);
        if (field) {if (key === 'enabled') field.checked = historyCacheOptions[key]; else field.value = historyCacheOptions[key];}
    }
    renderHistoryCacheStatus();
}

// Refleja estado, espacio y pausa en Aplicacion y el visor historico; sin parametros.
function renderHistoryCacheStatus() {
    const status = document.getElementById('appCacheStatus'), usage = document.getElementById('appCacheUsage'), pause = document.getElementById('appCachePause');
    if (status) status.textContent = historyCache.paused ? 'Sincronizacion pausada.' : !historyCacheOptions.enabled ? 'Sincronizacion automatica desactivada.' : historyCache.message;
    if (usage) usage.textContent = (historyCache.bytes / 1048576).toFixed(2) + ' / ' + historyCacheOptions.limitMiB + ' MiB locales · ' + historyCache.hits + ' bloques reutilizados';
    if (pause) pause.textContent = historyCache.paused ? 'Continuar' : 'Pausar';
    for (const id of ['appCacheClear','appCacheForce']) {const button = document.getElementById(id); if (button) button.disabled = !!historyCache.foreground;}
}

// Reprograma un unico ciclo luego de una accion local; sin parametros, respeta la pausa configurada.
function scheduleHistoryCache() {
    clearTimeout(historyCache.timer);
    historyCache.timer = setTimeout(() => {historyCache.task = syncHistoryCache();},Math.max(1500,historyCacheOptions.pauseSec*1000));
}

// Inicializa persistencia, botones y sincronizacion diferida despues de cargar la app; sin parametros.
async function initHistoryCache() {
    historyCacheOptions = loadHistoryCacheOptions();
    fillHistoryCacheSettings();
    document.getElementById('appCacheReload')?.addEventListener('click', fillHistoryCacheSettings);
    document.getElementById('appCacheForm')?.addEventListener('submit', async event => {
        event.preventDefault();
        const value = {};
        for (const key of Object.keys(historyCacheDefaults)) {const input = document.getElementById('appCache_' + key); value[key] = key === 'enabled' ? input.checked : Number(input.value);}
        const valid = validateHistoryCacheOptions(value);
        if (!valid) {historyCache.message = 'Revise los rangos: el bloque debe ser multiplo de 2 KiB.'; renderHistoryCacheStatus(); return;}
        try {
            localStorage.setItem('powerMeterHistoryOptions', JSON.stringify(valid)); historyCacheOptions = valid;
            pauseHistoryCacheRequest(); await historyCache.task; await trimHistoryCache(); historyCache.message = 'Ajustes locales guardados.';
            scheduleHistoryCache();
        } catch (error) {historyCache.message = error.message;}
        renderHistoryCacheStatus();
    });
    document.getElementById('appCachePause')?.addEventListener('click', () => {historyCache.paused = !historyCache.paused; pauseHistoryCacheRequest(); scheduleHistoryCache(); renderHistoryCacheStatus();});
    document.getElementById('appCacheForce')?.addEventListener('click', async () => {
        pauseHistoryCacheRequest(); await historyCache.task;
        try {
            const scope = await historyCacheScope(null,true);
            historyCache.force = {to: scope.committedTo,before: null}; historyCache.paused = false;
            historyCache.message = 'Se volvera a descargar el periodo reciente, conservando la copia hasta validarlo.';
            scheduleHistoryCache();
        } catch (error) {historyCache.message = error.message;}
        renderHistoryCacheStatus();
    });
    document.getElementById('appCacheClear')?.addEventListener('click', async () => {
        if (!confirm('Borrar el historico descargado de este navegador? No se borrara la NAND ni las capturas de eventos.')) return;
        historyCache.paused = true; pauseHistoryCacheRequest(); await historyCache.task;
        try {await trimHistoryCache(0,true); historyCache.force = false; historyCache.message = 'Cache historica borrada.';}
        catch (error) {historyCache.message = error.message;}
        renderHistoryCacheStatus();
    });
    document.addEventListener('pointerdown', () => pauseHistoryCacheRequest(), {passive:true});
    document.addEventListener('visibilitychange', () => {if (document.hidden) pauseHistoryCacheRequest();});
    try {await trimHistoryCache();} catch (error) {historyCache.message = error.message;}
    historyCache.timer = setTimeout(() => {historyCache.task = syncHistoryCache();},5000);
}
