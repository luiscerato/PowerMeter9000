#include "history_recorder.h"
#include "battery.h"
#include "manager_api.h"
#include "wifiUtils.h"
#include <WiFi.h>
#include <Preferences.h>
#include <cmath>

static constexpr uint32_t fallbackTimestamp = 1767225600UL;
static constexpr uint8_t historyConfigSchema = 3;

// Asocia el recorder al Store que recibe las paginas completas.
HistoryRecorder::HistoryRecorder(HistoryStore& historyStore) : store(historyStore)
{
	config.enabled = true;
	config.groupConfig = standardGroupConfig;
	config.samplePeriod = standardSamplePeriod;
	loadConfig();
}

// Construye y encola una muestra al periodo configurado sin bloquear la adquisicion del medidor.
void HistoryRecorder::update(const meterValues& meter)
{
	const uint32_t now = millis();
	if (!config.enabled) return;
	const bool clockValid = managerClockSynchronized();
	HistoryStoreMetrics metrics;
	store.poll();
	store.getMetrics(metrics);
	if (!timestampReferenceReady) {
		estimatedTimestampBase = store.lastStoredTimestamp();
		if (!estimatedTimestampBase) estimatedTimestampBase = fallbackTimestamp;
		estimatedTimestampStartedMs = now;
		if (!clockValid) setTimeTo(estimatedTimestampBase);
		timestampReferenceReady = true;
	}
	if (clockValid && !lastClockValid && activePageEstimated && metrics.activeSamples) {
		const uint32_t elapsedMs = uint32_t(now - activePageStartedMs);
		const uint64_t nowTimestamp = managerNowMs() / 1000ULL;
		store.updateActiveTimestamp(nowTimestamp > elapsedMs / 1000U ? uint32_t(nowTimestamp - elapsedMs / 1000U) : 0);
		activePageEstimated = false;
	}
	lastClockValid = clockValid;
	const uint32_t sampleIntervalMs = uint32_t(config.samplePeriod) * 100U;
	if (sampleScheduleReady && int32_t(now - nextSampleMs) < 0) return;
	if (!sampleScheduleReady) {
		nextSampleMs = now;
		sampleScheduleReady = true;
	}
	nextSampleMs += sampleIntervalMs;
	const int32_t latenessMs = int32_t(now - nextSampleMs);
	if (latenessMs >= 0) nextSampleMs += (uint32_t(latenessMs) / sampleIntervalMs + 1U) * sampleIntervalMs;
	const size_t sampleSize = HistoryFormat::sampleSize(config.groupConfig);
	if (!metrics.activeSamples || metrics.activeSamples >= HistoryFormat::payloadSize / sampleSize) {
		activePageStartedMs = now;
		activePageEstimated = !clockValid;
	}
	uint8_t sample[maxSampleSize] = {};
	packSample(sample, meter, clockValid);
	const uint32_t timestamp = clockValid ? uint32_t(managerNowMs() / 1000ULL) :
		estimatedTimestampBase + uint32_t(now - estimatedTimestampStartedMs) / 1000U;
	store.addSample(timestamp, config.groupConfig, config.samplePeriod, sample, sampleSize);
}

// Cierra la pagina parcial actual para una transicion controlada.
bool HistoryRecorder::flush()
{
	return store.flush();
}

// Copia el perfil de guardado para publicarlo por la API sin exponer estado mutable.
void HistoryRecorder::getConfig(HistoryRecorderConfig& result) const
{
	result = config;
}

// Rechaza perfiles que el empaquetador actual no puede representar.
bool HistoryRecorder::isConfigSupported(const HistoryRecorderConfig& value)
{
	const size_t sampleSize = HistoryFormat::sampleSize(value.groupConfig);
	return HistoryFormat::isValidGroupConfig(value.groupConfig) &&
		value.samplePeriod && value.samplePeriod <= HistoryFormat::maxSamplePeriodDeciseconds &&
		sampleSize && sampleSize <= maxSampleSize;
}

// Valida un perfil, cierra la pagina parcial y lo vuelve persistente para la proxima sesion.
bool HistoryRecorder::setConfig(const HistoryRecorderConfig& value)
{
	if (!isConfigSupported(value) || !store.flush()) return false;
	config = value;
	nextSampleMs = 0;
	sampleScheduleReady = false;
	saveConfig();
	return true;
}

// Empaqueta los grupos V1 habilitados; dest recibe una muestra y meter los valores medidos.
void HistoryRecorder::packSample(uint8_t* dest, const meterValues& meter, bool clockValid) const
{
	uint8_t status = 0;
	if (WiFi.isConnected()) status |= 1U << 0;
	if (mqtt.connected()) status |= 1U << 1;
	if (Batt.getSource() == PowerSource::Battery) status |= 1U << 2;
	if (Batt.isCharging()) status |= 1U << 3;
	if (Batt.getChargeState() == ChargeState::CHARGE_DONE) status |= 1U << 4;
	if (Batt.getFault() != BatteryFault::None) status |= 1U << 5;
	if (clockValid) status |= 1U << 6;
	dest[0] = status;
	uint16_t bitOffset = 8;
	const phaseValues* phases[] = { &meter.phaseR, &meter.phaseS, &meter.phaseT, &meter.neutral };
	for (const phaseValues* phase : phases) writeBits(dest, bitOffset, scaleUnsigned(phase->Vrms, 100.0, UINT16_MAX), 16);
	if (config.groupConfig & HistoryFormat::voltageFullBit) {
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->VVrms, 100.0, UINT16_MAX), 16);
	}
	for (const phaseValues* phase : phases) writeBits(dest, bitOffset, scaleUnsigned(phase->Irms, 1000.0, 0x3FFFFU), 18);
	const uint8_t activeMode = (config.groupConfig >> HistoryFormat::activePowerModeShift) & 0x03U;
	const uint8_t reactiveMode = (config.groupConfig >> HistoryFormat::reactivePowerModeShift) & 0x03U;
	const uint8_t apparentMode = (config.groupConfig >> HistoryFormat::apparentPowerModeShift) & 0x03U;
	const uint8_t frequencyMode = (config.groupConfig >> HistoryFormat::frequencyModeShift) & 0x03U;
	if (activeMode == HistoryFormat::powerModeFull)
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleSigned(phases[index]->Watt, 10.0f, 21), 21);
	else if (activeMode == HistoryFormat::powerModeCompact) writeBits(dest, bitOffset, scaleSigned(meter.power.Watt, 10.0f, 24), 24);
	if (reactiveMode == HistoryFormat::powerModeFull)
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleSigned(phases[index]->VAR, 10.0f, 21), 21);
	else if (reactiveMode == HistoryFormat::powerModeCompact) writeBits(dest, bitOffset, scaleSigned(meter.power.VAR, 10.0f, 24), 24);
	if (apparentMode == HistoryFormat::powerModeFull)
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->VA, 10.0, 0x1FFFFFU), 21);
	else if (apparentMode == HistoryFormat::powerModeCompact) writeBits(dest, bitOffset, scaleUnsigned(meter.power.VA, 10.0, 0xFFFFFFU), 24);
	if (config.groupConfig & HistoryFormat::powerFactorBit) for (uint8_t index = 0; index < 3; ++index) {
		const float powerFactor = phases[index]->PowerFactor;
		const uint32_t magnitude = scaleUnsigned(fabsf(powerFactor), 500.0f, 500);
		writeBits(dest, bitOffset, magnitude | (powerFactor < 0 ? 0x200U : 0), 10);
	}
	if (config.groupConfig & HistoryFormat::voltageThdBit)
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->Vthd, 1.0, 1023), 10);
	if (config.groupConfig & HistoryFormat::currentThdBit)
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->Ithd, 1.0, 1023), 10);
	if (frequencyMode == HistoryFormat::powerModeFull) writeBits(dest, bitOffset, scaleUnsigned(meter.average.Freq, 100.0f, UINT16_MAX), 16);
	else if (frequencyMode == HistoryFormat::powerModeCompact) {
		const float frequency = meter.average.Freq;
		const uint32_t raw = std::isfinite(frequency) && frequency >= 40.0f
			? scaleUnsigned((frequency - 40.0f) * 10.0f + 1.0f, 1.0f, 255)
			: 0;
		writeBits(dest, bitOffset, raw, 8);
	}
	if (config.groupConfig & HistoryFormat::voltageAnglesBit)
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->AngleV, 20.0, 0x1FFFU), 13);
	if (config.groupConfig & HistoryFormat::energyBit) {
		for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->Watt_H, 0.1, 0xFFFFFFU), 24);
		const uint8_t energyMode = (config.groupConfig >> HistoryFormat::energyExtraModeShift) & 0x07U;
		if (energyMode == HistoryFormat::energyExtraReactiveTotal || energyMode == HistoryFormat::energyExtraReactiveAndApparentTotal)
			writeBits(dest, bitOffset, scaleUnsigned(meter.energy.VAR_H, 0.01, 0xFFFFFFU), 24);
		if (energyMode == HistoryFormat::energyExtraApparentTotal || energyMode == HistoryFormat::energyExtraReactiveAndApparentTotal)
			writeBits(dest, bitOffset, scaleUnsigned(meter.energy.VA_H, 0.01, 0xFFFFFFU), 24);
		if (energyMode == HistoryFormat::energyExtraReactivePhases || energyMode == HistoryFormat::energyExtraReactivePhasesApparentTotal || energyMode == HistoryFormat::energyExtraAllPhases)
			for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->VAR_H, 0.1, 0xFFFFFFU), 24);
		if (energyMode == HistoryFormat::energyExtraApparentPhases || energyMode == HistoryFormat::energyExtraAllPhases)
			for (uint8_t index = 0; index < 3; ++index) writeBits(dest, bitOffset, scaleUnsigned(phases[index]->VA_H, 0.1, 0xFFFFFFU), 24);
		if (energyMode == HistoryFormat::energyExtraReactivePhasesApparentTotal)
			writeBits(dest, bitOffset, scaleUnsigned(meter.energy.VA_H, 0.01, 0xFFFFFFU), 24);
	}
	if (config.groupConfig & HistoryFormat::batteryBit) {
		const uint32_t batteryVoltage = scaleUnsigned(double(battery.getVBAT()) - 2.304, 50.0, 127);
		const uint32_t inputVoltage = scaleUnsigned(double(battery.getVBUS()) - 2.6, 10.0, 127);
		const uint32_t chargeCurrent = scaleUnsigned(battery.getICHG(), 20.0, 127);
		const uint32_t batteryLevel = Batt.isBatteryPresent() ? scaleUnsigned(Batt.getPercent(), 0.05, 5) : 7U;
		writeBits(dest, bitOffset, batteryVoltage, 7);
		writeBits(dest, bitOffset, inputVoltage, 7);
		writeBits(dest, bitOffset, chargeCurrent, 7);
		writeBits(dest, bitOffset, batteryLevel, 3);
	}
}

// Escribe bits menos significativos primero dentro del stream de la muestra.
void HistoryRecorder::writeBits(uint8_t* dest, uint16_t& bitOffset, uint32_t value, uint8_t bits)
{
	for (uint8_t bit = 0; bit < bits; ++bit, ++bitOffset) {
		if (value & (1UL << bit)) dest[bitOffset >> 3] |= uint8_t(1U << (bitOffset & 7));
	}
}

// Escala y limita un valor positivo para que nunca desborde su representacion.
uint32_t HistoryRecorder::scaleUnsigned(double value, double scale, uint32_t maximum)
{
	if (!std::isfinite(value) || !(value > 0)) return 0;
	const double scaled = value * scale;
	return scaled >= maximum ? maximum : uint32_t(std::llround(scaled));
}

// Escala un valor con signo y devuelve sus bits en complemento a dos.
uint32_t HistoryRecorder::scaleSigned(float value, float scale, uint8_t bits)
{
	const int32_t minimum = -(1L << (bits - 1));
	const int32_t maximum = (1L << (bits - 1)) - 1;
	int32_t scaled = int32_t(lroundf(value * scale));
	if (scaled < minimum) scaled = minimum;
	if (scaled > maximum) scaled = maximum;
	return uint32_t(scaled) & ((1UL << bits) - 1U);
}

// Carga o migra el perfil NVS; schema identifica el bitmap guardado, no la version de pagina.
void HistoryRecorder::loadConfig()
{
	Preferences preferences;
	if (!preferences.begin("history", true)) return;
	HistoryRecorderConfig saved;
	saved.enabled = preferences.getBool("enabled", true);
	const uint8_t schema = preferences.getUChar("schema", 1);
	const bool hasStoredGroup = preferences.isKey("group");
	const uint32_t storedGroup = preferences.getUInt("group", 0);
	if (!hasStoredGroup) saved.groupConfig = standardGroupConfig;
	else if (schema >= historyConfigSchema) saved.groupConfig = storedGroup;
	else if (schema == 2) saved.groupConfig = migrateVersion2GroupConfig(storedGroup);
	else saved.groupConfig = completeVersion1GroupConfig(storedGroup);
	saved.samplePeriod = preferences.getUShort("period", standardSamplePeriod);
	preferences.end();
	bool migrated = schema < historyConfigSchema || !hasStoredGroup;
	if (!saved.samplePeriod || saved.samplePeriod > HistoryFormat::maxSamplePeriodDeciseconds) {
		saved.samplePeriod = standardSamplePeriod;
		migrated = true;
	}
	if (!isConfigSupported(saved)) {
		saved.groupConfig = standardGroupConfig;
		migrated = true;
	}
	config = saved;
	if (migrated) saveConfig();
}

// Guarda el perfil activo con claves cortas para reducir uso de NVS.
void HistoryRecorder::saveConfig() const
{
	Preferences preferences;
	if (!preferences.begin("history", false)) return;
	preferences.putBool("enabled", config.enabled);
	preferences.putUInt("group", config.groupConfig);
	preferences.putUShort("period", config.samplePeriod);
	preferences.putUChar("schema", historyConfigSchema);
	preferences.end();
}

// Completa el bitmap compacto de NVS esquema 1; groupConfig conserva las opciones previas.
uint32_t HistoryRecorder::completeVersion1GroupConfig(uint32_t groupConfig)
{
	return (groupConfig & 0x3FFFU) | HistoryFormat::energyBit |
		(uint32_t(HistoryFormat::energyExtraReactiveTotal) << HistoryFormat::energyExtraModeShift);
}

// Traduce el bitmap V2 guardado en NVS al bitmap V1 unificado; stored contiene el grupo persistido.
uint32_t HistoryRecorder::migrateVersion2GroupConfig(uint32_t stored)
{
	uint32_t group = 0;
	const uint8_t voltageMode = (stored >> HistoryFormat::temporaryV2VoltageModeShift) & HistoryFormat::temporaryV2VoltageModeMask;
	if (voltageMode == 2) group |= HistoryFormat::voltageFullBit;
	group |= ((stored >> HistoryFormat::temporaryV2ActivePowerModeShift) & 3U) << HistoryFormat::activePowerModeShift;
	group |= ((stored >> HistoryFormat::temporaryV2ReactivePowerModeShift) & 3U) << HistoryFormat::reactivePowerModeShift;
	group |= ((stored >> HistoryFormat::temporaryV2ApparentPowerModeShift) & 3U) << HistoryFormat::apparentPowerModeShift;
	if (stored & HistoryFormat::temporaryV2PowerFactorBit) group |= HistoryFormat::powerFactorBit;
	if (stored & HistoryFormat::temporaryV2VoltageThdBit) group |= HistoryFormat::voltageThdBit;
	if (stored & HistoryFormat::temporaryV2CurrentThdBit) group |= HistoryFormat::currentThdBit;
	group |= ((stored >> HistoryFormat::temporaryV2FrequencyModeShift) & 3U) << HistoryFormat::frequencyModeShift;
	if (stored & HistoryFormat::temporaryV2VoltageAnglesBit) group |= HistoryFormat::voltageAnglesBit;
	if (stored & HistoryFormat::temporaryV2BatteryBit) group |= HistoryFormat::batteryBit;
	if (stored & HistoryFormat::temporaryV2EnergyBit) group |= HistoryFormat::energyBit;
	group |= ((stored >> HistoryFormat::temporaryV2EnergyExtraModeShift) & 7U) << HistoryFormat::energyExtraModeShift;
	return group;
}
