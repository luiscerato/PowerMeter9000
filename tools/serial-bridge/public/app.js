const statusLabel = document.querySelector("#status");
const noticeLabel = document.querySelector("#notice");
const consoleView = document.querySelector("#console");
const receivedLabel = document.querySelector("#received");
const connectButton = document.querySelector("#connect");
const disconnectButton = document.querySelector("#disconnect");
let serialPort = null;
let serialReader = null;
let serialSocket = null;
let readTask = null;
let reading = false;
let receivedBytes = 0;
const displayDecoder = new TextDecoder();

// Muestra message y marca connected para reflejar el estado actual.
function setStatus(message, connected = false) {
	statusLabel.textContent = message;
	statusLabel.classList.toggle("connected", connected);
}

// Anexa text a la consola y conserva como maximo un megabyte de salida.
function appendText(text) {
	consoleView.value += text;
	if (consoleView.value.length > 1024 * 1024) consoleView.value = consoleView.value.slice(-1024 * 1024);
	consoleView.scrollTop = consoleView.scrollHeight;
}

// Muestra event, el mensaje WebSocket recibido del servidor local.
function handleBridgeMessage(event) {
	if (typeof event.data === "string") appendText(event.data);
}

// Abre el WebSocket local para enviar la salida serie al monitor de esta computadora.
async function connectBridge() {
	const socket = new WebSocket(`ws://${location.host}/serial`);
	socket.binaryType = "arraybuffer";
	socket.addEventListener("message", handleBridgeMessage);
	// Espera el WebSocket; resolve y reject completan la promesa con exito o error.
	await new Promise((resolve, reject) => {
		socket.addEventListener("open", resolve, { once: true });
		socket.addEventListener("error", reject, { once: true });
	});
	return socket;
}

// Lee el puerto abierto y copia sus bytes a la consola y al WebSocket; no recibe parametros.
async function readSerial() {
	try {
		while (reading) {
			const { value, done } = await serialReader.read();
			if (done) break;
			if (!value) continue;
			receivedBytes += value.byteLength;
			receivedLabel.textContent = `${receivedBytes.toLocaleString()} bytes`;
			appendText(displayDecoder.decode(value, { stream: true }));
			if (serialSocket?.readyState === WebSocket.OPEN) serialSocket.send(value);
		}
	} catch (error) {
		appendText(`\r\n[serial] ${error.message}\r\n`);
	} finally {
		reading = false;
		if (serialReader) {
			try { serialReader.releaseLock(); } catch {}
			serialReader = null;
		}
		if (serialPort) {
			try { await serialPort.close(); } catch {}
			serialPort = null;
		}
		if (serialSocket) {
			serialSocket.close();
			serialSocket = null;
		}
		connectButton.disabled = !navigator.serial || !window.isSecureContext;
		disconnectButton.disabled = true;
		setStatus("Desconectado");
	}
}

// Pide seleccionar serialPort, lo abre a 115200 baudios y comienza a leer; no recibe parametros.
async function connectSerial() {
	if (!window.isSecureContext || !navigator.serial) {
		noticeLabel.textContent = "Web Serial no esta habilitado en este origen. Revisa las instrucciones del README.";
		return;
	}
	try {
		serialSocket = await connectBridge();
		serialPort = await navigator.serial.requestPort();
		await serialPort.open({ baudRate: 115200, dataBits: 8, stopBits: 1, parity: "none", flowControl: "none" });
		serialReader = serialPort.readable.getReader();
		reading = true;
		receivedBytes = 0;
		receivedLabel.textContent = "0 bytes";
		connectButton.disabled = true;
		disconnectButton.disabled = false;
		noticeLabel.textContent = "";
		setStatus("Conectado", true);
		appendText("\r\n[serial] puerto abierto a 115200 baudios\r\n");
		readTask = readSerial();
	} catch (error) {
		if (serialSocket) serialSocket.close();
		serialSocket = null;
		if (serialPort) {
			try { await serialPort.close(); } catch {}
			serialPort = null;
		}
		noticeLabel.textContent = error.message;
		setStatus("Desconectado");
	}
}

// Cancela la lectura activa y libera serialPort; no recibe parametros.
async function disconnectSerial() {
	reading = false;
	if (serialReader) {
		try { await serialReader.cancel(); } catch {}
	}
	if (readTask) await readTask;
}

// Descarga el contenido de la consola visible como archivo; no recibe parametros.
function saveLog() {
	const file = new Blob([consoleView.value], { type: "text/plain;charset=utf-8" });
	const link = document.createElement("a");
	link.href = URL.createObjectURL(file);
	link.download = `power-meter-serial-${new Date().toISOString().replaceAll(":", "-")}.log`;
	link.click();
	URL.revokeObjectURL(link.href);
}

// Limpia la consola y su contador sin cerrar el puerto; no recibe parametros.
function clearConsole() {
	consoleView.value = "";
	receivedBytes = 0;
	receivedLabel.textContent = "0 bytes";
}

connectButton.addEventListener("click", connectSerial);
disconnectButton.addEventListener("click", disconnectSerial);
document.querySelector("#clear").addEventListener("click", clearConsole);
document.querySelector("#save").addEventListener("click", saveLog);

if (!window.isSecureContext || !navigator.serial) {
	connectButton.disabled = true;
	noticeLabel.textContent = "Web Serial requiere Chrome o Edge en un origen seguro; consulta tools/serial-bridge/README.md.";
}
