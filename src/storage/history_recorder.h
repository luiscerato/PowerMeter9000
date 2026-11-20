#pragma once

#include <Arduino.h>
#include "history_store.h"
#include "meter.h"

struct HistoryRecorderConfig {
	bool enabled = true;
	uint32_t groupConfig = 0;
	uint16_t samplePeriod = 10;
};

// Empaqueta la muestra periodica acordada y la envia al escritor historico.
class HistoryRecorder {
public:
	// Asocia el recorder con el escritor que conserva las paginas de historico.
	explicit HistoryRecorder(HistoryStore& store);

	// Carga el perfil una vez desde setup, no desde un constructor global; sin parametros.
	void begin();

	// Procesa una instantanea de medicion; meter contiene los valores RMS, potencia y frecuencia actuales.
	void update(const meterValues& meter);

	// Fuerza el cierre de una pagina parcial cuando se detiene o cambia el reloj.
	bool flush();

	// Copia el perfil activo en config y su estado en status si se solicita; no consulta NVS.
	void getConfig(HistoryRecorderConfig& config, const char** status = nullptr) const;

	// Valida si el firmware puede guardar config con el formato y el tamano de muestra actuales.
	static bool isConfigSupported(const HistoryRecorderConfig& config);

	// Cambia el perfil en tiempo de ejecucion y cierra la pagina actual para no mezclar formatos.
	bool setConfig(const HistoryRecorderConfig& config);

	// Devuelve la configuracion fija del perfil estandar de historico.
	static constexpr uint32_t standardGroupConfig =
		(uint32_t(HistoryFormat::powerModeFull) << HistoryFormat::activePowerModeShift) |
		(uint32_t(HistoryFormat::powerModeFull) << HistoryFormat::reactivePowerModeShift) |
		HistoryFormat::powerFactorBit |
		(uint32_t(HistoryFormat::powerModeFull) << HistoryFormat::frequencyModeShift) |
		HistoryFormat::energyBit |
		(uint32_t(HistoryFormat::energyExtraReactiveTotal) << HistoryFormat::energyExtraModeShift);
	static constexpr uint16_t standardSamplePeriod = 10;
	static constexpr size_t standardSampleSize = 52;
	static constexpr uint16_t standardSamplesPerPage = HistoryFormat::payloadSize / standardSampleSize;
	static constexpr size_t maxSampleSize = 97;

private:
	HistoryStore& store;
	uint32_t nextSampleMs = 0;
	bool sampleScheduleReady = false;
	bool lastClockValid = false;
	bool timestampReferenceReady = false;
	bool activePageEstimated = false;
	uint32_t estimatedTimestampBase = 0;
	uint32_t estimatedTimestampStartedMs = 0;
	uint32_t activePageStartedMs = 0;
	HistoryRecorderConfig config;
	HistoryRecorderConfig pendingConfig;
	mutable portMUX_TYPE configMutex = portMUX_INITIALIZER_UNLOCKED;
	uint8_t configState = 0;

	// Persiste y aplica una solicitud desde el loop del recorder, fuera de capturas activas; sin parametros.
	void pollConfig();

	// Construye STATUS y los paquetes del perfil fijo dentro de dest; meter aporta la instantanea.
	void packSample(uint8_t* dest, const meterValues& meter, bool clockValid) const;

	// Lee el perfil persistente y conserva el estandar si la entrada NVS no es valida.
	void loadConfig();

	// Persiste el perfil validado para conservarlo despues de reiniciar.
	bool saveConfig(const HistoryRecorderConfig& value) const;

	// Escribe un valor unsigned en un stream little-endian de bits; bitOffset se actualiza.
	static void writeBits(uint8_t* dest, uint16_t& bitOffset, uint32_t value, uint8_t bits);

	// Convierte value a entero unsigned escalado y limitado; scale define unidades por bit y max el limite.
	static uint32_t scaleUnsigned(double value, double scale, uint32_t maximum);

	// Convierte value a complemento a dos con bits utiles; scale define unidades por bit.
	static uint32_t scaleSigned(float value, float scale, uint8_t bits);

	// Completa el bitmap compacto de NVS esquema 1; groupConfig conserva las opciones previas.
	static uint32_t completeVersion1GroupConfig(uint32_t groupConfig);

	// Traduce el bitmap V2 de NVS al bitmap V1 unificado; groupConfig contiene la configuracion almacenada.
	static uint32_t migrateVersion2GroupConfig(uint32_t groupConfig);
};
