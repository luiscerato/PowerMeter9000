import { createServer } from "node:http";
import { networkInterfaces } from "node:os";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { WebSocket, WebSocketServer } from "ws";

const port = Number(process.env.PORT || 3001);
const root = dirname(fileURLToPath(import.meta.url));
const monitorClients = new Set();
const webSocketServer = new WebSocketServer({ noServer: true, maxPayload: 64 * 1024 });
let serialClient = null;

// Devuelve las IPv4 locales para mostrar las direcciones donde se puede abrir la interfaz.
function getLanAddresses() {
	const addresses = [];
	for (const interfaces of Object.values(networkInterfaces())) {
		for (const item of interfaces || []) {
			if (item.family === "IPv4" && !item.internal) addresses.push(item.address);
		}
	}
	return addresses;
}

// Atiende la pagina de control y el estado minimo del puente; request y response son HTTP.
async function handleHttpRequest(request, response) {
	const path = new URL(request.url || "/", "http://localhost").pathname;
	if (request.method !== "GET") {
		response.writeHead(405).end("Method Not Allowed");
		return;
	}
	if (path === "/health") {
		response.writeHead(200, { "Content-Type": "application/json", "Cache-Control": "no-store" });
		response.end(JSON.stringify({ serialConnected: serialClient?.readyState === WebSocket.OPEN, monitors: monitorClients.size }));
		return;
	}
	const assets = new Map([["/", "index.html"], ["/app.js", "app.js"], ["/style.css", "style.css"]]);
	const filename = assets.get(path);
	if (!filename) {
		response.writeHead(404).end("Not Found");
		return;
	}
	try {
		const body = await readFile(join(root, "public", filename));
		const contentType = filename.endsWith(".js") ? "text/javascript" : filename.endsWith(".css") ? "text/css" : "text/html";
		response.writeHead(200, { "Content-Type": `${contentType}; charset=utf-8`, "Cache-Control": "no-store" });
		response.end(body);
	} catch (error) {
		console.error(`[http] ${error.message}`);
		response.writeHead(500).end("Could not load the serial bridge UI");
	}
}

// Revisa request (ruta), socket (cliente remoto) y head (datos iniciales) para autorizar el WebSocket.
function handleUpgrade(request, socket, head) {
	const path = new URL(request.url || "/", "http://localhost").pathname;
	const address = request.socket.remoteAddress || "";
	const loopback = address === "127.0.0.1" || address === "::1" || address.startsWith("::ffff:127.");
	const mode = path === "/serial" ? "serial" : path === "/monitor" && loopback ? "monitor" : "";
	if (!mode) {
		socket.destroy();
		return;
	}
	webSocketServer.handleUpgrade(request, socket, head, (client) => {
		// Asigna client (WebSocket actualizado) a su canal y entrega la conexion al servidor.
		client.mode = mode;
		webSocketServer.emit("connection", client, request);
	});
}

// Configura client (WebSocket conectado) para la notebook serie o para el monitor local.
function handleConnection(client) {
	if (client.mode === "serial") {
		if (serialClient?.readyState === WebSocket.OPEN) {
			client.close(1013, "Serial bridge already in use");
			return;
		}
		serialClient = client;
		broadcastStatus("[bridge] serial conectado\r\n");
		console.log("[bridge] notebook conectada al websocket serial");
	} else {
		monitorClients.add(client);
		client.send(`[bridge] monitor conectado; serial=${serialClient?.readyState === WebSocket.OPEN ? "on" : "off"}\r\n`);
		console.log("[bridge] monitor local conectado");
	}

	// Reenvia data (bytes recibidos) y isBinary (tipo de trama) del navegador a los monitores.
	client.on("message", (data, isBinary) => {
		if (client.mode !== "serial") return;
		for (const monitor of monitorClients) {
			if (monitor.readyState === WebSocket.OPEN) monitor.send(data, { binary: isBinary });
		}
	});

	// Limpia el estado de client, la conexion WebSocket que acaba de cerrarse.
	client.on("close", () => {
		if (client.mode === "serial" && serialClient === client) {
			serialClient = null;
			broadcastStatus("\r\n[bridge] serial desconectado\r\n");
			console.log("[bridge] notebook desconectada del websocket serial");
		} else if (client.mode === "monitor") {
			monitorClients.delete(client);
			console.log("[bridge] monitor local desconectado");
		}
	});

	// Informa error (detalle de la conexion) sin detener el servidor.
	client.on("error", (error) => console.error(`[bridge] websocket ${client.mode}: ${error.message}`));
}

// Envia message (texto de estado) a todos los monitores WebSocket conectados.
function broadcastStatus(message) {
	for (const monitor of monitorClients) {
		if (monitor.readyState === WebSocket.OPEN) monitor.send(message);
	}
}

const server = createServer(handleHttpRequest);
server.on("upgrade", handleUpgrade);
webSocketServer.on("connection", handleConnection);
server.on("listening", handleServerReady);
server.listen(port, "0.0.0.0");

// Imprime las direcciones de acceso cuando el servidor comienza a escuchar.
function handleServerReady() {
	console.log(`[bridge] UI: http://localhost:${port}`);
	for (const address of getLanAddresses()) console.log(`[bridge] Notebook: http://${address}:${port}`);
	console.log(`[bridge] Health: http://127.0.0.1:${port}/health`);
}
