# PowerMeter9000

Medidor permanente de red trifasica con ESP32, ADE9000, medicion de neutro con BL0942,
almacenamiento NAND e interfaz web integrada.

## Version 2

La base `v2.0.0-preview.1` reune el trabajo de firmware y web acumulado hasta el
20 de noviembre de 2026. Es una version de desarrollo: permite identificar y
recuperar esta base mientras continuan las pruebas de estabilidad.

- Eventos de red, DIP, swell y sobrecorriente, con capturas FAST y oscilografias NAND.
- Historial que reconstruye estados entre eventos y reinicios, con linea temporal y visor emergente.
- Estadisticas por periodo y fase, con union temporal para evitar triplicar cortes simultaneos.
- Historico de mediciones con cache del navegador y sincronizacion progresiva.
- Osciloscopio y panel de calidad de energia con FFT y mediciones calculadas.
- Configuracion web, importacion/exportacion por secciones y ajustes aplicables en caliente.
- Diagnosticos por modulo, memoria, CPU, arranque y conectividad.
- Inicio con RTC, pantalla de progreso y optimizaciones de memoria y transporte web.

El detalle y las limitaciones de esta base estan en [CHANGELOG.md](CHANGELOG.md).

## Compilacion

Proyecto PlatformIO con entornos definidos en `platformio.ini`.

```sh
pio run -e ota
node tools/build_webfs.js
```

El primer comando compila el firmware y el segundo genera la imagen web
`.pio/build/webfs.fs32`. Compilar no actualiza la placa. Revisar puerto, direccion
de destino y particiones antes de cargar firmware o una imagen web.

## Documentacion

- [Historial de eventos](EVENT_HISTORY.md)
- [Capturas unificadas](UNIFIED_EVENT_CAPTURE.md)
- [Formato del historico](HISTORY_FORMAT_CORRECTION_GUIDE.md)
- [Cache del historico](HISTORY_CACHE.md)
- [Osciloscopio y FFT](SCOPE_FFT.md)
- [Revision de memoria](MEMORY_REVIEW.md)
- [Diagnostico de red](NETWORK_DIAGNOSTICS.md)
- [Endpoints](ENDPOINTS.md)

## Versionado

`main` contiene el desarrollo. Los tags identifican bases reproducibles;
`v2.0.0-preview.1` marca esta primera base de la version 2. Las correcciones
posteriores no modifican ese tag.
