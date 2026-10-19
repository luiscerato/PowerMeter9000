# Power Meter Serial Bridge

El servidor corre en la PC donde esta Codex. La notebook abre la interfaz, lee el USB serie con el navegador y envia los bytes al monitor local por WebSocket.

## Iniciar en la PC de Codex

```powershell
cd tools/serial-bridge
npm install
npm start
```

La consola muestra la URL LAN. En este equipo es `http://192.168.1.10:3001`.

## Abrir desde la notebook

Web Serial requiere un contexto seguro. Para usar la IP local por HTTP, abre Edge con un perfil temporal y habilita ese origen:

```cmd
msedge.exe --user-data-dir="%TEMP%\pm-serial-bridge" --unsafely-treat-insecure-origin-as-secure="http://192.168.1.10:3001" "http://192.168.1.10:3001"
```

En la pagina, pulsa **Abrir puerto serie**, selecciona el puerto USB del medidor y confirma. Usa Chrome/Edge actualizado. Si Windows pregunta, permite el puerto 3000 en la red privada.

## Monitor de Codex

El cliente local se ejecuta en otra terminal de esta misma PC:

```powershell
npm run monitor
```

Cuando la notebook abre el serial, la salida aparece en esa terminal y Codex puede leerla en vivo. La pagina tambien muestra la consola y permite guardar un `.log`.

El servidor escucha en el puerto 3001; `PORT` permite cambiarlo. `GET /health` informa si hay una conexion serie y monitores activos.
