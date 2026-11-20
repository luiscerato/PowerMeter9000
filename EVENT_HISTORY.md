# Historial de eventos

## Respaldo de desarrollo 2026-11-20

- Historial reconstruido por estados, filtros de entradas y tipos, y estadisticas por periodo.
- Tiempos combinados calculados por union temporal, sin triplicar cortes simultaneos de R/S/T.
- Conteos de DIP, swell y picos por fase disponibles en el resumen de Red.
- Orden de transiciones casi simultaneas corregido para no dejar sobrecorrientes abiertas falsas.
- Preferencias de periodo y filtros persistidas por navegador; Hoy conserva su significado relativo.
- Estados iguales unidos entre reinicios, con marcas circulares y visor emergente compartido.
- Ancho minimo visual para cortes breves y divisiones acotadas en graficas de duracion.
- Cache local de capturas/historico, precarga progresiva y herramientas de medicion de transferencias NAND.
- Osciloscopio y calidad de energia con FFT, controles y mediciones; mejoras de RAM y adquisicion.
- Configuracion de red y aplicacion, ajustes en caliente, arranque con RTC y pantalla de progreso.
- Logs por modulo y diagnosticos de CPU, memoria, arranque y conectividad.
- Pendiente: sacar lecturas lentas de logs del callback TCP conservando espera breve de precarga.
- Pendiente: timeout de descargas WebFS y recuperacion ante fallos HTTP persistentes.
- Pendiente: resolver todas las oscilografias de cada evento, incluso fuera de la cache RAM.
- Proxima etapa: filtros del visor de eventos y consulta independiente de oscilografias relacionadas.

Este respaldo no da por resueltos los panic ni todos los bloqueos de comunicacion.

Vista independiente `#eventHistory`, sin modificar el capturador ni los formatos NAND.
Periodo inicial: semana actual de lunes a domingo. La reconstruccion se ejecuta en el navegador.

## Controles y filtros

- Primera fila: periodo, desde y hasta. Las fechas muestran dia/mes y hora de forma compacta; al tocarlas se abre el control nativo con fecha/hora completas, incluidos los segundos guardados.
- Segunda fila: periodo anterior/actual/siguiente, Todos / Ninguno, entrada y los iconos de actualizar/detener. Las dos filas caben sin desplazamiento horizontal en movil.
- Debajo, Eventos y Estados son accesos rapidos exclusivos: muestran solo ese grupo. Cada tipo individual alterna su visibilidad sin cambiar los demas: DIP, swell, pico I, corte, baja V, alta V / neutro, alta I y red anormal. Todos activa todos los tipos; Ninguno los oculta. Los botones indican que esta seleccionado.
- Los filtros se aplican a linea, lista y estadisticas sin descargas nuevas. Cuando se muestra algun estado se conserva el contexto normal registrado; un estado oculto no se presenta como normal ni como un corte. El gris puede indicar datos ausentes o estados ocultos, segun la leyenda.
- La linea temporal tiene una fila por red/fase/neutro: los puntuales de tension y corriente comparten su sujeto. Las condiciones sostenidas de corriente usan una banda inferior, sin sustituir el estado de tension. Los registros internos conservan su canal original.
- Estadisticas es la primera pestaña, abierta por defecto; Lista mantiene los tramos y enlaces originales. Agrupacion y entrada de estadisticas comparten una fila compacta. En modo Eventos solo se muestran las cantidades de puntuales, sin grafico de duraciones vacio.
- Seleccionar una fase arriba lleva la entrada de estadisticas a esa fase; luego se puede cambiar su desglose independientemente. Si se eligen puntuales con Red seleccionada en estadisticas, se propone la primera fase con puntuales (R si aun no llegaron datos).

## Reconstruccion

- Consulta el indice por lotes de hasta 64 IDs y descarga las capturas de una
  en una, empezando por la mas reciente. Cada descarga actualiza linea y lista.
- Reconstruye con cabeceras y transiciones resumidas, sin retener muestras
  FAST decodificadas en la lista. Los binarios se reutilizan en la cache comun;
  las oscilografias se consultan al abrir la vista previa del evento.
- Separa red, R/S/T, tension neutro-tierra y estados/puntuales de corriente.
- Une inicio y retorno usando las fechas absolutas de los cambios de estado.
  Tambien puede reconstruir el inicio desde un retorno cuyo inicio no se descargo.
- Conserva los puntuales como intervalos independientes. No suma red y fases
  como si fueran cortes distintos de un mismo total ni reclasifica eventos.
- Deduplica registros superpuestos y conserva los IDs fuente para abrirlos
  en el visor Eventos existente.
- Se buscan capturas anteriores hasta tener contexto de estado para red y
  fases/neutro o agotar el indice. Un DIP previo no sustituye ese contexto.
  El fondo gris indica que no se dispone de un estado para esa entrada.
- El ultimo estado se mantiene hasta el siguiente cambio, incluso entre
  reinicios. Un arranque con el mismo estado no inicia otro corte. Los CRC
  invalidos y registros contradictorios siguen marcando tramos parciales.
  Los estados abiertos se contabilizan hasta el final del periodo o hasta ahora.
- Capturas sinteticas o sin hora fiable no se asignan al calendario.
- Se puede detener la descarga sin perder los resultados parciales. Cambiar de
  vista cancela solicitudes pendientes. Volver a Historial o pulsar Analizar
  consulta novedades y comienza otra pasada reutilizando los binarios en cache.

Pruebas: `node --test test/event_history.test.cjs` cubre union entre capturas,
inicio inferido por duracion, mutacion bajo voltaje/corte, separacion de entradas,
reinicios, discontinuidades, puntuales duplicados y calidad de los datos.
`node test/event_history_web.test.cjs` comprueba carga progresiva/secuencial,
filtros, cancelacion, enlaces al visor y ausencia de desborde en 1280 y 390 px.

Verificacion en placa: 34 capturas descargadas sin errores, 75 registros y
15 tramos cerrados enlazados entre capturas distintas. Vista real comprobada
en 1280 y 390 px, sin muestras crudas retenidas ni desborde horizontal.
Tambien pasaron las regresiones de navegacion y del visor de eventos original.
La misma prueba real paso por Cloudflare en ambos anchos. WebFS publicado;
firmware y eventos almacenados sin modificaciones.

## Estadisticas

La primera pestaña de Historial ofrece graficos y tablas: resumen por red/fase/neutro y
desglose por intervalos para la entrada seleccionada. Tension y corriente de una
misma fase se presentan en la misma fila, en columnas diferentes.

- Automatico: hasta dos dias agrupa por hora; hasta catorce, por dia; periodos
  mayores, por semana. Tambien se puede elegir hora, dia o semana manualmente.
- Los dias usan el calendario local y las semanas comienzan el lunes. Las
  duraciones respetan dias de 23/25 horas por cambios de horario.
- Estados: tiempo verificado e inicios dentro del periodo. Los cortes largos
  reparten duracion entre los intervalos atravesados, sin repetir el inicio.
- Puntuales: cantidad de dips, swells y picos de corriente, asignados a su inicio.
- Las duraciones se recortan al periodo y se unen solapamientos de una misma
  categoria/entrada. No se suma red con sus fases como un total artificial.
- Tramos parciales o sin cierre no aportan duracion; sus inicios conocidos se
  cuentan y se avisa de la duracion omitida. Sin registros no equivale a red normal.
- Se incluyen intervalos vacios. Si el rango genera mas de 1000 filas se pide
  una agrupacion mayor, conservando el resumen general.
- Se actualizan al recibir capturas y al cambiar los selectores, sin nuevas
  peticiones al medidor. El filtro propio de Entrada elige el detalle temporal;
  el resumen conserva todas las entradas, independiente del filtro de la linea.

Pruebas: `node --test test/event_statistics.test.cjs` comprueba reparto horario,
diario y semanal, bordes exclusivos, recorte, solapamientos, tipos de evento,
inicios conocidos sin cierre, semanas locales, cambios horarios y limite de filas.
Las pruebas web de Historial comprueban tambien los selectores y sus tablas.

Verificacion de estadisticas en placa: 40 capturas leidas sin errores en
escritorio y movil usando una misma sesion con cache. La suma diaria del
tiempo de corte coincide con el resumen. Modulo publicado en WebFS; sin
cambios de firmware ni formato. Archivo nuevo accesible por Cloudflare.

Incidencia de pruebas pendiente de diagnosticar: al combinar una carga local
con una prueba adicional por Cloudflare, HTTP dejo de responder. Por serie
continuaban heartbeat y Wi-Fi conectado, con heap libre 41.488 bytes, DRAM
libre 23.384 y bloque contiguo maximo 4.852, junto a mensajes de congestion
AsyncTCP. Se recupero con un reinicio por COM4. La repeticion con un cliente
y cache compartida completo ambos anchos correctamente. No se atribuye una
causa definitiva a esa interrupcion ni se considera resuelto el caso de carga
simultanea; no se hicieron cambios del servidor para ocultar esa incidencia.

## Navegacion y vistas previas

- Barra horizontal Desde/Hasta, sin partir las fechas. En pantallas estrechas
  se desplaza horizontalmente para conservar las fechas completas.
- Atajos hoy, ayer, esta semana, semana pasada, este mes, mes pasado y este anio.
  Los campos muestran limites inclusivos 00:00:00..23:59:59; internamente el
  final se convierte a exclusivo para repartir duraciones sin duplicar bordes.
- Flechas anterior/siguiente por dia, semana, mes o anio calendario; boton de
  periodo actual segun esa unidad. En un rango manual se desplaza su longitud.
- Eventos y Estadisticas tienen pestañas separadas. Estadisticas permite alternar
  columnas agrupadas y tablas, conservando entrada y granularidad. Los graficos
  separan cantidad de inicios/puntuales y duracion verificada en minutos.
- Inicio/final de la lista y segmentos de la linea ofrecen tooltip informativo
  y abren una ventana con FAST y Wave, sin cambiar el visor de Eventos.
- La fuente del inicio o retorno se elige por cercania de sus registros a ese
  borde. Se puede cambiar captura y Wave dentro de la ventana. Si el borde no
  esta en el tramo FAST guardado, se muestra explicitamente esa limitacion.
- Se reutilizan los decodificadores con CRC existentes; no se cambian formatos.
  Wave no incluye tension neutro-tierra: en ese caso muestra R/S/T como contexto.
- Historial y vista previa usan una cola de consultas compartida, incluida la
  lectura completa del cuerpo, sin lanzar descargas simultaneas desde esta vista.
  Cerrar cancela consultas y destruye graficas, liberando muestras en el navegador.
- Ver detalles abre la captura seleccionada en el visor original.

Pruebas adicionales: `test/event_history_period.test.cjs` cubre limites locales,
semanas, febrero bisiesto, desplazamientos, seleccion de fuente y cola/cancelacion.
La prueba web ejercita pestañas, graficos, tooltip, inicio/retorno, una Wave v2
de 64 KiB, cierre y enlace al visor en escritorio y movil.

Publicacion verificada: 40 capturas reales, sin errores de descarga ni JavaScript,
en 1280 y 390 px con una sesion y cache compartida. Se abrieron graficos y una
vista previa real FAST/Wave v2 desde el retorno, sin abandonar Historial; las
pruebas simuladas verificaron ademas el inicio y el enlace Ver detalles.
Se conservaron firmware, formatos y datos NAND. Modulo nuevo accesible por
Cloudflare sin otra carga completa simultanea de prueba.

## Cache persistente de capturas

- `capture_cache.js` comparte binarios FAST/Wave entre Historial, ventanas de
  vista previa y visor de Eventos. IndexedDB conserva las capturas al recargar
  o cerrar la pagina; no almacena indefinidamente arrays de muestras decodificadas.
- Claves: identidad del chip, generacion de particion, formato, ID y tiempo del
  evento; Wave agrega su ID y tiempos del descriptor. La posicion fisica no es
  identidad, porque los bloques se reutilizan. No cambia el formato de NAND.
- `/capture-cache` entrega la identidad y dos generaciones uint64 hexadecimales
  persistidas en NVS. Se renuevan ANTES de borrar cada particion (ambas en formato
  global). Si no se puede persistir la marca, el borrado no comienza.
- Los binarios incluyen `X-Capture-Identity`; solo se guardan si coincide con la
  consulta inicial. El indice Wave incluye ambas identidades y el timestamp del
  propietario, para no enlazar una oscilografia nueva con un FAST viejo.
- Las consultas pequenas de identidad/indices siguen siendo frescas. El indice
  Wave no se persiste: una oscilografia que termina despues del FAST aparece al
  volver a consultar. Capturas parciales o con CRC invalido no se reutilizan.
- Limite LRU: 32 MiB y 3000 entradas. Metadata separada evita leer todos los
  binarios para decidir cual eliminar. Si IndexedDB no esta disponible o falla,
  queda un respaldo de hasta 1 MiB en RAM del navegador; el visor sigue funcionando.
- Cada origen web conserva su propia base: entrar por IP y por dominio genera
  caches separadas. El navegador puede liberar estos datos bajo presion de espacio.
- El ESP agrega 104 bytes estaticos respecto de la compilacion anterior, sin
  duplicar capturas en su RAM. No se formatearon particiones durante las pruebas.

`test/capture_cache_web.test.cjs` verifica IndexedDB real, recarga, aislamiento por
dispositivo/formato, corrupcion, parciales, cancelacion, limites LRU y respaldo RAM.
Las pruebas de Historial y visor original cubren su integracion en ambas anchuras.

Verificacion publicada: firmware por COM4 y WebFS de 30 archivos/323584 bytes.
Se analizaron 41 capturas reales sin errores; al abrir nuevamente desde el mismo
contexto del navegador solo se descargo el FAST #25, marcado parcial. Los otros
40 FAST y la Wave de la vista previa se reutilizaron; reabrir la ventana tampoco
genero descargas binarias. La identidad responde correctamente por IP y Cloudflare.

## Vista conjunta del evento

- Al abrir un tramo se cargan TODOS sus FAST y los indices Wave vinculados,
  secuencialmente y mediante la misma cache compartida. Ya no hay que seleccionar
  cada oscilografia para ver el conjunto. El selector FAST del pie solo elige
  que captura abrir con Ver detalles; no cambia las curvas reunidas.
- Los dos graficos (RMS rapido y oscilografia) comparten un origen temporal:
  inicio del tramo del historial. Cada Wave conserva su referencia FAST decodificada.
  Se ordenan y deduplican muestras solapadas; una Wave enlazada a varios FAST
  se incorpora una sola vez por ID/version/tiempos del descriptor.
- Los huecos se conservan como null: no se dibujan puentes ni se interpola entre
  ventanas separadas. Se corta la continuidad cuando pasan mas de 30 ms entre
  muestras FAST o mas de 250 us entre muestras Wave.
- Vista completa, Inicio y Retorno funcionan en ambas graficas. Zoom/arrastre
  se sincronizan en ambos sentidos mediante Vincular vistas. Mover habilita el
  arrastre tambien desde controles tactiles. Las marcas muestran inicio, retorno
  y registros de todas las fases/magnitudes presentes en las capturas cargadas;
  sus botones acercan la vista al registro (50 ms de margen).
- La carga es progresiva y cancelable al cerrar. Los errores de un archivo no
  ocultan los demas: se listan como lecturas pendientes y el resto sigue visible.
  No se cambiaron firmware, formato ni registros almacenados en NAND.

`test/event_history_merge.test.cjs` cubre orden temporal, huecos, solapamientos,
125 us, NaN y deduplicacion de marcas. La prueba web reune dos FAST y tres Wave,
incluyendo una Wave referenciada dos veces, y verifica zoom en ambos sentidos,
arrastre, rango completo, cancelacion y escritorio/movil.

Publicado y verificado con 42 capturas reales en 1280 y 390 px: el ultimo tramo
reune FAST #42 y Wave #63/#64/#65 con ocho marcas, sin lecturas pendientes.
Reabrir no descargo binarios; la segunda vista reutilizo todo salvo el FAST #25
parcial. WebFS: 30 archivos/325632 bytes. Heap libre al terminar: 63892 bytes.

## Navegacion por tramos y pantalla completa

- Las ventanas FAST/Wave cuya separacion sea de hasta 15000 ms se agrupan.
  Pausas mayores generan botones Inicio, Tramo 2, ..., Fin, sin selector de
  graficas. Si hay un solo grupo se muestra Tramo 1. Un estado visible indica
  que grupo se esta viendo y cuantos hay disponibles.
- Al abrir se elige el grupo mas cercano al borde pulsado (inicio/final),
  manteniendo 50 ms a los lados. No se aplasta ni recorta el eje de tiempo.
  Ver todo conserva el rango total, con sus huecos y sus tiempos reales.
- El selector de captura del pie sigue destinado solo a Ver detalles.
- El boton de ampliacion junto al cierre ocupa toda el area de la ventana,
  con graficas mas altas. Conserva muestras, cache y zoom; no descarga nada.
  El mismo boton restaura el tamaño y Escape sale de la ampliacion sin cerrar
  el evento. Cerrar restaura el tamaño normal para la proxima apertura.
- Pruebas: limite exacto de 15 s, pausa de cinco minutos, solapamientos,
  botones intermedios, seleccion de Fin y pantalla completa a 1280/390 px.

Publicado WebFS de 327680 bytes y verificado con las 42 capturas reales en ambas
anchuras, sin errores JS ni desbordes horizontales. Ampliar/restaurar conserva
la ventana abierta y las tres Wave #63/#64/#65 se reutilizan al reabrir.

## Filtros de senales del visor

- Barra compacta con accesos Fases R/S/T/N: REEMPLAZAN la seleccion por el
  voltaje y corriente de esa fase. Se elimino RST. Repetir un acceso no apaga.
- V e I son tambien accesos exclusivos: muestran solo los cuatro voltajes o
  las cuatro corrientes. Los R/S/T/N dentro de cada magnitud alternan una unica
  serie, conservando las demas. Un acceso solo se marca activo si la seleccion
  coincide exactamente; coincidencias parciales usan `aria-pressed=mixed`.
- La seleccion es comun a FAST y Wave. Voltajes y corrientes simultaneos usan
  ejes V/A independientes; al dejar solo corriente se usa el eje principal A.
  N significa voltaje neutro-tierra/corriente de neutro. Wave no contiene voltaje
  N, por lo que no inventa esa curva y muestra la limitacion al seleccionarla.
- Las leyendas tambien alternan series y actualizan los botones. Los cambios
  solo ajustan visibilidad/ejes: no descargan datos ni reconstruyen las muestras,
  conservan zoom, tramos, pantalla completa y vistas enlazadas o independientes.
- Cada apertura empieza con la magnitud/sujeto del evento, como antes. La barra
  permite mostrar todo el contexto de otras fases disponible en esa captura.
- Criterio reutilizable documentado en `WEB_UI_CONVENTIONS.md`: accesos rapidos
  reemplazan; controles individuales y leyendas alternan sin afectar otras lineas.
- Pruebas puras: `test/event_history_signals.test.cjs`. Prueba web en 1280/390 px:
  R+S, ocultar corriente S, grupos mixtos/completos/vacios, N, RST, leyendas,
  unidades, conservacion del zoom y ausencia de solicitudes adicionales.

Publicado y probado con las 42 capturas reales a 1280/390 px. Seleccion R+S y
ocultacion de corriente S verificadas en ambos graficos, sin nuevas descargas.
Leyendas ajustadas para dejar libre la botonera CanvasJS en movil. Sin cambios
de firmware/NAND; heap libre del ESP tras las pruebas: 63848 bytes.

Barra de senales compactada: una sola fila sin saltos, grupos abreviados V/I
con nombres accesibles completos y desplazamiento horizontal cuando no cabe.
No achica la pagina ni genera desborde horizontal fuera de la barra. Prueba
web en 1280/390 px verifica una fila, altura de hasta 40 px y todos los controles.

## Refresco del indice al volver (22/10)

Se encontro que `loaded` impedia reanalizar al volver a Historial, conservando
una lista vieja aunque hubiera nuevos registros. Ahora cada entrada consulta
el indice y reutiliza la cache binaria, sin polling de fondo ni cargas duplicadas.
Tambien reanuda si la carga anterior fue cancelada al cambiar rapidamente de vista.
Una pagina que permanece abierta sigue pudiendo actualizarse con Analizar.

Verificacion real con `test/event_history_dips_live.test.cjs`: los FAST #43/#44/#45
aparecen como nueve filas DIP (R/S/T). Abren Wave #69/#70/#71 respectivamente,
con 5952 muestras y cero paquetes invalidos cada una. Salir y volver consulta
el indice sin volver a descargar esos FAST. No se modificaron datos ni firmware.

## Eventos puntuales destacados en la linea temporal

- Estados normales/anormales permanecen como franjas dentro de la pista.
- Se reemplazaron las etiquetas grandes/apiladas por triangulos de 10x7 px,
  sobre la pista en una unica franja de 14 px, sin numeros visibles.
- DIP usa el color de baja tension, swell el de alta y sobrecorriente turquesa;
  la leyenda muestra triangulos de los mismos colores. Los IDs quedan en tooltip
  y detalle, para no ocupar espacio en periodos largos.
- Marcas separadas por menos de 18 px comparten triangulo. Al tocarlo se abre
  una lista de los eventos originales y se elige cual ver; no se pierde acceso
  por superposicion. Si hay varios tipos se muestran sus colores en la marca.
- Una marca individual abre directamente la vista conjunta FAST/Wave. El ancho
  se recalcula con ResizeObserver sin consultas al ESP. No cambia el registro
  ni la estadistica: la agrupacion solo afecta al dibujo de la linea temporal.
- `event_history_timeline.test` comprueba posiciones, agrupaciones, extremos y
  capturas compartidas. `event_history_timeline_web` prueba PC/movil, resize,
  altura compacta y apertura de un evento entre varias marcas cercanas.

Publicacion WebFS: 30 archivos/331776 bytes, sin cambios de firmware o NAND.

Verificacion de triangulos: prueba simulada PC/movil con agrupacion y menu,
y prueba real de DIP #43/#44/#45 con Wave #69/#70/#71 correctas. En una primera
pasada real el analisis al volver termino con cero DIP; no se registro entonces
el estado detallado de esa recarga. El indice seguia con 45 capturas y una
repeticion completa paso sin cambios adicionales del cliente. Se agrego contexto
de diagnostico al test; no se determino la causa de aquel fallo intermitente.

### Continuidad de normalidad

La reconstruccion ahora conserva tambien el estado normal posterior a una
transicion (antes solo dejaba abiertos estados anormales). Sin una transicion
posterior se muestra como tramo abierto, no como intervalo cerrado confirmado.
Esto evita huecos grises artificiales despues de una recuperacion. Desde la
revision del 28/10 un reinicio no corta el ultimo estado registrado, que se
mantiene hasta el siguiente cambio. Un DIP aislado no demuestra que
toda su entrada estuvo normal durante el resto del periodo. No se rellena en
verde una entrada sin observaciones. Pruebas: 12 casos de reconstruccion,
13 de estadisticas y navegacion/cronologia PC-movil con datos simulados.

### Lectores y consulta Wave en segundo plano

FAST, Wave y la descarga NAND de prueba usan generaciones de sesion en sus
callbacks y cancelan al desconectar. Una respuesta anterior no puede consumir
ni cancelar la siguiente. El productor conserva sus buffers hasta que NAND
confirma el ultimo acceso aceptado, incluso si HTTP ya se desconecto.

La ruta `/wave-events` ahora requiere `eventId` y `unixUs` (timestamp FAST
en microsegundos). Evita volver a escanear FAST solo para averiguar su fecha.
La busqueda Wave se ejecuta por turnos desde su guardador, con dos consultas
acotadas de ocho resultados y sin otra tarea o buffer de paginas. HTTP responde
202 pending, 503 busy/unavailable, 500 ante fallo de lectura, o 200 con un
resultado terminado, que puede ser genuinamente vacio. No interpreta ocupado
como vacio. El cliente reintenta pending/busy con plazo y cancelacion.

Las consultas no entregadas caducan a los 10 s de completarse; las ya leidas
pueden reutilizarse para otro cliente inmediatamente y se refrescan tras 2 s.
Un formateo invalida las consultas. Es necesario publicar firmware y web juntos
por los parametros y estados nuevos de esta ruta. Los formatos NAND no cambian.

Pruebas: `nand_reader_generation`, `wave_index_policy`, `wave_index_retry`,
`event_history_web` y respaldo de configuracion con la seccion Wave incluida.
Pendiente separado: guardado conjunto de meter/Wave (hoy tres peticiones y
posible aplicacion parcial), y mover el resto de catalogos FAST a consultas
sincronicamente acotadas o tareas de lectura. No se afirma resolver esos puntos
con el cambio del lector Wave.

El guardado conjunto y los endpoints agrupados se implementaron posteriormente
en `EVENTS_API.md`: un POST URL-encoded y un registro NVS para todo el conjunto.
La optimizacion del catalogo FAST sigue siendo un trabajo independiente.

## Rendimiento web (2026-10-22)

El empaquetador WebFS genera `app.js` conservando el orden de los scripts
propios. Las librerias mantienen su cache independiente; el bundle se revalida
por ETag y solo se transfiere si cambia. Los fuentes individuales siguen
disponibles para pruebas y diagnostico. No agrega buffers ni tareas al ESP.

El historial conserva la descarga secuencial, validacion CRC y lectura de todas
las capturas, pero limita los redibujados de avance a uno cada 150 ms y siempre
actualiza al finalizar, cancelar o fallar. No cambia el formato ni la cache.

Medicion local en Edge de escritorio con 54 capturas: arranque sin cache de
11,9 a 5,7 s; con cache, 0,32 s. Refresco con binarios cacheados de 5,33 a
1,01 s, y de 56 a 4 redibujados (3,78 s de render a 0,20 s). Son mediciones,
no limites garantizados; una captura parcial antigua (#25) se vuelve a consultar
deliberadamente y esas pruebas incluyen su descarga. La descarga inicial de
todos los binarios sigue dependiendo de la NAND y la red.

Pruebas: `web_bundle`, `web_asset_cache`, `event_history_render`,
`event_history_web`, `capture_cache_web` y `web_performance_live`.

## Duracion en estadisticas (2026-10-28)

Los acumuladores conservan milisegundos. La grafica usa segundos numericos,
con eje y tooltips en ms, segundos, minutos/segundos u horas/minutos/segundos;
no presenta minutos decimales. Por ejemplo, el corte #87 del 26/10 a las
09:17:43 (Buenos Aires), de 2010 ms, se muestra como 2,01 s.

Revision secuencial de las 114 capturas disponibles: el 23/10 hay observaciones
sin tension desde #55 (06:29:04) hasta #61 (retorno a las 18:27:35), pero las
capturas #56 a #60 indican nuevos arranques. La politica inicial excluia esas
horas; se corrigio para contabilizar el ultimo estado hasta el siguiente cambio.
El corte de red #55 a #61 suma 43111195,9 ms (11 h 58 min 31 s), un solo inicio.
La duracion de la cabecera solo se usa si falta la transicion inicial; no recorta
un intervalo conocido al tiempo desde el ultimo arranque. Los limites del periodo
recortan la suma, no el origen del evento. Un estado observado al arrancar sin
inicio conocido suma desde esa observacion, sin contar un inicio ficticio.
Los datos incompletos o contradictorios siguen separados, al igual que las horas
no fiables. No cambia el firmware ni el almacenamiento.
Binarios y analisis: logs/event-statistics-2026-10-28T12-45-44-070Z/.
