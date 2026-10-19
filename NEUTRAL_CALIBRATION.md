# Calibracion de tension auxiliar (BL0942)

El canal IP/IN mide tension neutro-tierra. VP no se usa. Para calibrar,
el divisor del BL0942 debe recibir la misma senal que la fase elegida del ADE.
Este comando no es para ejecutarlo con la entrada neutro-tierra en servicio normal.

## Comando

`POST /calibration/neutral` con cuerpo `application/x-www-form-urlencoded`:

```text
phase=r
```

Admite `r`, `s`, `t`, sin distinguir mayusculas; por defecto usa R.
Devuelve HTTP 202 y `{"status":"queued"}`. Otra captura en curso devuelve 409;
una fase incorrecta devuelve 400. No usa parametros de URL ni parsing de JSON.

`GET /calibration/neutral` consulta estado, error, fase, muestras, referencia
y factores RMS/rapido. Estados: idle, queued, sampling, saving, done, error.
Solo done confirma guardado y aplicacion. No hay que reiniciar.

## Medicion y calculo

- La tarea de medicion recoge durante un segundo solo respuestas nuevas del BL0942.
- Promedia I_RMS (400 ms), I_FAST_RMS (10 ms) y la referencia RMS del ADE.
- Exige al menos 80 muestras rapidas y dos convencionales.
- Referencia valida: 50 a 300 V. Variacion maxima: 2% en ADE y 5% en rapido.
- Rechaza senal ausente y factores fuera de 0.1 a 10; conserva los factores activos.
- RMS factor = referencia media / RMS BL medio sin correccion.
- Fast factor = referencia media / Fast BL medio sin correccion.
- Son factores absolutos: repetir el comando no acumula correcciones.

NVS: namespace `ADE9000`, floats `neutralRms` y `neutralFast`, inicialmente 1.
El loop principal guarda; MeterTask no espera escrituras NVS. Los dos floats se
escriben por separado (no constituyen una transaccion atomica ante un corte).
Se cargan al arrancar, se incluyen como numeros en importar/exportar calibracion,
y restaurar calibracion los devuelve a 1 (requiere reinicio, igual que el ADE).

Esto ajusta ganancia en el punto de prueba, no offset ni exactitud con formas de
onda distintas. La lectura rapida sigue siendo la estimacion I_FAST_RMS del chip.
No se agrega en esta etapa un boton de calibracion en la web: el acceso es HTTP.

## Lectura ausente

El driver inicia los valores en NaN. La lectura rapida vence a los 50 ms sin una
respuesta valida y la convencional a los 1.2 segundos (tres periodos nominales).
Una nueva respuesta valida recupera cada valor de manera independiente.
Los endpoints /meter, /meter?neutral y /meter?fast envian null, nunca el literal
NaN, que no es JSON valido. La web muestra Sin datos y el LCD -- V. La captura
binaria usa 16383 (14 bits) para el canal rapido sin respuesta valida en su turno;
su resolucion actual es 0.025 V por unidad, hasta 409.55 V.
Los errores de comunicacion no borran ni cambian los factores de calibracion.
Desde el 2026-10-19 se reintenta automaticamente la deteccion sin limite de intentos.
Se alternan 38400, 4800, 9600 y 19200 baudios, con pausa de 250 ms entre intentos
fallidos. La configuracion y su verificacion se hacen por pasos sin esperas de
respuesta bloqueantes en MeterTask. Tras ocho fallos consecutivos durante la
lectura normal se invalida la medicion y se vuelve a detectar/configurar el chip.
Los timeouts y bytes recibidos son acumulativos y permiten observar los intentos.
Esto no determina la causa fisica de la falta de respuesta.
Las metricas cada cinco segundos se emiten en nivel verbose; los cambios de
comunicacion se notifican una sola vez por transicion en nivel info/warning.
El 2026-10-19 se reparo un contacto/corto en la linea UART de la placa: la consulta
HTTP volvio a mostrar lecturas finitas de RMS y Fast RMS. Se conserva la recuperacion
automatica, la invalidacion por antiguedad y los factores existentes en NVS.

## Turnos sincronizados ADE/BL

Cada lectura Fast del ADE, real o forzada, inicia una consulta Fast al BL. Se
conservan los siete valores ADE, su timestamp Unix reducido y su tiempo monotono
en una sola instantanea estatica. No se guarda el conjunto hasta recibir una
respuesta valida o vencer el turno. No se reutiliza el BL del turno anterior.

MeterTask atiende la UART sin esperar, sondeando a 1 ms mientras hay una consulta.
En operacion a 38400 baudios cada consulta vence a los 3 ms desde el envio. La
FIFO RX configurada a un byte y timeout hardware de un simbolo evita retener
tramas cortas dentro del periferico mientras available() aun devuelve cero. Esta
configuracion se reaplica al cambiar de baud durante la deteccion.
Se selecciona explicitamente UART normal: el core Arduino 2.0.2 usado por este
proyecto inicia el periferico en RS485 half-duplex por defecto, innecesario con
RX/TX separados y capaz de interferir con una respuesta de retorno inmediata.
Primero se consumen y validan los bytes ya disponibles. Una respuesta completa
con checksum correcto pertenece a la unica consulta pendiente y se conserva,
aunque la tarea la atienda despues de 3 ms; se registra como lateRx. El plazo
limita esperar bytes faltantes, no invalida una respuesta completa ya recibida.
No conocemos su hora fisica de llegada; lateRx mide demora de atencion, no prueba
que el BL haya tardado en contestar. Tras un error se drenan
bytes tardios durante una guarda no bloqueante de 2 ms; el proximo turno de 10 ms
queda disponible. La deteccion a velocidades inferiores mantiene su plazo de 20 ms.

La RMS comun vence cada 400 ms, pero solo se solicita despues de una Fast valida,
sin otra consulta pendiente y con al menos 4 ms hasta el siguiente turno previsto.
Si no hay margen se posterga. No existe un reloj autonomo para solicitar Fast.
Los estados y las muestras NAND usan siempre el instante original del ADE, no el
de finalizacion UART. Si llega otro turno antes de completar el pendiente, el
anterior se guarda con BL ausente antes de reutilizar su instantanea.

Metricas verbose: fastTx/rmsTx cuentan consultas, validRx respuestas con checksum
valido, emptyTimeout consultas vencidas sin bytes, partial respuestas incompletas,
lateRx respuestas validas procesadas despues de 3 ms, serviceGapMax la mayor pausa
entre servicios con una consulta pendiente. No se cambia el timestamp ADE ni se
reutiliza una lectura del turno anterior para rellenar un faltante.

No se desplaza la curva ni se garantiza sincronismo interno entre chips.
Los eventos anteriores no se modifican. Los huecos globales de 128/135 ms observados
en eventos 130/131 siguen pendientes de localizar; este ajuste no demuestra resolverlos.

## Primera prueba sincronizada en placa (2026-10-19)

Firmware subido por OTA a .90, sin puerto serie conectado. Se ajusto la recepcion
UART normal/FIFO corta y se corrigio que los turnos ADE pudieran cancelar una
reinicializacion en curso. Pruebas del driver real con UART simulada verifican
deadline 3 ms, respuestas parciales/tardias, overflow, RMS intercalada y el instante
original de la muestra pendiente.

En dos ventanas consecutivas de unos 5 s se recibieron 416/435 Fast validas y
11/13 RMS comunes, sin errores CRC UART. En la segunda vencieron 66 consultas
adicionales con bytes pendientes de procesar y ningun timeout de UART vacia.
El mayor intervalo de servicio observado fue 7697 us. No se puede afirmar que
todos esos bytes llegaron antes de los 3 ms: falta instrumentar su llegada real.

Captura manual de prueba 132: 584 muestras, cinco paginas, ningun CRC de pagina
incorrecto; 500 muestras BL validas y 84 sin dato (14.4%). Conserva el formato de
16 bytes, las fechas ADE y la compatibilidad con eventos anteriores. Los huecos
por vencimiento estricto siguen visibles; no se rellenan ni se mueve la curva.
Falta comparar un corte fisico nuevo para evaluar el desfase y mejorar la atencion
UART para reducir los vencimientos sin aumentar el plazo pedido.

## Correccion de descarte prematuro (2026-10-19)

Evento 133: 583 muestras, 504 BL validas y 79 ausentes, todas las paginas con CRC
correcto. Contadores UART: checksum=0, partial=0, emptyTimeout=0; los vencimientos
se producian con bytes disponibles. La revision inicial descartaba por tiempo
antes de leer UART. Se invirtio ese orden: completar y validar primero la unica
respuesta pendiente; solo vencer si aun faltan bytes. Se conserva el plazo de
3 ms sin esperar bloqueado. Las demoras de atencion pasan al contador lateRx en
vez de transformarse automaticamente en perdida de muestras. Eventos anteriores
no se reparan ni interpolan.

Verificacion tras subir por OTA: dos ventanas de 5 s con 501 Fast validas en cada
una, RMS comun 13/12, checksum=0, partial=0 y timeout=0. Las respuestas atendidas
despues del plazo aparecen en lateRx sin perderse. El maximo request-a-servicio fue
19391 us (incluye demora nuestra, no equivale al tiempo de respuesta fisico del BL).
Captura manual 135: 584/584 muestras BL validas, ninguna ausente y ninguna pagina
con CRC incorrecto. La demora general de MeterTask sigue pendiente de analizar;
esta correccion evita confundirla con una falta de respuesta del integrado.

## Verificacion en placa (2026-10-18)

Firmware subido por COM4 y web actualizada en NAND. Primera captura: 100 muestras
rapidas y dos RMS; referencia 231.1269 V. Comparacion posterior: ADE 231.2772 V,
BL RMS 231.1672 V, BL rapido 230.9206 V. Una segunda calibracion termino con
101 muestras y factores similares, confirmando que no se multiplican entre si.
Despues de reiniciar: factores 1.16988683 y 1.18127561 recuperados; ADE 230.5077 V,
BL RMS 230.5066 V y rapido 230.7964 V. La respuesta del POST de reinicio agoto
su timeout, pero la vuelta a estado idle y las lecturas confirmaron el reinicio.
Fase invalida devuelve 400; captura simultanea devuelve 409. Prueba automatizada
de exportacion/importacion aprobada, incluidos factores numericos y rechazo de negativos.
