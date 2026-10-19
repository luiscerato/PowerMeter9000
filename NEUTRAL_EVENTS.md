# Eventos de tension neutro-tierra

Se reutiliza la captura NAND de eventos de red, con sujeto 3 (neutro-tierra).
Se conserva el empaquetado Fast RMS de 16 bytes, sin cambiar paginas ni borrar
eventos existentes. La señal de disparo es Fast RMS del BL0942, no su RMS lento.

## Configuracion

GET/POST `/grid/config`, mismos campos existentes mas:

- neutralEnabled: booleano; inicialmente false.
- neutralHigh: umbral de tension alta, inicialmente 25 V; rango 0.025 a 409.55 V.
- neutralHysteresis: inicialmente 2 V; debe ser menor que el umbral.
- neutralConfirmMs: inicialmente 100 ms; rango 20 a 600000 ms.
- neutralRecoveryMs: inicialmente 250 ms; rango 20 a 60000 ms.

POST usa parametros en el cuerpo del formulario, no JSON ni URL. Los ajustes
se guardan en el mismo blob NVS de grid y se incluyen en importar/exportar.
Se cargan tambien los ajustes anteriores de 44/48 bytes, dejando neutro desactivado.
Un respaldo anterior sin esos campos conserva los ajustes actuales de neutro.
Cambiar solo neutro no reinicia los estados de fase R/S/T.

## Captura

Superar neutralHigh durante neutralConfirmMs confirma tension alta. Recuperar
requiere bajar de neutralHigh-neutralHysteresis durante neutralRecoveryMs.
Se guarda subida y retorno; no se genera un evento por cada muestra ni se
confunde el neutro normal de pocos voltios con una fase sin tension.

Las muestras con mas de 30 ms de antiguedad, NaN, infinitas o fuera del rango 0..409.55 V cancelan
la confirmacion pendiente. Un hueco mayor de 50 ms reinicia ese plazo. Una
perdida de comunicacion no genera una recuperacion ficticia.

El candidato conserva el primer cruce, su voltaje y el ultimo valor del estado
anterior. Al confirmarse usa la misma captura y linea de transiciones que las
fases. Una condicion cancelada libera la captura no confirmada solo cuando no
hay candidatos de otros sujetos. El primer estado normal no genera evento;
una tension ya alta al iniciar si se registra, con estado anterior desconocido.

No se amplia SettingsSnapshot: cuando la raiz es neutro, noVoltage contiene
su umbral, hysteresis su histeresis y voltageConfirmMs/recoveryConfirmMs sus
tiempos. Los demas campos se conservan. Como antes, las transiciones secundarias
comparten el snapshot de la raiz: no tienen una configuracion individual almacenada.
El estado agregado de la red sigue derivado exclusivamente de R/S/T.

## Alcance y prueba

Resumen: solo RMS neutro-tierra, sin diferencia entre fases ni valor Fast duplicado.
Mediciones completas: RMS en la fila Voltaje y Fast RMS en su fila especifica.
No hay nuevas consultas periodicas. No se agrega todavia aviso push, MQTT del
nuevo sujeto ni alarma sonora; el resultado es la captura persistente y su visor.

Probado con el observador real en un arnes de compilacion: subida, recuperacion,
histeresis, NaN, huecos, cancelacion y aislamiento de candidatos de otras fases.
La prueba fisica queda pendiente mientras el BL0942 no responda. Dejar desactivado
durante la conexion a fase usada para calibrar; activar con la entrada neutro-tierra real.

Diagnostico BL0942: rx cuenta bytes recibidos incluso durante los sondeos iniciales,
partial las respuestas incompletas y ready la inicializacion verificada. Estos
datos salen por serie/Telnet cada cinco segundos, sin agregar consultas UART.

## Comprobacion en placa (2026-10-18)

Firmware subido por COM4 y web actualizada en NAND. El sondeo del BL0942 informo
ready=0, rx=0, partial=0 y cuatro timeouts: no llego ningun byte en los intentos
de inicializacion. La causa fisica sigue pendiente.
Los ajustes de fase se conservaron; neutro permanece desactivado. POST con
histeresis igual al umbral devuelve 400. Guardar la configuracion actual devuelve
200 y conserva la antiguedad de los estados de fase (78206 -> 78359 ms en la prueba).
Pruebas del observador real, interfaz movil/escritorio, respaldo, codec y captura
Wave aprobadas. No se simulo una lectura electrica real del BL ni se afirmo haberla probado.
