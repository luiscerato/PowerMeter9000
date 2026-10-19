#pragma once

#include <Arduino.h>

namespace HistoryFormat {
constexpr uint8_t version = 1;
constexpr uint8_t temporaryVersion2 = 2;
constexpr size_t pageSize = 2048;
constexpr size_t headerSize = 20;
constexpr size_t payloadSize = pageSize - headerSize;
constexpr uint16_t maxSamplePeriodDeciseconds = 3000;

constexpr uint32_t voltageFullBit = 1U << 0;
constexpr uint32_t activePowerModeShift = 1;
constexpr uint32_t reactivePowerModeShift = 3;
constexpr uint32_t apparentPowerModeShift = 5;
constexpr uint32_t powerFactorBit = 1U << 7;
constexpr uint32_t voltageThdBit = 1U << 8;
constexpr uint32_t currentThdBit = 1U << 9;
constexpr uint32_t frequencyModeShift = 10;
constexpr uint32_t voltageAnglesBit = 1U << 12;
constexpr uint32_t batteryBit = 1U << 13;
constexpr uint32_t energyBit = 1U << 14;
constexpr uint32_t energyExtraModeShift = 15;

constexpr uint32_t temporaryV2VoltageModeShift = 0;
constexpr uint32_t temporaryV2VoltageModeMask = 0x03U;
constexpr uint32_t temporaryV2CurrentEnableBit = 1U << 2;
constexpr uint32_t temporaryV2ActivePowerModeShift = 3;
constexpr uint32_t temporaryV2ReactivePowerModeShift = 5;
constexpr uint32_t temporaryV2ApparentPowerModeShift = 7;
constexpr uint32_t temporaryV2PowerFactorBit = 1U << 9;
constexpr uint32_t temporaryV2VoltageThdBit = 1U << 10;
constexpr uint32_t temporaryV2CurrentThdBit = 1U << 11;
constexpr uint32_t temporaryV2FrequencyModeShift = 12;
constexpr uint32_t temporaryV2VoltageAnglesBit = 1U << 14;
constexpr uint32_t temporaryV2BatteryBit = 1U << 15;
constexpr uint32_t temporaryV2EnergyBit = 1U << 16;
constexpr uint32_t temporaryV2EnergyExtraModeShift = 17;

constexpr uint8_t powerModeOff = 0;
constexpr uint8_t powerModeCompact = 1;
constexpr uint8_t powerModeFull = 2;
constexpr uint8_t energyExtraNone = 0;
constexpr uint8_t energyExtraReactiveTotal = 1;
constexpr uint8_t energyExtraApparentTotal = 2;
constexpr uint8_t energyExtraReactiveAndApparentTotal = 3;
constexpr uint8_t energyExtraReactivePhases = 4;
constexpr uint8_t energyExtraApparentPhases = 5;
constexpr uint8_t energyExtraReactivePhasesApparentTotal = 6;
constexpr uint8_t energyExtraAllPhases = 7;

struct __attribute__((packed)) PageHeader {
	uint32_t timestamp;
	uint8_t sequence[3];
	uint8_t groupConfig[3];
	uint16_t samplePeriod;
	uint16_t sampleCount;
	uint8_t formatVersion;
	uint8_t pageFlags;
	uint16_t headerCrc16;
	uint16_t payloadCrc16;
};
static_assert(sizeof(PageHeader) == headerSize, "Unexpected history page header size");

// Valida los bits de configuracion del formato indicado; groupConfig contiene el bitmap de 24 bits.
inline bool isValidGroupConfig(uint8_t formatVersion, uint32_t groupConfig)
{
	if (formatVersion == version) {
		if (groupConfig & 0xFC0000U) return false;
		const uint8_t activeMode = (groupConfig >> activePowerModeShift) & 0x03U;
		const uint8_t reactiveMode = (groupConfig >> reactivePowerModeShift) & 0x03U;
		const uint8_t apparentMode = (groupConfig >> apparentPowerModeShift) & 0x03U;
		const uint8_t frequencyMode = (groupConfig >> frequencyModeShift) & 0x03U;
		const uint8_t energyMode = (groupConfig >> energyExtraModeShift) & 0x07U;
		return activeMode != 3 && reactiveMode != 3 && apparentMode != 3 && frequencyMode != 3 &&
			((groupConfig & energyBit) || energyMode == energyExtraNone);
	}
	if (formatVersion != temporaryVersion2 || (groupConfig & 0xF00000U)) return false;
	const uint8_t voltageMode = (groupConfig >> temporaryV2VoltageModeShift) & temporaryV2VoltageModeMask;
	const uint8_t activeMode = (groupConfig >> temporaryV2ActivePowerModeShift) & 0x03U;
	const uint8_t reactiveMode = (groupConfig >> temporaryV2ReactivePowerModeShift) & 0x03U;
	const uint8_t apparentMode = (groupConfig >> temporaryV2ApparentPowerModeShift) & 0x03U;
	const uint8_t frequencyMode = (groupConfig >> temporaryV2FrequencyModeShift) & 0x03U;
	const uint8_t energyMode = (groupConfig >> temporaryV2EnergyExtraModeShift) & 0x07U;
	return voltageMode != 3 && activeMode != 3 && reactiveMode != 3 &&
		apparentMode != 3 && frequencyMode != 3 &&
		((groupConfig & temporaryV2EnergyBit) || energyMode == energyExtraNone);
}

// Valida groupConfig usando el bitmap unificado de V1; groupConfig contiene 24 bits.
inline bool isValidGroupConfig(uint32_t groupConfig)
{
	return isValidGroupConfig(version, groupConfig);
}

// Calcula los bytes por muestra segun la version y los grupos activados en groupConfig.
inline size_t sampleSize(uint8_t formatVersion, uint32_t groupConfig)
{
	if (!isValidGroupConfig(formatVersion, groupConfig)) return 0;
	size_t size = 1;
	if (formatVersion == version) {
		size += groupConfig & voltageFullBit ? 14 : 8;
		size += 9;
		const uint8_t activeMode = (groupConfig >> activePowerModeShift) & 0x03U;
		const uint8_t reactiveMode = (groupConfig >> reactivePowerModeShift) & 0x03U;
		const uint8_t apparentMode = (groupConfig >> apparentPowerModeShift) & 0x03U;
		const uint8_t frequencyMode = (groupConfig >> frequencyModeShift) & 0x03U;
		const uint8_t energyMode = (groupConfig >> energyExtraModeShift) & 0x07U;
		size += activeMode == powerModeFull ? 8 : activeMode == powerModeCompact ? 3 : 0;
		size += reactiveMode == powerModeFull ? 8 : reactiveMode == powerModeCompact ? 3 : 0;
		size += apparentMode == powerModeFull ? 8 : apparentMode == powerModeCompact ? 3 : 0;
		if (groupConfig & powerFactorBit) size += 4;
		if (groupConfig & voltageThdBit) size += 4;
		if (groupConfig & currentThdBit) size += 4;
		size += frequencyMode == powerModeFull ? 2 : frequencyMode == powerModeCompact ? 1 : 0;
		if (groupConfig & voltageAnglesBit) size += 5;
		if (groupConfig & batteryBit) size += 3;
		if (groupConfig & energyBit) size += 9 + (energyMode == 1 || energyMode == 2 ? 3 :
			energyMode == 3 ? 6 : energyMode == 4 || energyMode == 5 ? 9 :
			energyMode == 6 ? 12 : energyMode == 7 ? 18 : 0);
		return size;
	}

	const uint8_t voltageMode = (groupConfig >> temporaryV2VoltageModeShift) & temporaryV2VoltageModeMask;
	const uint8_t activeMode = (groupConfig >> temporaryV2ActivePowerModeShift) & 0x03U;
	const uint8_t reactiveMode = (groupConfig >> temporaryV2ReactivePowerModeShift) & 0x03U;
	const uint8_t apparentMode = (groupConfig >> temporaryV2ApparentPowerModeShift) & 0x03U;
	const uint8_t frequencyMode = (groupConfig >> temporaryV2FrequencyModeShift) & 0x03U;
	const uint8_t energyMode = (groupConfig >> temporaryV2EnergyExtraModeShift) & 0x07U;
	if (voltageMode) size += voltageMode == 1 ? 8 : 14;
	if (groupConfig & temporaryV2CurrentEnableBit) size += 9;
	size += activeMode == powerModeFull ? 8 : activeMode == powerModeCompact ? 3 : 0;
	size += reactiveMode == powerModeFull ? 8 : reactiveMode == powerModeCompact ? 3 : 0;
	size += apparentMode == powerModeFull ? 8 : apparentMode == powerModeCompact ? 3 : 0;
	if (groupConfig & temporaryV2PowerFactorBit) size += 4;
	if (groupConfig & temporaryV2VoltageThdBit) size += 4;
	if (groupConfig & temporaryV2CurrentThdBit) size += 4;
	size += frequencyMode == powerModeFull ? 2 : frequencyMode == powerModeCompact ? 1 : 0;
	if (groupConfig & temporaryV2VoltageAnglesBit) size += 5;
	if (groupConfig & temporaryV2BatteryBit) size += 3;
	if (groupConfig & temporaryV2EnergyBit) size += 9 + (energyMode == 1 || energyMode == 2 ? 3 :
		energyMode == 3 ? 6 : energyMode == 4 || energyMode == 5 ? 9 :
		energyMode == 6 ? 12 : energyMode == 7 ? 18 : 0);
	return size;
}

// Calcula los bytes por muestra para la configuracion V1 usada al escribir paginas nuevas.
inline size_t sampleSize(uint32_t groupConfig)
{
	return sampleSize(version, groupConfig);
}

// Calcula CRC-16/CCITT-FALSE sobre length bytes de data.
inline uint16_t crc16(const uint8_t* data, size_t length)
{
	uint16_t crc = 0xFFFF;
	for (size_t index = 0; index < length; ++index) {
		crc ^= uint16_t(data[index]) << 8;
		for (uint8_t bit = 0; bit < 8; ++bit) crc = crc & 0x8000 ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
	}
	return crc;
}
}
