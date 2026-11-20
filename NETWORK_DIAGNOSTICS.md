# Diagnostico de red

Los logs persistentes y recientes distinguen `wifi`, `mqtt`, `tcp` y `health`.
El endpoint `GET /debug` publica los ID de modulo y `POST /debug/config`
permite cambiar su nivel con `moduleId` y `level`; `GET /debug/recent` muestra
el anillo reciente.

Wi-Fi y MQTT registran conexion y desconexion, con desconexiones limitadas a
un aviso por minuto. AsyncTCP ya no vuelca cada poll congestionado por Serial:
cuenta coalescencias, descartes de poll y errores lwIP, y escribe un resumen
`tcp` con el pico de cola. Los avisos urgentes esperan al menos 10 s entre
resumenes; en reposo se revisan cada 60 s.

Cuando el servidor web inicia, queda programada una consulta local a
`GET /ping`: la primera a los 20 s y luego cada hora. Corre en una tarea de
baja prioridad, con hasta 1 s para conectar y 1 s para recibir respuesta; no
bloquea el loop ni la adquisicion. Si no hay IP de estacion usa la IP del AP.
El resultado incluye codigo HTTP, demora e IP en `health`. No consulta Internet
y no reinicia la placa automaticamente: comprueba el recorrido local Wi-Fi,
TCP y servidor HTTP.

Verificado en la placa tras OTA: `/ping` respondio 200 y la autoprueba quedo
registrada con 24 ms; `/debug` mostro los siete modulos y NAND estaba activa.
