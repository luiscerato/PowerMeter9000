# Pantalla de arranque

- Bitmap monocromo de 128 x 60: `assets/boot/power-meter-128x60.png`.
- Vista ampliada con barra: `assets/boot/power-meter-preview.png`.
- Regenerar imagen y cabecera con `node tools/build_boot_splash.cjs`.
- La cabecera generada ocupa 960 bytes en flash (`PROGMEM`), sin otro framebuffer en RAM.

`bootScreenShow(lcd)` muestra la imagen sobre el framebuffer existente y dibuja
la barra en las cuatro filas inferiores. `bootScreenAdvance(lcd)` agrega 12
pixeles por etapa, enviando solamente las palabras de 16 pixeles afectadas en
las dos filas interiores. El parametro opcional permite cambiar el avance.

La UI normal se prepara al final de `setup`. `bootScreenFinish(lcd)` completa
la barra y mantiene la imagen 500 ms. En `loop`, `bootScreenHold()` demora
solamente `ui.Run()`: mediciones, eventos, historico y red siguen funcionando.
No hay una espera bloqueante ni un tiempo minimo impuesto al almacenamiento.

Prueba: `node test/boot_screen.test.cjs`.

## Verificacion en placa

Medicion del 27/10/2026 luego de subir por OTA: primera muestra FAST a los
302 ms, primer estado confirmado a los 551 ms, primera pagina de evento a los
881 ms y fin de inicializacion a los 1478 ms. La etapa LCD completa conserva
48 ms. Al finalizar, la UI queda habilitada a partir de aproximadamente
1978 ms, sin retrasar esos registros. Almacenamiento listo, sin error de E/S.

RAM estatica compilada: 116264 bytes, 24 bytes mas que antes de agregar el
splash. La comprobacion visual fisica del LCD queda para observar la placa;
la vista PNG y las transferencias parciales se verificaron por separado.

## Marca web

La pantalla de carga y la cabecera del menu usan una variante vectorial de la
misma senoidal y el nombre Power Meter. El SVG y el estilo inicial estan
incluidos en `index.html`, sin solicitudes de imagen ni fuentes externas.
La barra avanza al terminar cada CSS/JS; las descargas siguen siendo estrictamente
secuenciales para no saturar el ESP. El registro tecnico queda desplegable y
se abre automaticamente si falla un recurso. No se agrega una demora artificial.

Pruebas de carga, error y layout desktop/mobile:
`node test/boot_brand_web.test.cjs` y `node test/web_bundle.test.cjs` (esta ultima
requiere construir WebFS con `node tools/build_webfs.js`).
