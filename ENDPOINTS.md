# Inventario de endpoints

Revisado contra el codigo del proyecto el 2026-10-25. Relevamiento estatico: no se
ejecutaron reinicios, pruebas de escritura, calibraciones ni formateos.

Hay **54 rutas HTTP explicitas y 64 combinaciones metodo/ruta**, contando las
rutas antiguas que rechazan escrituras. Aparte estan el WebSocket `/ws` y los
archivos estaticos atendidos por WebFS. Los fragmentos `#eventHistory`, `#grid`,
`#scopePage`, etc. son vistas del navegador, no endpoints del ESP.

Origen local: `http://192.168.1.90`. Las rutas son relativas a ese origen o al
dominio/proxy configurado. Salvo indicacion contraria, las respuestas son JSON.

## Convenciones

- GET: parametros en query string, por ejemplo `/meter?timing`.
- POST: configuraciones con `application/x-www-form-urlencoded`, no JSON.
- Excepcion: POST de imagen WebFS usa cuerpo binario `application/octet-stream`.
- Una aceptacion `202` no siempre significa escritura/aplicacion finalizada:
  consultar el estado de la operacion correspondiente.
- Unidades distintas: historico usa Unix **segundos**; cabeceras de eventos usan
  Unix **milisegundos**, con fracciones; `unixUs` usa Unix **microsegundos**.
  Los `triggerUs/endUs` Wave usan el reloj monotono del dispositivo, no Unix.
- FAST y Wave comparten un lector de descarga NAND: consultar/descargar de manera
  secuencial. Algunas operaciones ocupadas devuelven `409`, `429` o `503`.
- La carga de librerias y codigo web tambien debe mantenerse **secuencial** para
  evitar agotar recursos del ESP.

## 1. Estado y mediciones

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/ping` | Ninguno | Responde texto `ok`; prueba minima de conectividad. |
| GET | `/status` | Ninguno | SSID, RSSI, IP, memoria, tasas NAND y CPU estimada por nucleo. |
| GET | `/meter` | Sin query | Medidas generales actuales. |
| GET | `/telemetry` | Ninguno | Instantanea de telemetria preparada por manager. |
| GET | `/manager/discovery` | Ninguno | Identidad/capacidades para descubrimiento e integracion del equipo. |
| GET | `/manager/status` | Ninguno | Estado de manager, conectividad y sincronizacion/integracion. |

Variantes de `/meter`, seleccionadas por el **nombre del primer parametro**:

| Consulta | Funcion |
|---|---|
| `/meter?energy` | Energias acumuladas. |
| `/meter?fast` | Mediciones FAST actuales, no la captura historica binaria. |
| `/meter?phaseR` | Mediciones de fase R. |
| `/meter?phaseS` | Mediciones de fase S. |
| `/meter?phaseT` | Mediciones de fase T. |
| `/meter?neutral` | Mediciones del neutro. |
| `/meter?angles` | Angulos disponibles. |
| `/meter?thd` | Distorsion armonica medida. |
| `/meter?timing` | Tiempos de operaciones: `n/avg/max/gt3/gt10`, ultimas lentas y uptime. Unidad: us; `gt3/gt10` cuentan duraciones mayores a 3/10 ms. |

No son filtros combinables: enviar una variante por solicitud. Un primer
parametro desconocido devuelve `404`, aunque otro posterior sea reconocido.

`status.cpu.c0/c1` son porcentajes estimados por muestreo ocupado/idle en ventanas
de dos segundos; `null` significa no disponible todavia. Incluyen tareas del
sistema y sondeo continuo, no equivalen a cumplimiento de deadlines.

## 2. Configuracion unificada de eventos

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/events/config` | Sin parametros | Todos los grupos, revision y estado del guardado. |
| GET | `/events/config` | `group=volt|time|neut|curr|rec|wave` | Consulta un solo grupo. |
| POST | `/events/config` | `rev` y uno o varios campos listados abajo | Valida una copia, encola guardado/aplicacion; responde `202`. |
| GET | `/events/status` | Sin parametros | Estado compacto de captura, NAND de eventos y revision de configuracion. |
| GET | `/events/status` | `since=<seq>` | `204` sin cuerpo si no hubo cambios relevantes; de lo contrario JSON nuevo. |

Campos POST (GET los devuelve anidados por grupo, sin el prefijo):

| Grupo | Campos | Significado |
|---|---|---|
| `volt` | `volt.lowR/lowS/lowT`, `volt.highR/highS/highT`, `volt.loss`, `volt.hyst` | Umbrales por fase, ausencia de tension e histeresis, en V. Cada nombre abreviado representa su clave completa con prefijo. |
| `time` | `time.dip`, `time.swell`, `time.oc`, `time.loss`, `time.back` | Confirmaciones en ms. |
| `neut` | `neut.high`, `neut.hyst`, `neut.wait`, `neut.back` | Tension neutro-tierra: umbral/histeresis en V y tiempos en ms. |
| `curr` | `curr.high`, `curr.hyst` | Sobrecorriente e histeresis, en A. |
| `rec` | `rec.dip`, `rec.swell`, `rec.oc` | Habilita registro de puntuales; booleanos POST `0/1`. |
| `wave` | `wave.on`, `wave.pre`, `wave.dip`, `wave.swell`, `wave.oc` | Habilitacion general Wave, previo en ms y habilitaciones Wave por puntual. Booleanos `0/1`. |

Ejemplo de forma, no ejecutar sin leer primero la revision actual:

```text
POST /events/config
Content-Type: application/x-www-form-urlencoded

rev=<revision_actual>&volt.lowR=190&time.dip=50
```

La revision evita pisar cambios de otro cliente. Puede devolver `409` por
conflicto/guardado ocupado, o `400` por validacion. Los errores de campos incluyen
`key` cuando se identifica la clave. El estado `save.state` distingue
`ok`, `wait`, `save`, `fail`; el resultado puede indicar reinicio requerido.

## 3. Capturas FAST y oscilografias

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/grid-events` | Ninguno | Catalogo: cantidad, ID mas viejo/nuevo, paginas y estado de captura. |
| GET | `/grid-events/list` | `from=<id>&to=<id>` | Resumen de cabeceras en ese rango; hasta 64 entradas. IDs, no fechas. |
| GET | `/grid-events/list` | `last=<n>` | Solicita recientes. **Actualmente N no se usa para limitar: devuelve hasta 64.** |
| GET | `/grid-events/data` | `eventId=<id>` | Binario completo FAST: cabecera, contexto, ajustes, transiciones y muestras. |
| GET | `/wave-events` | `eventId=<id>&unixUs=<fecha_exacta_us>` | Indice Wave asociado a esa captura FAST. |
| GET | `/wave-events/data` | `block=<b>&page=<p>&version=<v>&waveId=<id>` | Binario Wave; usar ubicacion/version del indice, no inferirlas. |
| GET | `/wave-events/data` | `block=<b>&waveId=<id>` | Solicitud de dos parametros para disposicion antigua de 128 KiB; sigue presente en el codigo. |
| GET | `/capture-cache` | Ninguno | `deviceId`, `fastEpoch`, `waveEpoch` y disponibilidad para identificar/invalidar la cache local. |
| POST | `/grid-events/test` | `type=state|dip|swell|overcurrent|mixed` | Encola evento artificial de prueba. Por defecto `mixed`. **Genera registros.** |

Detalles:

- `/grid-events/list`: `last` no se combina con `from/to`; rango invertido devuelve
  lista vacia. La respuesta incluye totales y limites del catalogo.
- `/wave-events` requiere exactamente los dos parametros. Puede responder `202`
  con `wave_index_pending`: repetir secuencialmente hasta obtener el indice.
  `unixUs` es la fecha de cabecera FAST convertida a us, no `Date.now()`.
- Descarga Wave: `block` es relativo a la particion (0..319), `page` es 0 o 32,
  version 2 usa 64 KiB; version 1 usa 128 KiB. Son formatos existentes en codigo,
  no una recomendacion de fabricar consultas para otras versiones.
- Los datos se validan contra ID/ubicacion y pueden estar ocupados o haber sido
  reemplazados por la rotacion. La identidad de cache ayuda a detectar cambios.

## 4. Rutas antiguas de eventos

| Metodo | Ruta | Funcion actual |
|---|---|---|
| GET | `/grid/config` | Consulta plana antigua de ajustes de red/neutro. |
| POST | `/grid/config` | `410`, error `use_events_config`. |
| GET | `/wave/config` | Ajustes Wave mas metricas detalladas: RAM, previo, slots, capturas, huerfanas, errores, pila, etc. |
| POST | `/wave/config` | `410`, error `use_events_config`. |
| POST | `/options/reload` | `410`, error `use_events_config`. |

Tambien se rechazan con `410` claves de eventos antiguas enviadas a `/options`
en el namespace `meter`. La configuracion de eventos nueva va por `/events/config`.

## 5. Historico de mediciones

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/history` | Sin query | Rango disponible, paginas persistidas, muestras RAM y estado de escritura. |
| GET | `/history` | `from=<unix_s>&to=<unix_s>` o `last=<segundos>` | Plan de lectura: muestras, paginas, bytes y tiempos de busqueda; no descarga las muestras. |
| GET | `/history/data` | `from=<unix_s>&to=<unix_s>` o `last=<segundos>` | Descarga binaria del historico solicitado. |
| GET | `/history/data` | Sin query | Ultimos 300 s respecto del dato mas nuevo almacenado. |
| GET | `/history/data` | `from`, `to`, `pages=1..256`, `before=<secuencia24>` opcional, `identity` opcional, `bg` opcional | Descarga limitada de paginas confirmadas para cache; cursor inclusivo hacia atras. `bg` cede si hay streams/lectores activos (409). Cabeceras `X-History-Identity`, `X-History-First-Sequence`, `X-History-Pages`. Ver `HISTORY_CACHE.md`. |
| GET | `/history/config` | Ninguno | Perfil de registro y estado de aplicacion. |
| POST | `/history/config` | Perfil completo detallado abajo | Guarda/aplica perfil; responde `202`. |
| GET | `/history/estimate` | Perfil de consulta, sin `enabled` | Calcula capacidad/autonomia estimada sin guardar cambios. |

`last` no se combina con `from/to`; estos ultimos deben enviarse juntos.
La referencia de `last` es el ultimo dato guardado, no necesariamente la hora actual.

Campos del perfil (todos requeridos por el parser; `enabled` solo en POST):

| Campo | Valores / unidad |
|---|---|
| `enabled` | `true/false` o `1/0`. |
| `samplePeriod` | Segundos, 0.1..300, pasos de 0.1. |
| `voltageMode` | `basic` o `full`. |
| `activePower`, `reactivePower`, `apparentPower`, `frequency` | `off`, `compact`, `full`. |
| `powerFactor`, `voltageThd`, `currentThd`, `voltageAngles`, `battery`, `energy` | Booleanos. |
| `energyExtraMode` | Entero 0..7; solo se aplica si energia esta habilitada. |

Puede haber combinaciones no soportadas (`422`). La descarga entrega paginas
binarias de 2048 bytes y cabeceras `X-History-From/To/Page-Size`; el cliente decodifica.

## 6. Red y servicios

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/network/config` | Sin query | Opciones persistidas de WiFi, AP, NTP, MQTT, OTA y debug. **Incluye credenciales guardadas.** |
| GET | `/network/config` | `view=setup` | Configuracion del formulario WiFi/NTP con valores por defecto, sin exponer contrasenas: indica si existen. Incluye estado de conexion/AP. |
| POST | `/network/config` | Campos permitidos; `?defer=1` opcional | Valida, guarda y verifica lectura. Encola aplicacion WiFi/NTP, o solo guarda si se difiere. |
| GET | `/network/status` | Ninguno | Estado de aplicacion, conexion e IP de estacion/AP. |
| POST | `/network/apply` | `services=1|2|3` | Aplica diferidos: 1 WiFi, 2 hora/NTP, 3 ambos. |
| GET | `/network/scan` | Ninguno | Inicia/consulta escaneo asincrono; repetir hasta obtener listado. |

Campos permitidos de red (POST parcial, sin `ns`):

- Estacion: `wifi-mode`, `sta-ssid`, `sta-pass`, `staticIp`, `sta-ip`, `sta-mask`,
  `sta-gw`, `sta-dns1`, `sta-dns2`.
- AP: `ap-ssid`, `ap-pass`, `ap-hide`, `ap-custom-ip`, `ap-ip`, `ap-mask`, `ap-gw`.
- NTP/hora: `time-start`, `time-dsttime`, `time-minwest`, `time-server`, `time-sync`.
- MQTT: `mqtt-enabled`, `mqtt-server`, `mqtt-port`, `mqtt-user`, `mqtt-pass`.
- OTA: `ota-enabled`, `ota-name`, `ota-pass`.
- Debug remoto: `debug-enabled`, `debug-name`, `debug-pass`.

Los cambios que no se aplican en caliente pueden indicar `restartRequired`.
Guardar WiFi puede cortar la propia conexion desde la que se envia la solicitud.
El indicador de existencia de password evita que el formulario necesite leerlo.

## 7. NVS, respaldo y calibracion

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/options` | `ns=<namespace>&<clave1>&<clave2>...` | Lee claves NVS por nombre; `ns` debe ser el primer parametro. |
| POST | `/options` | `ns=wifi-stuff|meter|ADE9000` y claves/valores; `?defer=1` opcional | Escritura validada por lista permitida y verificacion posterior. Usado para importar secciones. |
| GET | `/calibration/neutral` | Ninguno | Progreso/resultados de calibracion BL0942 y factores RMS/FAST. |
| POST | `/calibration/neutral` | `phase=r|s|t`, por defecto `r` | Encola calibracion BL usando la fase ADE como referencia; `202` o `409` si ocupado. |
| POST | `/calibration/restore` | Ninguno | Restaura calibracion ADE, verifica y devuelve `restartRequired:true`. **Modifica calibracion.** |

Ejemplo de lectura: `/options?ns=ADE9000&AVGAIN&BVGAIN&neutralFast`.

El GET generico abre el namespace recibido; no tiene la lista cerrada del POST.
Claves ausentes devuelven texto `key no existe`. La importacion/exportacion JSON
por secciones se organiza en la web: no existe un endpoint unico de backup JSON.

POST de `meter`: quedan principalmente energias `rWattH/rVarH/rVaH`,
`sWattH/sVarH/sVaH`, `tWattH/tVarH/tVaH`, `WattH/VarH/VaH`; los antiguos campos
de configuracion de eventos se rechazan para que no haya dos caminos de escritura.

POST de `ADE9000`: `neutralRms`, `neutralFast` y registros de calibracion permitidos:

```text
APHCAL0 BPHCAL0 CPHCAL0 NPHCAL
AIGAIN BIGAIN CIGAIN NIGAIN AVGAIN BVGAIN CVGAIN
AIRMSOS BIRMSOS CIRMSOS NIRMSOS AVRMSOS BVRMSOS CVRMSOS
AIFRMSOS BIFRMSOS CIFRMSOS AVFRMSOS BVFRMSOS CVFRMSOS
AIRMSONEOS BIRMSONEOS CIRMSONEOS NIRMSONEOS AVRMSONEOS BVRMSONEOS CVRMSONEOS
AIRMS1012OS BIRMS1012OS CIRMS1012OS NIRMS1012OS AVRMS1012OS BVRMS1012OS CVRMS1012OS
```

El POST generico admite hasta 40 opciones por solicitud, no cualquier clave NVS.
Los errores de validacion/escritura/verificacion incluyen `key` cuando corresponde.

## 8. Hora y mantenimiento general

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/clock` | Ninguno | Hora Unix del sistema en segundos, texto plano. |
| POST | `/clock` | `time=<unix_s>` | Ajusta hora, sincroniza RTC y devuelve hora actual. |
| GET | `/clock/rtc` | Ninguno | RTC presente/valido, perdida de alimentacion, hora RTC/sistema, diferencia y fuente. |
| POST | `/restart` | Ninguno | Solicita reinicio (`202`), guarda energias y reinicia el ESP. |
| POST | `/energy/reset` | Ninguno | Reinicia contadores de energia. **Destructivo para esos acumulados.** |

`/clock` registra GET y POST con el mismo handler, pero solo modifica hora si
recibe `time` como parametro del cuerpo POST. `GET /clock?time=...` no la ajusta.

## 9. Diagnostico y logs

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/debug` | Ninguno | Configuracion logger, modulos, colas, descartes, ocupacion NAND y progreso de formato. |
| GET | `/debug/recent` | `count=1..16`; opcional `after=<seq>` o `before=<seq>` | Logs recientes RAM en JSON; por defecto 16. No combinar cursores no nulos. |
| GET | `/debug/nand` | `count=1..16` | Ultimos logs persistidos NAND en JSON. |
| GET | `/debug/nand` | `b`, `p`, `o`, `ab`, `ap` y `count` | Continua lectura hacia atras usando todos los campos del cursor `next` anterior. |
| GET | `/debug/binary` | Sin parametros | Inicia descarga binaria de logs con sesion. |
| GET | `/debug/binary` | `session=<token>&before=<bloque>` | Pagina hacia atras en esa sesion; `204` al terminar. |
| GET | `/debug/binary` | `refresh=<token>` | Refresca ultimos datos de una sesion existente. No combinar con `session`. |
| POST | `/debug/config` | `globalLevel`; `moduleId` + `level`; `serial`; `nand` | Guarda nivel global/por modulo y salidas habilitadas. Admite cambios parciales. |
| POST | `/debug/format` | Ninguno | Borra particion de logs asincronamente. **No exige confirmacion textual.** |

Niveles: `none`/`off`, `error`, `warn`, `info`, `verbose`, `debug`.
Para un modulo se admite tambien `level=inherit`. Booleanos de salidas:
`true/false` o `1/0`. El logger puede necesitar varias partes para un mensaje largo.

## 10. NAND y pruebas de almacenamiento

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/storage` | Sin parametros | Estado general NAND, particiones, IO y formateo. |
| GET | `/storage` | `detail=1` | Agrega detalles de diagnostico. |
| POST | `/storage/format` | `confirm=FORMAT` | Formatea NAND globalmente. **Destructivo, incluye almacenamiento web.** |
| POST | `/storage/partition/format` | `confirm=FORMAT&partition=<nombre>` | Formatea solo la particion indicada; progreso por `/storage`/estado especifico. |
| POST | `/storage/test` | `block=<bloque_fisico>` opcional | Prueba borrado/escritura/lectura de un bloque, por defecto el ultimo. **Destructiva**, solo NAND sin formato. |
| GET | `/storage/benchmark` | Ninguno | Estado, progreso y resultados del benchmark. |
| POST | `/storage/benchmark` | `mode`, `speedMHz`, `startBlock`, `blockCount` | Inicia benchmark destructivo; solo NAND sin formato. |
| POST | `/storage/benchmark/cancel` | Ninguno | Solicita cancelar benchmark (`202` o `409`). |
| GET | `/storage/test-download` | `size=<bytes>` | Descarga bytes de la particion history para medir transferencia; por defecto 3 MiB. No es una consulta filtrada por fechas. |
| GET | `/test/*` | Nombre terminado en `.log`, `.raw` o `.csv` | Prueba de transferencia sintetica de asteriscos: 128 KiB para log/raw, 16 MiB para csv. No son registros reales. |

Particiones admitidas: `history`, `waveEvents`, `gridEvents`, `diagnostics`,
`webFsA`, `webFsB`. El gestor puede rechazar formatos por uso/estado del banco.

Benchmark: `mode=quick|full|verify` (defecto quick),
`speedMHz=20|25|30|35|40` (defecto 20), `startBlock=0` y `blockCount=0` por
defecto; la seleccion final de rango depende del modo y la geometria NAND.

## 11. Aplicacion web y recuperacion

| Metodo | Ruta | Variantes / parametros | Funcion |
|---|---|---|---|
| GET | `/` | Ninguno | Pagina principal desde WebFS; recuperacion compilada si falta la web. |
| GET | `/scope.html` | Ninguno | Redireccion `302` a `/index.html#scopePage`. |
| GET | `/nav.html` | Ninguno | Redireccion `302` a `/index.html`. |
| GET | `/recovery` | Ninguno | Pagina minima de recuperacion incluida en firmware. |
| GET | `/webfs` | Ninguno | Banco activo A/B, secuencia, validez y tamanos de imagen. |
| GET | `/webfs/image` | Ninguno | Descarga imagen completa del banco activo `.fs32`. |
| POST | `/webfs/image` | Cuerpo binario `.fs32` | Actualiza imagen WebFS con validacion y gestion de bancos; no sube firmware ESP. |
| POST | `/webfs/rollback` | `confirm=ROLLBACK` | Cambia al banco web anterior si esta disponible y no hay lecturas activas. |

Los demas recursos, por ejemplo `/index.html`, `/app.js`, `/style.css`, `/lib/...`,
se resuelven desde la imagen WebFS. Son archivos, no handlers API individuales.
Su entrega soporta gzip/cache/ETag segun el recurso y los headers de la solicitud.

## 12. WebSocket

`/ws` es el unico WebSocket registrado actualmente. Se usa `ws://` en LAN HTTP o
`wss://` detras del proxy HTTPS. Una sola adquisicion/configuracion es compartida
por los clientes; calidad de energia tiene prioridad sobre los ajustes del scope.

| Mensaje cliente -> ESP | Funcion |
|---|---|
| `scope_alive` | Mantiene vivo el cliente. |
| `scope_get` | Solicita configuracion efectiva y modo de captura. |
| `scope_view,1` | Declara vista osciloscopio. |
| `scope_view,2` | Declara vista calidad/FFT; actua tambien como heartbeat de prioridad. |
| `scope_view,3` | Declara ambos consumidores. |
| `scope,Vgain,Igain,timeDivMs,triggerLevel,channel,rising,mask` | Cambia configuracion scope compartida si calidad no tiene prioridad. |
| `quality_set,Vpeak,Apeak,mask` | Configura rangos pico y canales para calidad; requiere cliente con vista calidad vigente. Rangos 0.05..500. |
| `scope_fft_on,0` / `scope_fft_on,1` | Deshabilita/habilita flujo FFT; requiere cliente calidad vigente. |

Canales y mascara de bits: `0=IR`, `1=VR`, `2=IS`, `3=VS`, `4=IT`, `5=VT`,
`6=IN`; mascara 0..127. No se transmite Wave de tension neutro-tierra.
En el comando `scope`, las ganancias son unidades fisicas por cuenta, no V/div
ni A/div; `channel` vale 0..6 y `rising` 0/1. El parser admite timeDivMs de
0.01..100; los controles de la web pueden ofrecer un subconjunto mas acotado.

Respuestas principales ESP -> cliente:

- `scope_cfg,...`: configuracion efectiva; `quality_cfg,...`: prioridad/rangos y
  generacion; `scope_fft_on,...`: habilitacion. Pueden compartir mensaje con `\n`.
- Binarios `SC`: capturas de osciloscopio.
- Binarios `SF`: fragmentos FFT/calidad y cancelaciones de captura.
- Prioridad calidad vence a los 5 s sin declaracion de esa vista; el cliente
  general vence a los 30 s sin actividad. Configuracion se persiste diferida.

`/screen` tiene un objeto declarado, pero su `addHandler` esta comentado:
**no esta habilitado como WebSocket**. Los antiguos comandos `scaleVoltage=...`
y `scaleCurrent=...` pertenecen a otro callback, no al actualmente conectado.

## Observaciones del estado actual

1. Coexisten rutas agrupadas nuevas y lecturas antiguas. Para eventos, usar
   `/events/config` y `/events/status`; no intentar guardar por las rutas `410`.
2. `last=N` de `/grid-events/list` no respeta N como limite. Requiere corregir el
   handler para que el numero solicitado tenga efecto; aqui solo se documenta.
3. `/meter` y `/options` dependen del orden de parametros. Son contratos menos
   uniformes que los endpoints nuevos y convendria revisarlos antes de ampliarlos.
4. No se encontro autenticacion HTTP propia en estos handlers ni en `/ws`.
   La proteccion que pueda existir en Cloudflare/red es externa a este inventario.
   Algunas rutas revelan credenciales y otras reinician/borran sin autenticacion
   de aplicacion; no exponerlas sin una proteccion externa adecuada.
5. JSON no tiene una envoltura de errores unica: conviven `error`, `reason`,
   texto plano, codigos de estado y respuestas vacias. La web debe interpretarlos
   segun la operacion; seria un candidato a normalizacion posterior.
6. Los endpoints de pruebas siguen registrados. Deben distinguirse de funciones
   operativas normales; incluso algunas consultas de prueba generan trafico grande.

## Fuentes del proyecto

- `src/webserver.cpp`: API general, configuracion, diagnostico y descargas.
- `src/web/events_http.cpp`: configuracion/estado unificados de eventos.
- `src/web/webfs_http.cpp`: bancos web, recursos y recuperacion.
- `src/scope_stream.cpp`: protocolo activo de `/ws`.
- `src/storage/storage_manager.cpp` y `src/storage/nand_benchmark.cpp`: efectos y
  restricciones de formateos/pruebas.

Comprobacion de cobertura: `node test/endpoints_inventory.test.cjs`.
