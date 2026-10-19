# Diseno de almacenamiento NAND

Este documento define la arquitectura acordada para usar la NAND Winbond como
almacenamiento persistente del Power Meter. La implementacion se realiza por
capas para mantener la adquisicion electrica independiente de los tiempos de
lectura, programacion y borrado de la memoria.

## Objetivos

- Un unico componente ejecuta operaciones fisicas sobre la NAND.
- `MeterTask` nunca espera una operacion NAND.
- Las capturas Wave tienen prioridad sobre historico, web y diagnostico.
- La web puede esperar una lectura sin bloquear adquisicion ni otras tareas.
- Los bloques defectuosos se excluyen sin construir una tabla de traduccion
  logica completa.
- Cada particion trabaja con bloques relativos y no conoce direcciones fisicas
  externas a su region.
- Los anillos se reconstruyen desde los registros almacenados. No dependen de
  actualizar un cursor global despues de cada escritura.

## Geometria

- W25N02KV: 256 MiB, 2048 bloques.
- W25N01GV/KV: 128 MiB; el driver ya puede identificar estos modelos.
- Pagina: 2048 bytes.
- Bloque de borrado: 128 KiB, 64 paginas.
- Los enteros multibyte se almacenan little-endian.
- Un bloque con marcador de fabrica o fallo de uso se descarta completo.
- El bitmap global de bloques utilizables ocupa 2048 bits, 256 bytes para la
  W25N02KV.

## Mapa implementado actualmente

| Particion | Tamano | Bloques de 128 KiB | Uso |
|---|---:|---:|---|
| `history` | 192 MiB | 1536 | Medicion periodica compacta. |
| `waveEvents` | 40 MiB | 320 | Una captura Wave por bloque; hasta 320 eventos. |
| `gridEvents` | 6 MiB | 48 | Eventos de red y sus muestras Fast RMS. |
| `diagnostics` | 16 MiB | 128 | Registro circular de diagnostico. |
| `reserved` | 2 MiB | 16 | Metadata redundante, bitmap y margen futuro. |

El mapa definitivo se escribe durante el formato. En una NAND de 1 Gbit el
layout debe adaptarse explicitamente a la geometria detectada; nunca se debe
asumir que existen 256 MiB.

## Compatibilidad del historico

La especificacion completa es `energy_flash_format_v1_1.md`. El unico cambio
posterior fue compactar `group_config`: voltaje BASIC/FULL queda en el bit 0 y
corriente pasa a ser obligatoria. Los campos posteriores se desplazan dos bits;
el orden y las codificaciones del payload no cambian. V1 incluye todos los
grupos opcionales definidos, los modos COMPACT/FULL y los ocho selectores de
energia. La cabecera permanece en 20 bytes con dos CRC16.

El perfil predeterminado usa `group_config=0x00C894`: voltaje BASIC, corriente,
potencias activa/reactiva por fase, factor de potencia, frecuencia FULL y
energia activa R/S/T mas reactiva total. Bateria, THD, angulos y los demas modos
de potencia/energia se pueden seleccionar sin cambiar de version. La muestra
maxima V1 ocupa 97 bytes.

Durante una actualizacion se escribieron temporalmente paginas `formatVersion=2`
con un bitmap distinto. La lectura conserva un decoder V2 unicamente para esas
paginas ya grabadas; las paginas nuevas usan V1. Cuando las V2 se reciclen en el
anillo, se puede eliminar ese decoder sin reformatear NAND.

El esquema de configuracion NVS es independiente del byte `formatVersion` de
cada pagina. Su migracion solo traduce las preferencias persistidas al bitmap
V1 compacto; no cambia el formato de las paginas ni crea otra version.

### Mapa objetivo con web A/B

Para trasladar la aplicacion web a la NAND se mantienen sin cambios las
posiciones de `history`, `waveEvents` y `gridEvents`. Los 16 MiB finales que
actualmente pertenecen a `diagnostics` se subdividen de esta forma:

| Region | Tamano | Bloques | Rango fisico |
|---|---:|---:|---:|
| `reserved` | 2 MiB | 16 | 0..15 |
| `history` | 192 MiB | 1536 | 16..1551 |
| `waveEvents` | 40 MiB | 320 | 1552..1871 |
| `gridEvents` | 6 MiB | 48 | 1872..1919 |
| `diagnostics` | 10 MiB | 80 | 1920..1999 |
| `webA` | 3 MiB | 24 | 2000..2023 |
| `webB` | 3 MiB | 24 | 2024..2047 |

La primera implementacion puede conservar el layout de metadata actual y crear
estas tres vistas como subregiones de la particion fisica `diagnostics`. Esto
permite probar WebFS sin reformatear ni mover datos de las otras particiones.
Una futura version de metadata puede declarar las tres regiones de forma
explicita manteniendo exactamente los mismos limites fisicos.

## Formateo logico

El formateo se inicia de forma explicita con:

```text
POST /storage/format   body: confirm=FORMAT
```

La confirmacion exacta evita iniciar un borrado total por error. El proceso se
ejecuta en una tarea independiente y usa `NandIoService`; `MeterTask` no espera
lecturas de marcadores ni borrados NAND.

Secuencia:

1. Lee los marcadores de todos los bloques y crea el bitmap de bloques utilizables.
2. Borra cada bloque utilizable; un fallo de borrado excluye ese bloque del bitmap.
3. Programa y relee dos copias de metadata en los primeros dos bloques
   utilizables dentro de la reserva fisica `0..15`.
4. Valida sus CRC, carga la copia de mayor secuencia y habilita las particiones.

`GET /storage` informa `state`, bloque actual, bloques escaneados,
borrados, utilizables, malos y fallidos. Si se interrumpe antes de escribir una
metadata valida, el siguiente arranque queda como `unformatted` y requiere
volver a iniciar el formateo.

## Arquitectura

```text
MeterTask                         AsyncWebServer
   |                                    |
   v                                    v
Recorders                           ReadSession
- HistoryRecorder                      |
- WaveEventRecorder                    |
- GridEventRecorder                    |
   |                                    |
   +---------------+--------------------+
                   v
Domain Stores
- HistoryStore
- WaveEventStore
- GridEventStore
- DiagnosticStore
                   |
                   v
NandPartition
                   |
                   v
NandIoService
- highQueue
- mediumQueue
- lowQueue
- storageTaskLoop
                   |
                   v
WinbondSpiNand
                   |
                   v
NAND fisica y bus SPI compartido con pantalla
```

`StorageManager` es propietario y coordinador de estos objetos. No forma parte
del camino de cada lectura o escritura:

```text
StorageManager
|- NandIoService
|- metadata
|- badBlockBitmap
|- historyPartition
|- wavePartition
|- gridPartition
|- diagnosticPartition
|- HistoryStore
|- WaveEventStore
|- GridEventStore
`- DiagnosticStore
```

## NandIoService

`NandIoService` es la API publica de acceso fisico. Internamente contiene una
tarea FreeRTOS llamada `storageTaskLoop`, con nombre de tarea `nandIo`.

Una vez iniciado el servicio, ningun otro componente puede ejecutar lecturas,
programaciones o borrados directamente mediante `WinbondSpiNand`. La deteccion
del chip y la inicializacion basica del driver ocurren antes de iniciar la
tarea; todas las operaciones posteriores pasan por el servicio.

### Operaciones

```cpp
enum class NandOperation : uint8_t {
    readPage,
    programPage,
    eraseBlock,
    readBadBlockMarker
};

enum class StoragePriority : uint8_t {
    high,
    medium,
    low
};
```

La solicitud contiene como minimo:

```cpp
struct NandIoRequest {
    NandOperation operation;
    StoragePriority priority;
    uint16_t physicalBlock;
    uint8_t page;
    uint16_t column;
    uint8_t* data;
    size_t length;
    uint32_t requestId;
    QueueHandle_t completionQueue;
    void* context;
};
```

La finalizacion contiene como minimo:

```cpp
struct NandIoResult {
    uint32_t requestId;
    void* context;
    bool success;
    NandError error;
    size_t transferredBytes;
    uint32_t elapsedUs;
};
```

Los nombres y tipos pueden ajustarse al implementar, pero no debe cambiar el
contrato de propiedad y ejecucion descrito a continuacion.

### Prioridades iniciales

- `high`: cabecera, datos y cierre de una captura Wave; borrado urgente.
- `medium`: historico, eventos de red y Fast RMS.
- `low`: lecturas web, diagnostico y borrado anticipado.

La prioridad la decide el Store que conoce el motivo de la operacion.
`NandPartition` solo la transmite al servicio. El servicio termina la operacion
actual y luego selecciona la siguiente cola; nunca interrumpe una transferencia
NAND a la mitad.

Inicialmente se usan tres colas estaticas con capacidades configurables:

- alta: 16 solicitudes;
- media: 8 solicitudes;
- baja: 4 solicitudes.

Estas cantidades se validaran con las metricas reales antes de conectar la
captura Wave progresiva.

### Propiedad de buffers

- La estructura de solicitud se copia dentro de la cola.
- El contenido apuntado por `data` no se copia.
- El solicitante conserva el buffer, pero no puede modificarlo ni liberarlo
  hasta recibir `NandIoResult`.
- Cuando un Recorder entrega un buffer dinamico a su Store, el Store conserva
  su propiedad hasta completar todas las paginas asociadas y luego lo libera o
  lo devuelve a su pool.
- No se ejecutan callbacks de aplicacion dentro de `storageTaskLoop`; los
  resultados se entregan mediante una cola de finalizacion.
- `MeterTask` siempre envia con espera cero. Una cola llena produce un error
  inmediato y nunca detiene la adquisicion.

### Esperas y timeouts

- Los timeouts fisicos dependen de la operacion y de los limites del chip.
- Una NAND que no abandona el estado busy debe finalizar con timeout, registrar
  el error y permitir que el servicio siga procesando solicitudes.
- Una lectura web puede esperar su resultado mediante notificacion o semaforo;
  solamente queda suspendida la tarea web.
- Los Stores no mantienen sus mutex internos mientras esperan una operacion
  NAND.
- No se implementa cancelacion de una operacion que ya comenzo en esta primera
  version.

### Bus SPI

La NAND comparte el bus SPI con la pantalla. El mutex del bus es distinto de
las colas de almacenamiento:

- se toma solamente para emitir un comando o transferir datos;
- se libera mientras la NAND esta ocupada internamente, cuando el protocolo lo
  permita;
- cada dispositivo controla su propio CS y lo deja inactivo al liberar el bus;
- el ADE9000 utiliza otro controlador SPI y no participa de este mutex.

### Metricas minimas

- solicitudes aceptadas y rechazadas por prioridad;
- profundidad actual y maxima de cada cola;
- lecturas, programaciones y borrados completados;
- bytes leidos y escritos;
- errores y timeouts por operacion;
- duracion total, maxima y promedio por operacion;
- ultima operacion y ultimo error.

## NandPartition

`NandPartition` es una vista de una region fisica de la NAND. Se construye con:

- referencia a `NandIoService`;
- referencia de solo consulta al bitmap global de bloques;
- bloque fisico inicial;
- cantidad de bloques fisicos de la region;
- estado habilitado o deshabilitado.

Sus responsabilidades son:

- validar bloque, pagina, columna y longitud;
- convertir bloque relativo de particion a bloque fisico;
- rechazar accesos a bloques defectuosos;
- encontrar el siguiente o anterior bloque utilizable;
- transmitir operacion, prioridad, buffer y finalizacion a `NandIoService`;
- informar capacidad configurada y capacidad utilizable.

No decide prioridades, no interpreta formatos, no reconstruye anillos y no
decide si la memoria necesita formato.

Las operaciones usan indices relativos a la particion:

```cpp
submitReadPage(relativeBlock, page, column, data, length, priority, completion);
submitProgramPage(relativeBlock, page, data, length, priority, completion);
submitEraseBlock(relativeBlock, priority, completion);
isUsableBlock(relativeBlock);
nextUsableBlock(relativeBlock);
previousUsableBlock(relativeBlock);
```

Un acceso directo a un bloque defectuoso devuelve error. `readPage(10, ...)`
nunca se transforma silenciosamente en una lectura del bloque 11. El recorrido
de bloques se realiza explicitamente con `nextUsableBlock()` o
`previousUsableBlock()` para conservar identidades estables.

## StorageManager y metadata

`StorageManager` controla el ciclo de vida completo:

1. Inicializa y detecta la NAND.
2. Inicia `NandIoService`.
3. Lee la metadata desde bloques fisicos reservados.
4. Carga el layout y el bitmap global de bloques defectuosos.
5. Construye las particiones.
6. Inicializa los Stores y reconstruye sus anillos.
7. Expone estado, progreso de formato y estadisticas.

Estados iniciales:

```text
unformatted -> formatting -> ready
                    |
                    `-> formatError
```

Las particiones solo se habilitan en estado `ready`.

La metadata es un caso de arranque especial: como aun no existen particiones,
`StorageManager` accede a sus bloques fisicos reservados mediante
`NandIoService`. Lo mismo ocurre con la lectura de marcadores de bloques malos
durante el formato.

### Journal de metadata

- Se reservan dos bloques de borrado.
- Cada registro ocupa una pagina NAND completa y se programa una sola vez.
- Cada registro incluye tipo, secuencia, estado, layout, bitmap y CRC.
- Se agregan registros hasta llenar el bloque activo.
- Para rotar, se prepara el otro bloque, se escribe el nuevo registro valido y
  solamente despues se recicla el anterior.
- No se actualiza metadata por cada pagina o evento almacenado.

### Formato fisico inicial

El primer formato es destructivo y no intenta migrar datos:

1. Detectar geometria y marcadores de fabrica.
2. Escribir metadata valida con estado `formatting`.
3. Borrar todos los bloques utilizables de datos.
4. Registrar fallos nuevos en el bitmap.
5. Escribir metadata final con estado `ready`, layout y bitmap definitivos.

Si se interrumpe la energia y al arrancar se encuentra `formatting`, la memoria
no se monta y se debe reanudar o repetir el formato. Metadata ausente o con CRC
invalido se informa como `unformatted` o `formatError`; nunca se interpretan
datos antiguos sin una cabecera valida.

## Domain Stores

Los Stores conocen el formato y la politica de cada particion:

- `HistoryStore`: paginas historicas, anillo, busqueda por fecha y recuperacion.
- `WaveEventStore`: metadata, Fast RMS y bloques Wave de un evento.
- `GridEventStore`: transiciones de red y captura Fast RMS asociada.
- `DiagnosticStore`: registros de diagnostico y politica de nivel.

Los Stores pueden ser llamados desde `MeterTask` y desde la web. Protegen solo
su pequeno estado en RAM mediante mutex cortos. Nunca mantienen ese mutex al
esperar una finalizacion de NAND.

Los datos se publican a lectores solamente cuando alcanzan un estado valido:

- una pagina historica activa en RAM no se expone hasta programarse con CRC;
- una captura Wave activa no se descarga hasta su cierre;
- una captura parcial recuperada despues de un reinicio puede exponerse como
  parcial si sus fragmentos tienen CRC valido;
- una sesion web verifica secuencia y vigencia en cada fragmento;
- si el anillo recicla un registro durante la descarga, esta se aborta.

## Recorders

Los Recorders son maquinas de estado ejecutadas desde el contexto que les
entrega los datos; inicialmente no tienen tareas propias y nunca bloquean:

```cpp
historyRecorder.addSample(sample);
waveEventRecorder.beginEvent(metadata);
waveEventRecorder.addFastSample(sample);
waveEventRecorder.addWaveFrame(frame);
waveEventRecorder.endEvent(result);
gridEventRecorder.onTransition(transition);
```

`MeterTask` informa eventos y entrega copias o buffers terminados. Los
Recorders preparan el formato y los Stores encolan las operaciones NAND.
No se consulta periodicamente a `MeterTask` para descubrir eventos.

## Recuperacion de anillos

- Cada registro persistido contiene secuencia y CRC.
- No se guarda un cursor global despues de cada escritura.
- `HistoryStore` inspecciona primero una cabecera cada 16 bloques utilizables,
  acota la transicion y luego revisa los bloques y paginas cercanos.
- Los anillos Wave, grid y diagnostico son menores y pueden inspeccionar una
  cabecera por bloque al arrancar.
- La comparacion de secuencias debe contemplar desborde modular.
- Durante la reconstruccion se obtienen timestamp y bloque mas antiguo y mas
  nuevo para rechazar rapidamente consultas fuera de rango.

## Eventos Grid V1

`GridEventStore` reserva entre tres y seis paginas consecutivas de 2048 bytes
por incidente. La cantidad se toma de `gridSettings.capturePages` al abrir la
captura y queda escrita en su cabecera, por lo que pueden coexistir eventos de
distinto tamano. La pagina raiz se programa apenas se confirma la primera
transicion y las restantes al llenarse o al vencer la ventana Fast RMS, aun si
faltaron muestras. Asi un corte que
apague el medidor conserva como minimo el contexto previo y la transicion que
lo desencadeno.

- La captura se arma tras 150 ms de una condicion pendiente, sin escribir NAND.
- Una condicion que se cancela antes de confirmarse se descarta sin ocupar la
  particion.
- Con la tension rapida neutro-tierra se conservan hasta 113 muestras Fast RMS
  previas y entre 236 y 590 posteriores: aproximadamente 3.49 a 7.03 segundos
  totales a 100 Hz, sin aumentar las paginas configuradas para cada captura.
  Cada muestra ocupa 16 bytes: tiempo unsigned de 16 bits y ocho canales unsigned
  de 14 bits, LSB-first, en orden VR/VS/VT/IR/IS/IT/IN/VN. Escalas: 0.025 V y
  0.01 A; maximos 409.55 V y 163.82 A. 16383 es sin dato, tambien fuera de rango.
  La marca 0x20 en timeStatus identifica el empaquetado actual. Sin esa marca
  ni 0x10, el visor lee el original de 16 bytes con siete canales de 16 bits.
  La marca 0x10 corresponde al intento de 18 bytes descartado: el visor devuelve
  error de formato, sin modificar ni borrar las paginas originales. Sus cabeceras
  solo se reconocen en el catalogo/recuperacion para respetar el cursor escrito.
  VN queda sin dato si no hubo respuesta BL0942 en los ultimos 30 ms.
  Se reutiliza la lectura BL0942: no agrega consultas UART ni consultas web.
  Esto no agrega un disparador por sobretension neutro-tierra; queda pendiente.
  Validacion del intento de 18 bytes (ahora no admitido): prueba #115 con 520 muestras
  neutro-tierra entre 227.80 y 228.67 V, sin paginas invalidas. Captura anterior #113: 575 muestras,
  CRC correcto, sin canal neutro-tierra. El registro #1 tiene cinco paginas con CRC
  invalido; no se modificaron sus datos ni se omitio esa validacion.
  Validacion del empaquetado final de 14 bits: captura #118 con 584 muestras y CRC
  correcto, misma asignacion de paginas; #113 sigue legible y #115 se rechaza por
  formato de 18 bytes. El BL0942 continuaba sin responder: VN queda sin dato,
  no como cero ni como el ultimo valor recibido. Encoder/decoder verificados
  con patrones conocidos, maximos de escala, NaN y preservacion de canales vecinos.
- Una captura admite entre 24 y 60 transiciones confirmadas de fases R/S/T y
  del estado global de red. Cada transicion se almacena en la pagina temporal
  que le corresponde, sin modificar paginas ya programadas.
- El voltaje previo es la ultima lectura compatible con el estado confirmado
  anterior; durante una condicion pendiente ese valor se conserva. Los eventos
  anteriores a esta correccion pueden tener como voltaje previo una lectura
  inmediatamente anterior a la confirmacion, ya afectada por la caida.
- En un reinicio no electrico solo se crea captura inicial cuando el estado
  estable difiere del ultimo estado guardado en NVS; el tiempo previo figura
  como cero si no puede reconstruirse entre reinicios.
- Un evento nunca cruza un bloque NAND. Si no entra completo en las paginas
  restantes, comienza en el siguiente bloque utilizable; se desperdician como
  maximo cinco paginas pero la recuperacion ante cortes sigue siendo simple.
- `eventId` es un `uint32_t` reconstruido al iniciar mediante la cabecera raiz
  valida mas reciente; no existe un journal mutable por evento.
- Todos los enteros multibyte son little-endian. El formato se identifica con
  `formatVersion = 1` y usa CRC-16/CCITT-FALSE.

La cabecera compacta de pagina raiz ocupa 30 bytes e incluye `eventId`, Unix
seconds, milliseconds, sujeto y estados previo/actual empaquetados, estado de
validez temporal, duracion previa en unidades de 10 ms, voltajes previo/actual,
offset de la primera muestra, cantidad de paginas y CRC de cabecera. Le siguen
una copia fija de 48 bytes de `gridSettings`, un snapshot de 156 bytes de
mediciones y energia, las muestras previas y su CRC de cuerpo.

Cada pagina posterior comienza con `eventId`, indice, cantidad total de paginas,
cantidad de muestras, cantidad de transiciones y CRC. Reserva 12 transiciones
de 12 bytes; cada una guarda offset relativo, duracion previa en unidades de
10 ms, sujeto y estados empaquetados, y voltajes previo/actual en centesimas de
voltio. Las muestras Fast RMS comienzan en un offset fijo y permiten
recuperacion parcial sin reconstruir la captura completa.

## WebFS sobre NAND

La aplicacion web se almacena como una imagen inmutable. El ESP32 no crea,
elimina ni modifica archivos individuales: recibe una imagen completa y la
escribe en el banco inactivo. Este criterio elimina la necesidad de implementar
un sistema de archivos, un journal o una tabla mutable dentro del medidor.

### Principios

- Existen dos bancos independientes, `webA` y `webB`, de 3 MiB cada uno.
- Una actualizacion nunca modifica el banco que se esta sirviendo.
- El nuevo banco se activa solamente despues de escribirlo y verificarlo.
- Un corte durante la carga deja intacta la imagen anterior.
- Los archivos se identifican por su path completo normalizado; no existen
  entradas de directorio ni un arbol de carpetas.
- La imagen se transmite y programa por partes. Nunca se reservan 3 MiB de RAM.
- Las lecturas web y las actualizaciones usan prioridad baja en
  `NandIoService`; las capturas electricas conservan sus prioridades actuales.

### Generador compartido

La logica binaria se implementara una sola vez en JavaScript puro. El nucleo
recibe una lista de archivos con su contenido y metadata, y devuelve la imagen
`.fs32`. No accede directamente al disco ni al DOM.

Se utilizaran dos adaptadores pequenos:

1. Un adaptador Node.js recorre `data`, prepara la lista y genera la imagen para
   PlatformIO o para una carga manual.
2. Un adaptador web usa `File`/`ArrayBuffer` para generar la misma imagen desde
   una carpeta elegida en el navegador.

De esta manera el formato, los CRC y la compresion no se duplican entre Python,
Node.js y el navegador. El propio script compartido puede formar parte de
`data`; el adaptador debe excluir de forma explicita cualquier archivo auxiliar
que no deba publicarse.

Antes de empaquetar se aplican estas reglas:

- reemplazar `\\` por `/` y asegurar que cada path comience con `/`;
- rechazar `..`, paths repetidos y nombres que excedan el limite del formato;
- ordenar alfabeticamente por path para permitir busqueda binaria;
- detectar MIME mediante una tabla compartida;
- permitir activar o desactivar gzip para toda la generacion;
- cuando gzip esta activo, comprimir individualmente solo los archivos donde
  reduzca el tamano; nunca comprimir la imagen completa;
- verificar que la imagen entre en la capacidad util informada por el medidor.

La compresion individual permite servir directamente cada archivo con
`Content-Encoding: gzip`, sin descomprimirlo en el ESP32 y sin perder acceso
aleatorio. El navegador realiza la descompresion de forma transparente; la
aplicacion web no necesita cambios. El generador debe informar tamano original,
tamano almacenado y porcentaje ahorrado para poder comparar ambas variantes.

La primera prueba NAND se realizara con gzip desactivado. Una vez validada la
lectura desde NAND se generaran ambas imagenes y se compararan tamano y tiempo
de carga. El campo `encoding` existe desde V1 aunque inicialmente todas las
entradas usen el valor `none`.

### Formato de imagen V1

La imagen contiene tres zonas consecutivas:

```text
ImageHeader | FileEntry[fileCount] | padding | file data
```

`ImageHeader` es fijo para la version 1 e incluye como minimo:

- identificador `FS32` y `formatVersion`;
- cantidad de archivos;
- offset de la tabla y de los datos;
- tamano real de la imagen;
- fecha Unix de generacion;
- CRC32 de la tabla y CRC32 del contenido;
- CRC32 de la propia cabecera.

El identificador no es un nombre decorativo: evita interpretar datos de
diagnostico, memoria borrada o una version incompatible como una imagen web.

Cada `FileEntry` ocupa 128 bytes en V1:

| Campo | Tipo | Uso |
|---|---|---|
| `path` | `char[96]` | Path UTF-8 terminado en cero. |
| `dataOffset` | `uint32_t` | Posicion del contenido dentro de la imagen. |
| `storedSize` | `uint32_t` | Bytes realmente almacenados. |
| `originalSize` | `uint32_t` | Tamano antes de comprimir. |
| `modifiedTime` | `uint32_t` | Fecha Unix de modificacion, si esta disponible. |
| `dataCrc32` | `uint32_t` | CRC del contenido almacenado. |
| `mimeType` | `uint16_t` | Codigo MIME de una tabla comun. |
| `encoding` | `uint8_t` | Ninguno o gzip. |
| `flags` | `uint8_t` | Reservado para atributos del archivo. |
| `reserved` | `uint8_t[8]` | Debe escribirse en cero. |

No se almacenan propietario, permisos ni fecha de creacion porque no son
portables entre Windows, Node.js y el navegador y no intervienen al servir el
archivo. El path, los tamanos, MIME, encoding, fecha de modificacion y CRC son
los atributos funcionales necesarios.

### Confirmacion de banco

La secuencia de activacion no forma parte del archivo `.fs32`, porque pertenece
al medidor y no al contenido generado. Cada banco reserva su ultima pagina
fisica util para un `BankCommit` escrito por el ESP32 al final de la carga.

El registro contiene:

- identificador y version;
- secuencia `uint32_t`;
- tamano y CRC32 de la imagen;
- fecha de activacion;
- CRC32 del propio registro.

La capacidad nominal de cada banco es, por lo tanto, 3 MiB menos una pagina.
Si existen bloques defectuosos, `WebBundleStore` recorre los bloques utilizables
del banco y reduce la capacidad publicada. No se crea una tabla de traduccion
permanente: como cada banco tiene solo 24 bloques, convertir un bloque logico
en fisico mediante el bitmap es barato y queda encapsulado en el Store.

Al iniciar se leen los commits de ambos bancos:

1. Se descartan commits ausentes, incompatibles o con CRC invalido.
2. Se valida la cabecera de la imagen asociada.
3. Se elige la secuencia valida mas reciente con comparacion modular.
4. Si ningun banco es valido, WebFS queda desmontado. La ruta principal puede
   mostrar la recuperacion embebida, pero los demas archivos NAND devuelven
   `404`.

### Actualizacion atomica

`POST /webfs/image` recibe la imagen como cuerpo binario. Solo se admite
una actualizacion simultanea.

Secuencia de carga:

1. Leer y validar la cabecera recibida antes de borrar nada.
2. Elegir el banco que no esta activo.
3. Borrar todos sus bloques utilizables para eliminar datos y commits viejos.
4. Programar la imagen progresivamente mientras se calcula su CRC32.
5. Releer y validar cabecera, tabla y CRC completo.
6. Escribir `BankCommit` con `activeSequence + 1` como ultima operacion.
7. Cambiar el banco activo en RAM para las solicitudes nuevas.

Una sesion HTTP ya iniciada conserva el banco y la secuencia con los que
comenzo. La siguiente actualizacion se rechaza con `409 Busy` mientras exista
una lectura que use el banco que se pretende borrar.

La vuelta a la version anterior puede invalidar solamente el commit del banco
actual; el banco anterior permanece completo. Esta operacion se expondra como
una accion explicita y protegida, no como parte del flujo normal de carga.

### Lectura y endpoints

`WebBundleStore` queda sobre `NandPartition` y usa `NandIoService`; no accede al
driver directamente. La tabla ordenada permite buscar un path con lecturas
pequenas y transmitir el archivo en fragmentos de una pagina NAND.

Endpoints iniciales:

```text
GET  /webfs
GET  /webfs/image
POST /webfs/image
POST /webfs/rollback   body: confirm=ROLLBACK
GET  /recovery
```

Los campos de las operaciones POST se envian como `application/x-www-form-urlencoded` en el cuerpo, no en la query string.

`GET /webfs` informa si WebFS esta montado, el banco activo, su secuencia y la
validez y capacidad de los bancos A y B. La web se sirve siempre desde el banco
NAND activo; SPIFFS ya no forma parte de la tabla de particiones interna.
`GET image` descarga la imagen NAND activa para inspeccion o respaldo. Estos
endpoints y `/recovery` pertenecen al firmware y no dependen de WebFS.

### Origen fijo y recuperacion

Todos los recursos estaticos se buscan exclusivamente en el banco WebFS activo
de la NAND. Si WebFS no esta montado o no contiene el archivo pedido, la
solicitud devuelve `404 Not Found`, excepto en las dos rutas de entrada:

```text
GET /
GET /index.html
```

Si NAND no esta disponible o falta `index.html`, esas rutas devuelven la pagina
de recuperacion compilada dentro del firmware. Todos los demas recursos ausentes
(`.js`, `.css`, imagenes, fuentes u otros HTML) responden siempre `404`; nunca
se devuelve la pagina de recuperacion en su lugar porque el navegador podria
interpretarla con un tipo incorrecto y ocultar el fallo real.

La recuperacion tambien queda accesible explicitamente mediante
`GET /recovery`, aunque ambos almacenamientos funcionen. Es una pagina pequena,
autocontenida y sin dependencias externas. Se compila como un unico literal
HTML en `PROGMEM`, con CSS y JavaScript minimos en linea. No usa Bootstrap,
imagenes, fuentes, iconos ni archivos del almacenamiento que intenta reparar.

Su contenido inicial se limita a:

- estado de WebFS en NAND;
- estado valido/invalido de los bancos NAND A y B;
- banco NAND activo, secuencia y tamano de su imagen;
- formulario para cargar una nueva imagen `.fs32` a NAND;
- progreso basico y resultado de validacion/carga.

No muestra listas de archivos, CRC individuales, fechas, metricas NAND ni
detalles internos. Esa informacion queda disponible en los endpoints para una
herramienta futura, pero no aumenta el firmware de recuperacion. El objetivo
inicial es mantener el HTML, CSS y JavaScript juntos en aproximadamente 4 a 6
KiB sin minificar; si el resultado real supera ese margen se simplifica la
presentacion antes de agregar funcionalidad.

La carga usa bancos A/B: la imagen activa solo se reemplaza despues de validar
la imagen nueva y confirmar el banco inactivo. Si la nueva imagen no es valida,
la activa sigue sirviendo la web; `/recovery` permite cargar otra imagen o hacer
rollback sin depender de los archivos instalados.

Para servir un recurso:

1. Normalizar el path y convertir `/` en `/index.html`.
2. Buscar su entrada en la tabla ordenada.
3. Abrir una lectura fijada al banco activo actual.
4. Enviar fragmentos sin construir el archivo completo en RAM.
5. Aplicar `Content-Type`, `Content-Encoding`, `ETag` y cache segun la entrada.

### Etapas de implementacion

1. Congelar constantes y estructuras V1, y probar el generador JavaScript en
   la PC con empaquetado, lectura y corrupcion intencional.
2. Crear las vistas `diagnostics` de 10 MiB, `webA` y `webB` dentro de los 16
   MiB actuales, sin cambiar aun la metadata global.
3. Implementar `WebBundleStore` y probar montaje y lectura con una imagen
   escrita mediante el endpoint de diagnostico.
4. Implementar la carga atomica al banco inactivo y las pruebas de corte en
   cada etapa: borrado, programacion, verificacion y commit.
5. Agregar `info`, descarga, rollback y la pagina de recuperacion embebida.
6. Implementado: servir los recursos desde el banco NAND activo y mantener la
   pagina de recuperacion embebida como entrada cuando no haya `index.html`.
7. Probar varias actualizaciones A/B, reinicios y descargas simultaneas con
   grabacion historica y eventos activa.
8. Comparar la misma imagen con gzip desactivado y activado.
9. Completado: retirar SPIFFS del layout interno del ESP32 y reasignar ese
   espacio a las dos aplicaciones OTA mediante la migracion serial documentada
   abajo. La web se sirve desde NAND y la recuperacion embebida permite cargar
   otra imagen.

### Migracion de la tabla interna del ESP32

Esta es la flash interna de 4 MiB del ESP32, no el layout de la NAND externa.
La tabla anterior compilada por PlatformIO, antes de retirar SPIFFS, era:

| Particion | Offset | Tamano |
|---|---:|---:|
| `nvs` | `0x9000` | `0x5000` |
| `otadata` | `0xE000` | `0x2000` |
| `app0` (`ota_0`) | `0x10000` | `0x140000` |
| `app1` (`ota_1`) | `0x150000` | `0x140000` |
| `spiffs` | `0x290000` | `0x160000` |
| `coredump` | `0x3F0000` | `0x10000` |

La tabla actual compilada por PlatformIO elimina `spiffs`, conserva NVS y
otadata en sus offsets, deja dos slots OTA de `0x1F0000` bytes cada uno y
conserva los 64 KiB finales de `coredump`:

```csv
# Name,   Type, SubType, Offset,   Size,      Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xE000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1F0000,
app1,     app,  ota_1,   0x200000, 0x1F0000,
coredump, data, coredump,0x3F0000,0x10000,
```

Cada aplicacion pasa de 1.25 MiB a 1.9375 MiB. Los offsets de aplicacion
respetan la alineacion de 64 KiB. Antes de congelar la tabla hay que comprobar
el binario real, el tamano fisico de flash y que ambos slots admitan el firmware.

La tabla de particiones no se actualiza mediante OTA: OTA escribe la aplicacion
inactiva y cambia `otadata`, pero no instala de forma segura un bootloader o una
tabla nuevos. Ademas, `ota_1` cambia de `0x150000` a `0x200000`, por lo que el
estado OTA anterior podria apuntar a la direccion equivocada. La migracion se
hace una sola vez por USB/serial con el entorno `serial-migration` de PlatformIO,
que escribe bootloader, tabla nueva, `boot_app0.bin` para iniciar en `ota_0`
y el firmware compilado con la tabla nueva. El entorno OTA habitual no cambia.

Secuencia de migracion:

1. Confirmar que la aplicacion completa carga desde NAND y que `/recovery`
   sigue disponible antes de migrar la tabla interna.
2. Guardar fuera de `.pio` la tabla y firmware actuales, el firmware nuevo y la
   imagen WebFS que permita recuperar el sitio. Registrar la version de PlatformIO.
3. Compilar `pio run -e serial-migration` sin cambiar el entorno OTA habitual.
4. Compilar y revisar la tabla generada, los offsets, el tamano de ambos slots y
   el binario. El upload serial debe inicializar `otadata` y programar `ota_0`.
5. Conectar por USB, mantener alimentacion estable y ejecutar
   `pio run -e serial-migration -t upload --upload-port COMx` sin
   borrar toda la flash. NVS queda intacto porque conserva offset y tamano;
   ahi viven credenciales, calibraciones, energia acumulada y configuracion.
6. Al reiniciar, verificar arranque, NVS, web desde NAND, endpoints y recovery.
   Luego hacer dos actualizaciones OTA consecutivas para probar ambos slots.

No ejecutar `erase_flash`: borraria tambien NVS. La region SPIFFS ya no esta
definida y su contenido se perdio al ocupar su espacio con las nuevas
aplicaciones; WebFS en NAND conserva la aplicacion web.
Antes de cargar, respaldar los 4 MiB internos por el mismo puerto serie y
verificar que `/webfs` muestre WebFS montado y un banco activo.
Desde PowerShell en la otra PC, reemplazar `COMx` por el puerto detectado:

```powershell
$pioPython = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
& $pioPython $esptool --chip esp32 --port COMx flash_id
& $pioPython $esptool --chip esp32 --port COMx read_flash 0x0 0x400000 esp32-backup-4m.bin
pio run -e serial-migration
pio run -e serial-migration -t upload --upload-port COMx
```

`flash_id` debe detectar 4 MiB y el respaldo debe medir 4.194.304 bytes.
Conservarlo fuera de `.pio`; el upload serial de este entorno escribe
bootloader (`0x1000`), tabla (`0x8000`), `boot_app0.bin` (`0xE000`) y
aplicacion (`0x10000`), sin escribir NVS (`0x9000`). No usar el entorno OTA
para esta migracion ni ejecutar `erase_flash`.
Si hay que recuperar la tabla anterior, se hace tambien por USB/serial con el
firmware anterior que quepa en el slot de 1.25 MiB. Esa vuelta no restaura los
archivos SPIFFS que ya hayan sido sobrescritos.

En V1 no se implementan edicion individual, borrado de archivos, carpetas,
journal ni actualizaciones diferenciales. Para cambiar un archivo siempre se
genera y carga una imagen completa.

## Formatos de datos pendientes

Estos puntos no bloquean la implementacion de `NandIoService`,
`NandPartition` y `StorageManager`, pero deben cerrarse antes de implementar
cada Store:

- layout binario del historico definido en `energy_flash_format_v1_1.md`:
  cabecera de 20 bytes, secuencia `uint24`, `group_config` de 24 bits,
  flags, CRC16 de cabecera y CRC16 de payload;
- layout de las cuatro paginas auxiliares del bloque Wave;
- CRC y cabecera de cada registro Wave de 8192 bytes;
- formato de diagnostico;
- protocolo HTTP definitivo de lectura historica desde NAND.

## Orden de implementacion

1. Implementar `NandIoService`, colas, finalizaciones, timeouts y metricas.
2. Probarlo con las operaciones de diagnostico existentes, sin particiones.
3. Implementar `NandPartition` con layout y bitmap inyectados para las pruebas.
4. Probar rangos, traduccion relativa y salto explicito de bloques malos.
5. Implementar `StorageManager`, journal de metadata y formato fisico.
6. Implementar `HistoryStore` y probar recuperacion con paginas sinteticas.
7. Conectar `HistoryRecorder` a las mediciones reales.
8. Implementar `WaveEventStore` primero con eventos sinteticos y luego con
   escritura progresiva desde la captura real.
9. Implementar `DiagnosticStore` y la lectura web de `GridEventStore`.
10. Agregar pruebas de concurrencia, reinicio y corte de energia.

Antes de conectar datos reales se revisan obligatoriamente:

- propiedad y liberacion de cada buffer;
- comportamiento con colas llenas;
- timeouts y recuperacion de errores;
- acceso exclusivo al driver;
- interaccion con el mutex SPI de la pantalla;
- reinicio durante programacion, borrado y formato.
