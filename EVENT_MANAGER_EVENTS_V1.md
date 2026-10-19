# Power Meter ADE9000 - eventos locales v1

Este documento define el contrato actual entre el firmware y Power Meter Manager.
La implementacion es local por HTTP y MQTT y no realiza HTTP POST. Los artefactos
Fast RMS/Wave y sus colas viven solamente en RAM y se pierden al reiniciar. Los
eventos de estado `grid` guardan en NVS unicamente el ultimo estado conocido y
su inicio como contexto orientativo para el proximo arranque.

## Flujo del firmware

1. Una condicion electrica inicia una captura: `voltage_dip`, `voltage_swell`,
   `over_current` o `manual_capture`.
2. Se asigna `eventId = boot-<4-hex>-<counter>`. Es unico durante el arranque;
   el mismo id vincula metadata, Fast RMS, Wave, MQTT y las rutas HTTP.
3. Se conserva Fast RMS: 100 muestras previas y hasta 300 muestras totales a
   100 Hz. La captura se detiene un segundo despues de la ultima transicion o
   cuando llega al limite de tres segundos.
4. Si el osciloscopio web no esta activo, se inicia Wave desde el ultimo frame
   de 96 ms ya disponible y se agregan frames posteriores. Wave conserva como
   maximo seis frames (576 ms). Al finalizar la condicion se agrega un frame
   posterior. Una condicion que no termina se limita naturalmente por los seis
   frames.
5. Solo cuando Fast RMS termino y Wave termino o fue declarado no disponible,
   el evento queda listo, aparece en `GET /events` y se publica por MQTT.
   No hay una notificacion MQTT temprana.
6. Mientras siga retenido, el mismo evento se vuelve a publicar cada cinco
   segundos. El Manager debe deduplicar por el par `deviceId + eventId`: una
   repeticion actualiza el mismo evento y no debe crear una alerta nueva.
7. El Manager descarga los artefactos, valida tamano y CRC, y recien entonces
   invoca `clear`. El clear libera todos los buffers de ese evento.

La cola mantiene como maximo cinco eventos. Si se llena, el firmware libera el
evento mas antiguo para conservar el nuevo. Una descarga que estaba en curso
puede quedar truncada por ese reemplazo o por `clear`; el Manager debe tratarla
como fallida y no confirmar el evento.

## Metadata y MQTT

Topic MQTT:

```text
<baseTopic>/events/notification
```

Actualmente el base topic es `PowerMeter9000`. La publicacion usa QoS 1 y
retain false. Su cuerpo es el mismo JSON final que devuelve REST, mas
`sequence`, que aumenta en cada publicacion MQTT.

Ejemplo:

```json
{
  "schemaVersion": 1,
  "sequence": 1234,
  "eventId": "boot-a41f-42",
  "deviceId": "pm-ade9000-c4c07612cfa4",
  "category": "wave",
  "type": "voltage_dip",
  "timestamp": 1788457847186,
  "phases": ["R"],
  "value": 182.4,
  "unit": "V",
  "durationMs": 140,
  "fastRms": {
    "available": true,
    "channelCount": 7,
    "sampleRateHz": 100,
    "sampleCount": 214,
    "channels": ["vr", "vs", "vt", "ir", "is", "it", "in"],
    "scales": { "voltageVPerBit": 0.01, "currentAPerBit": 0.01 },
    "triggerSample": 95,
    "checksum": "A34F219C"
  },
  "wave": {
    "available": true,
    "channelCount": 7,
    "sampleRateHz": 8000,
    "blockCount": 5,
    "samplesPerChannel": 3840,
    "triggerSample": 608,
    "channels": ["ir", "vr", "is", "vs", "it", "vt", "in"],
    "scales": { "voltageVPerBit": 0.25, "currentAPerBit": 0.1 },
    "size": 40452,
    "checksum": "BC012DEF"
  },
  "links": {
    "fastRms": "/events/fast-rms?eventId=boot-a41f-42",
    "wave": "/events/wave?eventId=boot-a41f-42",
    "clear": "/events/clear"
  }
}
```

Todos los timestamps de metadata son Unix Epoch UTC en milisegundos y se
serializan como enteros JSON. Si el reloj no esta sincronizado, `timestamp` es
`null`. Como excepcion, Grid omite `timestamp` y usa `ageMs` despues de su espera
maxima por NTP. La zona horaria solo se aplica al mostrar datos.

`durationMs` es la duracion electrica observada, no el largo de los buffers. Es
`null` si la condicion todavia no finalizo antes de cerrar las capturas.

`phases` siempre es un array y contiene unicamente la fase que origino la
captura. Los tipos iniciales son extensibles: `voltage_dip`, `voltage_swell`,
`over_current` y `manual_capture`.

Si `fastRms.available` o `wave.available` es `false`, el objeto contiene
`reason`. Valores actuales: `scope_active`, `insufficient_heap`,
`allocation_failed`, `no_wave_frames` y `no_samples`.

Versiones anteriores podian informar `continuity_lost` al no conseguir el mutex.
La captura actual no comparte ese mutex con HTTP ni descarta frames por su bloqueo.
Esto no detecta ni corrige por si solo una discontinuidad originada en la lectura del ADE.

Si un bloque Wave posterior no puede asignarse, se conservan los bloques
anteriores validos; por eso `wave.available` sigue siendo `true` y el tamano
real se obtiene de `blockCount`, `samplesPerChannel` y `size`.

## Eventos de estado de red

Los eventos de estado usan el mismo topic MQTT y se distinguen con
`category: "grid"`. No tienen artefactos Fast RMS/Wave ni rutas de descarga:
cada mensaje representa una unica transicion confirmada de una fase o de la red
general. Es inmutable: no existe una segunda publicacion para cerrarlo.

Ejemplo de transicion de fase `normal -> undervoltage`:

```json
{
  "schemaVersion": 1,
  "sequence": 1301,
  "eventId": "boot-a41f-57",
  "deviceId": "pm-ade9000-c4c07612cfa4",
  "category": "grid",
  "type": "undervoltage",
  "previousState": "normal",
  "state": "undervoltage",
  "timestamp": 1788457847186,
  "previousStateStartedAt": 1788450000000,
  "previousStateDurationMs": 7847186,
  "phases": ["R"],
  "value": 181.35,
  "unit": "V"
}
```

Semantica temporal:

- `timestamp`: instante en que comenzo `state`, es decir, cuando ocurrio la
  transicion.
- `previousStateStartedAt`: instante conocido en que habia comenzado
  `previousState`.
- `previousStateDurationMs`: duracion calculada del estado anterior.

Los dos campos del estado anterior pueden ser null si un reinicio sin RTC impide
reconstruirlos. Si el medidor no obtiene NTP durante cinco minutos, omite
`timestamp` y envia `ageMs`, que indica cuantos milisegundos transcurrieron desde
el evento hasta esa publicacion. El Manager puede estimar el instante como
`horaDeRecepcion - ageMs`; no debe interpretar `ageMs` como Unix Epoch.

Tipos por fase:

- `phase_normal`
- `undervoltage`
- `overvoltage`
- `phase_loss`

Tipos generales, siempre con `phases: ["R", "S", "T"]`:

- `grid_normal`: las tres fases estan normales.
- `grid_abnormal`: al menos una fase esta fuera de rango, pero no se perdieron
  las tres.
- `grid_outage`: las tres fases estan en `phase_loss`.

Configuracion inicial del detector, modificable desde la web del medidor:

| Ajuste | Valor inicial |
| --- | --- |
| Bajo voltaje R/S/T | 185 V |
| Alto voltaje R/S/T | 250 V |
| Sin tension | 20 V |
| Histeresis comun | 5 V |
| Confirmacion de perdida de fase | 250 ms |
| Confirmacion de bajo/alto voltaje | 5000 ms |
| Confirmacion de recuperacion normal | 250 ms |

La histeresis mantiene `phase_loss` hasta superar 25 V, `undervoltage` hasta
superar 190 V y `overvoltage` hasta bajar de 245 V. El estado general no agrega
otra demora: es `normal` si las tres fases son normales, `outage` si las tres
estan en `phase_loss`, y `abnormal` en cualquier otra combinacion confirmada.
El Manager debe guardar los valores recibidos y no reclasificar historicos con
sus propios umbrales.

Los eventos por fase y el evento general son intencionalmente independientes.
Por ejemplo, un corte completo puede generar tres transiciones hacia
`phase_loss` y una hacia `grid_outage`. Esto permite consultar el historial
general o el detalle de una fase. El filtrado de notificaciones corresponde al
Manager.

NVS guarda cuatro snapshots fijos para R, S, T y red general. Cada snapshot
contiene el ultimo estado conocido y su fecha de inicio si era conocida. Es
informacion auxiliar y puede quedar desactualizada si la alimentacion desaparece
antes de completar la escritura. No guarda la cola ni las muestras Fast RMS/Wave.

Al arrancar:

1. El firmware espera cinco segundos para que las mediciones se estabilicen.
2. Con `ESP_RST_POWERON` o `ESP_RST_BROWNOUT`, publica un registro inicial para
   cada fase y otro para la red general. Todos llevan `source: "startup"` y
   `resetReason: "power_on"` o `"brownout"`.
3. `previousState` proviene del snapshot NVS o vale `unknown`; `state` contiene
   la medicion estable actual. Es valido que ambos estados sean iguales.
4. Un reset de software, panic, watchdog, externo o desconocido solo establece
   la linea base y no genera registros electricos.
5. La nueva linea base se guarda en NVS inmediatamente, sin depender de MQTT.
6. Los registros esperan MQTT y NTP hasta cinco minutos. Si aparece NTP se
   reconstruye `timestamp` usando el `millis()` capturado. Si vence el plazo,
   pueden publicarse con `ageMs`.

Ejemplo de registro inicial por fase:

```json
{
  "schemaVersion": 1,
  "sequence": 1302,
  "eventId": "boot-a41f-58",
  "deviceId": "pm-ade9000-c4c07612cfa4",
  "category": "grid",
  "type": "phase_normal",
  "previousState": "phase_loss",
  "state": "normal",
  "timestamp": 1788457847186,
  "previousStateStartedAt": 1788450000000,
  "previousStateDurationMs": 7847186,
  "phases": ["R"],
  "value": 229.4,
  "unit": "V",
  "source": "startup",
  "resetReason": "power_on"
}
```

El firmware conserva hasta 16 registros grid pendientes en una cola estatica de RAM,
publica uno por vez con QoS 1 y lo retira al recibir PUBACK. Tras una desconexion
reintenta el mismo cambio. Puede haber duplicados, por lo que `sequence` no debe
usarse como clave y el upsert por `deviceId + eventId` es obligatorio. Si se
llena, descarta el pendiente mas antiguo para conservar la informacion reciente.
La cola RAM se pierde al reiniciar, pero los snapshots NVS permanecen.

### Diferencias entre Wave y Grid

| Campo/comportamiento | `category: "wave"` | `category: "grid"` |
| --- | --- | --- |
| Identidad | `deviceId + eventId` | `deviceId + eventId` |
| Topic | `events/notification` | `events/notification` |
| Cuerpo comun | schemaVersion, sequence, eventId, deviceId, category, type, timestamp y phases | Grid agrega state, previousState, previousStateStartedAt y previousStateDurationMs; sin NTP usa ageMs; value/unit se omiten en estados generales |
| Detalles | `fastRms`, `wave`, `links` | No tiene artefactos ni links |
| Ciclo de vida | Se repite completo hasta `clear` HTTP | Un mensaje inmutable por transicion |
| Confirmacion | Descarga, CRC y `clear` | No hay `clear`; la cola avanza al recibir PUBACK |
| Persistencia | Hasta cinco capturas descargables en RAM | Ultimo estado por fase/red en NVS y hasta 16 mensajes pendientes en RAM |

## Rutas HTTP

Las rutas no requieren autenticacion dentro de la red local.

```text
GET  /events
GET  /events/fast-rms?eventId=<eventId>
GET  /events/wave?eventId=<eventId>
POST /events/clear   body: eventId=<eventId>
POST /events/test
```

Los campos de las operaciones POST se envian como `application/x-www-form-urlencoded` en el cuerpo, no en la query string.

`GET /events` devuelve:

```json
{
  "schemaVersion": 1,
  "events": []
}
```

La respuesta binaria usa `application/octet-stream` y `Cache-Control: no-store`.
El array contiene los objetos finales completos del ejemplo MQTT, sin sequence;
vacio significa que no hay eventos listos en ese instante. No indica si hay
una captura en curso. Los checksums del ejemplo son ilustrativos.
La longitud anunciada por HTTP debe coincidir exactamente con el archivo
recibido. Si el evento desaparece durante una transferencia, el stream se
corta; no se debe invocar `clear` ni persistir la descarga incompleta.

`POST clear` devuelve `{"ok":true}` o `404` si el evento ya no existe.
`POST /events/test` solicita una captura real de prueba por el mismo flujo de adquisicion
con tipo `manual_capture`; no inventa muestras.

## Artefacto Fast RMS

El binario Fast RMS contiene `sampleCount` registros consecutivos de 16 bytes.
El tiempo es uint16 little-endian; los canales unsigned se empaquetan LSB-first:

| Offset en bits | Bits | Campo | Escala |
| --- | --- | --- | --- |
| 0 | 16 | timeStamp | campo legado, usar solo como referencia |
| 16 | 14 | VrmsR | 0.025 V |
| 30 | 14 | VrmsS | 0.025 V |
| 44 | 14 | VrmsT | 0.025 V |
| 58 | 14 | IrmsR | 0.01 A |
| 72 | 14 | IrmsS | 0.01 A |
| 86 | 14 | IrmsT | 0.01 A |
| 100 | 14 | IrmsN | 0.01 A |
| 114 | 14 | VrmsN | 0.025 V |

16383 indica sin dato en cualquier canal. Maximos validos: 409.55 V y 163.82 A.
NaN, infinito, negativos y valores fuera de rango se codifican como sin dato,
sin truncamiento modular ni saturacion silenciosa. Las lecturas actuales de
/meter no se cuantizan: este empaquetado afecta solamente a las capturas Fast.

El descriptor anuncia `channelCount:8`, `sampleBytes:16`, `channelBits:14`,
`noData:16383` y canales `vr, vs, vt, ir, is, it, in, vn`.
Los consumidores externos deben aplicar este empaquetado, no leer uint16 por canal.
No se modifica el binario Wave ni se agrega VN al osciloscopio de 8 kHz.

La posicion temporal fiable es la secuencia de muestras a 100 Hz, correlacionada
con el `timestamp` y el indice del evento. `timeStamp` es un campo legado de 16
bits y puede envolver; no debe usarse como Unix timestamp.

El checksum es CRC32 IEEE de todos los bytes Fast RMS.

`triggerSample` es el indice estimado de inicio dentro del array Fast RMS.
Se parte de la frontera posterior a las muestras previas realmente copiadas y
se descuentan los semiciclos configurados para confirmar el dip/swell. Con 100
muestras previas y 5 semiciclos de confirmacion vale 95. En captura manual vale
100 porque no hay confirmacion. Si el inicio queda fuera de las muestras
retenidas se publica `null`, sin desplazarlo artificialmente al primer/ultimo dato.

## Evento sintetico para probar almacenamiento y descarga

POST /events/test body: pattern=ramp genera un evento completo en RAM, sin
leer el ADE ni modificar el prebuffer real o el osciloscopio. Tambien se puede
solicitar con el boton Generar rampa de prueba en la web. El endpoint sin
pattern conserva el test anterior: captura muestras electricas reales.

La respuesta es HTTP 201 con schemaVersion:1, ok:true, eventId y
testPattern:"triangle-int12-v1". HTTP 503 significa que no se pudo generar
(reason: insufficient_heap, test_busy o event_store_busy); no se conserva
un evento sintetico parcial. Otros valores de pattern devuelven HTTP 400.

El evento usa type:"manual_capture" y agrega testPattern:"triangle-int12-v1"
al JSON de metadata y a MQTT. No es una alarma electrica real. timestamp es
el momento de generacion, UTC ms o null sin reloj; durationMs es null y los
triggerSample son 0. Se notifica solo completo, con los mismos topics, links,
CRC32, repeticiones MQTT y clear que los eventos normales. El Manager debe
deduplicar por eventId y reconocer testPattern como prueba sintetica.

Wave: seis bloques de 8064 bytes, 4608 muestras por canal a 8000 Hz, archivo
PMEV v1 de 48516 bytes. preBlockCount es 0. Se usa el mismo compresor int12
del osciloscopio, con seis partes de 128 muestras por bloque. Todos los canales
contienen la misma secuencia entera q(n), con n absoluto desde 0 (no se reinicia
en ningun limite de fragmento o bloque):

    p = (n + 2049) % 8190
    q = p <= 4095 ? p - 2048 : 6142 - p

Ejemplos: q(0)=1, q(127)=128, q(128)=129, q(767)=768, q(768)=769,
q(2046)=2047, q(2047)=2046, q(4607)=-514. El minimo -2048 corresponderia a
n=6141, fuera de esta captura. Voltajes = q * 0.25 V; corrientes = q * 0.1 A.
La curva fisica de cada magnitud tiene distinta escala, aunque los codigos
int12 de los siete canales son iguales. Esto no prueba permutas de canales.

Fast RMS: 300 registros de 16 bytes. El contador temporal local uint16_t de
cada registro es n*10; los siete valores uint16_t son n+1 para n=0..299.
Con escala 0.01 se ven rampas de 0.01 a 3.00 V/A. Son datos fabricados para
probar el formato, NO valores RMS calculados a partir de la onda sintetica.

La generacion usa buffers privados fuera del mutex del almacen; solo toma el
mutex al insertar el evento terminado. Requiere memoria para los seis bloques,
Fast RMS y un fragmento temporal, respetando el margen de 30 KiB. No escribe
datos en flash. Insertar puede desplazar el evento mas antiguo si la cola esta
llena, igual que una captura normal.

Comprobar todos los enteros descargados contra q(n), no solo mirar la curva.
Si coinciden, ese ensayo valida generacion, compresion, retencion y descarga;
no valida lectura del ADE, tiempos de adquisicion ni armado del prebuffer real.

## Diagnostico temporal de adquisicion Wave

GET /events/wave-diagnostics devuelve los primeros 48 registros de
lectura posteriores al inicio del ultimo evento. Quedan retenidos hasta el
siguiente evento o reinicio; no forman parte del binario ni del contrato MQTT.
No escribir en flash ni consultar continuamente este endpoint durante capturas.

Cada registro contiene eventId, block (contador global de frames, no indice
relativo al evento), part (fragmento 0..5 dentro del frame), half (mitad ADE
0/1), beforePage y afterPage (WFB_LAST_PAGE antes/despues del SPI), irqSequence,
irqUs, readUs, intervalUs, partHashFnv1a y result. intervalUs mide el tiempo entre
inicios de lecturas Wave consecutivas (nominalmente 16000 us; 0 sin lectura anterior).
irqSequence cuenta todas las IRQ0, no solo PAGE_FULL;
irqUs es la ultima marca local micros() de IRQ0, compartida con otras fuentes,
no Unix Epoch ni una marca exacta exclusiva de PAGE_FULL. Se reinicia por wrap
de uint32_t. readUs es el tiempo de lectura. result es observed: el diagnostico
no filtra ni reordena muestras. partHashFnv1a es FNV-1a de 32 bits sobre los 1344
bytes de ese fragmento comprimido, antes de copiarlo al almacen de eventos.
Se inicia en 2166136261 y, para cada byte, aplica XOR y multiplica por 16777619
modulo 2^32. Sirve para comparar con los fragmentos del archivo descargado;
no reemplaza el CRC32 ni demuestra por si solo que dos fragmentos son identicos.
Si no hay memoria para construir el diagnostico, devuelve error insufficient_heap.

La raiz tambien incluye lastStoreUs y maxStoreUs: tiempo de la ultima llamada
de guardado de un frame de 96 ms y maximo desde el arranque. Incluyen reserva y
copia de bloques, pero no envio WebSocket ni CRC/publicacion del loop principal.
Son mediciones locales de duracion, no timestamps UTC. No se presupone una cota
de tiempo para malloc; se verifica en la placa.

Se conserva la seleccion original del osciloscopio: mitad 1 para paginas 14,
15 y 0..2; mitad 0 para las restantes. Las IRQ siguen configuradas en 7 y 15.
PAGE_FULL ahora se atiende antes de RMS, potencia, THD y energia dentro de
STATUS0; las reservas/finalizacion de eventos y los comandos manuales se
procesan despues de atender IRQ0 pendiente. Las impresiones de adquisicion,
inicio/cierre y registro de eventos quedan comentadas para evitar bloqueos de
Serial/RemoteDebug. No se modifican el empaquetado ni el descarte de mitades.
La causa de las repeticiones de 128 muestras sigue pendiente de validacion en hardware.

El WebSocket del osciloscopio descarta frames nuevos si el heap libre es menor
que 50 KiB (51200 bytes) o alguna cola esta llena. No envia avisos mientras
persista esa condicion. Al recuperarse envia el texto scope_gap antes del
siguiente binario; la web vacia sus muestras pendientes para no unir ambos
lados de la perdida. Los bloques descartados no se reintentan ni se guardan.
Esto no modifica el formato PMEV, MQTT ni la politica de retencion de eventos.

## Artefacto Wave PMEV v1

El archivo Wave es little-endian y tiene esta forma:

```text
cabecera PMEV de 128 bytes
blockCount bloques de 8064 bytes
CRC32 final de 4 bytes
```

La cabecera contiene:

| Offset | Tamano | Campo |
| --- | --- | --- |
| 0 | 4 | ASCII `PMEV` |
| 4 | uint16 | version = 1 |
| 6 | uint16 | headerSize = 128 |
| 8 | uint64 | timestamp Unix ms, 0 si no habia reloj |
| 16 | uint32 | sampleRateHz = 8000 |
| 20 | uint16 | channelCount = 7 |
| 22 | uint16 | blockCount |
| 24 | uint16 | preBlockCount (0 o 1) |
| 26 | uint16 | voltageScaleMilli = 250 |
| 28 | uint16 | currentScaleMilli = 100 |
| 30 | uint16 | type code: 1 dip, 2 swell, 3 over current, 4 manual |
| 32 | uint8 | phase mask: R=1, S=2, T=4, N=8 |
| 33 | 1 | reservado |
| 34 | uint16 | samplesPerBlock = 768 por canal |
| 36 | uint32 | samplesPerChannel = blockCount * 768 |
| 40 | 24 | reservado |
| 64 | 48 | eventId ASCII terminado en NUL |
| 112 | 16 | reservado |

Cada bloque representa 96 ms: 768 instantes de 7 canales a 8 kHz. Los canales
por instante son `IA, VA, IB, VB, IC, VC, IN`. Voltajes usan 0.25 V por bit y
corrientes usan 0.1 A por bit.

El JSON repite esta informacion en `wave.channels` y `wave.scales`. El orden
Wave es necesariamente `ir, vr, is, vs, it, vt, in`, que coincide con el orden
fisico comprimido. Fast RMS usa un orden distinto: `vr, vs, vt, ir, is, it, in, vn`.

### Alineacion de capturas

Los dos objetos incluyen `triggerSample`, indice base cero por canal del inicio
estimado de la condicion que origino la captura. Para alinear las graficas:

```text
tiempoRelativoMs = (indiceMuestra - triggerSample) * 1000 / sampleRateHz
```

Fast RMS parte de la frontera posterior a las muestras previas realmente copiadas,
con resolucion nominal de 10 ms. Wave parte de las partes de 128 muestras ya
comprimidas en el frame actual. La RAM conserva el frame anterior completo y
otro frame en construccion; se suman 768 muestras si se incluyo el anterior.
La posicion se toma antes de reservar/copiar los buffers del nuevo evento.

Se aplica a ambos indices la misma demora nominal: semiciclos configurados del
detector de origen por 10 ms (red actual de 50 Hz). No se recalcula por eventos
posteriores de otra fase/tipo. Captura manual y sobrecorriente no descuentan
esa confirmacion. Si excepcionalmente la captura se inicia con un cierre, se
descuenta tambien la duracion conocida de ese evento.

```text
fastRms.triggerSample = muestrasPreviasCopiadas - demoraUs / 10000
wave.triggerSample = muestrasFrameAnterior + partesCopiadas * 128
                     + refinamientoUs / 125 - demoraUs / 125
```

La division es entera. El refinamiento usa el tiempo desde IRQ0 solo si la
ultima pagina completa era 7/15, se atendio dentro de 2 ms y al disparo esa
referencia tiene menos de 16 ms. Si no se cumple, refinamientoUs vale cero:
se conserva la referencia gruesa de la ultima parte copiada, NO se publica null
solo por perder la precision de la IRQ. IRQ0 es compartida con otras fuentes;
si se perdieron paginas, no hay garantia de continuidad ni de exactitud temporal.

Ejemplo con 5 semiciclos: 768 previas + 128 copiadas + 14000/125 - 50000/125
= indice Wave 608; Fast RMS vale 95. Otro caso: 384 copiadas - 400 de demora
= -16 respecto del frame actual, que es valido dentro del anterior:
768 - 16 = indice 752. Es importante sumar el preframe ANTES de validar el signo.

Solo se publica `triggerSample: null` si el inicio estimado no esta contenido
en las muestras retenidas. El Manager debe admitir ese caso y dibujar sin una
alineacion conocida. Una seccion no disponible sigue sin tener muestras/indice.
Ambos indices son estimaciones, no marcas exactas del ADC: la confirmacion,
el calculo RMS y la latencia de atencion impiden certificar el inicio fisico.
El [manual del ADE9000](https://www.analog.com/media/en/technical-documentation/user-guides/ADE9000-UG-1098.pdf),
secciones RMS y Dip/Swell Indication, describe los filtros RMS y la confirmacion
del detector. No se compensa aqui la latencia interna de esos filtros.

Los indices siguen solo en JSON; no cambian los binarios ni sus escalas. Esta
correccion no cambia timestamp/durationMs de metadata, ni habilita triggers,
ni rearma la captura continua del ADE. La alineacion relativa de los dos
buffers debe usar los indices, no asumir que timestamp es una marca ADC exacta.

Las muestras son enteros con signo de 12 bits, comprimidos de a dos muestras en
tres bytes. Para `b0, b1, b2`:

```text
sample0 = b0 | ((b1 & 0x0F) << 8)
sample1 = (b1 >> 4) | (b2 << 4)
si sample & 0x800: sample -= 0x1000
```

El CRC32 final se escribe little-endian y coincide con `wave.checksum`. Se
calcula con CRC32 IEEE (polinomio 0xEDB88320, inicio 0xFFFFFFFF, XOR final
0xFFFFFFFF) sobre cabecera mas payload, sin incluir esos cuatro bytes finales.

## Politica de memoria

La captura activa es privada de MeterTask. Cada fragmento de 128 muestras se
lee y comprime como antes; al completar seis fragmentos, se copia el frame de
8064 bytes al prebuffer y, si corresponde, al bloque propio del evento. No hay
mutex HTTP, CRC ni MQTT en ese recorrido. No se encolan frames pendientes ni
se guarda una referencia al buffer reutilizable outputSamples.

El loop principal copia Fast RMS terminado y entrega su puntero a MeterTask
por una cola de dos descriptores sin espera. Si no puede encolarlo, conserva
el buffer original y reintenta. MeterTask incorpora ese resultado y entrega el
puntero al evento completo por otra cola de dos punteros, tambien sin espera.
Desde ese momento no vuelve a escribirlo. El loop calcula el CRC Wave fuera
del mutex, inserta el evento terminado en el catalogo y publica la notificacion.
HTTP y clear solo acceden a ese catalogo; no pueden borrar la captura activa.

El catalogo conserva hasta cinco eventos terminados; transitoriamente puede
haber una captura privada, dos eventos en entrega y uno en finalizacion. No
son copias adicionales de las muestras: se transfiere su propiedad. Si se
llena la cola de entrega, se libera el mas antiguo aun en esa cola para dejar
el nuevo y se registra el descarte desde el loop principal. Nunca se libera
por ese mecanismo el evento que el loop ya recibio para finalizar.

Wave no reserva un bloque fijo de 60 KiB. Cada evento reserva al inicio dos
bloques de 8064 bytes y solicita los siguientes uno por uno, hasta seis. Antes
de cada reserva se exige que queden como minimo 30 KiB de heap libre. Fast RMS
y los bloques Wave pertenecen a una unica entrada de la cola y se liberan juntos
por `clear` o por reemplazo del evento mas antiguo.

El osciloscopio tiene prioridad: si esta activo al iniciar o durante una captura,
la parte Wave se descarta con `reason: "scope_active"`; Fast RMS continua.

## Implementacion requerida en el Manager

### Enrutamiento comun

1. Obtener `deviceId` y `baseTopic` con `GET /manager/discovery` y
   suscribirse a `<baseTopic>/events/notification`.
2. Parsear primero `schemaVersion` y `category`. En v1, `category` puede ser
   `wave` o `grid`. Aceptar tipos nuevos como texto y no descartar todo el
   mensaje por un `type` desconocido.
3. Usar `deviceId + eventId` como identidad durable. `sequence` sirve para
   diagnostico y orden aproximado, pero no es clave: puede saltar, reiniciarse o
   cambiar al reintentar una publicacion.
4. Hacer el procesamiento idempotente. QoS 1, las repeticiones Wave y los
   reintentos Grid pueden entregar el mismo evento mas de una vez.
5. Guardar el JSON original junto con los campos normalizados para incorporar
   tipos o campos futuros sin perder informacion.

La base de datos puede usar una tabla comun con columnas especificas opcionales,
o tablas separadas para Wave y Grid. En ambos casos debe existir una restriccion
unica por `deviceId + eventId` y una operacion de upsert.

### Procesamiento de category wave

1. Obtener la direccion HTTP local del medidor desde su configuracion. Los links
   del JSON son relativos a esa direccion, nunca a la direccion del Manager.
2. Consultar `GET /events` al arrancar o reconectar para recuperar la cola
   Wave aunque se haya perdido MQTT. Se puede repetir cada cinco segundos y se
   debe desactivar cache HTTP. El catalogo no contiene eventos Grid.
3. Crear o actualizar un unico evento Wave por `deviceId + eventId`. Una
   repeticion no genera otra alerta ni otra descarga simultanea.
4. Encolar una sola descarga por evento. Procesar eventos y archivos
   secuencialmente por medidor, primero Fast RMS y luego Wave, para limitar
   conexiones y consumo de memoria del ESP32.
5. Descargar solamente las secciones con available=true. Un link presente no
   implica disponibilidad. No esperar que una seccion false se complete luego:
   la notificacion ya es final.
6. Validar cada binario antes de decodificar o confirmar. Fast RMS debe medir
   sampleCount * sampleBytes bytes (actualmente 16). Wave debe medir size = 128 + blockCount * 8064 + 4.
   Comprobar ademas magic, version, cabecera, eventId, cantidad de canales,
   muestras y CRC Wave. Comparar CRC como ocho digitos hexadecimales sin
   distinguir mayusculas/minusculas.
7. Guardar de forma durable metadata y todos los binarios anunciados como
   disponibles. Solo entonces enviar POST al link clear, sin body requerido.
   Conservar metadata y triggerSample: el indice Wave no esta en la cabecera.
8. Si clear falla por red, reintentarlo sin volver a descargar archivos ya
   validados. Un 404 al reintentar clear es aceptable si el evento ya se guardo:
   la placa no lo retiene. No generar una nueva alerta por ese reintento.
9. Si una descarga falla, reconsultar el catalogo. Si el evento sigue presente,
   reintentar desde cero; si desaparecio, marcar el detalle como perdido y
   conservar la metadata conocida. No confirmar un evento con archivos
   anunciados pero incompletos. Los detalles false no bloquean clear.
10. Mostrar Fast RMS y Wave dentro del mismo evento. Convertir los valores
    multiplicando el entero decodificado por la escala JSON correspondiente a
    su canal. Alinear con triggerSample y sampleRateHz. Aplicar zona horaria
    solamente al mostrar timestamp. Si timestamp es null, conservarlo como
    desconocido y guardar por separado la hora de recepcion del Manager.

### Procesamiento de category grid

1. No consultar `GET /events` ni descargar archivos: Grid llega solamente
   por MQTT y no contiene `fastRms`, `wave` ni `links`.
2. Cada `eventId` representa una transicion inmutable. Insertarla una vez y usar
   upsert solamente para hacer idempotentes los duplicados QoS 1.
3. Guardar `previousState`, `state`, `timestamp` o `ageMs`, `previousStateStartedAt`,
   `previousStateDurationMs`, `phases`, `value` y `unit`. Los dos ultimos se
   omiten en estados generales; los tres campos temporales pueden ser null.
4. `timestamp` inicia el estado nuevo. Si solo existe `ageMs`, estimarlo desde la
   hora de recepcion y conservar que su origen fue relativo. Los campos
   `previousState*` describen el tramo que acaba de terminar y permiten dibujarlo
   sin buscar necesariamente la transicion anterior.
5. Un `eventId` nuevo puede generar una notificacion al usuario. Un duplicado del
   mismo id solo confirma/actualiza la fila y no genera otra alerta. No existen
   mensajes separados de apertura y cierre.
6. Los registros con `source: "startup"` son observaciones posteriores a un
   `POWERON` o `BROWNOUT`. Deben aparecer en el historial de su fase, pero no
   garantizan que NVS haya registrado el ultimo cambio antes del apagado.
7. Los eventos de fase llevan una sola fase. Los estados generales llevan
   `phases: ["R", "S", "T"]`. Es valido recibir tres eventos `phase_loss` y un
   `grid_outage` para el mismo episodio: son vistas independientes, no duplicados.
8. Para una linea de tiempo, el segmento anterior va desde
   `previousStateStartedAt` hasta `timestamp`; el estado nuevo queda vigente
   desde `timestamp` hasta la siguiente transicion. Aplicar zona horaria solo al
   visualizar y conservar null cuando el firmware no conocia la hora.
9. El Manager puede agrupar notificaciones para evitar avisos repetitivos, pero
   debe conservar todos los registros por fase y el registro general.

Las rutas actuales sirven archivos completos: no implementan HTTP Range,
paginacion, descarga por offset ni reanudacion. El fragmentado interno de
AsyncWebServer es transparente al cliente. No usar las rutas anteriores
/events/{eventId}/rms ni /events/{eventId}/wave. Falta eventId: HTTP 400;
detalle ausente: HTTP 404. Si desaparece despues de enviar las cabeceras HTTP,
la respuesta puede truncarse o agotar el timeout; no puede cambiarse a 404.
Usar timeout de descarga finito (por ejemplo 15 segundos) y reintento limitado.

El topic legado PowerMeter9000/events y el websocket /acevents pueden seguir
existiendo para la web/Node-RED. El Manager nuevo debe consumir exclusivamente
events/notification y estas rutas, para evitar duplicados de contratos distintos.
No existe HTTP POST binario desde el medidor. Wave se confirma con `clear`; Grid
no requiere una confirmacion de aplicacion del Manager.

## Limites de esta version

- El bootId aleatorio de cuatro digitos no garantiza unicidad entre todos los
  reinicios. La clave compuesta indicada es el contrato actual, con ese riesgo
  de colision; no usar sequence como identidad persistente.
- Las escalas JSON describen los valores nominales actuales; en esta version
  se serializan como constantes. No se implemento un snapshot por evento de
  los ajustes modificables del osciloscopio. Conviene validar las escalas al
  volver del osciloscopio antes de interpretar nuevas capturas.
- Los 100 samples previos son la configuracion inicial; usar los conteos e
  indices anunciados, no asumir siempre 100 o 300. La captura Fast RMS actual
  tambien puede cerrar al pasar un segundo sin nuevas transiciones, aunque la
  condicion electrica siga activa. En ese caso durationMs permanece null.
- El margen de 30 KiB es una comprobacion previa a asignar bloques Wave,
  no una reserva de memoria garantizada para las otras tareas.
- La cola no anuncia cada reemplazo con un mensaje separado. El catalogo y los
  404 son la referencia para detectar eventos que dejaron de estar disponibles.
- La precision de alineacion y la continuidad de muestras necesitan validacion
  en hardware; no interpretar triggerSample como medida certificada de tiempo.

## Prueba de integracion

Solicitar POST /events/test una vez, esperar la notificacion final (o
consultar el catalogo durante unos segundos), descargar ambos detalles
disponibles, validar CRC y guardar. Hacer clear y comprobar que desaparece del
catalogo. La respuesta inmediata de test confirma la solicitud, no la captura
terminada; puede esperar si ya hay una captura o calibracion en curso.

Repetir con Manager desconectado y luego reconectado, notificaciones duplicadas,
Wave ausente, timestamp null y evento eliminado durante descarga. Comprobar que
hay una sola alerta por evento y que no se elimina antes de guardar sus datos.

Para Grid, verificar por separado:

1. Una transicion normal a bajo/alto voltaje o perdida de fase produce una
   unica notificacion con `state`, `previousState` y una sola fase.
2. La recuperacion produce otro `eventId` que describe la transicion inversa y
   la duracion del estado anterior cuando pudo calcularse.
3. Perder las tres fases produce tres transiciones de fase y una transicion
   `grid_outage`; no deben deduplicarse entre si.
4. Un `POWERON` o `BROWNOUT` produce tres registros de fase y uno general luego
   de los cinco segundos de estabilizacion, aun si el estado no cambio.
5. Un reset de software, panic, watchdog o causa desconocida no produce registros
   Grid de arranque.
6. Sin NTP durante cinco minutos pero con MQTT disponible, el registro usa
   `ageMs` en lugar de `timestamp`.
7. Desconectar MQTT, generar varios cambios y reconectar conserva su orden hasta
   el limite de 16 pendientes.
8. Enviar dos veces el mismo `eventId` produce una sola fila por upsert.
La captura manual usa muestras reales con metadata de prueba; no provoca una
caida de tension fisica y no debe esperarse ver un dip artificial en las curvas.
