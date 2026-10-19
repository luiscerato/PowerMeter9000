# Tiempos de adquisicion

Consulta manual: `GET /meter?timing`. No agrega polling desde la web.
Instrumentacion estatica en RAM, sin prints ni asignaciones dinamicas en MeterTask.
Solo la consulta HTTP copia los contadores y construye el JSON.

- `unit`: duraciones en microsegundos; `uptime` y `slow[].at` en milisegundos desde arranque.
- `parts`: por bloque, `n`, promedio `avg`, maximo `max`, cantidad >=3 ms `gt3` y >=10 ms `gt10`.
- `slow`: ultimas ocho operaciones >=5 ms, excluyendo esperas, edad de IRQ y bloques padres anidados.
- Contadores acumulados desde arranque; no se reinician al consultar.

Bloques principales:

- `wait`: tiempo dentro de ulTaskNotifyTake; `wakeLate`: exceso sobre la espera maxima pedida.
- `cycle`: cuerpo de una pasada, excluyendo la espera anterior.
- `irq0`: atencion de STATUS0; `irqDelay`: tiempo desde la ultima IRQ0 hasta atenderla, una vez por secuencia.
- `wave`: proceso completo de medio buffer Wave del ADE, no medio ciclo electrico.
- `waveSpi`, `waveScale`, `wavePack`: lectura SPI, escalado y compresion/traza.
- `waveStore`, `waveWeb`: entrega de frames de 96 ms al almacen y al osciloscopio web.
- `fast`, `slowRms`, `thd`, `energy`: grupos de lectura/procesamiento ADE.
- `bl`: servicio del driver y publicacion de valores, excluye commit posterior.
- `commit`: cierre del conjunto pendiente ADE/BL y copia a buffers/capturas.
- `grid`: actualizacion de estados y observacion del almacen NAND, dentro de commit.
- `gridNvs`: persistencia sincronica de un estado de fase/red en la flash interna.
- `events`, `commands`, `irq1`, `calibration`: otras secciones del ciclo de adquisicion.

Son tiempos de pared: incluyen preempciones, esperas por mutex y pausas externas
ocurridas dentro del bloque. No son mediciones de CPU exclusiva. Hay bloques
anidados (`cycle`/`irq0`/`wave`, `commit`/`grid`), por lo que no se suman entre si.
La etiqueta de un pico localiza el tramo afectado, no prueba por si sola si el
tiempo lo gasto su algoritmo o una suspension dentro del mismo.

ADE usa VSPI; pantalla y NAND usan HSPI compartido. El ADE no espera directamente
el mutex de ese bus HSPI. Sigue siendo posible interferencia por otros recursos,
memoria, flash interna o planificacion; se requiere medir antes de atribuir causas.

## Mediciones del 2026-10-19

Firmware subido por OTA. Pruebas del registrador real: umbrales, anillo, indices
invalidos y suma de 64 bits; pruebas de prioridad Wave, captura y sincronismo BL
aprobadas. Incremento RAM estatico: 672 bytes frente al firmware sin perfiles.

En la primera observacion, lectura Wave SPI media 3236 us, RMS/potencia 726 us,
THD/angulos/periodos 481 us y energia 278 us. Maximos iniciales: grid 27288 us,
Wave SPI 17323 us y escalado 12282 us. Entre la primera y segunda consulta no
crecieron los contadores de operaciones >=10 ms: no era una demora continua.

Se agrego `gridNvs` al guardado sincronico de la linea base de estados. Segunda
revision: cuatro escrituras al resolver el arranque, promedio 28089 us, maximo
99348 us. Duracion acumulada aproximada 112356 us; `grid` maximo 113332 us y
`commit` 113678 us. Las marcas lentas ubican escrituras a uptime 6315/6418 ms.
La llamada `gridEventsUpdate` de MeterTask llega directamente a `setCurrentState`
y `saveSnapshot`, que abre Preferences y ejecuta putBytes en la flash interna.

Esto confirma un bloqueo por persistencia dentro de adquisicion. Tambien ocurre
ese mismo camino en cambios de R/S/T y estado general, pudiendo encadenar cuatro
escrituras cuando cambian todas las fases. Es compatible con los huecos globales
de 128/135 ms ya observados, pero no prueba retrospectivamente cada evento.

Pendiente: sacar la persistencia sincronica de adquisicion y definir cuando se
guarda sin perder la linea base para reinicios. El uso de flash interna puede
afectar otros nucleos/cache aun desde otra tarea, por lo que debe verificarse el
resultado en placa. No se cambio NVS ni el significado de la configuracion en
esta etapa; solamente se instrumento y midio. Los raros picos en Wave SPI durante
el arranque tambien incluyen posibles suspensiones externas y siguen sin causa
individual demostrada.

Archivos de evidencia: logs/meter-timing-baseline-2026-10-19.json,
logs/meter-timing-baseline2-2026-10-19.json y logs/meter-timing-nvs-2026-10-19.json.

## Persistencia diferida de estados

`setCurrentState` y `refreshStartTimestamp` ahora solo publican una copia estatica
pendiente por R/S/T/red. Un bloqueo corto protege esa copia; nunca se abre NVS ni
se escribe flash mientras esta tomado. `gridEventsPersistLoop`, llamado desde
`MeterLoop` (loop principal, no MeterTask), guarda como maximo un slot por pasada.

Una revision por slot impide borrar un estado nuevo al confirmar una escritura
anterior. Si hay varios cambios antes de poder persistir, se conserva la ultima
linea base; las notificaciones/eventos siguen por sus colas originales sin agruparse.
Los errores mantienen el pendiente y reintentan tras un segundo, sin bloquear
adquisicion. Se conserva namespace, claves y formato persistido version 4.

La escritura sigue siendo bloqueante para el loop principal. Se elimina la espera
sincronica dentro de MeterTask, no el posible efecto global de flash/cache del ESP32.
Si se pierde toda alimentacion antes de la escritura diferida, NVS puede conservar
la linea base previa. El estado RAM y los eventos NAND no cambian de formato.

Pruebas del codigo real: un solo guardado por pasada, agrupacion por ultimo estado,
cambio del productor durante putBytes, fallo/reintento, revision y reloj con wrap;
verificacion de que MeterTask/productores no llaman al consumidor de NVS.

Verificacion en placa tras subir por OTA: cuatro guardados NVS ejecutados por el
loop principal, media 9840 us y maximo 12804 us. El procesamiento `grid` bajo de
113332 us a un maximo de 1784 us; `commit` de 113678 us a 2060 us. No hubo llamadas
grid/commit >=3 ms en esa observacion. Wave SPI mantuvo su media de 3280 us.

Todavia hubo cuatro picos Wave SPI de 10.8-17.1 ms, todos alrededor de uptime
2.9 s; las escrituras gridNvs ocurrieron luego, entre 6322 y 6364 ms. Esos picos
de arranque no coinciden con este guardado y su causa sigue pendiente. No se
afirma que haber movido NVS elimine toda pausa de adquisicion.
Evidencia: logs/meter-timing-deferred-nvs-2026-10-19.json.
