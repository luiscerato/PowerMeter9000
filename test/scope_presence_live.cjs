const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const url = process.argv[2];
if (!url) throw Error('Uso: node test/scope_presence_live.cjs wss://meter.luiscerato.com.ar/ws');
const quiet = new WebSocket(url);
quiet.binaryType = 'arraybuffer';
const context = vm.createContext({console: {log() {}, warn() {}}, ArrayBuffer, Uint8Array, Float64Array, DataView,
    WebSocket, setTimeout, clearTimeout, setInterval, clearInterval, document: {getElementById: () => null}, updateUI() {}});
for (const file of ['scope.js', 'scope_fft_vendor.js', 'scope_fft.js'])
    vm.runInContext(fs.readFileSync('data/' + file, 'utf8'), context);
vm.runInContext('globalThis.scope = Scope;', context);
const active = context.scope;
active.server = url;
active.drawWaves = () => {};
active.updateMetrics = () => {};
let openedAt = 0, expiredAfterMs = 0, quietCode = 0, activeFrames = 0, framesAfterExpiry = 0;
let quietFrames = 0, lastQuietFrameAt = 0;

// Registra cuando se abrio el cliente que no enviara presencia; sin parametros.
quiet.onopen = () => { openedAt = Date.now(); };
// Registra el cierre remoto; event aporta el codigo WebSocket.
quiet.onclose = event => { expiredAfterMs = Date.now() - openedAt; quietCode = event.code; };
// Registra si la conexion abandonada sigue recibiendo datos; event es el mensaje recibido.
quiet.onmessage = event => { if (typeof event.data !== 'string') { ++quietFrames; lastQuietFrameAt = Date.now(); } };
const processFrame = active.newData;
// Ejecuta el cliente real, incluidos presencia y watchdog; data contiene un mensaje del ESP.
active.newData = data => {
    processFrame(data);
    if (!(data instanceof ArrayBuffer)) return;
    ++activeFrames;
    if (expiredAfterMs) ++framesAfterExpiry;
};
active.start();

// Termina sin modificar ajustes del equipo y comprueba el aislamiento entre clientes; sin parametros.
setTimeout(() => {
    const stillActive = active.getState().state === 'ready';
    active.stop();
    quiet.close();
    console.log(JSON.stringify({expiredAfterMs, quietCode, stillActive, activeFrames, framesAfterExpiry,
        quietOpened: Boolean(openedAt), quietFrames, lastQuietFrameAfterMs: lastQuietFrameAt - openedAt}));
    assert.ok(expiredAfterMs >= 29000 && expiredAfterMs < 34000);
    assert.ok(quietCode === 1006 || quietCode === 1001, 'el vencimiento puede cerrar directamente TCP sin handshake WebSocket');
    assert.equal(stillActive, true);
    assert.ok(framesAfterExpiry >= 8);
}, 36000);
