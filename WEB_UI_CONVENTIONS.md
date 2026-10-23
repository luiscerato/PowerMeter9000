# Seleccion rapida de senales

Criterio acordado para reutilizar en los distintos visores de la web:

- Los accesos rapidos REEMPLAZAN la seleccion; no son interruptores de grupo.
- R, S, T o N muestran solo el voltaje y la corriente de esa fase/neutro.
- V muestra solo los cuatro voltajes; I muestra solo las cuatro corrientes.
- Repetir un acceso rapido conserva esa seleccion. No apaga ni acumula series.
- Los botones individuales R/S/T/N dentro de V o I alternan solo su serie,
  sin modificar las restantes. Las leyendas funcionan del mismo modo individual.
- Para combinar fases: seleccionar R y agregar V S e I S individualmente.
  Pulsar despues el acceso S debe dejar solamente V S e I S.
- Los indicadores reflejan las series visibles: un acceso rapido esta activo
  cuando la seleccion coincide exactamente; una coincidencia parcial se distingue.
- Cambiar senales conserva datos descargados, zoom, tramo y modo de ventana.
  Senales no capturadas no se inventan ni se representan como cero.

Primera aplicacion: barra compacta del visor del Historial. No incluye acceso RST.
