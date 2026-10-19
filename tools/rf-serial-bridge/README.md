# Puente serie RF para ESP32-C3 Super Mini

Firmware comun para dos placas C3. El USB y el UART local se envian por NRF24; cada paquete recibido por RF se copia a USB y UART. La comunicacion RF es bidireccional, aunque el enlace de radio transmite un paquete por vez.

## Pines

| C3 Super Mini | NRF24L01+ PA/LNA |
| --- | --- |
| GPIO 4 | SCK |
| GPIO 5 | MISO |
| GPIO 6 | MOSI |
| GPIO 7 | CSN |
| GPIO 10 | CE |
| 3V3 regulados | VCC |
| GND local | GND |

| C3 Super Mini | Equipo serie |
| --- | --- |
| GPIO 20 / RX | TX del equipo |
| GPIO 21 / TX | RX del equipo |
| GND | GND de la interfaz serie aislada |

USB usa el conector de la placa C3. UART y USB trabajan a 115200 baudios, 8N1.

## Roles

El firmware es identico en los dos C3. Deja GPIO 1 abierto en el nodo conectado a la PC (ID 2) y conecta GPIO 1 a GND en el nodo del medidor (ID 1). GPIO 1 se muestrea al arrancar.

## Alimentacion

Alimenta el C3 conectado a la PC por USB. En el nodo del medidor, alimenta el C3 desde los 5 V aislados disponibles en ese lado. El NRF24 PA/LNA requiere 3.3 V; no conectar VCC del modulo a 5 V. Usa un regulador que soporte sus picos de consumo y coloca cerca del modulo un capacitor de 470 uF en paralelo con 100 nF.

Conecta GND entre el C3, el NRF24 y la UART solamente dentro de cada nodo. No unas las masas ni la alimentacion de los dos nodos: el enlace entre ellos es solo por radio.

En el nodo del medidor, conectar UART al lado aislado de baja tension ya previsto en el esquema. Para el retorno C3 TX -> RX del medidor, aislar primero el TXD del CH340 si ambos manejan la misma linea.

## Compilar y cargar

Desde esta carpeta:

```text
pio run
pio run -t upload --upload-port COMx
pio device monitor --baud 115200 --port COMx
```

Cargar el mismo firmware en ambos C3. El nodo USB aparece como puerto serie de la PC; los bytes remotos se muestran sin mensajes de depuracion agregados al flujo.
