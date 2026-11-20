let Scope = {};
const scopeMinTimeMsPerDiv = 5;

// Object.defineProperty(Scope, scaleCorriente, {value: [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100], writable: false, configurable: false});
const colors = {r: "#804000", s: "#000000", t: "#ff0000", n: "#0000cc", ir: "#cc6600", vr: "#ff9933", is: "#404040", is: "#999999", it: "#cc0000", vt: "#ff3333", in: "#0000cc"};

Scope.Data = {
    sample_rate: 8000,
    Scale: {Current: 10, Voltage: 100, Time: 10},
    Trigger: {Channel: 0, Edge: "rising", Value: 0, rechazo: 3},
    window: 320,
    configSynced: false,
    waves: [
        //Datos presentes en el gráfico
        {id: "ir", unit: "A", visible: true, name: "Corriente R", gain: 1.0, data: [], max: 1, min: -1, color: "#804000"},
        {id: "vr", unit: "V", visible: true, name: "Voltaje R", gain: 1.0, data: [], max: 1, min: -1, color: "#ff9933"},
        {id: "is", unit: "A", visible: true, name: "Corriente S", gain: 1.0, data: [], max: 1, min: -1, color: "#000000"},
        {id: "vs", unit: "V", visible: true, name: "Voltaje S", gain: 1.0, data: [], max: 1, min: -1, color: "#999999"},
        {id: "it", unit: "A", visible: true, name: "Corriente T", gain: 1.0, data: [], max: 1, min: -1, color: "#cc0000"},
        {id: "vt", unit: "V", visible: true, name: "Voltaje T", gain: 1.0, data: [], max: 1, min: -1, color: "#ff8080"},
        {id: "in", unit: "A", visible: true, name: "Corriente Neutro", gain: 1.0, data: [], max: 1, min: -1, color: "#0000cc"},
    ],
};

Scope.mode = "scope";
if (typeof appPhaseColor === 'function') for (const wave of Scope.Data.waves)
    wave.color = appPhaseColor(wave.id.slice(1),wave.id[0] === 'i' ? .22 : 0);

Scope.Buffer = [[], [], [], [], [], [], []]; //Datos que van llegando

Scope.begin = function (at, server, modo) {
    if (modo != null) Scope.mode = modo;
    console.log("Iniciando aplicación de osciloscopio!");
    Scope.initGraph(at);

    Scope.Data.waves.forEach((x) => {
        x.max = 2047 * x.gain; //Calcular valores máximos y mínimos
        x.min = -2048 * x.gain;
        x.data = Array(Scope.Data.window).fill(0);
    });
    Scope.Data.framePoints = 0;
    Scope.Data.framePeriodUs = 0;
    Scope.Data.frameTriggerIndex = -1;
    Scope.Data.configSynced = false;
    Scope.qualityGeneration = undefined;

    if (Scope.mode == "event") Scope.Data.timeStamps = [];
    else Scope.drawWaves();

    Scope.server = server;

    // Scope.start();
};

Scope.loadEventData = function (eventData) {
    Scope.Data.eventInfo = eventData.eventInfo;
    Scope.Data.waves[0].data = eventData.waves.ir;
    Scope.Data.waves[1].data = eventData.waves.vr;
    Scope.Data.waves[2].data = eventData.waves.is;
    Scope.Data.waves[3].data = eventData.waves.vs;
    Scope.Data.waves[4].data = eventData.waves.it;
    Scope.Data.waves[5].data = eventData.waves.vt;
    Scope.Data.waves[6].data = eventData.waves.in;
    Scope.Data.timeStamps = eventData.waves.t;

    Scope.drawWaves();
};

Scope.start = function () {
    if (!Scope.server) {
        console.warn("No hay servidor websocket configurado.");
        return;
    }
    Scope.shouldReconnect = true;
    if (Scope.websocket && (Scope.websocket.readyState === WebSocket.OPEN || Scope.websocket.readyState === WebSocket.CONNECTING)) return;
    clearTimeout(Scope.reconnectTimer);
    Scope.reconnectTimer = undefined;
    Scope.Data.configSynced = false;
    if (typeof updateUI === "function") updateUI();
    console.log("Intentando abrir una conexión WebSocket...");
    const socket = new WebSocket(Scope.server);
    Scope.websocket = socket;
    Scope.connectTimer = setTimeout(() => Scope.reconnect(socket), 10000);

    socket.onclose = (event) => {
        console.log("Conexión cerrada", event?.code);
        Scope.reconnect(socket);
    };

    socket.onerror = () => {
        if (Scope.websocket !== socket) return;
        console.log("Error de conexión...");
        Scope.reconnect(socket);
    };

    socket.onmessage = (event) => {
        if (Scope.websocket !== socket) return;
        Scope.newData(event.data);
    };

    socket.onopen = () => {
        if (Scope.websocket !== socket) return;
        clearTimeout(Scope.connectTimer);
        Scope.connectTimer = undefined;
        Scope.lastFrameAt = Date.now();
        Scope.lastHeartbeatAt = Scope.lastFrameAt;
        socket.send("scope_alive");
        if (Scope.view) socket.send("scope_view," + (Scope.view === "quality" ? 2 : 1));
        Scope.timer = setInterval(() => {
            if (Scope.websocket !== socket) return;
            if (socket.readyState === WebSocket.OPEN && Scope.view === "quality") socket.send("scope_view,2");
            if (socket.readyState === WebSocket.OPEN && !Scope.Data.configSynced) socket.send("scope_get");
            if (typeof ScopeFft !== "undefined" && ScopeFft.expireCapture) ScopeFft.expireCapture();
            if (socket.readyState === WebSocket.OPEN && Date.now() - Scope.lastHeartbeatAt >= 5000) {
                socket.send("scope_alive");
                Scope.lastHeartbeatAt = Date.now();
            }
            Scope.bandWidth += Scope.bytes;
            Scope.bandWidth /= 2;
            Scope.bytes = 0;

            // Una escala temporal corta reduce el caudal, pero no significa que el socket se haya detenido.
            const expectsFrames = Scope.qualityLocked ? ScopeFft.enabled && ScopeFft.getMask() : Scope.view !== "quality";
            if (expectsFrames && Date.now() - Scope.lastFrameAt > 5000) {
                console.warn("No se recibieron tramas válidas durante 5 segundos; reconectando...");
                Scope.reconnect(socket);
            }
        }, 1000);
    };

    socket.binaryType = "arraybuffer";
    Scope.bytes = 0;
    Scope.bandWidth = 0;
};

// Recupera un corte de red sin reiniciar las vistas ni reconectar tras Detener; socket es la conexion que fallo.
Scope.reconnect = function (socket) {
    if (Scope.websocket !== socket) return;
    clearInterval(Scope.timer);
    clearTimeout(Scope.connectTimer);
    Scope.timer = Scope.connectTimer = undefined;
    Scope.websocket = undefined;
    Scope.Data.configSynced = false;
    if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING) socket.close();
    if (Scope.shouldReconnect) {
        const delay = Math.min(5000, 500 * 2 ** Math.min(Scope.reconnectAttempts || 0, 4));
        Scope.reconnectAttempts = (Scope.reconnectAttempts || 0) + 1;
        Scope.reconnectTimer = setTimeout(() => {
            Scope.reconnectTimer = undefined;
            if (Scope.shouldReconnect && !Scope.websocket) Scope.start();
        }, delay);
    }
    if (typeof updateUI === "function") updateUI();
};

Scope.getState = function () {
    if (!Scope.websocket && Scope.reconnectTimer !== undefined) return {state: "connecting", speed: 0};
    if (Scope.websocket == undefined) return {state: "stop", speed: 0};
    if (Scope.websocket.readyState === WebSocket.OPEN) return {state: "ready", speed: Scope.bytes};
    else if (Scope.websocket.readyState === WebSocket.CONNECTING) return {state: "connecting", speed: 0};
    else if (Scope.websocket.readyState === WebSocket.CLOSING) return {state: "stop", speed: 0};
    else if (Scope.websocket.readyState === WebSocket.CLOSED) return {state: "stop", speed: 0};
};

Scope.stop = function () {
    Scope.shouldReconnect = false;
    Scope.reconnectAttempts = 0;
    clearTimeout(Scope.connectTimer);
    Scope.connectTimer = undefined;
    if (typeof ScopeFft !== "undefined") {
        ScopeFft.pending = null;
        if (ScopeFft.expanded) ScopeFft.setExpanded(false);
    }
    console.log("Cerrando web socket...");
    clearInterval(Scope.timer);
    Scope.timer = undefined;
    clearTimeout(Scope.reconnectTimer);
    Scope.reconnectTimer = undefined;
    const socket = Scope.websocket;
    Scope.websocket = undefined;
    if (socket && (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CONNECTING)) {
        socket.close();
    }
    Scope.Data.configSynced = false;
    if (typeof updateUI === "function") updateUI();
};

Scope.newData = function (data) {
    if (typeof data == "string") {
        if (data.includes("\n")) {
            data.split("\n").forEach(line => Scope.newData(line));
            return;
        }
        if (data.startsWith("quality_cfg,")) {
            Scope.applyQualityConfig(data);
            return;
        }
        if (data.startsWith("scope_fft_on,") && typeof ScopeFft !== "undefined") {
            ScopeFft.setEnabled(Number(data.slice(13)));
            return;
        }
        if (data.startsWith("scope_cfg,")) {
            Scope.applyRemoteConfig(data);
            return;
        }
        if (data === "scope_gap" && Scope.mode === "scope") {
            // No unir muestras anteriores y posteriores a bloques descartados por el ESP.
            Scope.Buffer.forEach((buffer) => { buffer.length = 0; });
            Scope.Data.waves.forEach((wave) => { wave.data = []; });
            Scope.drawWaves();
        }
    } else if (data instanceof ArrayBuffer) {
        const bytes = new Uint8Array(data);
        if (bytes[0] === 83 && bytes[1] === 70 && typeof ScopeFft !== "undefined") {
            if (ScopeFft.receive(bytes)) {
                Scope.reconnectAttempts = 0;
                Scope.bytes += data.byteLength;
                Scope.lastFrameAt = Date.now();
            }
            return;
        }
        const frame = Scope.decodeFrame(new Uint8Array(data));
        if (!frame) return;
        Scope.reconnectAttempts = 0;
        Scope.bytes += data.byteLength;
        Scope.lastFrameAt = Date.now();
        if (Scope.view === "quality" || Scope.qualityLocked) return;
        Scope.Data.framePoints = frame.points;
        Scope.Data.framePeriodUs = frame.periodUs;
        Scope.Data.frameTriggerIndex = Scope.getFrameTriggerPosition(frame);
        Scope.Data.frameClipMask = frame.clipMask;
        frame.waves.forEach((signal, index) => {
            Scope.Data.waves[index].data = signal.map((sample) => sample * Scope.Data.waves[index].gain);
        });
        Scope.updateMetrics(frame);
        Scope.drawWaves();
    }
};

// Aplica prioridad, rango y revision compartidos sin reenviar cambios; message contiene quality_cfg del ESP.
Scope.applyQualityConfig = function (message) {
    const values = message.split(",").slice(1).map(Number);
    if (values.length !== 5 || values.some(value => !Number.isFinite(value))) return;
    const [locked, voltage, current, mask, generation] = values;
    if (![0, 1].includes(locked) || voltage < 0.049 || voltage > 1001 || current < 0.049 || current > 1001 ||
        !Number.isInteger(mask) || mask < 0 || mask > 127 || !Number.isInteger(generation) || generation < 0 || generation > 0xffffffff) return;
    const rangeChanged = Scope.qualityCaptureVoltage !== voltage || Scope.qualityCaptureCurrent !== current;
    Scope.qualityCaptureVoltage = voltage;
    Scope.qualityCaptureCurrent = current;
    Scope.qualityLocked = Boolean(locked);
    Scope.qualityGeneration = generation;
    if (typeof powerQuality !== "undefined") {
        powerQuality.voltageRange = voltage;
        powerQuality.currentRange = current;
    }
    if (typeof ScopeFft !== "undefined" && ScopeFft.pending && ScopeFft.pending.generation !== generation) ScopeFft.pending = null;
    if (rangeChanged && typeof ScopeFft !== "undefined") {
        ScopeFft.captureNote = ScopeFft.channels.length ? "Captura anterior; esperando el nuevo rango" : "";
        ScopeFft.draw();
    }
    if (typeof updateUI === "function") updateUI();
};

// Muestra solo los 50 ms centrales del primer fragmento FFT; waves contiene muestras crudas y meta sus factores y trigger.
Scope.showQualityFrame = function (waves, meta) {
    if (Scope.view !== "scope") return;
    const center = meta.trigger === 0xffff ? 256 : meta.trigger;
    const start = Math.max(0, Math.min(112, Math.round(center) - 200));
    const frame = {points: 400, periodUs: 125, triggerIndex: meta.trigger === 0xffff ? -1 : meta.trigger - start,
        clipMask: meta.clipped, waves: waves.map(signal => signal.slice(start, start + 400))};
    Scope.Data.framePoints = frame.points;
    Scope.Data.framePeriodUs = 125;
    Scope.Data.waves.forEach((wave, index) => { wave.gain = index % 2 ? meta.voltageGain : meta.currentGain; });
    Scope.Data.frameTriggerIndex = frame.triggerIndex >= 0 ? Scope.getFrameTriggerPosition(frame) : 200;
    Scope.Data.frameClipMask = frame.clipMask;
    frame.waves.forEach((signal, index) => {
        Scope.Data.waves[index].gain = index % 2 ? meta.voltageGain : meta.currentGain;
        Scope.Data.waves[index].data = signal.map(value => value * Scope.Data.waves[index].gain);
    });
    Scope.updateMetrics(frame);
    Scope.drawWaves();
    const status = document.getElementById("scopeMetricsStatus");
    if (status && meta.trigger === 0xffff) status.textContent += " · Sin trigger";
};

// Aplica ajustes compartidos enviados por el ESP sin volver a publicarlos.
// message: linea scope_cfg recibida por WebSocket.
Scope.applyRemoteConfig = function (message) {
    const values = message.split(",").slice(1).map(Number);
    if (values.length !== 7 || values.some((value) => !Number.isFinite(value))) return;
    const [voltageScale, currentScale, timeDiv, triggerLevel, triggerChannel, rising, mask] = values;
    const maxGain = Scope.qualityLocked ? 0.4884 : 0.2442;
    if (voltageScale < 0.000024 || voltageScale > maxGain || currentScale < 0.000024 || currentScale > maxGain ||
        timeDiv < 0.01 || timeDiv > 100 || triggerChannel < 0 || triggerChannel > 6 ||
        (rising !== 0 && rising !== 1) || mask < 0 || mask > 127) return;

    Scope.applyingRemote = true;
    Scope.Data.configSynced = true;
    Scope.Data.Scale.Voltage = voltageScale * 409.6;
    Scope.Data.Scale.Current = currentScale * 409.6;
    Scope.Data.Scale.Time = Math.max(scopeMinTimeMsPerDiv, timeDiv);
    Scope.Data.Trigger.Channel = triggerChannel;
    Scope.Data.Trigger.Value = triggerLevel;
    Scope.Data.Trigger.Edge = rising ? "rising" : "falling";
    Scope.Data.waves.forEach((wave, index) => {
        wave.gain = (index % 2 ? voltageScale : currentScale);
        wave.visible = Boolean(mask & (1 << index));
        wave.max = 2047 * wave.gain;
        wave.min = -2048 * wave.gain;
    });
    Scope.applyingRemote = false;
    if (typeof updateUI === "function") updateUI();
    if (timeDiv < scopeMinTimeMsPerDiv) Scope.sendConfig();
    Scope.drawWaves();
};

// Publica la configuracion completa para mantener sincronizados todos los clientes.
// sin parametros; solo envia si el WebSocket esta abierto.
Scope.sendConfig = function () {
    if (Scope.applyingRemote || !Scope.Data.configSynced || !Scope.websocket || Scope.websocket.readyState !== WebSocket.OPEN) return;
    const mask = Scope.Data.waves.reduce((value, wave, index) => value | (wave.visible ? 1 << index : 0), 0);
    if (Scope.view === "quality" && typeof powerQuality !== "undefined") {
        powerQuality.sendSettings(mask);
        return;
    }
    if (Scope.qualityLocked) return;
    const trigger = Scope.Data.Trigger;
    Scope.websocket.send(`scope,${Scope.Data.waves[1].gain},${Scope.Data.waves[0].gain},${Scope.Data.Scale.Time},${trigger.Value},${trigger.Channel},${trigger.Edge === "rising" ? 1 : 0},${mask}`);
};

Scope.setOptions = function (options) {
    if (Scope.qualityLocked && Scope.view !== "quality") return;
    if (options.hasOwnProperty("corrDiv")) {
        let div = limitarValor(options.corrDiv, 0.01, 100.0);
        if (isNaN(div)) console.warn(`corrDiv no tiene un valor correcto, debe ser Number() ${div}`);
        else {
            Scope.Data.Scale.Current = div;
            let scale = (div * 10) / 4096;
            Scope.Data.waves[0].gain = scale;
            Scope.Data.waves[2].gain = scale;
            Scope.Data.waves[4].gain = scale;
            Scope.Data.waves[6].gain = scale;
            [0, 2, 4, 6].forEach((index) => {
                Scope.Data.waves[index].max = 2047 * scale;
                Scope.Data.waves[index].min = -2048 * scale;
            });
        }
    }
    if (options.hasOwnProperty("voltDiv")) {
        let div = limitarValor(options.voltDiv, 0.01, 500.0);
        if (isNaN(div)) console.warn(`voltDiv no tiene un valor correcto, debe ser Number() ${div}`);
        else {
            Scope.Data.Scale.Voltage = div;
            let scale = (div * 10) / 4096;
            Scope.Data.waves[1].gain = scale;
            Scope.Data.waves[3].gain = scale;
            Scope.Data.waves[5].gain = scale;
            [1, 3, 5].forEach((index) => {
                Scope.Data.waves[index].max = 2047 * scale;
                Scope.Data.waves[index].min = -2048 * scale;
            });
        }
    }
    if (options.hasOwnProperty("timeDiv")) {
        let div = limitarValor(options.timeDiv, scopeMinTimeMsPerDiv, 100.0);
        if (isNaN(div)) console.warn(`timeDiv no tiene un valor correcto, debe ser Number() ${div}`);
        else Scope.Data.Scale.Time = div;
    }
    if (options.hasOwnProperty("corrR")) Scope.Data.waves[0].visible = Boolean(options.corrR);
    if (options.hasOwnProperty("voltR")) Scope.Data.waves[1].visible = Boolean(options.voltR);
    if (options.hasOwnProperty("corrS")) Scope.Data.waves[2].visible = Boolean(options.corrS);
    if (options.hasOwnProperty("voltS")) Scope.Data.waves[3].visible = Boolean(options.voltS);
    if (options.hasOwnProperty("corrT")) Scope.Data.waves[4].visible = Boolean(options.corrT);
    if (options.hasOwnProperty("voltT")) Scope.Data.waves[5].visible = Boolean(options.voltT);
    if (options.hasOwnProperty("corrN")) Scope.Data.waves[6].visible = Boolean(options.corrN);

    if (options.hasOwnProperty("triggerLevel")) {
        let trigger = Scope.getTriggerInfo();
        let level = limitarValor(options.triggerLevel, trigger.minValue, trigger.maxValue);
        if (isNaN(level)) console.warn(`triggerLevel no tiene un valor correcto, debe ser Number() ${level}`);
        else Scope.Data.Trigger.Value = level;
    }
    if (options.hasOwnProperty("triggerChannel")) {
        let channel = limitarValor(options.triggerChannel, 0, 6);
        if (isNaN(channel)) console.warn(`triggerChannel no tiene un valor correcto, debe ser Number() ${channel}`);
        else Scope.Data.Trigger.Channel = channel;
    }
    if (options.hasOwnProperty("triggerEdge")) {
        if (options.triggerEdge == "falling") Scope.Data.Trigger.Edge = options.triggerEdge;
        else if (options.triggerEdge == "rising") Scope.Data.Trigger.Edge = options.triggerEdge;
        else console.warn(`triggerEdge no tiene un valor correcto, debe ser 'rising' or 'falling' ${options.triggerEdge}`);
    }

    //Si se desactiva el canal del trigger actual, pasar al próximo canal disponible
    if (!Scope.Data.waves[Scope.Data.Trigger.Channel].visible) {
        for (let index in Scope.Data.waves) {
            if (Scope.Data.waves[index].visible) {
                Scope.Data.Trigger.Channel = index;
                break;
            }
        }
    }
    Scope.sendConfig();
};

Scope.getDivScales = function () {
    let scales = {};
    scales.Voltage = Scope.Data.Scale.Voltage;
    scales.Current = Scope.Data.Scale.Current;
    scales.Time = Scope.Data.Scale.Time;
    scales.SampleRate = Scope.Data.sample_rate;
    scales.SampleCount = Scope.Data.window;
    return scales;
};

// Alterna un canal en ambas vistas y publica la seleccion; index es el canal ADE entre 0 y 6.
Scope.toggleWave = function (index) {
    if (Scope.qualityLocked && Scope.view !== "quality") return;
    const keys = ["corrR", "voltR", "corrS", "voltS", "corrT", "voltT", "corrN"];
    if (!Number.isInteger(index) || index < 0 || index >= keys.length) return;
    Scope.setOptions({[keys[index]]: !Scope.Data.waves[index].visible});
    if (typeof updateUI === "function") updateUI();
    Scope.drawWaves();
};

Scope.getChannelsInfo = function (ch) {
    if (ch != undefined && ch >= 0 && ch < Scope.Data.waves.length) {
        let info = (({data, ...resto}) => resto)(Scope.Data.waves[ch]);
        return info;
    }
    let channels = Scope.Data.waves.map(({data, ...resto}) => resto);
    return channels;
};

Scope.getTriggerInfo = function () {
    let trigger = Scope.Data.Trigger;
    let info = Scope.getChannelsInfo(trigger.Channel);

    trigger.unit = info.unit;
    trigger.minValue = info.min;
    trigger.maxValue = info.max;
    return trigger;
};

Scope.drawWaves = function () {
    if (Scope.view === "quality" && Scope.mode !== "event") return;
    let mode = Scope.mode === "event" ? false : true;
    let dataSeries = [];
    let ms = mode && Scope.Data.framePeriodUs ? Scope.Data.framePeriodUs / 1000 : (1 / Scope.Data.sample_rate) * 1000;
    //Ajustar escalas de tiempo
    if (mode && Scope.Data.framePoints) Scope.Data.window = Scope.Data.framePoints;
    else Scope.Data.window = 10 * Scope.Data.Scale.Time * (Scope.Data.sample_rate / 1000);
    const frameTrigger = Number.isFinite(Scope.Data.frameTriggerIndex) && Scope.Data.frameTriggerIndex >= 0
        ? Scope.Data.frameTriggerIndex : Math.floor(Scope.Data.window / 2);
    let minV = Infinity,
        maxV = -Infinity,
        minI = Infinity,
        maxI = -Infinity;

    Scope.Data.waves.forEach((element) => {
        const type = element.id.slice(0, 1);
        if (element.visible == true && (type == "v" || type == "i")) {
            let serie = {
                type: "line",
                axisYType: type == "i" ? "primary" : "secondary",
                name: element.name,
                showInLegend: true,
                markerSize: 0,
                yValueFormatString: type == "v" ? "0.00V" : type == "i" ? "0.000A" : "",
                color: element.color,
                signalUnit: type === "v" ? "V" : "A",
                shortName: (type === "v" ? "V " : "I ") + element.id.slice(1).toUpperCase(),
            };

            let points;
            if (mode) {
                points = element.data.map((value, index) => ({x: (index - frameTrigger) * ms, y: value}));
            }
            // points = element.data.map((value, index) => ({x: index, y: value}));
            else {
                let timeStamps = Array.isArray(Scope.Data.timeStamps) ? Scope.Data.timeStamps : [];
                points = element.data.map((value, index) => ({x: new Date(timeStamps[index] || Date.now()), y: value}));
            }

            serie.dataPoints = points;
            dataSeries.push(serie);
            let min = Math.min(...element.data);
            let max = Math.max(...element.data);
            if (type == "i") {
                if (min < minI) minI = min;
                if (max > maxI) maxI = max;
            } else {
                if (min < minV) minV = min;
                if (max > maxV) maxV = max;
            }
        }
    });

    let scaleY = (Scope.Data.Scale.Current * 10) / 2;
    let scaleY2 = (Scope.Data.Scale.Voltage * 10) / 2;

    Scope.options.data = dataSeries;
    if (mode) {
        const halfWindow = Scope.Data.window * ms / 2;
        Scope.options.axisX.minimum = -halfWindow;
        Scope.options.axisX.maximum = halfWindow;
        Scope.options.axisX.interval = Scope.Data.window * ms / 10;
        Scope.options.axisY.maximum = scaleY;
        Scope.options.axisY.minimum = -scaleY;
        Scope.options.axisY.interval = (2 * scaleY) / 10;

        Scope.options.axisY2.maximum = scaleY2;
        Scope.options.axisY2.minimum = -scaleY2;
        Scope.options.axisY2.interval = (2 * scaleY2) / 10;
    } else {
        // Scope.options.axisX.minimum = new Date(Scope.Data.eventInfo.start);
        // Scope.options.axisX.maximum = new Date(Scope.Data.eventInfo.end);
        // Scope.options.axisX.interval = (Scope.Data.window * ms) / 10
        let currentRange = Scope.getAxisRange(minI, maxI, 1);
        Scope.options.axisY.maximum = currentRange.maximum;
        Scope.options.axisY.minimum = currentRange.minimum;
        Scope.options.axisY.interval = currentRange.interval;

        let voltageRange = Scope.getAxisRange(minV, maxV, 10);
        Scope.options.axisY2.maximum = voltageRange.maximum;
        Scope.options.axisY2.minimum = voltageRange.minimum;
        Scope.options.axisY2.interval = voltageRange.interval;
        let eventInfo = Scope.Data.eventInfo || {};
        let firstEvent = Array.isArray(eventInfo.list) && eventInfo.list.length > 0 ? eventInfo.list[0] : {};
        let start = eventInfo.start ? new Date(eventInfo.start).toLocaleString() : "Evento";
        Scope.options.title.text = start;
        if (firstEvent.type || firstEvent.phase) Scope.options.title.text += ", evento " + (firstEvent.type || "") + " en " + (firstEvent.phase || "");
        let durationMs = eventInfo.eventDurationMs || firstEvent.durationMs || eventInfo.captureDurationMs || (eventInfo.sampleCount ? eventInfo.sampleCount * 10 : 0);
        if (durationMs) Scope.options.title.text += ", duración: " + durationMs + "ms.";
        Scope.options.exportFileName = Scope.options.title.text.replaceAll("/", "-").replaceAll(":", ".");
        // Scope.options.title.text += ", duración: " + (Scope.Data.eventInfo.end-Scope.Data.eventInfo.start) + "ms."
    }

    Scope.chart.render();
    if (mode && typeof ScopeFft !== "undefined") ScopeFft.syncScope();
};

// Decodifica una trama SC, su cabecera y sus canales de 12 bits.
// data: bytes completos recibidos desde el ESP.
Scope.decodeFrame = function (data) {
    if (data.length < 24 || data[0] !== 0x53 || data[1] !== 0x43 || data[2] !== 1) return null;
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const points = view.getUint16(4, true);
    const triggerIndex = view.getUint16(6, true);
    const periodUs = view.getUint16(8, true);
    const clipMask = data[10];
    if (points < 2 || points > 512 || periodUs < 125 || (triggerIndex !== 0xffff && triggerIndex >= points) ||
        data.length !== 24 + Math.ceil(points * 7 * 12 / 8)) return null;

    const waves = Array.from({length: 7}, () => []);
    const payload = data.subarray(24);
    for (let index = 0; index < points * 7; index += 1) {
        const bit = index * 12;
        const byte = bit >> 3;
        const shift = bit & 7;
        let value = payload[byte] | ((payload[byte + 1] || 0) << 8);
        value = (value >> shift) & 0x0fff;
        if (value & 0x800) value -= 0x1000;
        waves[index % 7].push(value);
    }
    return {points, periodUs, triggerIndex: triggerIndex === 0xffff ? -1 : triggerIndex, clipMask, waves};
};

// Centra el cruce entre puntos reducidos; frame contiene muestras y la marca de disparo del ESP.
Scope.getFrameTriggerPosition = function (frame) {
    const mark = frame.triggerIndex;
    if (mark < 0) return mark;
    const channel = Scope.Data.Trigger.Channel;
    const signal = frame.waves[channel];
    const level = Scope.Data.Trigger.Value / Scope.Data.waves[channel].gain;
    const rising = Scope.Data.Trigger.Edge === "rising";
    let position = mark, nearest = Infinity;
    for (let index = Math.max(1, mark - 1); index <= Math.min(frame.points - 1, mark + 2); ++index) {
        const previous = signal[index - 1], current = signal[index];
        const crossed = rising ? previous < level && current >= level : previous > level && current <= level;
        if (!crossed) continue;
        const crossing = index - 1 + (level - previous) / (current - previous);
        const distance = Math.abs(crossing - mark);
        if (distance < nearest) { position = crossing; nearest = distance; }
    }
    return position;
};

// Calcula RMS, potencias y frecuencia sobre las muestras escaladas de la última trama.
// frame: metadatos de la trama, incluyendo cantidad de puntos y periodo de muestreo.
Scope.updateMetrics = function (frame) {
    const phases = [
        {name: "R", current: Scope.Data.waves[0].data, voltage: Scope.Data.waves[1].data, currentChannel: 0, voltageChannel: 1},
        {name: "S", current: Scope.Data.waves[2].data, voltage: Scope.Data.waves[3].data, currentChannel: 2, voltageChannel: 3},
        {name: "T", current: Scope.Data.waves[4].data, voltage: Scope.Data.waves[5].data, currentChannel: 4, voltageChannel: 5},
        {name: "N", current: Scope.Data.waves[6].data, voltage: null, currentChannel: 6, voltageChannel: -1},
    ];
    const mean = (values) => values.reduce((sum, value) => sum + value, 0) / values.length;
    const rms = (values, average) => Math.sqrt(values.reduce((sum, value) => sum + (value - average) ** 2, 0) / values.length);
    const format = (value, digits, unit = "") => Number.isFinite(value) ? `${value.toFixed(digits)}${unit}` : "--";
    let totalActive = 0;
    let activePhases = 0;
    const clippedChannels = [];

    const rows = phases.map((phase) => {
        const currentMean = mean(phase.current);
        const currentRms = rms(phase.current, currentMean);
        const currentPeakToPeak = Math.max(...phase.current) - Math.min(...phase.current);
        const currentClip = Boolean(frame.clipMask & (1 << phase.currentChannel));
        const currentVisible = Scope.Data.waves[phase.currentChannel].visible;
        const currentTrace = `<span class="scopeMetricTrace ${currentVisible ? "isVisible" : "isHidden"}">I</span>`;
        if (currentClip) clippedChannels.push(Scope.Data.waves[phase.currentChannel].name);
        if (!phase.voltage) {
            return `<tr><th scope="row">N</th><td class="scopeMetricTraces"><span class="scopeMetricTrace isUnavailable">V</span>${currentTrace}</td>` +
                `<td>--</td><td>--</td><td>${format(currentPeakToPeak, 3, " A")}</td><td>${format(currentRms, 3, " A")}${currentClip ? " *" : ""}</td>` +
                `<td colspan="5" class="text-muted">Sin canal de tensión</td></tr>`;
        }

        const voltageMean = mean(phase.voltage);
        const voltageRms = rms(phase.voltage, voltageMean);
        const voltagePeakToPeak = Math.max(...phase.voltage) - Math.min(...phase.voltage);
        const count = Math.min(phase.current.length, phase.voltage.length);
        let active = 0;
        for (let index = 0; index < count; index += 1) {
            active += (phase.voltage[index] - voltageMean) * (phase.current[index] - currentMean);
        }
        active /= count;
        const apparent = voltageRms * currentRms;
        const powerFactor = apparent > 0 ? active / apparent : NaN;
        const voltageClip = Boolean(frame.clipMask & (1 << phase.voltageChannel));
        const voltageVisible = Scope.Data.waves[phase.voltageChannel].visible;
        const traces = `<span class="scopeMetricTrace ${voltageVisible ? "isVisible" : "isHidden"}">V</span>${currentTrace}`;
        if (voltageClip) clippedChannels.push(Scope.Data.waves[phase.voltageChannel].name);

        let frequency = NaN;
        if (voltageRms >= 1 && phase.voltage.length >= 8) {
            const crossings = [];
            for (let index = 1; index < phase.voltage.length; index += 1) {
                const previous = phase.voltage[index - 1] - voltageMean;
                const current = phase.voltage[index] - voltageMean;
                if (previous <= 0 && current > 0) crossings.push(index - 1 + (-previous / (current - previous)));
            }
            if (crossings.length >= 2) {
                let periodPoints = 0;
                for (let index = 1; index < crossings.length; index += 1) periodPoints += crossings[index] - crossings[index - 1];
                const averagePeriod = periodPoints / (crossings.length - 1);
                frequency = 1000000 / (averagePeriod * frame.periodUs);
                if (frequency < 40 || frequency > 70) frequency = NaN;
            }
        }

        const reactiveMagnitude = Math.sqrt(Math.max(0, apparent * apparent - active * active));
        let reactive = NaN;
        if (Number.isFinite(frequency)) {
            let voltageSin = 0, voltageCos = 0, currentSin = 0, currentCos = 0;
            for (let index = 0; index < count; index += 1) {
                const angle = 2 * Math.PI * frequency * index * frame.periodUs / 1000000;
                const sin = Math.sin(angle), cos = Math.cos(angle);
                voltageSin += (phase.voltage[index] - voltageMean) * sin;
                voltageCos += (phase.voltage[index] - voltageMean) * cos;
                currentSin += (phase.current[index] - currentMean) * sin;
                currentCos += (phase.current[index] - currentMean) * cos;
            }
            const voltagePhase = Math.atan2(voltageCos, voltageSin);
            const currentPhase = Math.atan2(currentCos, currentSin);
            const phaseSign = -Math.sin(currentPhase - voltagePhase);
            reactive = Math.abs(phaseSign) < 0.0001 ? 0 : reactiveMagnitude * Math.sign(phaseSign);
        }

        if (Number.isFinite(active)) { totalActive += active; ++activePhases; }
        return `<tr><th scope="row">${phase.name}</th><td class="scopeMetricTraces">${traces}</td>` +
            `<td>${format(voltagePeakToPeak, 1, " V")}${voltageClip ? " *" : ""}</td><td>${format(voltageRms, 1, " V")}${voltageClip ? " *" : ""}</td>` +
            `<td>${format(currentPeakToPeak, 3, " A")}${currentClip ? " *" : ""}</td><td>${format(currentRms, 3, " A")}${currentClip ? " *" : ""}</td>` +
            `<td>${format(active, 1, " W")}</td><td>${format(reactive, 1, " var")}</td><td>${format(apparent, 1, " VA")}</td>` +
            `<td>${format(powerFactor, 3)}</td><td>${format(frequency, 2, " Hz")}</td></tr>`;
    });

    const rowsElement = document.getElementById("scopeMetricsRows");
    if (rowsElement) rowsElement.innerHTML = rows.join("");
    const totalElement = document.getElementById("scopeMetricsTotal");
    if (totalElement) totalElement.textContent = `${activePhases === 3 ? "Potencia activa total" : "Potencia activa disponible"}: ${format(activePhases ? totalActive : NaN, 1, " W")}`;
    const windowElement = document.getElementById("scopeMetricsWindow");
    if (windowElement) windowElement.textContent = `${format(frame.points * frame.periodUs / 1000, 1, " ms")} · ${frame.points} puntos · ${format(frame.periodUs / 1000, 2, " ms/punto")}`;
    const statusElement = document.getElementById("scopeMetricsStatus");
    if (statusElement) statusElement.textContent = clippedChannels.length ? `Saturación: ${clippedChannels.join(", ")} (*)` : "";
};

Scope.redondearLimite = function (valor) {
    if (!isFinite(valor) || valor == 0) return 1;
    const signo = valor < 0 ? -1 : 1;
    valor = Math.abs(valor);
    const magnitud = Math.pow(10, Math.floor(Math.log10(valor)));
    const paso = magnitud >= 10 ? 10 : magnitud >= 5 ? 5 : 2;
    return Math.round(valor / paso) * paso * signo;
}

// Calcula un rango de eje valido aunque la senial sea plana.
// min: valor minimo. max: valor maximo. minSpan: apertura minima del eje.
Scope.getAxisRange = function (min, max, minSpan) {
    if (!isFinite(min) || !isFinite(max)) return {minimum: 0, maximum: minSpan, interval: minSpan / 10};

    let span = max - min;
    if (span < minSpan) {
        let center = (max + min) / 2;
        min = center - (minSpan / 2);
        max = center + (minSpan / 2);
        span = minSpan;
    } else {
        let margin = span * 0.1;
        min -= min <= 0 ? 0 : margin;
        max += margin;
    }

    let minimum = Scope.redondearLimite(min);
    let maximum = Scope.redondearLimite(max);
    if (maximum <= minimum) maximum = minimum + minSpan;
    return {minimum, maximum, interval: (maximum - minimum) / 10};
}

/*
				Incia el gráfico del osciloscopio*********************************************
*/

Scope.initGraph = function (container) {
    let events = false;
    Scope.options = {
  exportEnabled: true,
        zoomEnabled: true,
        zoomType: "xy",
        animationEnabled: Scope.mode === "event",
        // height: 600,
        title: {
            text: "Osciloscopio",
            fontSize: 18,
        },
        toolTip: chartTooltip.options({title: "Osciloscopio", mode: Scope.mode === "event" ? "date" : "relative", digits: 3}),
        legend: {
            cursor: "cross",
            verticalAlign: "bottom",
            horizontalAlign: "center",
            dockInsidePlotArea: false,
            itemclick: function (e) {
                if (Scope.mode === "scope") {
                    Scope.toggleWave(Scope.Data.waves.findIndex(wave => wave.name === e.dataSeries.name));
                    return;
                }
                console.log("legend click: " + e.dataPointIndex);
                console.log(e);
                if (typeof (e.dataSeries.visible) === "undefined" || e.dataSeries.visible) {
                    e.dataSeries.visible = false;
                } else {
                    e.dataSeries.visible = true;
                }
                e.chart.render();
            }
        },
        axisY: {
            lineThickness: 1,
            title: "Corriente",
            suffix: "A",
            minimum: -2048,
            maximum: 2048,
            gridDashType: "dot",
            interval: 4096 / 10,
        },
        axisY2: {
            title: "Voltaje",
            suffix: "V",
            minimum: -2048,
            maximum: 2048,
            gridDashType: "dot",
            interval: 4096 / 10,
        },
        axisX: {
            valueFormatString: Scope.mode === "event" ? "HH:mm:ss.fff" : "0.0ms",
            interval: 80,
            gridDashType: "dot",
            gridThickness: 1,
        },
        data: [],
    };

    Scope.chart = new CanvasJS.Chart(container, Scope.options);
    Scope.chart.render();
};

// Decompression of the data received by the ESP32
Scope.decode = function (data) {
    if (data instanceof Uint8Array == false) return; //Los datos deben ser un Uint8Array
    if (data.length % 3 != 0) return; //El array debe ser múltiplo de 3
    let expanded = [];

    // Loop through the data array with a step of 3
    for (let x = 0; x < data.length; x += 3) {
        let a = (data[x] & 0xff) | ((data[x + 1] & 0xf) << 8); // Extracting two 12-bit values from three 8-bit bytes
        let b = ((data[x + 2] & 0xff) << 4) | ((data[x + 1] & 0xf0) >> 4);
        if (a & 0x800) a |= 0xfffff800; // Sign extension for negative values
        if (b & 0x800) b |= 0xfffff800;
        expanded.push(a, b); // Push the two expanded values to the array
    }

    let waves = [[], [], [], [], [], [], []];
    expanded.map((value, index) => {
        waves[index % 7].push(value);
    });
    return waves;
};

Scope.checkTrigger = function (offset) {
    let ch = Scope.Data.Trigger.Channel;
    let edge = Scope.Data.Trigger.Edge === "rising" ? true : false;
    return findTriggerIndex(Scope.Buffer[ch], Scope.Data.Trigger.Value, edge, Scope.Data.Trigger.rechazo, offset);
};

function findTriggerIndex(signal, value, rising, rechazo, offset = 1) {
    let counter = 0;
    offset = (offset < 1 ? 1 : offset) || 1;

    for (let i = offset; i < signal.length; i++) {
        // Recorrer la onda para encontrar el índice de trigger
        const valorActual = signal[i],
            valorAnterior = signal[i - 1];
        let ok = false;

        if (rising && valorAnterior < valorActual) counter++; // Verificar el flanco de trigger ascendente
        else if (!rising && valorAnterior > valorActual) counter++; // Verificar el flanco de trigger descendente
        else counter = 0;

        if (rising && valorAnterior < value && valorActual >= value) ok = true; // Verificar el flanco de trigger ascendente
        else if (!rising && valorAnterior > value && valorActual <= value) ok = true; // Verificar el flanco de trigger descendente

        if (counter > rechazo && ok) {
            //console.log("Trigger a: ", i, "val: ", valorActual);
            return i;
        }
    }
    return -1;
}

function limitarValor(val, min, max) {
    min = Number(min);
    max = Number(max);
    let num = Number(val);

    if (isNaN(num)) return val; //i no es un numero, devuelve el mismo número
    if (!isNaN(min)) val = Math.max(num, min);
    if (!isNaN(max)) val = Math.min(num, max);

    return val;
}
