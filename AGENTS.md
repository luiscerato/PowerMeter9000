# Instrucciones del proyecto

## Codigo

- Generar codigo compacto, simple y legible para una persona con conocimientos de programacion.
- Usar lowerCamelCase para codigo nuevo.
- Al crear una funcion, agregar un comentario corto sobre lo que hace y los parametros que recibe.
- Comentar las tareas complejas; evitar comentarios que solo repitan operaciones evidentes.

## Seguimiento y GitHub

- Mantener presente la funcionalidad o correccion en la que se esta trabajando y sus pruebas pendientes.
- Al cerrar una funcionalidad, o cuando la conversacion empiece a pasar a otro tema, avisar brevemente:
  que quedo terminado, que se verifico y que sigue pendiente.
- En ese momento recordar el estado de GitHub: cambios sin commit, commits sin subir y si conviene
  integrar una base probada o marcar una nueva version. Consultar Git antes de afirmar ese estado.
- Usar un aviso concreto, por ejemplo: "Cerramos los filtros del historial y pasaron las pruebas.
  Quedan estos cambios por subir a GitHub. Conviene guardar este punto antes de seguir con la FFT."
- Evitar acumular varias funcionalidades terminadas en un unico respaldo grande como ocurrio antes de v2.
- No interrumpir cada ajuste pequeno con recordatorios repetidos; agruparlos al cerrar un bloque de trabajo.
- Si aparecen cambios de alcance, senalarlo y separar lo terminado de lo nuevo para no perder el punto de cierre.
- Estos recordatorios son contextuales durante el trabajo, no avisos programados por calendario.
- El flujo acordado es main para bases probadas, develop para desarrollo y pruebas, y tags para hitos.
  Comprobar las ramas existentes antes de usar ese flujo; este archivo no implica que develop ya exista.
- Una compilacion correcta no basta para declarar estabilidad en la placa. Distinguir compilacion,
  pruebas automaticas y pruebas reales; conservar visibles los problemas conocidos.
- Un recordatorio no autoriza por si solo commits, pushes, merges, tags o cargas de firmware.
  Respetar las autorizaciones vigentes del usuario y no pedirlas nuevamente si ya cubren la accion.
- Mantener los tags publicados sin modificaciones y evitar pushes forzados.
