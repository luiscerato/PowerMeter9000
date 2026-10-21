# Capturas unificadas: contrato vigente

## Politica

Un solo escritor `GridEventStore` conserva FAST de ocho canales: VR/VS/VT,
IR/IS/IT/IN y neutro-tierra. No hay envio de capturas ni confirmaciones por MQTT
o WebSocket. El osciloscopio en vivo permanece separado.

- Bajo y alto voltaje de cada fase comparten umbral con DIP y swell.
- Confirmacion minima DIP, swell y corriente: 10..2000 ms, pasos de 10 ms.
- Maximo puntual: 3700 ms desde el primer cruce. Se calcula con 5 paginas,
  118 muestras por continuacion, 100 muestras posteriores y 2 de margen.
- El pretrigger conserva hasta 113 muestras (1130 ms). La ventana maxima es
  585 muestras (5850 ms nominales). El anillo previo conserva 400 muestras.
- Se conservan candidatos en RAM. Un puntual solo se registra al recuperar.
- Si persiste, confirma un estado sostenido. No se guarda ademas un DIP/swell
  independiente de esa misma condicion. Alta corriente tiene inicio y retorno.
- Solo DIP, swell y sobrecorriente puntuales tienen habilitacion de guardado.
  Los detectores siguen trabajando aunque esa habilitacion este apagada.
  Bajo/alto voltaje, ausencia, red y neutro alto se registran siempre con datos
  validos. El neutro sin datos BL no se interpreta como cero.
- La recuperacion de fase y corriente usa histeresis y confirmacion comun.
  Si recupera justo al limite puntual, tiene prioridad el retorno observado.
- Una caida que luego termina en corte conserva la captura desde la caida.
  El timestamp del corte sigue siendo el instante real de ausencia, no el de
  la caida anterior. Los offsets de las muestras permiten representar ambos.
- Un estado confirmado permite escribir progresivamente en NAND. Su retorno
  se agrega a la captura activa mientras entre en su ventana completa, sin
  cerrarla anticipadamente al cumplirse un segundo desde la caida. Si ya
  termino la ventana inicial, abre otra captura. No se retienen muestras
  durante toda la duracion de un estado largo.
- Un puntual cerrado espera su segundo posterior y se escribe desde RAM.
  Se usan 3..5 paginas segun muestras y marcas; nunca mas de cinco.
- Paginas en vuelo o escritas son inmutables. Un apagado puede perder RAM y la
  pagina en vuelo; el visor reconoce las capturas parciales ya persistidas.

No hay recorte del medio ni reconstruccion de dos ventanas dentro de un
puntual largo: al superar el limite pasa a estado. No se aumento el limite a
diez paginas. Wave se guarda en una particion independiente, enlazada al FAST durable.

## Parametros

`GET /grid/config` publica ajustes de voltaje y politica calculada.
`POST /grid/config` recibe exactamente estos 14 campos, como formulario:

| Campo | Uso / rango |
|---|---|
| lowR, lowS, lowT | Umbral bajo por fase, V |
| highR, highS, highT | Umbral alto por fase, V (hasta 500) |
| noVoltage | Ausencia, 1..100 V |
| hysteresis | Voltaje, 0..50 V; bandas sin solapamiento |
| lossConfirmMs | Ausencia, 20..2000 ms |
| recoveryConfirmMs | Retorno de fase/corriente, 20..1000 ms |
| neutralHigh | Neutro-tierra, 0.025..409.55 V |
| neutralHysteresis | Menor al umbral de neutro |
| neutralConfirmMs | 20..2000 ms |
| neutralRecoveryMs | 20..1000 ms |

La respuesta agrega `pointLimitMs=3700` y `capturePages=5`, ambos calculados,
no configurables. No existe una confirmacion separada de bajo/alto voltaje.

`POST /options`, namespace `meter`, configura:

| Campo | Uso |
|---|---|
| dipEnabled, swellEnabled, overcurrent | Guardar puntuales; no desactivan estados |
| dipMs, swellMs, ocMs | Confirmacion minima 10..2000 ms, pasos de 10 |
| ocCurrent | Umbral comun R/S/T/N, mayor que 0 y hasta 163.82 A |
| ocHyst | Histeresis en A, no negativa y menor que ocCurrent |

`POST /options/reload` aplica desde NVS sin SPI ni NVS dentro de adquisicion.
Los limites de carga desde NVS tambien se normalizan; tiempos corregidos se
persisten y se avisan por log. Configurar durante una perturbacion puede
interrumpir su seguimiento; una duracion inconsistente se marca limitada,
nunca se deja desbordar por un timestamp de retorno invalido.

Valores iniciales de voltaje: 190/250 V por fase, ausencia 20 V, histeresis 5 V,
ausencia y retorno 250 ms; neutro 25 V, histeresis 2 V, confirmacion 100 ms y
retorno 250 ms. Se usa la clave NVS nueva `grid/settings`; no se convierten las
configuraciones antiguas. Las calibraciones, red, historico y logs no se borran.
Los respaldos nuevos incluyen todos los ajustes; las claves retiradas se
rechazan, no hay aliases ni conversion de respaldos de eventos anteriores.

## Formato unico

Solo se acepta el discriminador de formato **4**. No hay lectores de formatos
anteriores, migraciones ni reinterpretaciones. Hay que formatear `gridEvents`
para comenzar. Esta puesta en marcha tambien formatea `waveEvents`, nunca
historico, NVS, WebFS ni diagnosticos.

- Cabecera raiz: **30 bytes**, mismo layout.
- Ajustes: **48 bytes**.
- Contexto RMS: **156 bytes**, sin modificar.
- FAST raiz: offset **234**, hasta 113 muestras.
- Continuacion: cabecera **10 bytes**, 12 marcas de **12 bytes** y FAST desde
  offset **154**, hasta 118 muestras. Las cabeceras y cada pagina tienen CRC.
- Cada FAST ocupa 16 bytes: tiempo uint16 y ocho valores unsigned de 14 bits.
  Voltajes 0.025 V, corrientes 0.01 A; 16383 indica sin dato.
  Tiempo monotono en ticks de **100 us**, vuelta cada 6.5536 s. El visor
  desenvuelve deltas modulares, suficientes para toda la captura de 5.85 s.
- Offset 9: fraccion Unix de un segundo en ticks de 100 us (uint16).
  Offset 22: primer FAST relativo a la raiz en us (int32). Se conservan los
  30 bytes de cabecera y todos los offsets; no se reinterpretan archivos viejos.
- Una transicion conserva offset int16 en ms, duracion uint32 en 10 ms,
  sujeto/estados uint16 y magnitudes anterior/actual uint16 a 0.01 V o A.
- Sujeto bits 0..2, estado anterior 3..5, actual 6..8. Bit 9 indica puntual,
  tipo puntual 10..14 (1 DIP, 2 swell, 3 OC); bit 15 indica limitado.
- En estados, bit 10 indica magnitud corriente y estado 5 indica alta
  corriente; normal sigue siendo 1. No se confunde con estados de voltaje.
- Cabecera timeStatus: bit0 hora estimada, bits1..3 arranque, bit4 pretrigger
  insuficiente, bit5 FAST empaquetado, bit6 marcas omitidas, bit7 prueba.
- transitionCount: bits0..6 cantidad (0..12), bit7 marcas omitidas. El aviso
  puede persistir en una continuacion aunque la raiz ya se haya escrito.

### Ajustes: offsets absolutos de pagina

| Offset | Bytes | Contenido |
|---|---:|---|
| 30 | 6 | Bajo R/S/T, uint16 a 0.1 V |
| 36 | 6 | Alto R/S/T, uint16 a 0.1 V |
| 42 | 2 | Ausencia a 0.1 V |
| 44 | 2 | Histeresis V a 0.1 V |
| 46 | 2 | Confirmacion ausencia a 10 ms |
| 48 | 2 | Confirmacion retorno a 10 ms |
| 50 | 2 | Limite puntual a 10 ms |
| 52 | 2 | Umbral corriente a 0.1 A |
| 54 | 2 | Histeresis corriente a 0.01 A |
| 56 | 1 | Minimo DIP a 10 ms |
| 57 | 1 | Minimo swell a 10 ms |
| 58 | 1 | Minimo OC a 10 ms |
| 59 | 1 | Bits0..2 guardar puntuales, bit3 neutro, bit5 redondeo, bit6 saturacion |
| 60 | 2 | Umbral neutro a 0.1 V |
| 62 | 2 | Histeresis neutro a 0.1 V |
| 64 | 2 | Confirmacion neutro a 10 ms |
| 66 | 2 | Recuperacion neutro a 10 ms |
| 68 | 2 | Muestras previas |
| 70 | 2 | Muestras posteriores reservadas |
| 72 | 1 | Maximo de paginas |
| 73 | 1 | Reservado, cero |
| 74 | 2 | Ventana nominal a 10 ms |
| 76 | 2 | Reservado, cero |

## Limites visibles y pruebas

Hay una captura FAST activa. Hasta ocho descriptores que llegan fuera de su
ventana pueden diferirse sin perder su timestamp ni recortar un offset grande
a int16. Se procesan al liberar el escritor. Los candidatos puntuales tardios
pueden volver a armarse desde el anillo. No hay memoria ilimitada: si se agota
la cola o las 48 posiciones de marcas de una captura de cinco paginas, se
incrementa `dropped` y se marca la omision. Si el anillo ya no conserva todo el
pretrigger al rearmar, el archivo lo informa. No se garantiza cobertura FAST
completa ante rafagas arbitrarias, falta de heap o reinicios.

`test/incident_policy.test.cjs` compila las implementaciones reales de los
metodos con NAND simulada: DIP cerrado, persistente con guardado puntual
apagado, conversion a corte manteniendo inicio, swell/alto voltaje, corriente
sostenida y retorno, recuperacion exacta al limite, descartes, cola llena y
retorno a los 99 s. `test/unified_events.test.cjs` agrega formato, CRC,
rechazo de versiones anteriores y visor/configuracion escritorio/movil.

Tambien se verifican estado de fases, neutro, persistencia NVS diferida,
respaldo, prioridad Wave y sincronizacion BL. Las pruebas artificiales no
reemplazan perturbaciones electricas reales. Sigue pendiente la oscilografia
de eventos en NAND y el nuevo sistema de notificaciones.

## Verificacion en placa

Firmware y WebFS cargados en `192.168.1.90`. Compilacion final: 111240 bytes
de RAM estatica y 1309145 bytes de firmware. Se verificaron los formularios,
el rechazo de parametros retirados o fuera de rango y el osciloscopio en vivo.
Las capturas sinteticas probaron escritura progresiva de estados y espera en
RAM hasta completar el posttrigger de un puntual.

Se formateo fisicamente **solo `gridEvents`**: 48 bloques, 6291456 bytes,
100 %, cero bloques fallidos y particion habilitada al terminar. La primera
prueba encontro un fallo de reapertura: el recuperador consultaba el estado
publico deshabilitado durante el formato, aunque era el propietario autorizado
para recuperar el indice. Se corrigio, se agrego una prueba y se repitio el
formato con resultado satisfactorio. El resto de las particiones y NVS no se
formatearon.

Tras ese borrado aparecio el evento nuevo 1, neutro alto de unos 234 V,
coherente con el BL conectado a linea durante las pruebas. Su descarga
`tmp/fresh-event-after-format.bin` tiene 213 muestras, captura completa y cero
paginas invalidas por CRC. No es un evento antiguo conservado. Los parametros
guardados en su snapshot coinciden con la configuracion vigente. Quedan por
probar cortes y perturbaciones electricas reales con este firmware.

### Revision de los primeros cortes reales

Se descargaron los eventos 2..9: todos completos, sin paginas invalidas por CRC
y sin registros descartados. Los retornos estaban guardados, pero separados:
2/3 (2770 ms sin tension), 4/5 (14390 ms), 6/7 (3120 ms) y 8/9 (3120 ms
hasta el retorno confirmado, con conmutaciones intermedias). Las capturas de
retorno 3/5/7/9 tienen raiz de neutro alto y las recuperaciones de R/S/T/red en
sus transiciones: el titulo de la lista solo representa la raiz, no todo su
contenido. El neutro sigue conectado a linea en esta prueba.

Se encontro un cierre anticipado: cada estado reducia la captura a un segundo
posterior, desaprovechando la ventana de cinco paginas. Se corrigio para que
cualquier estado confirmado mantenga toda la ventana disponible. Solo las
capturas exclusivamente puntuales pueden cerrar antes, tras su posttrigger.
Pruebas nativas agregadas: corte de 2770 ms y retorno en la misma captura,
dos cortes con dos recuperaciones, y raiz de neutro con retorno de fase.
No cambia el formato ni se borran las capturas descargadas. Un retorno fuera
de ventana sigue necesitando otra captura; no se retiene un corte largo entero.

Correccion cargada por OTA en `.90`, sin formatear. La placa conserva los
eventos 1..9. El evento nuevo 10, neutro alto tras reiniciar, tiene 585 muestras
entre -1128 y +4711 ms, completo y sin paginas invalidas: ya aprovecha la
ventana de 5,84 s en lugar de cerrar aproximadamente un segundo tras el
disparador. Archivo de comprobacion: `tmp/review-event-10-fixed.bin`.
Pendiente repetir cortes fisicos con la correccion; las pruebas nativas de
caida/retorno y conmutaciones sucesivas pasan.

## Lectura web de capturas

El visor diferencia captura de disparador inicial. Una vez descargada, ordena
cabecera y transiciones, las numera y agrupa por sujeto y magnitud: corriente
del neutro y tension neutro-tierra no se mezclan. Presenta la secuencia de
estados y los retornos realmente guardados. Una captura sin retorno lo indica;
un salto entre estados incompatibles se marca como secuencia incompleta.
No se reclasifican DIP/swell ni se inventan inicios o finales a partir de FAST.
La duracion del estado previo procede del registro de retorno; el tramo
temporal cubierto por las muestras se presenta por separado.

Las marcas agrupan cambios simultaneos y usan la misma numeracion que la
tabla. Al elegir una fila se amplia el cambio; si es puntual, incluye su
duracion guardada. Al elegir un resumen de fase/red/neutro se amplian sus
cambios. Los puntuales usan su duracion; los estados emparejan cambios del
mismo sujeto y magnitud. Un retorno aislado usa la duracion previa guardada,
recortada a las muestras disponibles, sin confundirla con la duracion de un
estado nuevo. Ambos graficos conservan el mismo rango y 50 ms de margen por lado. El
control de rango completo restaura toda la ventana. Las marcas se pueden ocultar.
Contexto y configuracion siguen plegables bajo la grafica.

Se conservan hasta doce capturas completas decodificadas en el navegador,
no en el ESP. Las parciales se vuelven a descargar; un ID reutilizado tras
formatear invalida su copia al cambiar la fecha de cabecera. Una respuesta
lenta de una seleccion anterior no reemplaza la ultima seleccion del usuario.
La ampliacion Wave y el formato 4 se describen abajo; esta vista no modifica la deteccion.

Pruebas: `test/grid_reading.test.cjs` para agrupacion, magnitudes y marcas;
`test/unified_events.test.cjs` para controles, cache, respuestas fuera de orden,
capturas parciales y escritorio/movil. Para revisar una captura real:
`node test/unified_events.test.cjs tmp/review-event-11.bin`.
Separar y enlazar episodios entre distintas capturas sigue pendiente de definir.

### DIP de la manana (eventos 12..15)

Las cuatro capturas son completas, CRC validos y sin muestras de canales sin
dato. Cada una contiene tres DIP (R/S/T), no solo el sujeto de su cabecera.
No son pruebas artificiales. Horarios locales de Buenos Aires del 20/10/2026:

| Captura | Hora | Minimo de fase | Duraciones R / S / T |
|---|---|---|---|
| 12 | 09:59:39 | 179.07 V | 100 / 90 / 90 ms |
| 13 | 10:00:36 | 177.64 V | 110 / 120 / 120 ms |
| 14 | 10:01:27 | 176.62 V | 130 / 120 / 120 ms |
| 15 | 10:02:18 | 176.68 V | 130 / 130 / 130 ms |

Umbral 190 V, histeresis 5 V, minimo 50 ms. Las duraciones coinciden con el
cruce de recuperacion a 195 V, con redondeo a 10 ms: no incluyen los 250 ms de
confirmacion posterior. Las diferencias de una muestra entre fases coinciden
con muestras a ambos lados del umbral. El mayor intervalo entre FAST es 14 ms.
La captura 15 avisa pretrigger corto: 112 muestras en lugar de 113, alrededor
de 10 ms menos previos; el DIP y su recuperacion estan completos.

Se repiten a intervalos de 56.935, 50.586 y 51.332 segundos y tienen una forma
muy similar en los canales de fase y BL. Eso sugiere una perturbacion repetida
comun a la alimentacion medida, pero el registro no identifica su causa o la
carga responsable. Archivos de comprobacion: `tmp/review-event-12.bin` a
`tmp/review-event-15.bin`. El zoom DIP usa inicio/duracion y 50 ms por lado.

## Oscilografia NAND

Contrato Wave v1, sin compatibilidad con otros formatos:

- Un bloque fisico de 128 KiB por captura. Canales IA/VA/IB/VB/IC/VC/IN,
  signed12 a 8000 Hz; escalas fijas 0.1 A y 0.25 V. No depende de los controles
  del osciloscopio en vivo. BL no tiene oscilografia.
- 31 paquetes de 4096 bytes: cabecera de 64 bytes y 4032 bytes utiles, tres
  tramos de 16 ms. Total nominal 1488 ms, incluyendo el historial previo.
- Cada paquete tiene magic, version, Wave ID, indice, secuencia, tiempo inicial
  uint64 us y offsets int32 us de sus otros dos tramos; disparo uint64 us,
  escalas, frecuencia/cantidad, mascara de canales/tramos y CRC32 separados
  para cabecera y muestras. Flags: saturacion, hueco, reloj estimado, previo corto.
- La referencia temporal se ancla al contador de paginas ADE y sigue mitades
  de 128 muestras a 8 kHz. No usa cada IRQ0 compartida como un nuevo reloj:
  filtra el jitter de atencion (correccion maxima 8 us por mitad), reconoce
  duplicados y marca mitades perdidas. Se marca **estimada**: no se promete
  precision fisica de 100 us sin validar la latencia ADE con instrumental.
  FAST toma su referencia monotona
  antes de la lectura SPI y espera BL sin cambiar ese instante.
- Las paginas 62 y 63 contienen dos copias identicas de un cierre de 112 bytes,
  resto 0xff. CRC32 y ambas copias deben coincidir. Guarda hasta cuatro enlaces
  con FAST ID, raiz monotona us y Unix us. Nunca se reprograma una pagina usada.
- Se captura desde el primer cruce candidato (tambien retornos). Cruces dentro
  de una Wave activa no prolongan la captura. Al confirmar y persistir FAST se
  asocian las Wave cuyo disparo pertenece a su ventana. El cierre espera seis
  segundos despues de terminar las muestras para recibir enlaces tardios.
  Un enlace fuera de ese plazo no rescata una Wave ya huerfana.
- Sin confirmacion, o con fallo de escritura, el bloque queda huerfano. No se
  borra inmediatamente; se reutiliza en la siguiente vuelta del anillo.
- Mantiene cuatro bloques pre-borrados, uno por operacion de baja prioridad,
  sin iniciar borrados durante adquisicion Wave activa ni sobre una descarga
  fijada o un cierre pendiente. Errores de borrado excluyen el bloque en esta
  ejecucion; el bitmap fisico global permanece bajo StorageManager.

RAM y propiedad:

- Previo pedido 192 ms por defecto, 48..288 ms en pasos de 48. Se reserva una
  sola vez antes de MeterTask; al fallar se reduce un paquete por vez hasta
  48 ms. Si no cabe ese minimo, solo Wave queda deshabilitada.
- Pool de N paquetes completos + parcial + margen: 24 KiB con 192 ms, maximo
  32 KiB con 288 ms. Anillo de adquisicion de tres tramos, aproximadamente
  4 KiB. El previo efectivo puede variar hasta un paquete adicional por la
  posicion del disparo; si falta historial al arrancar queda marcado.
- MeterTask empaqueta un tramo de 1344 bytes y publica una copia acotada. No
  espera NAND ni reserva heap. Scope conserva su ruta actual independiente.
- Pool y anillo se reservan en IRAM/heap de palabras, con accesos uint32
  alineados exclusivamente. Cabeceras con float y calculos CRC permanecen en
  DRAM; el CRC de datos se acumula al recibir cada tramo. No se lee IRAM por
  bytes ni se pasan esos punteros directamente al driver SPI. Esta restriccion
  esta documentada por [Espressif](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/system/mem_alloc.html).
- El guardador arma paquetes listos para NAND. Copia cada pagina por palabras
  a una pagina DRAM de 2048 bytes en el stack de su tarea, compartida mediante
  union con el tramo de recepcion y usada tambien para el cierre. Conserva
  ambos buffers hasta la finalizacion NAND. No solo hasta entrar en la cola.
  Los buffers de muestras se reciclan al terminar; solo queda metadata de
  ocho cierres pendientes. Un desborde se cuenta, no mezcla datos ni los inventa.
- El loop principal difiere persistencia de estados y energia en NVS mientras
  Wave adquiere, incluyendo un margen de dos segundos desde cada candidato.
  Una escritura de flash interna puede detener la otra tarea aunque se haya
  sacado de MeterTask. Al quedar libre, la persistencia conserva su politica
  de ultimo estado por slot. Otros guardados manuales NVS no estan bloqueados.
- El lector HTTP comparte con FAST sus dos paginas alternadas ya reservadas.
  Se admite una descarga FAST o Wave por vez; un segundo lector recibe ocupado.
  No agrega 4 KiB permanentes despues de abrir Wave ni reserva los 128 KiB de
  la captura. El historico conserva su lector independiente. La tarea de
  guardado, colas, metadata y pagina de cierre requieren RAM adicional al pool.

API y web:

- `GET /wave/config`: reserva efectiva, capturas/confirmadas/huerfanas,
  huecos, disparos descartados, fallos, bloques borrados y cierres pendientes.
  `wordHeapBytes` distingue los buffers en IRAM. Contadores de actividad son
  de esta ejecucion, no totales historicos. `/storage?detail=1` agrega free8,
  largest8 y min8: el heap total tambien incluye memoria no accesible por bytes.
- `POST /wave/config`, formulario `preMs`: persiste en NVS `wave/preMs`;
  requiere reiniciar para cambiar la reserva. Este ajuste tiene formulario
  propio y no forma parte del respaldo general de opciones anterior.
- `GET /wave-events?eventId=N`: devuelve hasta ocho Wave confirmadas para el
  ID y la hora de esa raiz FAST; evita confundir IDs reciclados tras formateo.
- `GET /wave-events/data?block=B&waveId=N`: 131072 bytes. El bloque se fija
  contra reutilizacion hasta terminar sus lecturas fisicas; HTTP consume
  despues las ultimas paginas ya copiadas. Un lector activo impide formatear.
- Panel plegable debajo de FAST: seleccion de Wave, recarga, leyenda por
  canal y RMS opcional superpuesto. Eje temporal comun relativo a la raiz
  FAST; el zoom por registro se replica. Paquetes invalidos dejan huecos,
  nunca unen datos a traves de una pagina corrupta.
- Formateo individual: servicio existente `/storage/partition/format`,
  `partition=waveEvents` o `gridEvents` y `confirm=FORMAT`. No usa borrado global.

Pruebas: `wave_events.test.cjs` (binario, CRC, signed12, huecos, cierre roto),
`wave_pool.test.cjs` (selector C++ real y propiedad de slots),
`wave_memory.test.cjs` (copia real compilada solo L32I/S32I),
`wave_clock.test.cjs` (jitter, duplicados y mitades perdidas),
`wave_nvs_hold.test.cjs` (pausa NVS, vencimiento y wrap),
`unified_events.test.cjs` (FAST y Wave, escritorio/movil), ademas de los
detectores, estado, neutro, contexto, prioridad Wave y orden de rutas.
Las pruebas sinteticas no reemplazan cortes reales ni la validacion de fase
temporal con el osciloscopio.

### Estado de esta puesta en marcha

Compilacion final serial correcta: 116992 bytes de RAM estatica, 1324889 bytes de
programa. WebFS instalado: 299008 bytes. Pasan pruebas Wave/FAST, selector de
slots, detectores, estados, neutro, contexto, rutas, prioridad de adquisicion,
respaldo anterior y vistas de escritorio/movil (incluido panel Wave).

La primera OTA recibio OK, pero despues la placa dejo de responder por red.
Conectada por CH340 COM4, seguia ejecutandose. La captura serie mostro unos
60 KiB de heap total pero solo 16 KiB accesibles por bytes y un bloque mayor
de 9716 bytes: fallaba la reserva FAST de 10240 bytes. El reset recupero red.
Se corrigio Wave usando 28656 bytes del heap de palabras y una pagina DRAM
de transferencia. En reposo: aproximadamente 45 KiB free8, bloque mayor de
42 KiB. El primer lector Wave dedicado retenia otros 4 KiB y dejaba al
osciloscopio por debajo de su margen; se reemplazo por el lector compartido
FAST/Wave, sin reducir las protecciones de heap del WebSocket.
La pagina de staging compartida con la recepcion elimina otros 2048 bytes
globales. Margen minimo observado del stack Wave: 1308 bytes.

La descarga real tambien expuso jitter de referencia temporal: IRQ0 compartida
producia solapamientos de tramos aunque todos los CRC fueran correctos.
Se agrego reloj por mitad/contador ADE. Un hueco restante del arranque coincidio
con gridNvs (hasta 168423 us) y desaparecio al diferir NVS durante Wave.

Firmware final subido y verificado por COM4. WebFS instalada correctamente.
`waveEvents` (41943040 bytes) y `gridEvents` (6291456 bytes) formateadas
individualmente, ambas 100%, sin bloques fallidos. No se borro historico,
NVS, calibraciones, configuracion de red ni diagnosticos. Previo 192 ms
persistido explicitamente en NVS; maximo 288 ms.

Comprobacion final: FAST #1, 585 muestras, cinco paginas, todos los CRC validos;
Wave #9, 11904 muestras por canal, 31 paquetes validos, ningun hueco, flags=4
(referencia temporal estimada). Intervalos relativos: FAST -1130.236..4710.864 ms;
Wave -196.233..1291.636 ms. Copias locales `tmp/wave-final-fast-1.bin` y
`tmp/wave-final-9.bin`. Las capturas anteriores de diagnostico quedan en tmp,
pero ya no en la particion. `test/live_wave_view.test.cjs` verifica la web real.
Con el firmware final: FAST #3 enlaza Wave #11, descargada en
`tmp/wave-final-11.bin`: 131072 bytes, 11904 muestras por canal, cero paquetes
invalidos y cero huecos, flags=4. La web real pasa en 1280 y 390 px sin errores
JavaScript ni desborde horizontal. Despues de esas descargas el osciloscopio
transmitio 56 tramas / 451584 bytes en seis segundos.
Queda para pruebas fisicas: cortes, neutro y alineacion temporal con instrumental.
