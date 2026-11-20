#pragma once

#include <Arduino.h>

namespace GridEventFormat {
constexpr uint8_t version = 4;
constexpr uint16_t pageSize = 2048;
constexpr uint8_t minPagesPerEvent = 3;
constexpr uint8_t maxPagesPerEvent = 5;
constexpr uint8_t preTriggerSamples = 113;
constexpr uint8_t continuationSamples = 118;
constexpr uint8_t transitionsPerPage = 12;
constexpr uint8_t maxTransitions = transitionsPerPage * (maxPagesPerEvent - 1);
constexpr uint8_t timestampEstimated = 1U << 0;
constexpr uint8_t startupPowerOn = 1U << 1;
constexpr uint8_t startupBrownout = 1U << 2;
constexpr uint8_t startupRecovered = 1U << 3;
constexpr uint16_t startupReasonMarker = 0xB000; // Identifica el motivo ESP en SettingsSnapshot.reserved, solo en arranques.
constexpr uint8_t preTriggerMissing = 1U << 4;
constexpr uint8_t packedFastRms = 1U << 5; // 16 bytes: tiempo de 16 bits y ocho canales de 14 bits.
constexpr uint8_t recordsDropped = 1U << 6;
constexpr uint8_t synthetic = 1U << 7;
constexpr uint16_t unavailableVoltage = UINT16_MAX;

enum class Subject : uint8_t { phaseR, phaseS, phaseT, neutral, grid };
enum class pointType : uint8_t { none, dip, swell, overCurrent };

// Empaqueta alta corriente sin mezclarla con los estados de voltaje; active indica inicio o retorno.
inline uint16_t packCurrentState(Subject subject, bool active) {
    return uint16_t(uint8_t(subject)) | uint16_t(active ? 1U : 5U) << 3 |
        uint16_t(active ? 5U : 1U) << 6 | 0x0400U;
}

// Marca un evento sin modificar los nueve bits de las transiciones antiguas; limited indica cierre parcial.
inline uint16_t packPointEvent(Subject subject, pointType type, bool limited = false)
{
	return uint16_t(uint8_t(subject) & 7U) | 0x0200U |
		(uint16_t(uint8_t(type) & 31U) << 10) | (limited ? 0x8000U : 0U);
}

// Reconoce en word el bit que distingue un puntual de una transicion de estado.
inline bool isPointEvent(uint16_t word) { return (word & 0x0200U) != 0; }

struct __attribute__((packed)) ListHeader {
	uint8_t formatVersion;
	uint32_t eventId;
	uint32_t unixSeconds;
	uint16_t fraction100Us;
	uint16_t subjectAndStates;
	uint8_t timeStatus;
	uint32_t previousStateDuration10Ms;
	uint16_t previousVoltage;
	uint16_t currentVoltage;
	int32_t firstSampleOffsetUs;
	uint8_t sampleCount;
	uint8_t pageCount;
	uint16_t headerCrc16;
};
static_assert(sizeof(ListHeader) == 30, "Unexpected Grid list header size");

struct __attribute__((packed)) SettingsSnapshot {
	uint16_t lowVoltage[3];
	uint16_t highVoltage[3];
	uint16_t noVoltage;
	uint16_t hysteresis;
	uint16_t lossConfirm10Ms;
	uint16_t recoveryConfirm10Ms;
	uint16_t pointLimit10Ms;
	uint16_t overCurrent;
	uint16_t currentHysteresis;
	uint8_t dipConfirm10Ms;
	uint8_t swellConfirm10Ms;
	uint8_t overCurrentConfirm10Ms;
	uint8_t flags;
	uint16_t neutralHigh;
	uint16_t neutralHysteresis;
	uint16_t neutralConfirm10Ms;
	uint16_t neutralRecovery10Ms;
	uint16_t preSamples;
	uint16_t postSamples;
	uint8_t capturePages;
	uint8_t reservedPages;
	uint16_t window10Ms;
	uint16_t reserved;
};
static_assert(sizeof(SettingsSnapshot) == 48, "Unexpected Grid settings snapshot size");

struct __attribute__((packed)) MeterSnapshot {
	float voltage[4];
	float current[4];
	float activePower[3];
	float reactivePower[3];
	float apparentPower[3];
	float powerFactor[3];
	float totalPower[3];
	float frequency;
	float activeEnergy[3];
	float reactiveEnergy[3];
	float apparentEnergy[3];
	double totalEnergy[3];
};
static_assert(sizeof(MeterSnapshot) == 156, "Unexpected Grid meter snapshot size");

struct __attribute__((packed)) Transition {
	int16_t offsetMs;
	uint32_t previousStateDuration10Ms;
	uint16_t subjectAndStates;
	uint16_t previousVoltage;
	uint16_t currentVoltage;
};
static_assert(sizeof(Transition) == 12, "Unexpected Grid transition size");

struct __attribute__((packed)) ContinuationHeader {
	uint32_t eventId;
	uint8_t pageIndex;
	uint8_t pageCount;
	uint8_t sampleCount;
	uint8_t transitionCount; // bits 0..6 cantidad; bit 7 indica entradas omitidas.
	uint16_t pageCrc16;
};
static_assert(sizeof(ContinuationHeader) == 10, "Unexpected Grid continuation header size");

constexpr size_t rootSampleOffset = sizeof(ListHeader) + sizeof(SettingsSnapshot) + sizeof(MeterSnapshot);
constexpr size_t continuationSampleOffset = sizeof(ContinuationHeader) + transitionsPerPage * sizeof(Transition);

// Empaqueta sujeto, estado anterior y estado actual en el formato compartido de 16 bits.
inline uint16_t packSubjectAndStates(Subject subject, uint8_t previousState, uint8_t currentState)
{
	return uint16_t(uint8_t(subject) & 0x07U) |
		uint16_t(previousState & 0x07U) << 3 |
		uint16_t(currentState & 0x07U) << 6;
}

// Calcula CRC-16/CCITT-FALSE sobre data con la longitud indicada.
inline uint16_t crc16(const uint8_t* data, size_t length)
{
	uint16_t crc = 0xFFFF;
	for (size_t index = 0; index < length; ++index) {
		crc ^= uint16_t(data[index]) << 8;
		for (uint8_t bit = 0; bit < 8; ++bit)
			crc = crc & 0x8000 ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
	}
	return crc;
}
}
