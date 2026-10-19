# Convenciones HTTP

El firmware y la aplicacion web se distribuyen juntos. Las rutas no necesitan versionado ni compatibilidad con rutas anteriores, salvo que se solicite expresamente para conservar acceso a datos persistidos.

## Rutas

- Usar nombres directos agrupados por recurso, sin prefijos `/api` ni `/v1`.
- El `GET` del recurso raiz devuelve su estado, resumen o listado principal.
- Usar subrutas para configuracion, estimaciones, datos binarios y operaciones concretas.
- No juntar en una misma respuesta representaciones distintas solo para reducir la cantidad de rutas.

Ejemplo para historico:

| Metodo y ruta | Uso |
| --- | --- |
| `GET /history` | Informacion actual: rango, muestras y paginas disponibles |
| `GET /history/config` | Leer solo la configuracion: `enabled`, `samplePeriod` en segundos, modos y grupos |
| `POST /history/config` | Actualizar la configuracion con opciones descriptivas en el cuerpo |
| `GET /history/estimate?...` | Estimar capacidad y `duration` en segundos para las opciones indicadas |
| `GET /history/data?from=...&to=...` | Descargar el rango en formato binario |

La configuracion y la estimacion usan los mismos campos: `samplePeriod`, `voltageMode`, `activePower`, `reactivePower`, `apparentPower`, `powerFactor`, `voltageThd`, `currentThd`, `frequency`, `voltageAngles`, `battery`, `energy` y `energyExtraMode`. Los modos aceptan texto (`basic`/`full` o `off`/`compact`/`full`); los booleanos aceptan `true`/`false`. No se expone el bitmap interno del formato.

Otros recursos mantienen el mismo criterio:

| Metodo y ruta | Uso |
| --- | --- |
| `GET /events` | Listar capturas temporales en RAM |
| `POST /events/test` | Solicitar una captura de prueba; `pattern` va en el cuerpo |
| `GET /events/fast-rms?eventId=...` | Descargar Fast RMS |
| `GET /events/wave?eventId=...` | Descargar Wave |
| `GET /events/wave-diagnostics` | Metricas del stream Wave y fragmentos de la ultima captura |
| `POST /events/clear` | Liberar una captura temporal |
| `GET /grid-events` | Catalogo y estado de eventos persistidos |
| `GET /grid-events/list?...` | Listar eventos por rango o cantidad reciente |
| `GET /grid-events/data?eventId=...` | Descargar un evento binario |
| `POST /grid-events/test` | Solicitar una captura Grid de prueba |
| `GET/POST /network/config` | Leer o guardar la configuracion de red |
| `GET /network/scan` | Consultar el resultado del escaneo Wi-Fi |
| `GET/POST /grid/config` | Leer o guardar umbrales de red |
| `GET/POST /options` | Leer o guardar opciones NVS por namespace |
| `POST /options/reload` | Recargar opciones aplicadas al medidor |
| `POST /calibration/restore` | Restaurar la calibracion por defecto |
| `GET/POST /clock` | Consultar o ajustar la hora del sistema |
| `POST /energy/reset` | Reiniciar los contadores de energia |
| `GET /webfs` | Estado de los bancos NAND que contienen la web |
| `GET/POST /webfs/image` | Descargar o cargar la imagen WebFS |
| `POST /webfs/rollback` | Volver al banco WebFS anterior |
| `GET /manager/discovery` | Identidad, capacidades y enlaces del dispositivo |
| `GET /manager/status` | Estado de ejecucion para el manager |
| `GET /telemetry` | Telemetria actual |
| `POST /restart` | Reiniciar el dispositivo |
| `GET /status` | Estado resumido para la web local |
| `GET /meter?...` | Mediciones o contadores solicitados |
| `GET /ping` | Comprobar conectividad HTTP |

Ejemplo para almacenamiento:

| Metodo y ruta | Uso |
| --- | --- |
| `GET /storage` | Estado y distribucion de NAND |
| `POST /storage/format` | Formatear almacenamiento |
| `POST /storage/test` | Ejecutar una prueba de bloque NAND |
| `GET/POST /storage/benchmark` | Consultar o iniciar una prueba de rendimiento |
| `POST /storage/benchmark/cancel` | Cancelar una prueba de rendimiento |
| `GET /storage/test-download?size=...` | Descargar datos sinteticos para medir HTTP |

## Parametros y cambios

- Usar query parameters para filtros, rangos y opciones de lectura.
- Enviar cambios y comandos `POST` en el cuerpo de la solicitud; usar cuerpo binario cuando se carga un archivo.
- No duplicar en JSON datos que se puedan derivar de otros campos ni incluir campos ajenos al proposito del endpoint.
- Al cambiar una ruta, actualizar en el mismo cambio el firmware, la aplicacion web, la interfaz de recuperacion, los enlaces generados, las pruebas y la documentacion. No conservar la ruta anterior como alias salvo pedido explicito.
- La aplicacion web se sirve siempre desde el banco NAND activo; la pagina embebida solo rescata `/` y `/index.html` si WebFS no esta disponible.
- Las rutas de recursos y subrecursos deben rechazar caminos adicionales no registrados, no devolver silenciosamente la respuesta de un prefijo.
