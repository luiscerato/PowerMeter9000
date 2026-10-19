# Formato historico Flash V1

La fuente del formato completo es `energy_flash_format_v1_1.md`. Se mantiene
`formatVersion = 1`; no se crea otra version para incorporar grupos que ya
estaban definidos.

## Cambios acordados al bitmap

El unico cambio posterior afecta a `group_config`:

- bit 0: voltaje BASIC/FULL; BASIC siempre esta presente y FULL agrega Vrs/Vst/Vtr.
- La corriente siempre esta presente; se elimina su bit de habilitacion.
- Los campos siguientes se desplazan dos bits hacia abajo respecto del bitmap
  original, sin cambiar el formato ni el orden del payload.

| Bits V1 actual | Campo |
|---|---|
| 0 | `voltage_full`: 0 BASIC, 1 FULL |
| Siempre | Corriente R/S/T/N |
| 1..2 | `active_power_mode` |
| 3..4 | `reactive_power_mode` |
| 5..6 | `apparent_power_mode` |
| 7 | `pf_enable` |
| 8 | `vthd_enable` |
| 9 | `ithd_enable` |
| 10..11 | `frequency_mode` |
| 12 | `voltage_angles_enable` |
| 13 | `battery_enable` |
| 14 | `energy_enable` |
| 15..17 | `energy_extra_mode` |
| 18..23 | Reservado |

Los modos de P/Q/S y frecuencia son OFF, COMPACT y FULL. Los ocho modos de
energia adicional se conservan tal como se especificaron originalmente.

## Orden canonico de la muestra

1. STATUS
2. VOLTAGE
3. CURRENT
4. ACTIVE_POWER
5. REACTIVE_POWER
6. APPARENT_POWER
7. POWER_FACTOR
8. VTHD
9. ITHD
10. FREQUENCY
11. VOLTAGE_ANGLES
12. ENERGY
13. BATTERY

STATUS, voltaje y corriente son obligatorios. Los paquetes bit-packed se
concatenan en este orden; los bits no utilizados de cada muestra quedan en
cero.

## Cabecera y CRC

La cabecera sigue siendo fija de 20 bytes: Unix seconds, secuencia uint24,
bitmap de 24 bits, periodo en decisegundos, cantidad de muestras, version,
flags, CRC16 de cabecera y CRC16 del payload. No se agregan magic, header size
ni campos nuevos. Se conserva CRC-16/CCITT-FALSE. El periodo valido para
configuraciones nuevas es 1..3000 decisegundos (0,1 s..300 s).

## Perfil actual

El perfil predeterminado es BASIC, corriente, potencia activa y reactiva FULL,
factor de potencia, frecuencia FULL y energia activa R/S/T mas reactiva total.
Su bitmap es `0x00C894`, ocupa 52 bytes por muestra y permite 39 muestras por
pagina. Bateria y los demas grupos son seleccionables; no se activan por
defecto. La muestra maxima del formato es 97 bytes.

## Compatibilidad

Las paginas nuevas se escriben como V1 y siguen el orden canonico anterior. Se
mantiene temporalmente el decoder V2 solo para las paginas V2 que ya llegaron a
grabarse por error durante una iteracion anterior. No se cambia el significado
de las paginas V1 existentes ni se requiere reformatear la NAND.

El bitmap NVS tiene su propio esquema de migracion y no cambia `formatVersion`
ni el formato de las paginas. `HistoryRecorder`, el endpoint de configuracion,
el estimador y el decoder web deben aceptar el mismo conjunto de modos V1.
