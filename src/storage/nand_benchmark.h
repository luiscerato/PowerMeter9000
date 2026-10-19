#pragma once

#include <Arduino.h>

enum class NandBenchmarkMode : uint8_t {
	quick,
	full,
	verify
};

// Inicia una prueba en segundo plano sobre el rango indicado.
// mode define la operacion; speedMHz acepta 20, 25, 30, 35 o 40; startBlock y blockCount limitan el rango.
bool nandBenchmarkStart(NandBenchmarkMode mode, uint8_t speedMHz,
	uint16_t startBlock = 0, uint16_t blockCount = 0);

// Solicita detener la prueba al terminar la operacion NAND actual; sin parametros.
bool nandBenchmarkCancel();

// Reserva el acceso directo de diagnostico NAND; devuelve false si otra prueba ya esta en marcha.
bool nandDiagnosticTryAcquire();

// Libera el acceso directo de diagnostico NAND reservado previamente; sin parametros.
void nandDiagnosticRelease();

// Construye el estado actual en formato JSON; json recibe la respuesta.
void nandBenchmarkGetJson(String& json);
