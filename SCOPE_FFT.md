# FFT en vivo

- Sin clientes de Calidad, el osciloscopio conserva su captura reducida y sus ajustes guardados.
- Calidad tiene prioridad: captura 2048 muestras consecutivas a 8000 Hz (256 ms) de los canales seleccionados, en cuatro fragmentos de 512 muestras. El primer fragmento incluye pretrigger y sirve tambien al osciloscopio, que muestra sus 50 ms centrales a 5 ms/div y bloquea los ajustes de adquisicion.
- El trigger cruza cero en el canal configurado, con 256 puntos previos; si no aparece tras 250 ms se envia una ventana automatica marcada sin trigger. El analisis nunca depende de que haya cruce. Los rangos son +/-V y +/-A pico, de 0.05 a 1000, convertidos a un factor rango/2048 por cuenta signed de 12 bits. Son rangos digitales, no ganancias analogicas. NVS conserva qCfg independiente de cfg; los ajustes originales se recuperan al liberar la prioridad.
- El navegador calcula la FFT con fft.js 4.0.4 (MIT) y resta la continua.
- Usa automaticamente la frecuencia ADE como referencia, refina el periodo por repeticion de la onda y verifica su estabilidad. Sin referencia valida, parte de la estimacion espectral. Solo se intenta sincronizar entre 40 y 70 Hz; no es un enlace con el osciloscopio.
- Toma 10 ciclos a 50 Hz o 12 a 60 Hz, siguiendo la frecuencia estimada (umbral de seleccion 55 Hz), dentro de los 2048 puntos. Remuestrea con sinc Lanczos de 16 taps a 2048 puntos y aplica ventana rectangular; los bins son aproximadamente de 5 Hz. No se grafican frecuencias sobre los 4 kHz originales. Si los ciclos no entran o no hay periodicidad estable, usa Hann y no publica THD valida.
- Si no puede sincronizar, conserva Hann sobre los 256 ms (bins de 3.90625 Hz). No hay checkbox para desactivar el ajuste coherente. Es una visualizacion, no un analisis IEC certificado: todavia faltan agrupacion de bins y compensacion de respuesta del circuito/ADE.
- La amplitud por componente se expresa en V RMS o A RMS con correccion de ventana. Fuera del modo coherente, un tono entre bins reparte su energia entre varios; el pico de un bin no equivale al RMS total de la señal. El remuestreo es aproximado y no suprime ruido ni interarmonicos reales.
- Calidad de energia presenta seleccion de canales, referencia del equipo, espectro y calculos, en ese orden. El osciloscopio conserva trigger, tiempo y mediciones de ventana, sin espectro. Se reutilizan la conexion y el decodificador.
- THD-F estimada: 100 * sqrt(sum(H2^2...H40^2)) / H1, solo con captura coherente, sin clipping y fundamental mayor que cuatro pasos de cuantificacion y 0.1 V/0.001 A. No incluye ruido ni interarmonicos. El limite H40 no depende del zoom del espectro. El detalle muestra tambien H1 y porcentajes relativos a H1. El remuestreo y la cuantificacion limitan la exactitud, sobre todo en armonicos altos.
- RMS ventana AC se calcula sobre los mismos ciclos remuestreados usados para FFT, no sobre los 256 ms recibidos. En modo Hann o sin sincronismo usa toda la captura, restando continua. El estado muestra ciclos, duracion efectiva y separacion entre bins; no confundir esta separacion con exactitud ni con agrupacion IEC.
- Cambiar la revision compartida invalida el armado parcial, pero conserva el ultimo espectro completo en unidades fisicas hasta recibir otro. Cambiar de vista libera la captura pendiente y aborta su consulta ADE: una respuesta/finalizacion vieja no puede borrar la referencia ni programar polling sobre la vista nueva.
- La referencia /meter se consulta en serie cada 2 s, solo mientras alguna de las vistas esta activa y en marcha. Usa medidas del firmware ya en RAM; no lee NVS. Una referencia de mas de 6 s deja de mostrarse. Las ventanas ADE y FFT no son simultaneas. El neutro de voltaje es BL; no hay oscilografia ni FFT de ese voltaje en el hardware actual.
- Las escalas FFT son fijas: corriente primaria y voltaje secundario, de cero a cinco divisiones RMS. Cada selector se guarda en localStorage y define automaticamente el rango pico digital: unidades/division * 5 * sqrt(2) * 1.10. Por ejemplo, 1 A/div muestra hasta 5 A RMS y captura +/-7.778 A pico. El margen del 10% se calcula para una senoide: una corriente con mayor factor de cresta aun puede saturar. No se modifica la ganancia analogica ni el muestreo de 8 kHz. Las amplitudes espectrales son RMS, no picos temporales.
- Los selectores FFT ofrecen 0.01, 0.1, 1, 2, 5, 10, 20, 50 y 100 unidades/division, sin pasos intermedios ni opcion Osc. Predeterminados: 100 V/div y 1 A/div. Ya no hay un control manual de rango pico. El maximo automatico es +/-777.817 pico; el transporte admite hasta +/-1000 para no recortar ese margen, conservando 12 bits y los mismos buffers.
- El zoom manual X/Y se conserva al actualizar y puede restablecerse con el boton correspondiente. Cambiar la escala FFT restablece el zoom de ese eje y solicita el rango automatico al ESP; el zoom no cambia el rango ni solicita otra captura. Al entrar en Calidad se aplican las escalas locales una vez confirmada la configuracion. El ultimo cliente que ajusta escala o canales define los rangos compartidos.
- Si el ESP informa clipping, aparece un aviso rojo con los canales afectados: espectro y RMS pueden ser incorrectos y THD queda invalida. El aviso se retira cuando los canales visibles reciben una captura sin saturacion; no se reconstruyen los picos perdidos ni se oculta el recorte ajustando el eje.
- Los controles del espectro se agrupan en una barra: reset, ampliacion, banda de frecuencia y escalas V/A. El selector de canales queda encima de esa barra, dentro de la columna izquierda; mediciones, resumen FFT y armonicos empiezan arriba en la columna derecha. En escritorio (>1050 px), la vista ocupa la altura disponible descontando titulo y pie real, sin scroll de pagina. Las tablas tienen scroll propio; en movil se conserva el apilado y scroll normal.
- Los datos de captura y los avisos de saturacion van debajo del grafico, en letra compacta. El tooltip compartido muestra Hz con dos decimales, nombre del canal, orden Hn (o entre armonicos), amplitud RMS, porcentaje respecto de H1 y THD-F H2-H40. Las capturas saturadas no muestran THD valida.
- Calidad transmite al entrar y se detiene al salir: no tiene boton Detener ni checkbox Activa. Si el ESP informa FFT deshabilitada mientras esta vista esta activa, el navegador solicita scope_fft_on,1; el osciloscopio conserva su propio control de inicio/parada.
- El boton Ampliar FFT ocupa la pantalla dentro de la pagina, sin depender de Fullscreen API ni de HTTPS. Se sale con el mismo boton, Escape o al detener el osciloscopio. No cambia los datos ni el zoom.
- La habilitacion, rangos y canales de Calidad son compartidos entre clientes y se guardan diferido en NVS, fuera de la adquisicion. Los canales del osciloscopio guardado no se sobrescriben por la prioridad.

## Transporte

`scope_view,1` solicita temporal; `scope_view,2` o `scope_view,3` renuevan Calidad. La suscripcion es volatil y no escribe NVS. Con Calidad activa todos reciben el mismo SF v3, sin SC adicional. El osciloscopio consume solo parte 0, sin reservar la ventana completa. No se agregan buffers estaticos de muestras: se reutiliza scopeRing y scopeFlagsRing. RAM estatica total: 116128 bytes, +112 frente a la separacion anterior de vistas; incluye dos punteros de traspaso, no dos arrays nuevos.

`quality_set,<V pico>,<A pico>,<mask>` solo se admite desde un cliente de Calidad vigente. `quality_cfg,<locked>,<V pico>,<A pico>,<mask>,<revision>` informa prioridad y ajustes. `scope_get` solicita la configuracion. El saludo y las actualizaciones agrupan quality_cfg, scope_cfg y scope_fft_on con saltos de linea en un solo mensaje para no llenar la cola de cuatro al conectar. Durante la prioridad, el ESP rechaza scope y devuelve los ajustes efectivos sin modificar cfg.

Se reutiliza `/ws`. Habilitacion y confirmacion: `scope_fft_on,<0|1>`.
La mascara de canales viene de `scope_cfg`: 0 Ir, 1 Vr, 2 Is, 3 Vs, 4 It, 5 Vt, 6 In.

Paquete SF v3, 52 + 768 bytes por canal activo. Enteros little-endian:

| Offset | Bytes | Contenido |
| --- | --- | --- |
| 0 | 2 | ASCII SF |
| 2 | 1 | Version 3 |
| 3 | 1 | Tipo: 0 datos, 1 cancelacion |
| 4 | 4 | ID de captura |
| 8 | 1 | Parte 0...3 |
| 9 | 1 | Total: 4 |
| 10 | 1 | Mascara de canales |
| 11 | 1 | Mascara de clipping de este fragmento |
| 12 | 2 | 512 muestras por canal |
| 14 | 2 | Periodo: 125 us |
| 16 | 2 | Trigger 256 en parte 0, o 65535 sin trigger |
| 18 | 2 | Reservado |
| 20 | 8 | Primera muestra del fragmento, us |
| 28 | 4 | V/cuenta float32 |
| 32 | 4 | A/cuenta float32 |
| 36 | 12 | Frecuencias ADE R/S/T float32 |
| 48 | 4 | Revision de ajustes |
| 52 | Variable | Signed12 intercalado, solo canales activos en orden ascendente |

Cada tramo se junta en el mismo anillo y se empaqueta en heap al completar 64 ms, no al comenzar el tramo siguiente. meterTask entrega el mensaje terminado a una cola fija de dos punteros; el loop principal consulta las colas WebSocket y transmite, porque esas operaciones pueden bloquear un mutex durante decenas de ms. La adquisicion solo consulta credito publicado en RAM y nunca espera al socket. El credito limita a dos mensajes de datos pendientes entre la cola propia y la cola WebSocket mas ocupada. La libreria comparte el payload entre clientes, pero TCP puede necesitar memoria adicional por conexion.

Se mantiene una reserva de 24 KiB de RAM MALLOC_CAP_8BIT y se verifica el mayor bloque libre con memoryHasRoom antes de reservar el fragmento mas 1 KiB; al publicar tambien se exige ESP.getFreeHeap >=32 KiB. Si falta credito o memoria se cancela, no se espera ni se agranda la cola. El inicio de capturas esta separado al menos 500 ms; sin trigger o con red ocupada el refresco efectivo baja. Siete canales: 5428 bytes por fragmento, 43424 bytes/s si se logran dos capturas/s. El ritmo es un objetivo, no una garantia en carga maxima o con multiples clientes.

Cancelacion: 10 bytes, SF/3/tipo1/ID32/motivo8/reservado8. Motivos: 1 hueco, 2 memoria, 3 cola, 4 configuracion. Se intenta enviar cuando hay credito, nunca agregando un tercer mensaje. No pausa y retoma las muestras. La web tambien descarta por ID nuevo, metadata inconsistente o timeout de 3 s. Solo publica FFT con las cuatro partes consecutivas; puede conservar la vista temporal del primer tramo aunque la FFT se cancele. Las revisiones evitan interpretar paquetes previos con ajustes nuevos. El decoder web conserva SF v2 para pruebas anteriores; el firmware solo produce v3.

Las librerias y scripts del cargador siguen descargandose **secuencialmente**, nunca en paralelo.

## Clientes y reposo

El grafico conserva siete objetos de serie y alterna visible/showInLegend al seleccionar canales, sin reemplazar el array. Las capturas completas actualizan solo los canales recibidos y conservan los otros para volver a mostrarlos. Un cambio de mascara o rango invalida el armado parcial por revision, no el ultimo espectro completo: sus valores ya estan escalados a unidades fisicas. Al cambiar el rango se indica que la captura visible es anterior hasta recibir la nueva. Un canal nunca recibido debe esperar su primera captura. Las tablas siguen distinguiendo capturas vencidas (6 s) de lecturas vigentes.

Mediciones del equipo, Resumen FFT y Armonicos son paneles desplegables abiertos por defecto, sin persistencia de ese estado. En Calidad cada consulta /meter se sigue secuencialmente por /meter?thd; si esta ultima falla se conserva la referencia RMS y no se inventa una THD. El equipo muestra THD V/I del ADE en R/S/T, no del neutro. El resumen FFT solo contiene calculos web (RMS AC, H1, H2-H40, THD-F y frecuencia), sin duplicar lecturas del equipo. La RMS fundamental del ADE todavia no se adquiere.

Calidad presenta el espectro a la izquierda y las mediciones del equipo y armonicos a la derecha; en movil los bloques se apilan. Una sola tabla H1-H40 agrupa RMS y porcentaje de H1 por cada canal seleccionado. La columna Hz toma como referencia la primera tension valida, o la primera corriente valida si no hay tension; las amplitudes mantienen el calculo propio de cada canal. Los dos selectores de escala sobre el grafico controlan tambien el rango automatico, sin controles pico adicionales. Las tablas anchas se desplazan dentro de su panel, no ensanchan la pagina.

La vista del osciloscopio conecta automaticamente al entrar. Detener, navegar a otra vista o cerrar la pagina cierra su WebSocket. Sin clientes conectados, `scopeStreamProcess` retorna antes de escalar, disparar, empaquetar o enviar, y libera el mensaje FFT pendiente. La adquisicion para medicion y eventos NAND sigue funcionando.

Cada navegador envia scope_alive cada cinco segundos. Calidad agrega scope_view,2 cada segundo: su prioridad vence tras 5 s sin ese aviso; el loop principal detecta el vencimiento y devuelve los ajustes originales sin escribir NVS. Cerrar el ultimo cliente libera inmediatamente. El cierre general de conexiones abandonadas sigue siendo a 30 s, independiente por navegador. No hay sesiones ni buffers de muestras por cliente.
