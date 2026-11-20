# Revision de memoria y descargas web (2026-10-21)

Las paginas nuevas representan 24 ms, no 24 KB. Cada una ocupa 2048 bytes:
32 de cabecera y 2016 de muestras (192 muestras, siete canales de 12 bits).

## Auditoria actual, 2026-10-29

El build `ota` queda en **114.368 B de RAM estatica (34,9% de 327.680 B)**.
Ese numero no incluye las pilas de FreeRTOS ni las reservas dinamicas que se
crean al montar los servicios, por lo que no alcanza para evaluar el margen
real del servidor web.

Cambios aplicados para recuperar RAM dinamica:

| Reserva | Situacion actual | Recuperado |
| --- | ---: | ---: |
| Pilas permanentes de NAND IO, Wave y logger | 3 x 3 KiB | 3 KiB frente a 4 KiB cada una |
| Wave previo | maximo 96 ms, 18.192 B | 8.064 B si habia 192 ms persistidos |
| Anillo reciente del logger | 16 fragmentos, 1.920 B | 1.920 B frente a 32 fragmentos |
| Buffer serial del logger | 513 B | 512 B frente a 1.024 B |
| Probe de red condicional | 3 KiB mientras esta activo | 1 KiB frente a 4 KiB |

El objetivo de recuperar al menos 16 KiB no se debe estimar sumando solo estas
pilas: en este port los tamanos son bytes y Wave conserva sus reservas propias.
El `MeterTask` se valida aparte con su high-water mark en la placa antes de
seguir reduciendo la pila, para no poner en riesgo la adquisicion ADE/BL0942.

El logger conserva dos caminos: `log()` es inmediato y descarta si la cola
esta llena; `logWait()` espera como maximo el timeout recibido y devuelve si el
mensaje completo entro. Ninguno escribe NAND desde el productor: la tarea del
logger sigue drenando la cola y escribiendo la flash. `logWait()` no debe usarse
desde una ISR ni desde `MeterTask`.

`/debug` expone ahora los campos heredados `stackFreeWords` del logger y NAND IO
y `meterStackFreeWords` del medidor; en este port sus valores representan bytes,
ademas del heap libre, el bloque contiguo mas grande y los contadores de
cola/escritura.
Esos valores son la comprobacion definitiva despues de cargar el firmware;
la compilacion no puede mostrar el minimo de heap durante una descarga real.

## Reserva Wave

Con 192 ms previos se reservan once posiciones de 2016 bytes y tres tramos
de adquisicion de 1360 bytes: **26.256 bytes**. Se usan capacidades
`MALLOC_CAP_EXEC | MALLOC_CAP_32BIT`, sin recurrir a DRAM de acceso por bytes.
El escritor agrega su pila de 4096 bytes de DRAM y sus metadatos estaticos.

La pagina adicional para garantizar la prehistoria suma 2016 bytes al anillo.
El maximo de 288 ms requiere quince posiciones; el arreglo anterior tenia
catorce. Se corrigio el limite y se probo cada configuracion de 48 a 288 ms.

## RAM utilizable por red

| Medida | Antes de esta revision | Despues |
| --- | ---: | ---: |
| RAM estatica del firmware | 121.144 B | 116.784 B |
| Heap total en reposo observado | alrededor de 62 KB | alrededor de 66 KB |
| Heap de acceso por bytes en reposo | alrededor de 43 KB | alrededor de 48 KB |

Las descargas binarias de prueba, FAST y Wave ahora comparten un lector de
dos paginas. La segunda descarga recibe ocupado. Esto recupera RAM estatica
sin reducir la prehistoria ni modificar las capturas.

WebFS conserva sus dos paginas por respuesta mientras se usan. La tarea
libera esa memoria al terminar o desconectarse el cliente, incluso si ya
habia precargado todo el archivo. Una lectura NAND aceptada debe finalizar
antes de liberar su destino. Cada respuesta identifica su generacion para
que una desconexion antigua no cancele otra descarga.

## Fallo de transferencia capturado

Se observo `WebResponses.cpp: Failed to allocate` junto con un pedido de
**0 bytes**, con 31.888 bytes de DRAM aun libres y un bloque contiguo de
24.564 bytes. No era agotamiento de memoria en ese caso: el servidor llamaba
a `malloc(0)` cuando la salida TCP no tenia espacio, y abortaba la conexion.

`tools/patch_async_webserver.py` agrega la espera antes de esa reserva.
PlatformIO aplica el parche al compilar y detecta cambios incompatibles en
la dependencia. Se mantienen pruebas de reserva Wave, idempotencia del
parche, descargas interrumpidas y apertura completa en un navegador real.

Verificacion en placa: ocho cancelaciones seguidas, todos los assets con
longitud y SHA-256 correctos, y cuatro aperturas completas sin cache (dos
por LAN y dos por el dominio). Wave mantuvo 192 ms efectivos, sin gaps ni
errores de escritura. En estas pruebas no reaparecio el fallo de reserva.
El minimo de DRAM durante carga llego a 6536 bytes: el margen bajo carga
sigue siendo estrecho. Las aperturas externas demoraron entre 29 y 42 s.

## Segunda revision, sin reinicio ni cambio de firmware

La placa llevaba mas de dos horas encendida. Se observaron aproximadamente
64 KB de heap total, 46 KB de DRAM libre y un bloque contiguo de 39 KB.
El minimo de DRAM seguia en 6536 bytes, sin un minimo nuevo durante esta
observacion. Las tres capturas realizadas estaban confirmadas y no habia
errores de escritura; el contador Wave acumulaba dos gaps de adquisicion.

Se descargaron seis veces el evento 30 (10.240 bytes FAST) y su Wave 47
(65.536 bytes). El heap medido entre consultas fue 62.340, 62.344, 62.336,
62.356, 62.332 y 62.340 bytes. No se observo consumo acumulativo en ese uso.

### Reservas verificadas en el ELF

La RAM estatica suma 23.568 bytes de datos inicializados y 93.216 bytes de
BSS. Los tamanos siguientes son parte de esos 116.784 bytes, no reservas
adicionales de heap:

| Objeto o grupo | Bytes estaticos |
| --- | ---: |
| StorageManager completo | 25.552 |
| Buffers ADE halfCycleSamples, outputSamples y readBuffer | 18.048 |
| Lector binario compartido, incluidas dos paginas | 4.428 |
| Cinco objetos de sesion historica, sin sus paginas dinamicas | 2.040 |
| Cuatro objetos de sesion WebFS, sin sus paginas dinamicas | 1.376 |
| Buffer JSON principal | 1.280 |

Dentro de StorageManager, Wave ocupa 7528 bytes de metadatos y GridEventStore
7064 bytes. No deben volver a sumarse al total de StorageManager.

Reservas dinamicas principales, sin contar cabeceras del asignador, TCB ni
memoria interna de Wi-Fi/TCP:

| Reserva | Bytes y duracion |
| --- | --- |
| Wave: anillo y adquisicion | 26.256 de IRAM, permanentes |
| Pilas Wave, Meter, logger y NAND | 4.096 + 8.192 + 4.096 + 4.096 de DRAM |
| Pila AsyncTCP | 16.384 de DRAM, segun la dependencia instalada |
| Logger: cola, recientes, serie, paginas NAND y visor | 18.433 de DRAM, permanentes |
| Captura FAST | 6.144..10.240 de DRAM contigua mientras captura/escribe |
| WebFS por descarga | 4.096 de paginas + 2.048 de pila, mas respuesta/TCP |
| Historico por descarga | 6.144 de paginas + hasta 2.048 de snapshot + 3.072 de pila |
| Logs binarios por sesion | 4.104 de snapshot + 4.096 de paginas + 3.072 de pila durante lectura |

### Pendientes encontrados

1. **El historico retiene sus paginas al terminar.** `readTaskLoop()` termina
   sin liberar las paginas; `isAvailable()` las libera cuando otra consulta
   busca un lector. Una consulta real de 5 segundos devolvio 2048 bytes y
   dejo el heap en 54.540 frente a 62.792 bytes iniciales. Tras una consulta
   fuera del rango almacenado que reciclo el lector, volvio a 62.772. La retencion observada fue
   aproximadamente 8,2 KB. Es acotada por sesion, pero reduce el margen de red.
2. **Los snapshots binarios de logs no se liberan por reloj solamente.**
   El vencimiento de 120 segundos se comprueba al buscar una sesion para
   otra solicitud, sin limpieza periodica. Pueden quedar hasta 16.400 bytes
   de snapshots y paginas entre las dos sesiones. Esto se verifico en codigo;
   no se abrieron sesiones nuevas de logs durante la prueba para evitar
   dejar esa RAM retenida en la placa.
3. **Falta margen para combinar cargas pesadas.** Una captura FAST puede
   pedir 10 KB contiguos y las descargas agregan paginas, pilas y buffers TCP.
   Cinco lectores historicos no equivalen a poder sostener cinco descargas:
   solo sus paginas y pilas ya superarian la DRAM libre observada. El minimo
   de 6,5 KB durante la carga web impide descartar agotamiento en combinaciones
   mas exigentes.

Durante la observacion siguieron apareciendo avisos de coalescencia de polls
de AsyncTCP, aunque HTTP respondia y el heap permanecia estable. Esos avisos
por si solos no prueban agotamiento de RAM.

Conclusiones: no se observo una fuga creciente en reposo ni al descargar
eventos repetidamente. La asignacion Wave tiene el tamano esperado. Quedan
por corregir las retenciones de historico/logs y por comprobar el margen
cuando una captura coincide con descargas. Esta segunda revision solo
agrego documentacion; no modifico ni volvio a grabar el firmware.

## Correccion de las retenciones

Se implemento y subio por COM4 una nueva version con estas modificaciones:

- El productor historico espera el consumo HTTP o su cancelacion y libera
  sus tres paginas y snapshot antes de declararse disponible.
- Cada transferencia historica y de logs tiene una generacion. Una conexion
  anterior que se cierra tarde no puede cancelar otra respuesta en el mismo
  objeto. Los endpoints notifican la desconexion del cliente al lector.
- Los logs guardan entre peticiones solo token, bloque de anclaje y secuencia,
  dentro del objeto estatico. Las paginas y el snapshot de 4104 bytes se
  liberan al terminar o cancelar; continuar al bloque anterior no requiere
  conservar esa copia RAM.
- Los cursores vencen tras 120 segundos sin actividad. El loop comprueba
  ese vencimiento una vez por segundo, sin esperar el mutex de solicitudes.
  Al alcanzar el final del anillo, el cursor se invalida inmediatamente.
- Una lectura NAND aceptada mantiene su destino hasta recibir el resultado,
  incluso si el cliente ya corto la conexion. No se reciclan paginas en uso.
- Antes de reservar las paginas y la pila se comprueba DRAM libre y bloque
  contiguo, preservando un margen de 16 KiB y espacio para metadatos. Las
  consultas que no tienen margen reciben HTTP 503.

La RAM estatica paso de 116.784 a 116.816 bytes (+32 bytes). No se modificaron
los formatos almacenados ni el visor web, y Wave mantiene su reserva previa.

Primeras mediciones en placa: tres historicos completos y tres cancelados
recuperaron el heap, con valores entre 63.252 y 63.400 bytes. Los logs
completos, la continuacion con el cursor y la descarga cancelada tambien
devolvieron sus buffers; se observaron 63.284..63.392 bytes tras completarse.
No fue necesaria otra consulta para forzar esa liberacion.

Verificacion final en placa:

- El test de ciclo de vida completo paso, incluida una espera real de 122
  segundos: el cursor vencido se rechazo y se pudo crear otro correctamente.
- Ocho descargas web canceladas se recuperaron; todos los assets completos
  coincidieron en longitud y SHA-256 con sus archivos originales.
- Al finalizar las pruebas, el monitor serie mostro aproximadamente 65.164
  bytes de heap libre, 47.060 bytes de DRAM libre y un bloque contiguo DRAM
  de 40.948 bytes. No se observaron panics ni fallos de asignacion durante
  esta verificacion. El minimo DRAM observado fue de 17.548 bytes.
- Compilacion, pruebas de politica de sesiones, presupuesto RAM y orden de
  rutas correctas. Firmware instalado por COM4; puerto liberado al terminar.

Las retenciones identificadas quedan corregidas. Falta una prueba especifica
de captura simultanea con descargas para medir ese escenario de mayor carga;
estas pruebas no permiten dar por descartado todo posible problema de RAM.

## Cache web y revalidacion

Las dependencias lib/ conservan cache de un ano con su version vendor-1.
Al cambiar una dependencia debe actualizarse bootLibraryBuild en index.html.
HTML y archivos propios usan URLs estables y Cache-Control private, no-cache,
must-revalidate, no-transform: el navegador puede guardarlos pero consulta su
ETag antes de reutilizarlos. La firma sale del CRC ya almacenado en WebFS;
responder 304 no reserva un lector ni buffers de transferencia NAND.

Se aceptan ETag normales y debiles W/ usados por proxies. En la primera
prueba Cloudflare eliminaba la firma del HTML y debilitaba la de JavaScript;
no-transform conservo la firma y se verifico la revalidacion extremo a extremo.
Las pruebas locales y por https://meter.luiscerato.com.ar comprobaron 304 sin
cuerpo para firmas vigentes y 200 con contenido para firmas desactualizadas,
incluidos HTML, CSS y JavaScript. Firmware instalado por COM4 y WebFS actualizado.
RAM estatica sin cambios: 116.816 bytes. La primera visita tras migrar las
URLs necesita descargar los archivos propios; las siguientes los revalidan.

## Reservas permanentes de MeterTask, 2026-10-29

En el port ESP32 usado por este proyecto, `StackType_t` es de un byte y
`xTaskCreatePinnedToCore` recibe el tamaño de la pila en bytes. Por lo tanto,
el valor anterior `8192` reservaba **8 KiB**, no 32 KiB. El marco local de
`MeterTask` es de 64 bytes; el camino de lectura Wave llega aproximadamente a
1456 bytes y `scopeStreamProcess` a 144 bytes. El valor grande era un margen
conservador, no un buffer de muestras.

La implementación actual volvió a usar una pila dinámica de 8192 bytes para
`MeterTask`, conservando el margen original. Una pila estática de ese tamaño no
entra en la región DRAM actual: el enlazador informó un exceso de 5480 bytes.
También son estáticos la cola del logger y su buffer serie; el anillo reciente de logs queda dinámico
porque es una única reserva pequeña y no se recrea durante las descargas. Los
buffers temporales de HTTP, NAND y WebSocket siguen dinámicos porque nacen y se
liberan por sesión.

El entorno OTA compila con 118.456 bytes de RAM estática (36,1%) y 1.376.149
bytes de flash (67,7%). La comprobación pendiente en la placa es consultar
`/debug` después de una captura Wave y una descarga simultáneas: el margen de
pila debe permanecer holgado y `largest8` debe mejorar respecto de `free8`.
Los fallos de asignación registrados anteriormente pedían 10.240 bytes desde
`MeterTask` con un bloque contiguo menor que ese pedido; por eso se conserva la
reserva dinámica de 8192 bytes y se valida el margen real con el high-water mark.
