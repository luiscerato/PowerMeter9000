let Scope = {};

// Object.defineProperty(Scope, scaleCorriente, {value: [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100], writable: false, configurable: false});

const scaleCorriente = [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100]; //Amperes por división vertical
const scaleVoltaje = [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500]; //Voltios por división vertical
const scaleTiempo = [0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50]; //Ms por división horizontal
const colors = {r: "#804000", s: "#000000", t: "#ff0000", n: "#0000cc", ir: "#cc6600", vr: "#ff9933", is: "#404040", is: "#999999", it: "#cc0000", vt: "#ff3333", in: "#0000cc"};

Scope.Data = {
    sample_rate: 8000,
    Scale: {Current: 10, Voltage: 50, Time: 10},
    Trigger: {Channel: 0, Edge: "rising", Value: 0, rechazo: 3},
    window: 320,
    waves: [
        //Datos presentes en el gráfico
        {id: "ir", unit: "A", visible: true, name: "Corriente R", gain: 0.0732, data: [], max: 1, min: -1, color: "#804000"},
        {id: "vr", unit: "V", visible: true, name: "Voltaje R", gain: 0.244, data: [], max: 1, min: -1, color: "#ff9933"},
        {id: "is", unit: "A", visible: true, name: "Corriente S", gain: 0.0732, data: [], max: 1, min: -1, color: "#000000"},
        {id: "vs", unit: "V", visible: true, name: "Voltaje S", gain: 0.244, data: [], max: 1, min: -1, color: "#999999"},
        {id: "it", unit: "A", visible: true, name: "Corriente T", gain: 0.0732, data: [], max: 1, min: -1, color: "#cc0000"},
        {id: "vt", unit: "V", visible: true, name: "Voltaje T", gain: 0.244, data: [], max: 1, min: -1, color: "#ff8080"},
        {id: "in", unit: "A", visible: true, name: "Corriente Neutro", gain: 0.0732, data: [], max: 1, min: -1, color: "#0000cc"},
    ],
};

Scope.Buffer = [[], [], [], [], [], [], []]; //Datos que van llegando

Scope.begin = function (at, server) {
    console.log("Iniciando aplicación de osciloscopio!");
    Scope.initGraph(at);

    Scope.Data.waves.forEach((x) => {
        x.max = 2047 * x.gain; //Calcular valores máximos y mínimos
        x.min = -2048 * x.gain;
        x.data = Array(Scope.Data.window).fill(0);
    });

    Scope.drawWaves();

    Scope.server = server;
    // Scope.start();
};

Scope.start = function () {
    console.log("Trying to open a WebSocket connection…");
    Scope.websocket = new WebSocket(Scope.server);

    Scope.websocket.onclose = () => {
        console.log("Conexión cerrada...");
        Scope.stop();
    };

    Scope.websocket.onerror = () => {
        console.log("Error de conexión...");
        Scope.stop();
    };

    Scope.websocket.onmessage = (event) => {
        Scope.newData(event.data);
    };

    Scope.websocket.onopen = () => {
        Scope.timer = setInterval(() => {
            Scope.bandWidth += Scope.bytes;
            Scope.bandWidth /= 2;
            Scope.bytes = 0;

            if (Scope.bandWidth < 1024) {
                console.warn("No se ha detectado actividad en el socket en los últimos segundos...");
                Scope.stop();

                Scope.start(); //Reabrir el websocket
            }
        }, 1000);
    };

    Scope.websocket.binaryType = "arraybuffer";
    Scope.bytes = 0;
    Scope.bandWidth = 0;
};

Scope.getState = function () {
    if (Scope.websocket == undefined) return {state: "stop", speed: 0};
    if (Scope.websocket.readyState === WebSocket.OPEN) return {state: "ready", speed: Scope.bytes};
    else if (Scope.websocket.readyState === WebSocket.CONNECTING) return {state: "connecting", speed: 0};
    else if (Scope.websocket.readyState === WebSocket.CLOSING) return {state: "stop", speed: 0};
    else if (Scope.websocket.readyState === WebSocket.CLOSED) return {state: "stop", speed: 0};
};

Scope.stop = function () {
    console.log("Cerrando web socket...");
    clearInterval(Scope.timer);
    Scope.websocket.close();
};

Scope.newData = function (data) {
    if (typeof data == "string") {
        //   console.log(data);
    } else if (data instanceof ArrayBuffer) {
        Scope.bytes += data.byteLength;
        let output = new Uint8Array(data); //Convertir los datos binarios que llegaron a un array, luego descomprimir los mismos

        let waves = Scope.decode(output); //Unir las muestra nuevas con las que estaban el buffer
        if (waves == undefined) return;

        waves.forEach((signal, index) => {
            let scaled = signal.map((i) => i * Scope.Data.waves[index].gain); //Escalar los datos
            Scope.Buffer[index].push(...scaled);
        });

        //Una vez anexados los datos al buffer, se debe buscar la posición del trigger para dibujar ese punto al medio del gráfico.
        //Se usa un bloque while para dibujar todos los datos presentes de una sola vez y así evitar que se acumulen
        //Se necesita que en el buffer haya el doble de datos a mostrar, con esto se asegura que hay suficientes datos a ambos lados del punto de trigger
        let offset = Scope.Data.window / 2;
        let minLength = Scope.Data.window * 2 > 400 ? Scope.Data.window * 2 : 400;
        while (Scope.Buffer[0].length > minLength) {
            let index = 0; //Buscar el indice de trigger hasta que llegue a la mitad de la cantidad de datos a mostrar en el gráfico
            do {
                index = Scope.checkTrigger(index + 1);
            } while (index < offset && index > -1);
            if (index < 0) index = offset; //Si no se encuentra un punto de trigger, dibujar los datos que están en el buffer

            //Copiar la parte a mostrar del buffer en el buffer de los gráficos. Borrar los datos a dibujar del buffer.
            Scope.Buffer.forEach((buff, i) => {
                let start = index - Scope.Data.window / 2;
                Scope.Data.waves[i].data = buff.slice(start, start + Scope.Data.window + 1);
                buff.splice(0, start + Scope.Data.window + 1); //Borrar desde el primer elemento hasta el último que entra en el buffer del grafico
            });
            Scope.drawWaves();
        }
    }
};

Scope.setOptions = function (options) {
    if (options.hasOwnProperty("corrDiv")) {
        let div = limitarValor(options.corrDiv, 0, scaleCorriente.length - 1);
        if (isNaN(div)) console.warn(`corrDiv no tiene un valor correcto, debe ser Number() ${div}`);
        else Scope.Data.Scale.Current = scaleCorriente[div];
    }
    if (options.hasOwnProperty("voltDiv")) {
        let div = limitarValor(options.voltDiv, 0, scaleVoltaje.length - 1);
        if (isNaN(div)) console.warn(`voltDiv no tiene un valor correcto, debe ser Number() ${div}`);
        else Scope.Data.Scale.Voltage = scaleVoltaje[div];
    }
    if (options.hasOwnProperty("timeDiv")) {
        let div = limitarValor(options.timeDiv, 0, scaleTiempo.length - 1);
        if (isNaN(div)) console.warn(`timeDiv no tiene un valor correcto, debe ser Number() ${div}`);
        else Scope.Data.Scale.Time = scaleTiempo[div];
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
        let level = limitarValor(options.triggerLevel, trigger.min, trigger.max);
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
};

Scope.getDivScales = function () {
    let scales = {};
    scales.Voltage = {value: Scope.Data.Scale.Voltage, step: scaleVoltaje.indexOf(Scope.Data.Scale.Voltage)};
    scales.Current = {value: Scope.Data.Scale.Current, step: scaleVoltaje.indexOf(Scope.Data.Scale.Current)};
    scales.Time = {value: Scope.Data.Scale.Time, step: scaleVoltaje.indexOf(Scope.Data.Scale.Time)};
    scales.SampleRate = Scope.Data.sample_rate;
    scales.SampleCount = Scope.Data.window;
    return scales;
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
    let dataSeries = [];
    let ms = (1 / Scope.Data.sample_rate) * 1000;
    //Ajustar escalas de tiempo
    Scope.Data.window = 10 * Scope.Data.Scale.Time * (Scope.Data.sample_rate / 1000);
    let offset = Scope.Data.window / 2;

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
            };
            let points = element.data.map((value, index) => ({x: (index + -offset) * ms, y: value}));

            serie.dataPoints = points;
            dataSeries.push(serie);
        }
    });

    let scaleY = (Scope.Data.Scale.Current * 10) / 2;
    let scaleY2 = (Scope.Data.Scale.Voltage * 10) / 2;

    Scope.options.data = dataSeries;
    Scope.options.axisX.minimum = -offset * ms;
    Scope.options.axisX.maximum = offset * ms;
    Scope.options.axisX.interval = (Scope.Data.window * ms) / 10;
    Scope.options.axisY.maximum = scaleY;
    Scope.options.axisY.minimum = -scaleY;
    Scope.options.axisY.interval = (2 * scaleY) / 10;

    Scope.options.axisY2.maximum = scaleY2;
    Scope.options.axisY2.minimum = -scaleY2;
    Scope.options.axisY2.interval = (2 * scaleY2) / 10;

    Scope.chart.render();
};

/*
				Incia el gráfico del osciloscopio*********************************************
*/

Scope.initGraph = function (container) {
    Scope.options = {
        zoomEnabled: true,
        zoomType: "xy",
        animationEnabled: true,
        // height: 600,
        title: {
            text: "Osciloscopio",
        },
        toolTip: {
            shared: true,
            contentFormatter: function (e) {
                let content = `<strong>Posición:</strong> ${e.entries[0].dataPoint.x.toFixed(3)}ms <br/>`;
                for (let i = 0; i < e.entries.length; i++) {
                    let unit = e.entries[i].dataSeries.name.slice(0, 1).toLowerCase();
                    unit = unit == "c" ? "A" : unit == "v" ? "V" : "";
                    content += `<strong>${e.entries[i].dataSeries.name}:</strong> ${e.entries[i].dataPoint.y.toFixed(3)}${unit}<br/>`;
                }
                return content;
            },
        },
        legend: {
            cursor: "cross",
            verticalAlign: "bottom",
            horizontalAlign: "center",
            dockInsidePlotArea: false,
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
            valueFormatString: "0.0ms",
            interval: 80,
            gridDashType: "dot",
            gridThickness: 1,
        },
        data: [
            {
                type: "line",
                axisYType: "primary",
                name: "San Fransisco",
                showInLegend: true,
                markerSize: 0,
                yValueFormatString: "0.00V",
            },
            {
                type: "line",
                axisYType: "secondary",
                name: "San Fransisco",
                showInLegend: true,
                markerSize: 0,
                yValueFormatString: "0.000A",
            },
        ],
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
