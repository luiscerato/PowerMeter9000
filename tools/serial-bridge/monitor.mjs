import { WebSocket } from "ws";

const socket = new WebSocket("ws://127.0.0.1:3001/monitor");

// Imprime data (trama serie o mensaje de estado) que llega del puente.
socket.on("message", (data) => process.stdout.write(data));

// Indica que la conexion local ya puede recibir la salida serie; no recibe parametros.
socket.on("open", () => process.stdout.write("[monitor] escuchando el puente serie local\r\n"));

// Informa error (detalle de conexion) con el servidor del puente.
socket.on("error", (error) => process.stderr.write(`[monitor] ${error.message}\n`));

// Notifica que se cerro el canal local; no recibe parametros.
socket.on("close", () => process.stdout.write("\r\n[monitor] puente desconectado\r\n"));
