# Tiempos de las capturas

- FAST guarda el instante monotono de lectura del ADE, en ticks de 100 us.
  A 50 Hz la actualizacion esperada es cada 10 ms, pero la atencion de la tarea
  agrega jitter. No es una marca hardware del instante RMSONERDY.
- Cuando RMSONERDY y PAGE_FULL estan pendientes, se lee FAST antes del SPI y
  empaquetado Wave. Wave sigue antes de las lecturas lentas de RMS y energia.
  Esto reduce el jitter evitable, no garantiza una cadencia exacta de 10 ms.
- La decodificacion conserva los deltas guardados, incluido el overflow del
  contador de 16 bits. Los datos originales y las marcas de eventos no cambian.
- Solo al graficar, FAST usa posiciones nominales de 10 ms a 50 Hz o 8.333333 ms
  para contexto de 60 Hz. Se toma una referencia por transicion, incluida la raiz;
  los eventos puntuales aportan tambien su final. La lectura mas cercana identifica
  la muestra de sincronismo, con tolerancia de 1.5 periodos. Referencias sin muestras
  cercanas no se usan. No se cuenta toda la captura desde su primera muestra.
- Cada lectura se asigna a la posicion nominal mas cercana respecto de esa referencia.
  Su timestamp solo identifica esa posicion, no determina la separacion dibujada.
  Si dos lecturas ocupan la misma posicion se dibuja la mas cercana, sin promediar
  canales ni modificar los originales. Las posiciones faltantes no se interpolan.
  Los cambios de referencia y huecos se separan sin retroceder el eje temporal.
- Wave conserva su referencia inicial y pasos visuales de 125 us. No se modifica
  el sincronismo de 8 kHz. Las pequenas correcciones de reloj entre paginas no
  deforman el paso visual; los separadores y huecos amplios conservan sus cortes.
  La grilla visual no permite identificar con certeza todas las muestras perdidas:
  los timestamps originales siguen siendo marcas de lectura, no contadores hardware.
- Wave contiene muestras a 8 kHz: 125 us entre muestras. Su reloj reconstruye
  mitades ADE de 16 ms y las empaqueta en paginas de 24 ms. La referencia absoluta
  es estimada por software; no equivale a sincronizacion hardware perfecta con FAST.
- Las capturas ya almacenadas conservan sus fechas y no se corrigen retroactivamente.

## Tooltips web

Las graficas temporales comparten `chart_tooltips.js`: muestran tiempo relativo en ms o fecha/hora segun la vista, valores de todas las series visibles y unidades explicitas. Las capturas FAST indican RMS; las Wave y el osciloscopio muestran valores instantaneos. En el visor de eventos, las marcas a hasta 15 ms del punto se indican como marcas cercanas (hasta dos), no como un estado interpolado. El historial emergente y Wave agregan la hora de referencia cuando esta disponible. No se descargan datos al mover el tooltip.

La presentacion usa 12 px en escritorio y 8.5 px en movil (15% menos que la primera version movil); mas de cuatro señales se distribuyen en dos columnas en movil con nombres abreviados. FFT conserva Hn, amplitud RMS, porcentaje de H1, THD-F y avisos de saturacion. No se incluyen series ocultas ni valores nulos como cero. El historico distingue estados Si/No y unidades de energia Wh/varh/VAh. Estas etiquetas y estilos no modifican las muestras ni sus tiempos.

## Casos comprobados

Captura FAST #70, 2026-10-24 11:37:01, DIP de 60 ms: los datos descargados
contienen 29.632, 44.132 y 49.732 ms. Los intervalos 14.5 y 5.6 ms ya estan
en NAND; no los introduce CanvasJS ni la union de capturas del historial.

Su Wave #117 contiene 5952 muestras, sin CRC invalidos ni bandera de perdida.
Entre paginas hay correcciones de hasta 8 us del reloj reconstruido; dentro de
cada pagina las muestras conservan exactamente 125 us de separacion.

La prueba `node test/event_time_axis.test.cjs` verifica que la decodificacion
conserve esos deltas, incluso cruzando paginas y el overflow temporal, y que
la preparacion visual use las cadencias nominales sin cambiar los originales.

En #84, contar FAST desde el primer punto producia 4.88 ms de desplazamiento
en la raiz y unos 41 ms en la recuperacion a 2483 ms. La preparacion por referencias
coloca ambas muestras de sincronismo en sus marcas respectivas (0 y 2483 ms),
sin desplazar Wave ni alterar las duraciones registradas.

## Huecos FAST revisados el 2026-10-28

Se descargaron en secuencia las capturas 85..90 y 103..113 seleccionadas, con
CRC completos y sin paginas invalidas. Binarios y resumen anterior a la
correccion: `logs/fast-gaps-2026-10-28T12-00-51-740Z/`.

- #109 contiene 234 muestras con los ocho canales validos, sin saltos mayores
  a 18 ms. El separador de 85 ms entre puntos de 80 y 90 ms era sintetico:
  `gridFastDisplaySamples` cortaba cada vez que cambiaba el ancla nominal.
  Ocurria tambien en #88, #89, #90, #104, #106..112. Cambiar de ancla ya no
  agrega separadores; una ausencia completa o salto real mayor a tres periodos
  sigue separando tramos. La decodificacion, cache y tiempos originales no cambian.
- #103, #105, #111 y #113 si contienen saltos guardados de 379.5, 309.8, 329.9
  y 329.6 ms respectivamente, despues de la marca de arranque. Al recuperar el
  estado temprano tras montar NAND, `startStartupCapture` reconstruia el tramo
  posterior solo hasta el instante antiguo del estado, omitiendo datos que ya
  estaban en el anillo. Ahora `armCapture` recupera hasta `millis()` sin cambiar
  el instante original del candidato ni su descriptor. No agrega buffers ni RAM.
- No hay invalidacion compartida ADE/BL: en #113 falta un valor VN y los siete
  canales ADE estan completos. La finalizacion forzada del turno BL conserva
  ADE y codifica solamente VN como ausente. Los saltos cortos reales de tiempos
  de adquisicion (por ejemplo 34 ms en #103) no se rellenan ni se atribuyen a
  BL o al almacenamiento sin evidencia adicional.

Firmware y web publicados. Captura del reinicio OTA #114: 495 muestras, CRC
correctos, 25 previas, ninguna ausencia ADE y una ausencia VN. Ya no aparece el
salto inicial de 330 ms ni separadores globales. Datos nuevos en
`logs/fast-gaps-2026-10-28T12-07-24-824Z/`. RAM estatica sigue en 116368 bytes.
Los huecos reales antiguos no se recuperan retroactivamente.

Chrome con la web publicada: #109 dibuja 234 puntos sin ausencias R/S/T/VN;
#114 conserva el dato ausente solo en VN y no corta R/S/T, sin errores JS.
Pruebas: `event_time_axis`, `incident_policy` (arranque 600 ms recuperado hasta
930 ms sin mover su marca), `storage_startup`, `bl0942_sync`, `fast_rms_format`,
`boot_record`, `event_history_merge` y `web_bundle`.
`tools/analyze_fast_gaps.cjs [ids...]` permite repetir el diagnostico sin escribir
en la placa; guarda binarios y compara saltos originales con separadores visuales.
