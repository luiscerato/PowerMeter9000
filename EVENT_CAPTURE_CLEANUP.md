# Limpieza de eventos - 2026-10-19

> Punto de control previo a la unificacion. Estado posterior:
> [UNIFIED_EVENT_CAPTURE.md](UNIFIED_EVENT_CAPTURE.md).

Punto de restauracion anterior a la limpieza: commit `7f86610`.

## Camino que queda activo

- `GridEventStore`: estados R/S/T y red, y tension alta de neutro si esta habilitada.
- Capturas FAST, contexto y transiciones en la particion NAND `gridEvents`.
- Consulta y descarga por `/grid-events`; visor web en `#gridEvents`, menu Eventos.
- Formato guardado y lectura de capturas anteriores sin cambios.
- Referencia de estados en NVS con escritura diferida desde el loop principal.

## Retirado

- Capturador `capturedEvent` / `EventsData` y almacen de cinco eventos en RAM.
- Captura Wave antigua, buffers, colas y pruebas especificas de ese almacen.
- WebSocket `/acevents` y envio automatico de metadatos y muestras.
- Rutas `/events`, `/events/test`, `/events/fast-rms`, `/events/wave`,
  `/events/wave-diagnostics` y `/events/clear`.
- Publicacion MQTT de eventos, avisos periodicos, cola Grid y PUBACK/reintentos.
- Vista de eventos RAM/Node-RED y parametros de ventana de captura antiguos.
- Discovery anuncia notificaciones deshabilitadas y enlaza el catalogo NAND.

## Se conserva

Mediciones, sincronizacion ADE/BL, detectores dip/swell/sobrecorriente y sus
configuraciones, historico, logger, RTC, MQTT de telemetria y osciloscopio en vivo.
La limpieza no borra NAND ni NVS. Las claves antiguas de ventana pueden seguir
en NVS, pero no se utilizan ni se exportan con los respaldos nuevos.

## Pendiente explicito

Dip, swell y sobrecorriente se detectan, pero temporalmente no generan capturas
guardadas. Tampoco se captura ni persiste oscilografia de eventos. Los 40 MiB
Wave permanecen reservados, sin un grabador de eventos activo.

Todavia no se implementaron el coordinador unificado, el nuevo formato compacto
de configuracion de 48 bytes, las marcas de evento de 12 bytes, ni el nuevo
esquema de notificaciones. No se cambiaron umbrales comunes por umbrales por fase.

## Verificacion

- Firmware compilado y publicado por OTA a 192.168.1.90; web gzip publicada en NAND.
- RAM estatica: 106320 bytes; firmware: 1303721 bytes.
- Captura artificial 137: cinco paginas, 584 muestras, ninguna pagina con CRC
  incorrecto. Una muestra BL sin dato, conservada como tal (583 validas).
- Evento 136 conservado byte por byte, comprobado con SHA-256 antes/despues.
- Osciloscopio en vivo: frame binario de 8064 bytes recibido por `/ws`.
- `/events`, `/events/wave` y `/acevents` responden 404.
- Pruebas de limpieza, sincronizacion BL, estados, persistencia NVS, formato
  FAST/historico, neutro, contexto, respaldo, rutas y navegacion correctas.
- No se provoco un corte fisico de tension durante esta verificacion.
