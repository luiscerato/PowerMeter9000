const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

let now = 0;
let nextTimerId = 1;
const intervals = new Map();
const timeouts = new Map();

class FakeWebSocket {
    static CONNECTING = 0;
    static OPEN = 1;
    static CLOSING = 2;
    static CLOSED = 3;
    static instances = [];

    // Crea un socket de prueba para la URL solicitada.
    // url: direccion WebSocket que usaria la interfaz.
    constructor(url) {
        this.url = url;
        this.readyState = FakeWebSocket.CONNECTING;
        this.sentMessages = [];
        FakeWebSocket.instances.push(this);
    }

    // Abre el socket simulado y ejecuta su callback de apertura.
    open() {
        this.readyState = FakeWebSocket.OPEN;
        this.onopen?.();
    }

    // Cierra el socket simulado y ejecuta su callback de cierre.
    close() {
        this.readyState = FakeWebSocket.CLOSED;
        this.onclose?.();
    }

    // Registra el texto que la interfaz enviaría al servidor.
    // message: configuración publicada por el osciloscopio.
    send(message) {
        this.sentMessages.push(message);
    }

    // Entrega una trama binaria al callback registrado por la aplicación.
    deliver(data) {
        this.onmessage?.({data});
    }
}

const context = vm.createContext({
    console: {log() {}, warn() {}},
    Date: class extends Date { static now() { return now; } },
    WebSocket: FakeWebSocket,
    setInterval: (callback) => {
        const id = nextTimerId++;
        intervals.set(id, callback);
        return id;
    },
    clearInterval: (id) => intervals.delete(id),
    setTimeout: (callback) => {
        const id = nextTimerId++;
        timeouts.set(id, callback);
        return id;
    },
    clearTimeout: (id) => timeouts.delete(id),
    updateUI() {},
});
vm.runInContext(fs.readFileSync("data/scope.js", "utf8") + "\nglobalThis.scope = Scope;", context);

const scope = context.scope;
scope.drawWaves = () => {};
scope.updateMetrics = () => {};
scope.server = "ws://meter/ws";

const frame = vm.runInContext(`(() => {
    const buffer = new ArrayBuffer(108);
    const bytes = new Uint8Array(buffer);
    bytes.set([0x53, 0x43, 1]);
    const view = new DataView(buffer);
    view.setUint16(4, 8, true);
    view.setUint16(6, 0xffff, true);
    view.setUint16(8, 125, true);
    return buffer;
})()`, context);

scope.start();
const firstSocket = FakeWebSocket.instances[0];
firstSocket.open();
assert.equal(firstSocket.sentMessages[0], "scope_alive", "registrar presencia al abrir la vista");
const tick = intervals.get(scope.timer);

for (let second = 0; second < 8; second += 1) {
    now += 1000;
    firstSocket.deliver(frame);
    tick();
    assert.equal(scope.websocket, firstSocket, "una trama válida debe evitar la reconexión aunque el caudal sea bajo");
}
assert.ok(firstSocket.sentMessages.filter(message => message === "scope_alive").length >= 2, "renovar presencia cada cinco segundos");

now += 6000;
tick();
assert.equal(scope.websocket, undefined, "debe cerrar si no llegan tramas válidas durante cinco segundos");
assert.equal(timeouts.size, 1, "debe programar una reconexión tras una inactividad real");

const reconnect = [...timeouts.values()][0];
timeouts.clear();
reconnect();
const secondSocket = FakeWebSocket.instances[1];
assert.equal(scope.websocket, secondSocket);
firstSocket.onclose();
assert.equal(scope.websocket, secondSocket, "el cierre tardío del socket anterior no debe cerrar la conexión nueva");
secondSocket.open();
scope.applyRemoteConfig("scope_cfg,0.244140625,0.0244140625,0.1,0,0,1,127");
assert.equal(scope.Data.Scale.Time, 5, "una escala antigua menor al mínimo debe ajustarse a 5 ms/div");
assert.match(secondSocket.sentMessages.at(-1), /,5,/, "la escala corregida debe guardarse en la configuración compartida");

secondSocket.close();
assert.equal(scope.websocket, undefined);
assert.equal(scope.getState().state, "connecting");
assert.equal(timeouts.size, 1, "un cierre remoto debe reintentar sin intervencion");
const afterClose = [...timeouts.values()][0];
timeouts.clear(); afterClose();
const thirdSocket = FakeWebSocket.instances[2];
thirdSocket.onerror();
assert.equal(scope.websocket, undefined);
assert.equal(timeouts.size, 1, "un error no debe dejar la vista detenida ni duplicar reintentos");
scope.stop();
assert.equal(timeouts.size, 0, "Detener cancela la reconexion pendiente");
assert.equal(scope.getState().state, "stop");
thirdSocket.onclose();
assert.equal(timeouts.size, 0, "un cierre tardio no revive una vista detenida");
scope.start();
const stalled = FakeWebSocket.instances.at(-1);
const handshakeTimeout = [...timeouts.values()][0];
timeouts.clear(); handshakeTimeout();
assert.equal(scope.websocket, undefined);
assert.equal(timeouts.size, 1, "un handshake que no termina debe recuperarse");
scope.stop();

scope.view = "quality";
scope.start();
const qualitySocket = FakeWebSocket.instances.at(-1);
qualitySocket.open();
assert.ok(qualitySocket.sentMessages.includes("scope_view,2"), "calidad solicita solo espectro");
let temporalUpdates = 0;
scope.updateMetrics = () => { ++temporalUpdates; };
qualitySocket.deliver(frame);
assert.equal(temporalUpdates, 0, "calidad no procesa tramas temporales de otra vista");
vm.runInContext('globalThis.ScopeFft = {enabled: false, getMask: () => 127}', context);
now += 6000;
intervals.get(scope.timer)();
assert.equal(scope.websocket, qualitySocket, "desactivar FFT no debe provocar reconexiones");
scope.stop();
scope.view = "scope";
scope.start();
const temporalSocket = FakeWebSocket.instances.at(-1);
temporalSocket.open();
assert.ok(temporalSocket.sentMessages.includes("scope_view,1"), "osciloscopio solicita solo temporal");
temporalSocket.deliver(frame);
assert.equal(temporalUpdates, 1);
scope.stop();

console.log("PASS: reconexion por cierre, error, handshake e inactividad; Detener cancela reintentos y se ignoran callbacks tardios.");
