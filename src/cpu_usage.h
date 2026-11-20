#pragma once
#include <stdint.h>

// Registra el muestreo por nucleo una sola vez desde setup; sin parametros.
void cpuUsageBegin();

// Copia ocupacion estimada en decimas de porcentaje; values recibe C0/C1, -1 si no estan disponibles.
void cpuUsageRead(int16_t values[2]);
