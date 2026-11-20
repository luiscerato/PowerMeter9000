# Disponibilidad inicial de almacenamiento

El arranque de eventos no espera cinco segundos fijos ni consulta/persiste el
ultimo estado en NVS. Se eliminaron `grid_events.cpp/.h` y sus llamadas. Las
claves viejas quedan sin usar: no se borra configuracion, calibracion ni NAND.

Orden: hora UTC desde RTC/fallback, ajustes, ADE con calibracion y MeterTask,
pantalla con "Iniciando...", NAND, perfil historico y servicios accesorios.
Wi-Fi/SNTP/WebSocket no son necesarios para medir ni escribir FAST. La tarea
espera solo que finalice la configuracion de sus interrupciones, no 250 ms fijos.
ADE usa VSPI; pantalla y NAND usan HSPI con el mutex existente.

Mientras se monta NAND, una cola estatica de 8 cambios/256 bytes conserva los
estados confirmados R/S/T/red y sus tiempos originales. La consume MeterTask al
publicarse el Store mediante un puntero atomico. No se encolan muestras repetidas;
si se llena, se descartan los cambios mas viejos y se marca perdida de registros.
FAST conserva su anillo existente; el contexto RMS de esos cambios tempranos es
la ultima lectura disponible al entregarlos al Store. Los puntuales completos
anteriores al montaje no tienen una cola propia en esta etapa.

## Marca de arranque

Cada arranque, incluido software, genera una marca con estado anterior
desconocido. No reconstruye ni cierra incidentes anteriores y no supone que hubo
un corte durante el apagado. Se guardan estado general y referencias R/S/T;
los cambios siguientes se agregan normalmente a la captura activa.

Se mantienen cabecera de 30 B, ajustes de 48 B, transiciones de 12 B y formato 4.
Los dos bytes reservados de ajustes (offset 76 de la pagina) guardan
`0xB000 | esp_reset_reason()`, protegidos por CRC del cuerpo. Solo una marca
startup con ese identificador interpreta el campo como causa; los demas eventos
mantienen cero. La web muestra encendido, software, panic, watchdog o brownout.
Un arranque normal no cuenta como corte en las estadisticas.

El osciloscopio y sus callbacks se inicializan al final. Wave mantiene su
reserva despues de red/web; publica su puntero solo cuando colas y tarea existen.
Antes de tener Wave u osciloscopio listos no se lee/empaqueta el buffer Wave.
Por tanto, una marca FAST temprana puede no tener oscilografia de arranque.

## Preparacion real

- Grid recupera el cursor, verifica espacio para una captura maxima y, si el
  destino corresponde a una vuelta anterior del anillo, espera el borrado
  confirmado antes de publicar el Store a MeterTask.
- Historico recupera el cursor y, si necesita reutilizar el bloque actual,
  espera que su borrado termine correctamente. Preparar el siguiente bloque
  en segundo plano no retrasa su disponibilidad actual.
- Las esperas son por operaciones NAND, con limite de error de 250 ms; no son
  tiempos de estabilizacion. Si fallan, el montaje falla sin anunciar escritura
  disponible. No se borran particiones ni se cambia el formato persistente.

## Metricas

`GET /storage` incluye `mountReadyMs` y `writeReadyMs` dentro de `historyStore`
y `gridStore`. Son instantes en milisegundos desde el arranque del firmware,
no duraciones de una llamada ni fechas Unix. Cero indica que no se alcanzo el
punto. Un remontaje tras formato vuelve a calcularlos con el uptime actual.

- `mountReadyMs`: cursor y recursos del Store recuperados.
- `writeReadyMs`: primer destino disponible para escribir, incluido el borrado
  necesario; NO significa que se haya escrito o confirmado una primera muestra.
- `GET /storage?detail=1`, `gridStore.firstObserveMs`: primer estado observado
  por el capturador tras confirmar las mediciones y habilitar almacenamiento.

Los mismos tiempos de montaje/escritura y primera observacion salen por serie.
La web los muestra en Configuracion > Almacenamiento NAND usando su consulta
habitual, sin un endpoint nuevo ni lecturas adicionales.

`GET /storage?detail=1` agrega `boot`, con unidad `us` y entradas por etapa.
Cada entrada tiene `atUs` (instante final desde arranque), `us` (duracion) y
`ready`. Etapas: clock/settings/meter/screen/nand/history/wifi/ntp/web/wave/scope/
battery/complete. Hitos firstFast/firstState/firstEventPage miden el instante
desde arranque de primera muestra, estado confirmado y pagina Grid persistida.
Los hitos no necesitan endpoint ni tarea adicionales. La instrumentacion usa
128 bytes estaticos y serializa con un buffer local de 104 bytes.

Los tiempos no garantizan por si solos una duracion minima detectable de
retorno: faltan arranque fisico, estabilizacion del ADE, condiciones de
alimentacion y latencia de escritura efectiva. Hay que probar encendido en frio.

Sin bateria ni margen de alimentacion, un apagado anterior a la primera
escritura sigue pudiendo perder el registro. Se conserva solo la parte escrita;
no se inventa continuidad de incidentes entre reinicios.

## Pruebas

`node test/storage_startup.test.cjs`: codigo C++ real de cola temprana, desconocidos,
deduplicacion, overflow, orden y tiempo; destino libre, borrado, rechazo y timeout.
Verifica ausencia de persistencia NVS de estados y orden de inicializacion.
`node test/boot_record.test.cjs`: causa con CRC y arranque normal sin corte inferido.
`node test/grid_state.test.cjs`: confirmaciones, histeresis y muestras ausentes.

## Referencia anterior a separar adquisicion, 2026-10-27

Firmware y WebFS subidos por OTA. Lectura de `/storage?detail=1` despues de
reinicio por software, no encendido en frio:

| Punto | Uptime |
| --- | ---: |
| Historico, cursor recuperado | 609 ms |
| Historico, primer destino escribible | 609 ms |
| Eventos, cursor recuperado | 637 ms |
| Eventos, primer destino escribible | 637 ms |
| Eventos, primera observacion confirmada | 1488 ms |

No fue necesario borrar el destino inicial en esa pasada. Catalogo antes y
despues: 90 eventos, ultimo ID 90; aceptados desde el nuevo arranque: 0.
Historico acepto 36 muestras, sin rechazos, fallos de escritura ni errores I/O
al consultar. Estas cifras son una referencia de ese arranque, no una garantia
de deteccion en 1.488 s ni un ensayo fisico de corte y retorno.

## Referencia con adquisicion temprana y sin NVS de estados

Segundo firmware publicado el 2026-10-27; primer ensayo sin omitir Wave sin
consumidor: firstFast=309 ms, firstState=556 ms, primera pagina Grid=1146 ms.
Al omitir lecturas/empaquetado Wave sin consumidor, el reinicio OTA siguiente dio:

| Punto | Desde arranque | Duracion del modulo |
| --- | ---: | ---: |
| Hora local preparada | 164.6 ms | 30.9 ms |
| ADE y tarea preparados | 296.1 ms | 112.3 ms |
| Pantalla y mensaje enviados | 344.4 ms | 48.2 ms |
| Primera muestra FAST | 303.2 ms | - |
| Primer estado confirmado | 553.0 ms | - |
| Historico escribible | 809 ms | - |
| Eventos escribible | 843 ms | - |
| Primera pagina de evento persistida | 862.4 ms | - |
| Montaje NAND completo | 865.7 ms | 521.3 ms |
| Inicializacion completa | 1439.4 ms | - |

Se confirma una pagina antes del segundo en ese ensayo, no una garantia de
capturar cualquier retorno fisico de un segundo sin bateria. Falta la prueba
de encendido en frio y alimentacion intermitente real. La lectura guardada en
`logs/startup-early-2026-10-27.json` corresponde al arranque posterior descrito
abajo y muestra cifras similares: primera pagina a 862.3 ms.

RAM estatica: 116240 B frente a 116176 B de la version anterior (+64 B netos).
No se cambiaron formatos ni se borraron particiones. El esquema guarda ahora
una marca diagnostica por arranque: eventos 91 y 92 de software; 93 de panic.
El 93 se descargo y verifico: CRC correcto, captura completa, estado anterior
desconocido y red/R/S/T normales, sin inferir un corte previo. Historico sin
rechazos ni fallos; almacen sigue listo tras mas de 100 s de funcionamiento.

Incidencia observada: la primera subida WebFS corto la conexion y el ESP reinicio
con causa PANIC. La segunda subida completo 507904 bytes, la web nueva se verifico
y no se observaron nuevos reinicios durante las comprobaciones siguientes.
Los logs NAND muestran reset=4, pero no contienen backtrace. No se atribuye una
causa ni se declara corregido ese fallo sin evidencia; requiere captura serie
o un volcado del panic. El puerto USB/serie no estaba conectado en esta prueba.
