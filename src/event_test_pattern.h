#pragma once
#include <stdint.h>

// Convierte una posicion del ciclo triangular (0..8189) a un entero firmado de 12 bits.
constexpr int16_t eventTestTriangle(uint32_t position)
{
	return position <= 4095 ? (int32_t)position - 2048 : 6142 - (int32_t)position;
}

// Valor sintetico de la muestra absoluta sampleIndex, sin reiniciar entre bloques o canales.
constexpr int16_t eventTestRampValue(uint32_t sampleIndex)
{
	return eventTestTriangle((sampleIndex % 8190 + 2049) % 8190);
}
