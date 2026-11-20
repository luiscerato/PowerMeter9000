# Aplicacion de configuracion

Los consumidores frecuentes conservan sus valores activos en RAM. Guardar marca
solamente sus consumidores; consultar el estado de aplicacion no abre NVS. Los POST siguen usando
`application/x-www-form-urlencoded`.

| Grupo | Aplicacion |
| --- | --- |
| Umbrales y habilitaciones de eventos | Guardado diferido; publica la copia al medidor en el siguiente turno FAST. |
| Habilitaciones de oscilografia | Controles RAM despues de guardar, sin reinicio. |
| Tiempo previo de oscilografia | Reserva de arranque; `restartRequired` distingue pendiente de reinicio. |
| Historico | Solicitud propia; cierra la pagina, persiste y aplica desde el loop del recorder. |
| Wi-Fi, IP y AP | Lee NVS al conectar/reconfigurar, con AP temporal de recuperacion y sin copia permanente. |
| NTP y zona horaria | Consumidor separado; no reconecta Wi-Fi ni vuelve a tomar la hora inicial del RTC. |
| Calibracion automatica BL0942 | Referencia R/S/T del ADE, promedio de un segundo y factores RMS/FAST activos al guardar. |
| Registros ADE, restauracion e importacion de calibracion | Mantienen reinicio. |
| MQTT, OTA y RemoteDebug | Mantienen reinicio. |

## Historico

`POST /history/config` acepta una solicitud y responde 202. El GET entrega el
perfil activo y `apply`: `ok`, `wait`, `save`, `fail`. Otra solicitud pendiente
recibe 409. Se espera a terminar capturas FAST/Wave y a poder cerrar la pagina.
El formulario espera la confirmacion antes de informar que el perfil esta activo.

El perfil se guarda con una unica escritura de 8 bytes en `history/cfg`.
Al arrancar se recuperan las claves anteriores si todavia no hay ese registro.
Si falla NVS se mantiene el perfil RAM anterior. Ninguna de estas operaciones
cambia el formato de las muestras o de las paginas NAND.

## Lecturas NVS

Los umbrales FAST de estados, DIP, swell y sobrecorriente se comparan desde
RAM; solo se actualizan al consumir un cambio. La calibracion ADE se carga al
inicio y se aplica al integrado. Estos caminos no consultan NVS por muestra.
Eventos, logger, BL y energia conservan su carga inicial y cambios manuales.
Wi-Fi, AP, NTP y RemoteDebug leen sus claves al conectar/reconfigurar y liberan
los temporales al salir. Metadata del manager, setup y respaldos leen al ser
solicitados. Se eliminaron las copias permanentes de estas opciones poco usadas.

## Verificacion

- `test/runtime_settings_policy.test.cjs`: metodos C++ reales, marcas separadas,
  solicitudes concurrentes, fallo de lectura/escritura, captura activa y wrap.
- `test/runtime_settings_web.test.cjs`: calibracion BL0942 y perfil historico,
  URL-encoded, espera de aplicacion y fallo NVS en escritorio/movil.
- `test/network_settings_web.test.cjs`: red abierta, claves omitidas, escaneo
  y espera de aplicacion sin reinicio con API simulada.

Las pruebas simuladas no validan la transicion fisica entre redes ni realizan
una calibracion real. Cambiar firmware requiere cargar tambien la web nueva.

Compilacion del 2026-10-23: RAM estatica 117928 bytes (+56 respecto de la
version anterior). Se recuperaron 808 de los 864 bytes de las copias iniciales.
WebFS generado tras la auditoria de configuracion: 448512 bytes.
Esta revision fue compilada y probada con simulaciones; no se cargo a la placa.

## Guardado y respaldos verificados

El ESP escribe y relee cada registro NVS antes de confirmarlo. La web espera que
el ajuste este activo y compara el perfil publicado con lo enviado; los errores
no incluyen contrasenas. Los float se comparan a precision float32. `/options`
devuelve `verified` para evitar una consulta GET redundante.

Wi-Fi se guarda con `POST /network/config?defer=1`; el ESP confirma la lectura
posterior de NVS y solo entonces se solicita `POST /network/apply` con `services` URL-encoded. Si cambia
la direccion y se pierde la conexion, la web no afirma que termino la aplicacion.
Los clientes que no piden `defer` conservan la aplicacion diferida automatica.

Los respaldos JSON permiten elegir secciones en ambas direcciones: red/servicios,
estados, deteccion, Wave, historico, energia y calibracion. Se validan todas las
secciones elegidas antes de escribir; se aplican en serie, con red al final.
Cada seccion se verifica y su resultado queda en la lista. Si una validacion,
guardado o lectura falla, se registra y se continua con las demas. No hay
rollback de una escritura parcial ni de las secciones que ya terminaron.
Los controles quedan bloqueados durante la operacion y las vistas se invalidan
al terminar para que no presenten ajustes anteriores.

Energia exporta los totalizadores persistidos de `meter`, no los contadores vivos.
Energia/calibracion importadas requieren reinicio; la web no lo hace sola.
El JSON incluye credenciales en texto plano: debe guardarse como informacion privada.
Se rechazan archivos mayores a 256 KiB, secciones/campos desconocidos, rangos
invalidos y perfiles historicos con bits incompatibles.

Se comprobaron los limites de la web contra firmware: confirmacion puntual
10..2000 ms en pasos de 10; Wave 48..288 ms en pasos de 48; historico 0.1..300 s;
OC hasta 163.82 A; limites de tension hasta 500 V y neutro hasta 409.55 V.
Las relaciones entre limites e histeresis tambien se validan. MQTT limita textos
a los buffers reales (servidor 63 bytes, usuario/clave 31) y puerto 1..65535.
La restauracion de calibracion verifica cada clave NVS antes de responder.

Pruebas adicionales: `settings_validation`, `settings_backup` y `events_client`
cubren rangos, booleanos, float32, seleccion de secciones, discrepancia de
lectura y registro secuencial. `grid_web`, `navigation_web`,
`network_settings_web`, `runtime_settings_web` y `web_bundle` verifican la web.
Firmware: RAM estatica 117928 bytes, flash 1357533 bytes. No se modificaron
umbrales, red ni calibracion de la placa para estas pruebas.
