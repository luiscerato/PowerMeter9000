#pragma once

#include <Arduino.h>

enum class NandBenchmarkMode : uint8_t {
	quick,
	full,
	verify
};

// Inicia una prueba en segundo plano sobre el rango indicado.
// mode define la operacion; speedMHz acepta 20, 40 u 80; startBlock y blockCount limitan el rango.
bool nandBenchmarkStart(NandBenchmarkMode mode, uint8_t speedMHz,
	uint16_t startBlock = 0, uint16_t blockCount = 0);

// Solicita detener la prueba al terminar la operacion NAND actual; sin parametros.
bool nandBenchmarkCancel();

// Construye el estado actual en formato JSON; json recibe la respuesta.
void nandBenchmarkGetJson(String& json);
