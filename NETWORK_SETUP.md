# Red y hora

La pestaña Configuracion > Red y hora reutiliza `wifi-stuff`, el escaneo
asincrono y los endpoints de configuracion existentes.

- `GET /network/config?view=setup`: campos de Wi-Fi, IP, AP y NTP, mas estado
  de conexion. No entrega las contraseñas; `staHasPass` y `apHasPass` indican
  si ya existe una clave. Usa un JsonWriter con buffer externo de 1536 bytes.
- `POST /network/config`: formulario `application/x-www-form-urlencoded`, con
  las claves NVS existentes. Valida SSID, claves, IP/subred, DNS, zona horaria
  y nombre NTP antes de escribir. Aplica solo los grupos que cambiaron desde
  el loop principal: Wi-Fi reconecta y NTP/zona horaria se reconfiguran sin
  reiniciar. Las claves sin cambios no se vuelven a escribir.
- `GET /network/status`: estado `apply` (`wait`, `apply`, `ok`, `fail`) y
  direcciones actuales. No lee NVS. La web espera este estado al guardar.
- `GET /network/scan`: mantiene el protocolo running/completed/failed; ahora
  escapa los SSID mediante JsonWriter para admitir comillas y otros caracteres.

Una contraseña omitida conserva la guardada. Una contraseña vacia solicita
explicitamente una red abierta; la interfaz tiene casillas separadas para ello.
No se copia una clave guardada a otra red al cambiar el SSID sin contraseña.
Las claves de AP nuevas requieren 8-63 caracteres o vacio; estacion admite
tambien una PSK hexadecimal de 64 caracteres. NTP admite hasta 47 caracteres.

El GET sin view=setup se conserva para respaldos y sigue incluyendo credenciales.
El servicio no agrega autenticacion HTTP: no debe exponerse sin los controles
de acceso correspondientes. Un AP abierto permite acceder a la configuracion.

## Recuperacion

En modo estacion, tras 30 s sin conexion se abre el AP configurado en modo
AP+STA. Sin SSID se abre desde el primer ciclo. Se usa la clave del AP guardada;
si esta vacia, el AP es abierto. El AP de recuperacion siempre es visible.
Su IP es 192.168.4.1 salvo que se configure una direccion AP personalizada.
La direccion estatica de la estacion no se reemplaza por DHCP.

Se sigue intentando conectar la estacion. Cuando lleva 30 s conectada y no hay
clientes del AP, se cierra el AP temporal. Si hay clientes no se los desconecta.
El modo AP o AP+STA manual se mantiene; Wi-Fi apagado se respeta y no abre AP.
Los reintentos fallidos de apertura esperan 30 s. No hay esperas bloqueantes ni
tareas nuevas para esta politica. No es un portal cautivo ni redirige DNS: se
abre la IP del AP en el navegador.

Al cambiar Wi-Fi desde la web se abre un AP temporal en modo estacion para
recuperar el acceso si la nueva red no conecta. Cambiar SSID/IP puede cortar
la sesion HTTP; se debe abrir la nueva direccion o conectarse al AP.
El servidor web tambien arranca sin IP de estacion y puede atender el AP.

La hora sigue arrancando desde RTC/NVS. NTP se inicia al obtener una IP y no
solo al asociarse al router; tambien contempla una conexion ya disponible.
Las correcciones NTP siguen sincronizando el RTC. Desfase UTC en minutos:
Argentina -180; horario de verano 0.

Wi-Fi, AP, NTP y RemoteDebug leen NVS al conectar o reconfigurar sus servicios,
sin conservar copias permanentes de credenciales, direcciones o nombres.
Abrir setup tambien consulta NVS; status solo publica estado RAM.
Radio y NTP tienen bits pendientes independientes. Los accesos
a NVS de red se serializan para no leer un formulario parcialmente escrito.
OTA, MQTT y RemoteDebug conservan la aplicacion al reiniciar.

Pruebas: `wifi_recovery_policy`, `network_settings_web`, `navigation_web`,
`grid_web` y `settings_backup`. La politica de desconexion se simula con el
codigo C++ real; no sustituye probar fisicamente una perdida de Wi-Fi.

Primera publicacion el 2026-10-23: firmware por COM4 y WebFS de 434176 bytes. Verificado
en escritorio y movil contra la placa: lectura de ajustes, claves ocultas y
escaneo real sin guardar cambios. Se conserva la IP estatica 192.168.1.90.
Un POST con clave invalida devolvio 400 sin alterar los ajustes leidos.
RAM estatica: 117872 bytes (+16); flash de programa: 1352369 bytes. El AP real
requiere memoria adicional del controlador Wi-Fi cuando se habilita.

## Bloqueo de red observado el 2026-10-28

Antes de reiniciar: /status local no establecia conexion y Cloudflare respondia
502. COM4 seguia entregando heartbeat (uptime 24824 s): Wi-Fi conectado,
RSSI -36 dBm, free8 cercano a 28 KiB, largest8 13812 bytes y min8 3408 bytes.
AsyncTCP repetia avisos de coalescing polls; MQTT tambien perdia conexion.
Esto evidencia un problema de comunicacion mientras el programa seguia activo,
pero no identifica la causa raiz ni demuestra por si solo un fallo de RAM.

Se reinicio por RTS de COM4, sin borrar configuracion ni actualizar firmware.
Arranco con la misma IP 192.168.1.90. Log guardado en
logs/network-recovery-2026-10-28.log; observacion posterior en
logs/network-cloudflare-load-2026-10-28.log.

Despues: /status local respondio en 26 ms; app.js externo completo en 1,89 s.
Chrome nuevo por Cloudflare cargo toda la app e inicio descargas secuenciales
del historial sin errores JavaScript; app.js 1,64 s y /status 109-173 ms durante
esa prueba. Al terminar, /status externo seguia respondiendo (407 ms, libre
60076 bytes). La recuperacion esta comprobada; la causa del bloqueo permanece
pendiente. No se cambio la carga secuencial ni se agregaron reinicios automaticos.
