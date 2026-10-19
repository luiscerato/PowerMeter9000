#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Serializa transacciones de dispositivos que comparten el mismo bus SPI.
class SharedSpiBus {
public:
	// Crea el mutex estatico del bus; sin parametros.
	bool begin();

	// Toma el bus durante timeoutTicks; timeoutTicks define la espera maxima.
	bool lock(TickType_t timeoutTicks = portMAX_DELAY);

	// Libera el bus tomado previamente por la tarea actual; sin parametros.
	void unlock();

	// Indica si el mutex del bus ya esta disponible; sin parametros.
	bool isReady() const;

private:
	StaticSemaphore_t mutexStorage;
	SemaphoreHandle_t mutex = nullptr;
};
