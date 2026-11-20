# Visor historico y colores de aplicacion

## Graficas

Cada magnitud tiene una barra externa compacta: nombre/unidad, subir, bajar,
arrastrar, restablecer zoom y configurar escala. Se mueven entre graficas visibles
sin recrearlas ni consultar el medidor. El nombre ya no ocupa el eje Y.

Las areas de trazado comparten limites temporales y margen izquierdo de 82 px,
ajustado mediante la conversion publica de coordenadas CanvasJS. Se comprueba
tambien al cambiar escala, leyenda, zoom y tamano de pantalla. Las leyendas
mantienen seleccion independiente y la escala automatica no fuerza cero;
un neutro visible que mide cero si participa normalmente en esa escala.

Sincronizar vistas enlaza zoom, desplazamiento y reset de X en ambos sentidos.
Desactivado, cada grafica conserva su intervalo. El eje Y siempre es independiente:
automatico o minimo/maximo finitos con min < max. El zoom de una nueva descarga
se restablece; la escala configurada se conserva.

Selectores: tension RMS, corriente, tension entre fases, potencias activa/reactiva/
aparente, energia, FP, frecuencia, THD V/I, angulos, Vbat, Vbus, corriente de carga,
nivel de bateria y estados. Incluye potencia total de perfiles compactos. Las
variables ausentes no se convierten a cero ni generan curvas inventadas; una
grafica sin datos muestra un estado vacio. No cambia el formato binario.

Calendario: hoy/ayer/anteayer, esta semana/semana pasada (domingo a sabado), este
mes/mes pasado. Los finales calendario son 23:59:59; avanzar/retroceder conserva
el periodo calendario y sus cambios de mes. Ultimos 7 dias y ultimo mes movil
son independientes de los periodos calendario.

## Preferencias locales

`powerMeterHistoryView` conserva orden, paneles visibles, enlace y escalas.
`powerMeterAppearance` conserva R/S/T/N y opacidad en Configuracion > Aplicacion.
Iniciales: R marron #804000, S negro #000000, T rojo #e02020, N celeste #38bdf8;
opacidad 70%, configurable de 10 a 100%. Corrientes superpuestas usan una variante
mas clara y energias distinguen magnitudes tambien mediante trazos.

Los colores se reutilizan en historico, FAST, Wave, visor de historial,
osciloscopio, FFT y fasores; actualizar preferencias redibuja datos existentes,
sin descargarlos otra vez. Los colores de estados/alertas conservan su significado
propio. Todo queda en el navegador/origen: no usa NVS ni agrega RAM al ESP.
La carga de recursos sigue siendo secuencial, con bibliotecas cacheables.

## Verificacion

`node test/history_view_web.test.cjs` usa CanvasJS real con datos de prueba:
alineacion desktop/mobile, zoom enlazado e independiente, orden, escalas,
neutro oculto, bateria, colores y periodos calendario. Capturas en
`tmp/history-view-desktop.png` y `tmp/history-view-mobile.png`.

Web publicada en .90: imagen WebFS de 536576 bytes, sin cambiar firmware ni
formatear registros. Carga real secuencial y 17 paneles comprobados en Chrome
mobile. Prueba de cache desplegada: 18432 bytes, 324 registros, un acierto de
cache, cero descargas binarias adicionales y cero errores JS.
El primer intento de navegador agoto 60 s al navegar; las comprobaciones HTTP y
las dos aperturas posteriores funcionaron correctamente. No se atribuye una
causa a ese timeout ni se considera resuelto el problema previo de estabilidad.

Tambien pasaron formato historico, cache web, bundle, tooltips desktop/mobile,
FFT, calidad, configuracion en vivo y eventos unificados. La prueba de respaldo
se actualizo para cargar dependencias de Aplicacion y comprobar la politica ya
existente de continuar importando otras secciones tras un error; no se cambio
la implementacion de importacion.
