#include "shared_spi_bus.h"

// Crea el mutex estatico que protege las transacciones SPI compartidas.
bool SharedSpiBus::begin()
{
	if (!mutex) mutex = xSemaphoreCreateMutexStatic(&mutexStorage);
	return mutex != nullptr;
}

// Toma el mutex SPI durante el tiempo indicado por timeoutTicks.
bool SharedSpiBus::lock(TickType_t timeoutTicks)
{
	return mutex && xSemaphoreTake(mutex, timeoutTicks) == pdTRUE;
}

// Libera el mutex SPI si ya fue creado.
void SharedSpiBus::unlock()
{
	if (mutex) xSemaphoreGive(mutex);
}

// Indica si el mutex SPI fue inicializado correctamente.
bool SharedSpiBus::isReady() const
{
	return mutex != nullptr;
}
