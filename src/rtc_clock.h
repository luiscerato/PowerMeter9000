#pragma once

#include <Arduino.h>

struct rtcClockInfo
{
	bool present;
	bool valid;
	bool lostPower;
	uint32_t rtcEpoch;
	uint32_t systemEpoch;
	int32_t driftSeconds;
};

// Inicia I2C y detecta si el DS3231 contiene una hora utilizable.
bool rtcClockBegin();

// Lee la hora UTC del RTC; epoch recibe el resultado en segundos Unix.
bool rtcClockRead(uint32_t& epoch);

// Ajusta el RTC desde la hora UTC del sistema y devuelve si pudo escribirla.
bool rtcClockSyncFromSystem();

// Obtiene presencia, validez y deriva; info recibe el estado actual.
bool rtcClockGetInfo(rtcClockInfo& info);
