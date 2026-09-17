#pragma once

#include <Arduino.h>

enum class NandBenchmarkMode : uint8_t {
	quick,
	full
};

// Inicia una prueba destructiva en segundo plano.
// mode define el rango y speedMHz acepta 20, 40 u 80 MHz.
bool nandBenchmarkStart(NandBenchmarkMode mode, uint8_t speedMHz);

// Solicita detener la prueba al terminar la operacion NAND actual; sin parametros.
bool nandBenchmarkCancel();

// Construye el estado actual en formato JSON; json recibe la respuesta.
void nandBenchmarkGetJson(String& json);
