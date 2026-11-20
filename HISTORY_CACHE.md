# Cache y sincronizacion del historico

Configuracion > Aplicacion contiene ajustes exclusivos del navegador/origen:

El visor Historico abre por defecto la ultima hora. Un rango explicito en la URL
conserva prioridad sobre ese valor inicial.

| Ajuste | Inicial | Rango |
| --- | --- | --- |
| Sincronizacion automatica | Activada | Booleano |
| Dias recientes | 7 | 1 a 31 |
| Bloque | 256 KiB | 2 a 512 KiB, multiplos de 2 |
| Pausa posterior al bloque | 2 s | 1 a 60 s |
| Limite local | 100 MiB | 8 a 512 MiB |
| Retencion sin uso | 30 dias | 1 a 365 dias |

Al terminar la carga inicial espera 5 segundos. Completa primero los datos mas
recientes y avanza hacia dias anteriores, sin paralelismo ni ticks acumulados.
Cada pausa comienza cuando termina el bloque. Al completar el periodo consulta
el inventario como maximo una vez por minuto. No se sincroniza con la pestana
oculta ni en vistas de capturas, osciloscopio o calidad. La interaccion cancela
el bloque de fondo en curso; las descargas de eventos tambien tienen prioridad.
El ESP rechaza solicitudes de fondo si tiene clientes de osciloscopio o lectores
binarios activos, incluso si pertenecen a otro navegador.

IndexedDB `powerMeterHistory` conserva binarios originales y metadatos separados,
no JSON de muestras ni todos los binarios en RAM. Identidad MAC + nonce persistente
de historico; un formateo cambia el nonce antes del borrado. No se mezcla otra
placa ni una generacion anterior. Solo se escriben bloques con longitud,
identidad, secuencias, formato y CRC validos. El nonce agrega 8 bytes estaticos
al firmware y se lee de NVS solo al iniciar o formatear, no por muestra.

Los bloques confirmados son reutilizados por el visor Historico y por la descarga
automatica. La pagina RAM en curso se consulta aparte y no queda fijada en cache.
El cursor es de secuencia de 24 bits: no se pierden paginas por redondear sus fechas
a segundos. La descarga se transmite con los buffers del lector existente;
El tamanio de bloque HTTP no es una reserva de igual tamanio en el ESP.

Tras la comparacion se eligieron 256 KiB con pausa de 2 s como valor inicial.
La web migra una sola vez los ajustes locales que conservaban 128 KiB, sin
modificar los otros campos. Otros tamanos personalizados se respetan, y una
seleccion posterior de 128 KiB no vuelve a migrarse. No requiere firmware nuevo.

Al detectar una nueva generacion se eliminan binarios y metadata de generaciones
anteriores del mismo equipo; no se borran las caches de otros dispositivos.
Antes de declarar el periodo sincronizado se recorre toda la cadena de secuencias,
no solo su pagina mas reciente. Al abrir se verifica un bloque local por ciclo
(longitud, formato, CRC, cada secuencia y metadata); si falta o falla, se elimina
esa entrada y se vuelve a descargar. Los bloques recien descargados ya quedaron
validados antes de confirmar su escritura IDB. No se releen del ESP para chequear
una copia local valida. Los intervalos sin muestras entre secuencias consecutivas
no provocan descargas repetidas. La auditoria se limita al periodo configurado;
los periodos antiguos se validan tambien cuando se consultan manualmente.

La limpieza se basa en `usedAt`, nunca en la fecha de medicion. Consultar enero
hoy renueva su ultimo uso. Al superar el limite, las lecturas manuales usan LRU;
el fondo deja de retroceder cuando no entra mas informacion, evitando descargar
y expulsar indefinidamente la misma semana. Los datos nuevos mas recientes si
pueden desplazar a los antiguos. La cuota del navegador puede ser menor al limite
elegido; los errores de persistencia son visibles. No es un backup garantizado.

Forzar sincronizacion relee el periodo reciente y reemplaza cada bloque solo
despues de validar la descarga. Borrar cache historica no borra NAND ni la cache
separada de eventos/Wave y deja la sincronizacion pausada. Los ajustes sobreviven
al refresco en localStorage. La URL local y el dominio publico no comparten IDB.

La visualizacion limita a 16 MiB el binario de un periodo para evitar acumular RAM
indefinidamente en el telefono; la cache de fondo no tiene esa restriccion por
periodo. Un periodo mayor pide reducir fechas y conserva lo ya descargado.
La grafica combinada de historico y eventos queda para una etapa posterior.

## API

`GET /history` agrega `identity`, `committedTo` y `latestSequence`.

`GET /history/data?from=...&to=...&pages=64&before=...&identity=...&bg=1`

- `pages`: 1 a 256 paginas fisicas de 2048 bytes; omite la pagina RAM activa.
- `before`: secuencia inclusiva opcional, solo con `pages`, 0 a 16777215.
- `identity`: identidad esperada opcional; si cambio devuelve 409.
- `bg`: presencia indica baja prioridad; si otro lector/stream esta activo, 409.
- Respuesta: `X-History-Identity`, `X-History-First-Sequence`, `X-History-Pages`.
- Sin estos parametros conserva la descarga temporal existente con snapshot RAM.

No se agrega otro endpoint ni se modifica el formato almacenado.

## Pruebas

`node test/history_cache_web.test.cjs` prueba IndexedDB real, bloques, continuidad,
reutilizacion, refresco, CRC, identidad, prioridad, TTL y configuracion mobile.
`node test/history_format_compat.test.cjs` prueba el decoder existente.
`node test/capture_cache_web.test.cjs` verifica que la cache de capturas sigue aislada.

## Verificacion en placa, 27/10/2026

Firmware y WebFS subidos. `node test/history_cache_live.cjs` leyo dos bloques
reales de 131072 bytes: secuencias 80360..80423 y 80296..80359, todos los CRC
validos y sin repetir ni saltar paginas. Demoraron 4,2 y 5,0 segundos en esa
conexion, a lo que luego se suma la pausa elegida. Parametros invalidos devolvieron
400 y una identidad incorrecta devolvio 409.

`node test/history_cache_app_live.cjs` verifico la web desplegada en Chromium
mobile aislado: 14336 bytes guardados, 252 muestras reconstruidas, segunda lectura
y consulta del visor sin nuevas descargas binarias, sin errores JavaScript.
Al terminar: almacenamiento ready, ultimo error none, 57664 bytes de heap libre
y bloque contiguo de 33780 bytes. La primera pagina de evento siguio disponible
a los 867 ms del arranque. No se formateo ni borro ningun registro de la placa.

## Correccion de rendimiento HTTP

El 27/10 se separo preparacion y transferencia: plan de 64 paginas ~59 ms,
pero cuerpo HTTP de 128 KiB ~3847 ms (33 KiB/s total). No era una pausa elegida
por la web ni una lectura NAND de varios segundos. La entrega cortaba al final
de cada pagina, aunque el callback tuviera mas espacio. Ademas, una cola vacia
devolvia TRY_AGAIN inmediatamente y podia esperar al poll TCP de ~500 ms.

`HistoryReadSession::copyChunk` ahora cruza paginas ya disponibles y limita cada
entrega a dos segmentos TCP (`2 * CONFIG_LWIP_TCP_MSS`). Un solo segmento produjo
esperas compatibles con ACK diferido: aun sin episodios de cola vacia, el cuerpo
seguia demorando ~4,5 s. Con dos segmentos bajo a 0,74..1,06 s; completar esos
segmentos usando un presupuesto TOTAL de cola de 5 ms por callback lo redujo
finalmente a 0,60..0,75 s por bloque, incluyendo preparacion y validacion del test.
Si NAND se demora mas, devuelve lo ya copiado o TRY_AGAIN: no hay espera activa
ni lectura NAND desde HTTP. La pausa de la web sigue ocurriendo despues del bloque.

Se conservan las tres paginas y la pila del productor, sin buffers adicionales:
RAM estatica 116272 bytes, sin aumento por esta correccion. `node
test/history_stream.test.cjs` comprueba el callback C++ real con cruces de pagina,
parciales, cola vacia, cancelacion y final exacto. El presupuesto compartido de
5 ms es una espera de cola acotada; el callback ya no es estrictamente sin espera.

Firmware final subido. Seis bloques reales consecutivos de 131072 bytes dieron
598, 675, 630, 634, 679 y 745 ms, todos con CRC y secuencias correctas (172..214
KiB/s contando preparacion). Visor desplegado: 180 muestras reutilizadas sin otra
descarga binaria ni errores JavaScript. Las variantes intermedias de la prueba
no se consideran resultado final; la primera variante sin limite de segmento
tuvo descargas incompletas y fue reemplazada. No se modifico el formato NAND ni
se formateo el almacenamiento.

## Comparacion de tamanos, 27/10/2026

`node tools/benchmark_history_blocks.cjs` mide 32/64/128/256/512 KiB con la misma
cantidad de datos (1,5 MiB por tamano), tres rondas de orden alternado, pausa real
de dos segundos luego de cada descarga y sondas `/status` durante la transferencia.
Verifica CRC, secuencias y SHA-256 de todos los datos, iguales para los cinco tamanos.
Solo lee el ESP; no cambia la configuracion de la aplicacion ni del firmware.

| Bloque | Solicitudes | Media por bloque | KiB/s sin pausas | MiB/min con pausa 2 s | Status p95 / max |
| --- | ---: | ---: | ---: | ---: | ---: |
| 32 KiB | 48 | 0,249 s | 128 | 0,83 | 70 / 72 ms |
| 64 KiB | 24 | 0,424 s | 151 | 1,54 | 73 / 79 ms |
| 128 KiB | 12 | 0,750 s | 171 | 2,72 | 80 / 80 ms |
| 256 KiB | 6 | 1,377 s | 186 | 4,43 | 104 / 104 ms |
| 512 KiB | 3 | 3,119 s | 164 | 5,85 | 129 / 144 ms |

La tasa con pausas usa tiempos HTTP y descansos medidos; no incluye el costo de
IndexedDB del navegador. Son lecturas locales de esa sesion, no una garantia
para Cloudflare u otra red. Los bloques grandes no reservan mas RAM en el ESP:
heap libre observado por las sondas 44944..47928 bytes segun tamano, sin errores
de status. Las sondas no capturan todos los picos: el minimo global registrado
durante la prueba fue 25808 bytes. Al finalizar quedaron 59916 bytes libres
(59920 antes), con el mismo bloque contiguo maximo de 33780 bytes. No hubo
reinicios, errores NAND, rechazos de muestras historicas ni fallos de escritura.
Las muestras de 512 KiB
son tres, por lo que su variabilidad esta menos caracterizada.

Recomendacion: 256 KiB + 2 s para equilibrio entre sincronizacion y respuesta;
512 KiB + 2 s para completar mas rapido la carga inicial. Con la misma pausa,
512 KiB mejora el avance principalmente porque se hacen menos descansos, no
porque la transferencia por byte sea mas rapida. No se cambio el valor elegido
por el usuario. Resultado completo:
`logs/history-block-benchmark-2026-10-27T15-48-04-556Z.json`.

## Test sintetico y optimizacion posterior

El endpoint original `GET /test/speed.raw` genera 128 KiB de asteriscos sin leer
NAND; `GET /test/speed.csv` genera 16 MiB. Se probaron localmente: tres descargas
de 128 KiB dieron 436, 368 y 379 ms (293..348 KiB/s); 16 MiB completos demoraron
44943 ms (365 KiB/s). Se verifico que todos los bytes eran asteriscos.
La red por si sola seguia siendo mas rapida que el lector historico.

Se amplio la entrega de dos a cuatro segmentos TCP (hasta la ventana de 5744
bytes de este SDK) y el presupuesto compartido de cola paso de 5 a 20 ms por
callback. No es un delay fijo: solo espera si faltan paginas preparadas. Si se
agota, devuelve los bytes disponibles o TRY_AGAIN; no aborta por una demora corta
de NAND. Siguen siendo tres buffers de 2 KiB y la misma tarea de baja prioridad;
el callback no ejecuta operaciones NAND ni hace espera activa.

Tambien se optimizo exclusivamente el CRC del lector: `HistoryReadSession::isValidPage`
usa `uint16_t(~esp_rom_crc16_be(0, data, length))` de la ROM del ESP32. El SDK
documenta esa combinacion de seed/complemento como CRC-16/CCITT-FALSE. Conserva
exactamente los CRC del formato existente; no se quitaron comprobaciones ni se
cambiaron los escritores, el formato o los datos guardados. No agrega tablas en RAM.

Comparacion final de tres pasadas alternadas con `node tools/benchmark_history_transport.cjs`:

| Lectura | Media total | Media headers | Media cuerpo | KiB/s totales |
| --- | ---: | ---: | ---: | ---: |
| Asteriscos 128 KiB | 374 ms | 26 ms | 347 ms | 342 |
| Historico 128 KiB | 515 ms | 63 ms | 452 ms | 249 |
| Historico 256 KiB | 948 ms | 80 ms | 868 ms | 270 |

Los 256 KiB tardaron 917..983 ms: el cuerpo por si solo ronda 295 KiB/s, y la
preparacion explica parte de la diferencia con el test sintetico. Con ventana
completa/20 ms pero CRC anterior, la media de 256 KiB habia sido 1168 ms.
Resultados completos antes/despues del CRC ROM:
`logs/history-transport-2026-10-27T16-12-27-964Z.json` y
`logs/history-transport-2026-10-27T16-15-33-822Z.json`.

Firmware actualizado en la placa, RAM estatica sin cambio (116272 bytes), todos
los CRC validos, almacenamiento ready y sin errores de I/O, rechazos ni fallos
de escritura. Visor web comprobado usando cobertura cacheada: 252 muestras,
cero descargas binarias adicionales y cero errores JS. El test C++ tambien
simula paginas que demoran 8 ms y una demora de 30 ms: el presupuesto de 20 ms
se comparte y el timeout pide reintento sin cancelar la sesion.

La configuracion web sigue siendo 256 KiB y 2 s entre bloques; no se altero su
pausa ni se formateo la NAND. El benchmark previo de tamanos describe la version
anterior del lector; esta seccion contiene las mediciones de la version actual.
El argumento `--long` del benchmark de transporte agrega la prueba sintetica de
16 MiB. No extrapolar tasas locales a Cloudflare.

## Latencia hasta los primeros datos

Cinco pasadas locales alternadas, con `TRANSPORT_ROUNDS=5`, midiendo el primer
chunk no vacio del cuerpo HTTP (no solamente la llegada de headers):

| Lectura | Primeros datos, media | Minimo..maximo | Bloque completo, media |
| --- | ---: | ---: | ---: |
| Asteriscos 128 KiB | 29 ms | 23..36 ms | 370 ms |
| Historico 128 KiB | 57 ms | 51..61 ms | 490 ms |
| Historico 256 KiB | 80 ms | 71..87 ms | 948 ms |

Resultado: `logs/history-transport-2026-10-27T16-24-18-186Z.json`.
Todas las descargas validaron longitud y contenido; las historicas tambien CRC,
identidad y secuencia. No aumentaron los contadores de cola vacia/reintento.
Las tasas medias totales fueron 346, 261 y 270 KiB/s respectivamente.

Esta latencia incluye red y preparacion de la consulta. No mide directamente
la espera interna por cada pagina NAND y no debe usarse para dimensionar el
presupuesto de cola de 20 ms: ese limite es por callback, compartido entre sus
paginas, no una pausa fija ni un timeout de toda la descarga. Los contadores
de reintento tampoco miden esperas exitosas dentro de ese presupuesto.
No se modifico el firmware ni se agrego RAM al ESP para esta medicion.

## Espera interna real por pagina

Medicion posterior con firmware instrumentado: `/storage?detail=1`, dentro de
`historyRead`, expone `nand`, `prepare` y `queue`. Cada etapa contiene count,
totalUs, maxUs y cantidades superiores a 1, 2, 5 y 10 ms. Son acumulados desde
el arranque: calcular medias con diferencias de totalUs/count entre consultas;
maxUs es maximo desde el arranque, no solamente del intervalo consultado.
Los contadores ocupan 96 bytes globales, sin buffers nuevos por sesion.

Cinco pasadas alternadas de 128/256 KiB, 960 paginas NAND de 2 KiB:

| Etapa | Cantidad | Media | Maximo |
| --- | ---: | ---: | ---: |
| Operacion NAND (elapsedUs del servicio) | 960 | 2300 us | 12234 us |
| Desde intento de envio hasta lectura y validacion CRC | 960 | 4535 us | 18026 us |
| Espera HTTP solamente cuando no habia pagina lista | 603 | 2496 us | 9663 us |

La operacion NAND mide tiempo de pared: incluye SPI y cualquier desalojo de la
tarea durante esa operacion, no solamente el tiempo interno del chip. Preparacion
incluye ademas colas, planificacion y CRC. Las tres etapas se solapan; no sumarlas.
Las paginas ya preparadas no entran en la media de espera HTTP. De las 603 esperas,
335 superaron 2 ms, 45 superaron 5 ms y ninguna supero 10 ms. No hubo agotamientos
del presupuesto ni reintentos HTTP por cola vacia. El resto se entrego sin espera
bloqueante. La consulta inmediata de cola ahora se intenta primero sin bloqueo;
solo cuando falla se mide la espera acotada existente.

Se mantiene 20 ms como presupuesto compartido por callback, no delay fijo.
Reducirlo a 1..2 ms descartaria muchas oportunidades de entregar datos en el
mismo callback; el reintento TCP puede costar mucho mas. La muestra no incluye
una prueba forzada durante borrado NAND y no garantiza sus peores latencias.
Durante la prueba las descargas de 256 KiB promediaron 935 ms; todos los CRC y
secuencias fueron validos, sin errores I/O ni reinicios. Firmware subido por OTA;
RAM estatica 116368 bytes. No fue necesario actualizar la web ni formatear.
Resultado: `logs/history-transport-2026-10-27T16-44-00-114Z.json`.
