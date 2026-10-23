# Configuracion y estado de eventos

El firmware y la web usan una configuracion unica. No cambia el formato de las
capturas FAST ni Wave ni se formatea NAND. Estos cambios necesitan publicarse
juntos con las correcciones de lectura anteriores.

## Lectura

`GET /events/config` devuelve `rev`, `save` y seis grupos:

| Grupo | Campos | Unidades |
| --- | --- | --- |
| volt | lowR/S/T, highR/S/T, loss, hyst | V |
| time | dip, swell, oc, loss, back | ms |
| neut | high, hyst, wait, back | V y ms |
| curr | high, hyst | A |
| rec | dip, swell, oc | booleanos: registro puntual FAST |
| wave | on, pre, dip, swell, oc | habilitacion general, previo ms y seleccion |

`GET /events/config?group=wave` devuelve solo ese grupo junto a revision/guardado.
Los estados sostenidos siempre se registran; las habilitaciones rec afectan a
puntuales. La oscilografia requiere la habilitacion general y un enlace FAST
permitido. Cambiar wave.pre requiere reiniciar; la reserva no cambia en caliente.

Las respuestas usan JsonWriter con buffers externos de 1024 B para configuracion
y 768 B para estado. Si el documento no cabe, se devuelve un error, nunca JSON
truncado. No se concatenan Strings para generar estas respuestas.

## Guardado

`POST /events/config`, `Content-Type: application/x-www-form-urlencoded`:

```text
rev=7&volt.lowR=185&time.dip=50&rec.dip=1&wave.dip=1
```

Solo se requieren rev y al menos un campo. Los campos omitidos conservan su
valor confirmado. Se rechazan duplicados, campos desconocidos, valores no
finitos, decimales en tiempos y relaciones incompatibles entre umbrales.
No se parsean cuerpos JSON. GET no consulta NVS; POST no escribe NVS.

- 202: cambio validado y encolado; rev identifica el intento, aun no confirmado.
- 400: parametro, grupo o conjunto invalido; no cambia nada.
- 409 rev_conflict: la revision efectiva cambio; recargar antes de reintentar.
- 409 save_busy: ya hay otro cambio pendiente; no lo reemplaza.

El loop principal espera que no haya captura FAST/Wave activa y persiste un
registro completo, versionado y con CRC, en la clave NVS events/cfg. Solo tras
un putBytes exitoso publica la configuracion confirmada. Los ajustes de fase y
puntuales se aplican juntos al siguiente turno FAST; Wave actualiza sus flags
en RAM. No se agrega una tarea ni se escribe flash desde MeterTask o HTTP.
Esto evita aplicaciones parciales entre tres peticiones. No garantiza ausencia
de pausas del SDK si llega un evento justo durante una escritura interna NVS.

save.state vale ok, wait, save o fail; save.rev identifica el intento pendiente
o fallido. Un fallo conserva la revision y todo el conjunto anterior. Los IDs
de intentos no se reutilizan despues de fallos durante el mismo arranque.
La web consulta estado hasta confirmar la revision o mostrar fallo/espera.

## Estado

`GET /events/status` devuelve seq, cfg, save, ready, last, busy, grid, wave y lim.
Los contadores de la sesion se distinguen de la cantidad presente en NAND.
lim informa el maximo puntual y las paginas permitidas, sin convertirlos en
parametros modificables. wave.pre es el previo efectivo y wave.ram la reserva.

`GET /events/status?since=103` devuelve 204 sin cuerpo si el estado relevante no
cambio. seq no incluye uptime ni cantidad de slices, por lo que no cambia solo
por adquirir nuevas muestras. Se inicia con una referencia aleatoria por arranque.
Tambien cambia si se formatean las particiones de capturas, aunque no se haya
generado otro ID, usando sus generaciones de cache como parte del estado.

## Migracion

Al primer arranque sin registro unificado valido se importan los ajustes de
grid, meter y wave anteriores, sin escribirlos ni resetear sus valores validos.
El primer guardado unificado crea events/cfg. No se mantienen espejos NVS.
Los POST antiguos de grid/config, wave/config, options/reload y las opciones
de captura en options devuelven 410 use_events_config. Energia, calibracion y
configuracion de red mantienen sus propios caminos.

Los respaldos existentes por seccion conservan sus claves visibles; la web
traduce grid/capture/wave al servicio nuevo al leer y restaurar. La restauracion
de varias secciones sigue siendo secuencial, no una transaccion del respaldo
completo. El guardado normal de eventos y Wave ahora si usa una sola solicitud.

Pruebas: event_settings_policy (metodos reales con NVS simulada), events_client,
grid_web, settings_backup y navigation_web. La compilacion de firmware valida
la integracion; las pruebas simuladas no sustituyen una prueba real en la placa.

Artefactos preparados: firmware serial-migration con 117856 B de RAM estatica
y 1346065 B de flash; WebFS de 335872 B. Publicados el 2026-10-22:
firmware por COM4 y WebFS mediante /webfs/image. Configuracion comprobada
en navegador de escritorio y movil, sin cambiar ajustes ni borrar registros.
