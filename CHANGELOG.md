# Cambios de PowerMeter9000

## v2.0.0-preview.1 - 2026-11-20

Primera base identificada como version 2. Incluye los commits locales acumulados
desde la base anterior publicada en GitHub (`7518a95`) y el respaldo `ee5837e`.
La etiqueta preview indica desarrollo en pruebas, no una garantia de estabilidad
ni conformidad metrologica certificada.

### Firmware y almacenamiento

- Integracion de eventos de linea y estados de red con registros persistentes NAND.
- Capturas FAST y oscilografias vinculadas a eventos, con CRC y prehistoria configurable.
- Evolucion de Wave a capturas de 64 KiB con paginas de datos de 24 ms.
- Ajustes de RAM, pilas de tareas, adquisicion y empaquetado de muestras.
- Lecturas NAND con productor y buffers; espera breve de precarga para el historico.
- Metricas de arranque, CPU, memoria y transferencias; diagnosticos por modulo.
- Arranque con RTC y adquisicion temprana; pantalla inicial con progreso.
- Configuracion de WiFi/NTP y aplicacion de ajustes en ejecucion donde corresponde.
- Validacion y verificacion de configuraciones, con identificacion de claves rechazadas.

### Aplicacion web

- Carga secuencial de recursos, compresion y validacion de cache por archivo.
- Cache local de capturas e historico, con descarga progresiva en segundo plano.
- Historial cronologico con filtros, periodos calendario y preferencias persistentes.
- Estadisticas por hora, dia y semana, con detalle por entrada.
- Duraciones combinadas por union temporal: nueve minutos sin tension en tres fases
  no se presentan como veintisiete minutos de corte global.
- Conteos por fase de DIP, swell y picos disponibles en el resumen de Red.
- Correccion del orden de transiciones casi simultaneas que podia dejar estados abiertos falsos.
- Continuidad del mismo estado entre reinicios; marcas circulares para arranques
  y triangulos para eventos puntuales.
- Visor emergente compartido, secciones de captura, pantalla completa y filtros de senales.
- Minimo visual de cuatro pixeles para estados anormales breves, sin alterar su duracion.
- Historico con orden y escalas de graficas, sincronizacion y colores configurables.
- Osciloscopio y calidad de energia con FFT, controles de captura y mediciones.
- Configuracion agrupada, importacion/exportacion por secciones y visor de logs.

### Comprobaciones recientes

- 40 pruebas de historial, estadisticas y preferencias aprobadas al crear el respaldo.
- Pruebas anteriores de la linea temporal en escritorio y movil, generacion del
  bundle web y consultas reales a la placa.
- Estas comprobaciones no equivalen a una validacion integral de todas las funciones.

### Pendientes conocidos

- Investigar los reinicios `panic`: falta una traza que permita atribuir su causa.
- Evitar lecturas lentas de logs dentro del callback AsyncTCP, conservando la espera breve de precarga.
- Agregar timeout por inactividad a las descargas WebFS.
- Ampliar la recuperacion ante fallos HTTP persistentes; hoy cubre un caso de espera de cola TCP.
- Resolver todas las oscilografias asociadas a un evento, incluso si la cache RAM contiene solo una parte.
- Proxima etapa: filtros del visor de eventos y visor independiente de oscilografias relacionadas.
