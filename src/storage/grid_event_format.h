#pragma once

#include <Arduino.h>

namespace GridEventFormat {
constexpr uint8_t version = 1;
constexpr uint16_t pageSize = 2048;
constexpr uint8_t minPagesPerEvent = 3;
constexpr uint8_t maxPagesPerEvent = 6;
constexpr uint8_t preTriggerSamples = 113;
constexpr uint8_t continuationSamples = 118;
constexpr uint8_t transitionsPerPage = 12;
constexpr uint8_t maxTransitions = transitionsPerPage * (maxPagesPerEvent - 1);
constexpr uint8_t timestampEstimated = 1U << 0;
constexpr uint8_t startupPowerOn = 1U << 1;
constexpr uint8_t startupBrownout = 1U << 2;
constexpr uint8_t startupRecovered = 1U << 3;
constexpr uint8_t retiredNeutralFormat = 1U << 4; // Formato de 18 bytes descartado: no decodificar.
constexpr uint8_t packedFastRms = 1U << 5; // 16 bytes: tiempo de 16 bits y ocho canales de 14 bits.
constexpr uint16_t unavailableVoltage = UINT16_MAX;

enum class Subject : uint8_t { phaseR, phaseS, phaseT, neutral, grid };

struct __attribute__((packed)) ListHeader {
	uint8_t formatVersion;
	uint32_t eventId;
	uint32_t unixSeconds;
	uint16_t milliseconds;
	uint16_t subjectAndStates;
	uint8_t timeStatus;
	uint32_t previousStateDuration10Ms;
	uint16_t previousVoltage;
	uint16_t currentVoltage;
	int32_t firstSampleOffsetMs;
	uint8_t sampleCount;
	uint8_t pageCount;
	uint16_t headerCrc16;
};
static_assert(sizeof(ListHeader) == 30, "Unexpected Grid list header size");

struct __attribute__((packed)) SettingsSnapshot {
	float lowVoltage[3];
	float highVoltage[3];
	float noVoltage;
	float hysteresis;
	uint32_t lossConfirmMs;
	uint32_t voltageConfirmMs;
	uint32_t recoveryConfirmMs;
	uint8_t capturePages;
	uint8_t reserved[3];
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
	uint8_t transitionCount;
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
