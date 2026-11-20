# Ocupacion estimada de CPU

`GET /status` agrega `cpu: {"c0": 32.5, "c1": 48.1}`. Los numeros son porcentajes
estimados por nucleo, no una suma de ambos. `null` indica primera ventana pendiente
o callback no disponible. No hay endpoint ni consulta adicional.

Se registra un callback del tick existente de FreeRTOS en cada nucleo. Cada tick
compara la tarea interrumpida con la tarea idle del mismo nucleo. Publica una
ventana no solapada de dos segundos de ticks (2000 muestras con este SDK).
Consultar status no reinicia ni dispara mediciones. No se crean tareas, timers,
buffers dinamicos ni mensajes desde el callback. Solo el resultado terminado se
copia bajo una seccion critica breve, una vez por ventana y nucleo.

Es muestreo estadistico, no contabilidad exacta del scheduler. Puede tener sesgo
si el trabajo se sincroniza con el tick; no separa el costo de las ISR y tampoco
mide interrupciones atrasadas mientras estaban deshabilitadas. Incluye tareas del
sistema, WiFi y bucles de sondeo: un loop que gira sin bloquear cuenta como ocupado
aunque consulte cosas que no cambiaron. Por eso 100% no demuestra por si solo que
se incumplen los tiempos de adquisicion. Complementar con `/meter?timing`, huecos
Wave y capturas descartadas. No cambia el comportamiento ni agrega esperas al loop.

Validacion: `node test/cpu_usage.test.cjs`. Compilar con el SDK del proyecto y
verificar tambien que cpuUsageTick/record y xTaskGetCurrentTaskHandle queden en
IRAM antes de desplegar, para poder atender ticks durante operaciones de flash.
