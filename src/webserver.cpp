#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "esp_system.h"
#include <esp_heap_caps.h>
#include "Preferences.h"
#include "webserver.h"
#include "wifiUtils.h"
#include "meter.h"
#include "meter_timing.h"
#include "event_capture_config.h"
#include "bl0942/bl0942.h"
#include "manager_api.h"
#include "storage/winbond_spi_nand.h"
#include "storage/nand_benchmark.h"
#include "storage/storage_manager.h"
#include "diagnostic_logger.h"
#include "rtc_clock.h"
#include "storage/nand_read_session.h"
#include "storage/history_read_session.h"
#include "storage/diagnostic_read_session.h"
#include "storage/history_recorder.h"
#include "web/webfs_http.h"
#include "web/events_http.h"
#include "event_settings.h"
#include <math.h>
#include <new>
#include <stdlib.h>
#include <errno.h>


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket scopeWS("/ws");
AsyncWebSocket remoteScreen("/screen");
extern WinbondSpiNand eventFlash;
extern bool flashDetected;
extern BL0942 bl0942;
extern StorageManager storageManager;
extern HistoryRecorder historyRecorder;
// Las descargas binarias comparten dos paginas; una segunda solicitud recibe ocupado.
static NandReadSession nandReadSession;

// Identifica el origen de un binario; wave elige particion, buffer/size reciben MAC y generacion.
static bool captureCacheIdentity(bool wave, char* buffer, size_t size)
{
	uint64_t fastEpoch = 0, waveEpoch = 0;
	const auto id = wave ? StoragePartitionId::waveEvents : StoragePartitionId::gridEvents;
	if (!storageManager.isReady() || storageManager.partition(id).isFormatting() ||
		!storageManager.getEventCacheEpochs(fastEpoch, waveEpoch)) return false;
	snprintf(buffer, size, "%012llx:%016llx", ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL, wave ? waveEpoch : fastEpoch);
	return true;
}

// Vincula la respuesta con su generacion; response es el binario y wave indica su particion.
static void addCaptureCacheIdentity(AsyncWebServerResponse* response, bool wave)
{
	char identity[32];
	if (captureCacheIdentity(wave, identity, sizeof(identity))) response->addHeader("X-Capture-Identity", identity);
	response->addHeader("Access-Control-Expose-Headers", "X-Capture-Identity");
}
static constexpr uint8_t historyReadSessionCount = 5;
static HistoryReadSession historyReadSessions[historyReadSessionCount];
static DiagnosticReadSession diagnosticReadSessions[2];
static StaticSemaphore_t diagnosticReadControl;
static SemaphoreHandle_t diagnosticReadMutex = nullptr;

// Convierte el estado de montaje NAND a un identificador JSON estable.
static const char* storageStateName(StorageState state)
{
	switch (state) {
	case StorageState::unformatted: return "unformatted";
	case StorageState::formatting: return "formatting";
	case StorageState::ready: return "ready";
	case StorageState::formatError: return "format_error";
	default: return "not_detected";
	}
}

// Agrega el estado de formato a json usando la capacidad de info y el avance de progress.
static void appendPartitionStatus(String& json, const NandPartitionInfo& info,
	const NandPartitionFormatProgress& progress)
{
	json += "{\"blocks\":";
	json += info.blockCount;
	json += ",\"usableBlocks\":";
	json += info.usableBlockCount;
	json += ",\"enabled\":";
	json += info.enabled ? "true" : "false";
	if (!progress.active && !progress.done) {
		json += "}";
		return;
	}
	json += ",\"format\":{\"active\":";
	json += progress.active ? "true" : "false";
	json += ",\"done\":";
	json += progress.done ? "true" : "false";
	json += ",\"success\":";
	json += progress.success ? "true" : "false";
	json += ",\"bytesDone\":";
	json += progress.bytesDone;
	json += ",\"bytesErased\":";
	json += progress.bytesErased;
	json += ",\"totalBytes\":";
	json += progress.totalBytes;
	json += ",\"blocksDone\":";
	json += progress.blocksDone;
	json += ",\"blocksTotal\":";
	json += progress.blocksTotal;
	json += ",\"failedBlocks\":";
	json += progress.failedBlocks;
	json += ",\"percent\":";
	json += progress.percent;
	json += "}}";
}

// Convierte el motivo de reinicio ESP en una etiqueta estable para diagnostico web.
static const char* resetReasonName(esp_reset_reason_t reason)
{
	switch (reason) {
	case ESP_RST_POWERON: return "power_on";
	case ESP_RST_EXT: return "external";
	case ESP_RST_SW: return "software";
	case ESP_RST_PANIC: return "panic";
	case ESP_RST_INT_WDT: return "interrupt_watchdog";
	case ESP_RST_TASK_WDT: return "task_watchdog";
	case ESP_RST_WDT: return "other_watchdog";
	case ESP_RST_DEEPSLEEP: return "deep_sleep";
	case ESP_RST_BROWNOUT: return "brownout";
	case ESP_RST_SDIO: return "sdio";
	default: return "unknown";
	}
}

// Convierte una operacion NAND al identificador estable publicado por el estado web.
static const char* nandOperationName(NandOperation operation)
{
	switch (operation) {
	case NandOperation::readPage: return "read";
	case NandOperation::programPage: return "program";
	case NandOperation::eraseBlock: return "erase";
	default: return "read_bad_block_marker";
	}
}

// Convierte un error NAND al identificador estable publicado por el estado web.
static const char* nandIoErrorName(NandIoError error)
{
	switch (error) {
	case NandIoError::none: return "none";
	case NandIoError::notRunning: return "not_running";
	case NandIoError::queueFull: return "queue_full";
	case NandIoError::invalidRequest: return "invalid_request";
	case NandIoError::timeout: return "timeout";
	case NandIoError::nandFailure: return "nand_failure";
	default: return "completion_queue_full";
	}
}

// Calcula bytes por segundo sin perder precision en el producto intermedio.
static uint32_t nandRateBytesPerSecond(uint64_t bytes, uint64_t elapsedUs)
{
	if (!elapsedUs) return 0;
	const uint64_t rate = (bytes * 1000000ULL) / elapsedUs;
	return rate > UINT32_MAX ? UINT32_MAX : uint32_t(rate);
}

// Lee un parametro decimal de 32 bits; request contiene la URL y name identifica el parametro.
static bool getUint32Parameter(AsyncWebServerRequest* request, const char* name, uint32_t& value, bool post = false)
{
	if (!request->hasParam(name, post)) return false;
	const String text = request->getParam(name, post)->value();
	char* end = nullptr;
	const unsigned long parsed = strtoul(text.c_str(), &end, 10);
	if (!end || end == text.c_str() || *end || parsed > UINT32_MAX) return false;
	value = uint32_t(parsed);
	return true;
}

// Agrega texto escapado como valor JSON; text y length definen el tramo original.
static void appendJsonString(String& json, const char* text, size_t length)
{
	static const char hex[] = "0123456789abcdef";
	json += '"';
	for (size_t index = 0; index < length; ++index) {
		const uint8_t value = uint8_t(text[index]);
		if (value == '"' || value == '\\') { json += '\\'; json += char(value); }
		else if (value == '\n') json += "\\n";
		else if (value == '\r') json += "\\r";
		else if (value == '\t') json += "\\t";
		else if (value < 0x20) {
			json += "\\u00";
			json += hex[value >> 4];
			json += hex[value & 0x0F];
		} else json += char(value);
	}
	json += '"';
}

// Devuelve una etiqueta corta para el nivel registrado; level identifica el enum.
static const char* diagnosticLevelName(uint8_t level)
{
	switch (DiagnosticLevel(level)) {
	case DiagnosticLevel::error: return "ERR";
	case DiagnosticLevel::warn: return "WRN";
	case DiagnosticLevel::info: return "INF";
	case DiagnosticLevel::verbose: return "VRB";
	case DiagnosticLevel::debug: return "DBG";
	default: return "OFF";
	}
}

// Rechaza subrutas no registradas; request es la solicitud y path la ruta exacta esperada.
static bool requireExactPath(AsyncWebServerRequest* request, const char* path)
{
	if (request->url() == path) return true;
	request->send(404, "application/json", "{\"error\":\"not_found\"}");
	return false;
}

// Lee name como entero decimal de 64 bits; request aporta el parametro y value recibe el resultado.
static bool getUint64Parameter(AsyncWebServerRequest* request, const char* name, uint64_t& value) {
    if (!request->hasParam(name)) return false;
    const String& text = request->getParam(name)->value();
    if (!text.length() || text[0] < '0' || text[0] > '9') return false;
    char* end = nullptr; errno = 0;
    const unsigned long long parsed = strtoull(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end) return false;
    value = parsed; return true;
}

// Devuelve una sesion de descarga libre; las sesiones conservan buffers solo mientras transmiten.
static HistoryReadSession* findHistoryReadSession()
{
	for (HistoryReadSession& session : historyReadSessions) {
		if (session.isAvailable()) return &session;
	}
	return nullptr;
}

// Convierte una cabecera Grid al resumen compacto usado por el visor web.
static void appendGridEventJson(String& json, const GridEventIndexEntry& entry)
{
	const GridEventFormat::ListHeader& header = entry.header;
	const double changedAt = double(header.unixSeconds) * 1000.0 + header.fraction100Us / 10.0;
	const uint64_t previousAt = changedAt >= uint64_t(header.previousStateDuration10Ms) * 10ULL ?
		changedAt - uint64_t(header.previousStateDuration10Ms) * 10ULL : 0;
	const uint8_t subject = header.subjectAndStates & 0x07U;
	const uint8_t previous = header.subjectAndStates >> 3 & 0x07U;
	const uint8_t current = header.subjectAndStates >> 6 & 0x07U;
	const bool point = GridEventFormat::isPointEvent(header.subjectAndStates);
	const char* subjects[] = { "R", "S", "T", "N", "G" };
	const char* phaseStates[] = { "?", "N", "L", "H", "X", "C", "?", "?" };
	const char* gridStates[] = { "?", "N", "A", "O", "?", "?", "?", "?" };
	const char* const* states = subject == uint8_t(GridEventFormat::Subject::grid) ? gridStates : phaseStates;
	json += "{\"i\":";
	json += header.eventId;
	json += ",\"y\":\"";
	json += subject < 5 ? subjects[subject] : "?";
	json += "\",\"p\":\"";
	json += states[previous];
	json += "\",\"a\":";
	managerAppendUint64(json, previousAt);
	json += ",\"s\":\"";
	json += states[current];
	json += "\",\"t\":";
	json += String(changedAt, 1);
	json += ",\"v\":[";
	json += header.previousVoltage;
	json += ',';
	json += header.currentVoltage;
	json += ']';
	if (!point && (header.subjectAndStates & 0x0400U)) json += ",\"c\":true";
	if (point) {
		json += ",\"k\":";
		json += (header.subjectAndStates >> 10) & 31U;
		json += ",\"l\":";
		json += (header.subjectAndStates & 0x8000U) ? "true" : "false";
	}
	if (header.timeStatus & GridEventFormat::timestampEstimated) json += ",\"e\":1";
	json += '}';
}

enum class HistoryRangeStatus : uint8_t { ready, invalid, empty };

// Resuelve filtros last o from/to a un intervalo Unix concreto; request identifica la consulta recibida.
static HistoryRangeStatus resolveHistoryRange(AsyncWebServerRequest* request, uint32_t& fromTimestamp, uint32_t& toTimestamp)
{
	uint32_t lastSeconds = 0;
	const bool hasFrom = getUint32Parameter(request, "from", fromTimestamp);
	const bool hasTo = getUint32Parameter(request, "to", toTimestamp);
	const bool hasLast = getUint32Parameter(request, "last", lastSeconds);
	if (request->hasParam("from") && !hasFrom || request->hasParam("to") && !hasTo || request->hasParam("last") && !hasLast)
		return HistoryRangeStatus::invalid;
	if (hasLast && (hasFrom || hasTo || !lastSeconds)) return HistoryRangeStatus::invalid;
	if (hasLast || (!hasFrom && !hasTo)) {
		uint32_t oldestTimestamp = 0, newestTimestamp = 0;
		storageManager.history().getTimestampRange(oldestTimestamp, newestTimestamp);
		if (!newestTimestamp) return HistoryRangeStatus::empty;
		if (!hasLast) lastSeconds = 300;
		toTimestamp = newestTimestamp;
		fromTimestamp = toTimestamp > lastSeconds ? toTimestamp - lastSeconds : 0;
		return HistoryRangeStatus::ready;
	}
	return hasFrom && hasTo && fromTimestamp <= toTimestamp ? HistoryRangeStatus::ready : HistoryRangeStatus::invalid;
}

// Agrega al JSON los datos de capacidad del perfil; json es destino, config el perfil y status el estado NAND.
static void appendHistoryEstimate(String& json, const HistoryRecorderConfig& config, const StorageManagerStatus& status)
{
	const size_t sampleSize = HistoryFormat::sampleSize(config.groupConfig);
	const uint32_t samplesPerPage = sampleSize ? HistoryFormat::payloadSize / sampleSize : 0;
	const bool available = status.state == StorageState::ready && status.history.enabled && samplesPerPage;
	const uint32_t usableBlocks = available ? status.history.usableBlockCount : 0;
	const uint32_t capacityPages = usableBlocks * WinbondSpiNand::pagesPerBlock;
	const uint64_t estimatedSamples = uint64_t(capacityPages) * samplesPerPage;
	const uint64_t estimatedDuration = estimatedSamples * config.samplePeriod / 10ULL;
	json += "\"sampleSize\":";
	json += sampleSize;
	json += ",\"samplesPerPage\":";
	json += samplesPerPage;
	json += ",\"ready\":";
	json += available ? "true" : "false";
	json += ",\"usableBlocks\":";
	json += usableBlocks;
	json += ",\"capacityPages\":";
	json += capacityPages;
	json += ",\"estimatedSamples\":";
	managerAppendUint64(json, estimatedSamples);
	json += ",\"duration\":";
	managerAppendUint64(json, estimatedDuration);
}

// Devuelve el nombre del modo de un grupo; groupConfig contiene el bitmap y shift la posicion del campo.
static const char* historyModeName(uint32_t groupConfig, uint8_t shift)
{
	const uint8_t mode = (groupConfig >> shift) & 0x03U;
	return mode == HistoryFormat::powerModeFull ? "full" :
		mode == HistoryFormat::powerModeCompact ? "compact" : "off";
}

// Lee un parametro de consulta o formulario; request es la solicitud y body el origen esperado.
static bool getHistoryParam(AsyncWebServerRequest* request, const char* name, bool body, String& value)
{
	if (!request->hasParam(name, body)) return false;
	value = request->getParam(name, body)->value();
	return true;
}

// Interpreta un booleano true/false o 1/0; text es el valor recibido y result el destino.
static bool parseHistoryBool(const String& text, bool& result)
{
	if (text.equalsIgnoreCase("true") || text == "1") { result = true; return true; }
	if (text.equalsIgnoreCase("false") || text == "0") { result = false; return true; }
	return false;
}

// Convierte el nombre de nivel recibido en el enum del logger.
static bool parseDiagnosticLevel(const String& text, DiagnosticLevel& level)
{
	if (text.equalsIgnoreCase("none") || text.equalsIgnoreCase("off")) level = DiagnosticLevel::none;
	else if (text.equalsIgnoreCase("error")) level = DiagnosticLevel::error;
	else if (text.equalsIgnoreCase("warn")) level = DiagnosticLevel::warn;
	else if (text.equalsIgnoreCase("info")) level = DiagnosticLevel::info;
	else if (text.equalsIgnoreCase("verbose")) level = DiagnosticLevel::verbose;
	else if (text.equalsIgnoreCase("debug")) level = DiagnosticLevel::debug;
	else return false;
	return true;
}

// Convierte off/compact/full en el modo del formato; text es entrada y mode el resultado.
static bool parseHistoryMode(const String& text, uint8_t& mode)
{
	if (text.equalsIgnoreCase("off")) { mode = HistoryFormat::powerModeOff; return true; }
	if (text.equalsIgnoreCase("compact")) { mode = HistoryFormat::powerModeCompact; return true; }
	if (text.equalsIgnoreCase("full")) { mode = HistoryFormat::powerModeFull; return true; }
	return false;
}

// Construye la configuracion desde opciones legibles; body selecciona POST o query y includeEnabled incluye el interruptor.
static bool parseHistoryOptions(AsyncWebServerRequest* request, bool body, bool includeEnabled, HistoryRecorderConfig& config)
{
	String text;
	if (includeEnabled) {
		if (!getHistoryParam(request, "enabled", body, text) || !parseHistoryBool(text, config.enabled)) return false;
	}
	if (!getHistoryParam(request, "samplePeriod", body, text)) return false;
	char* end = nullptr;
	const double seconds = strtod(text.c_str(), &end);
	if (!end || end == text.c_str() || *end || !isfinite(seconds) || seconds < 0.1 || seconds > 300.0) return false;
	const double deciseconds = seconds * 10.0;
	const long roundedPeriod = lround(deciseconds);
	if (fabs(deciseconds - roundedPeriod) > 0.001) return false;
	config.samplePeriod = uint16_t(roundedPeriod);

	uint32_t groupConfig = 0;
	if (!getHistoryParam(request, "voltageMode", body, text)) return false;
	if (text.equalsIgnoreCase("full")) groupConfig |= HistoryFormat::voltageFullBit;
	else if (!text.equalsIgnoreCase("basic")) return false;
	const char* const modeNames[] = {"activePower", "reactivePower", "apparentPower", "frequency"};
	const uint8_t modeShifts[] = {HistoryFormat::activePowerModeShift, HistoryFormat::reactivePowerModeShift,
		HistoryFormat::apparentPowerModeShift, HistoryFormat::frequencyModeShift};
	for (uint8_t index = 0; index < 4; ++index) {
		uint8_t mode = 0;
		if (!getHistoryParam(request, modeNames[index], body, text) || !parseHistoryMode(text, mode)) return false;
		groupConfig |= uint32_t(mode) << modeShifts[index];
	}
	const char* const boolNames[] = {"powerFactor", "voltageThd", "currentThd", "voltageAngles", "battery", "energy"};
	const uint32_t boolBits[] = {HistoryFormat::powerFactorBit, HistoryFormat::voltageThdBit, HistoryFormat::currentThdBit,
		HistoryFormat::voltageAnglesBit, HistoryFormat::batteryBit, HistoryFormat::energyBit};
	bool energyEnabled = false;
	for (uint8_t index = 0; index < 6; ++index) {
		bool enabled = false;
		if (!getHistoryParam(request, boolNames[index], body, text) || !parseHistoryBool(text, enabled)) return false;
		if (enabled) groupConfig |= boolBits[index];
		if (index == 5) energyEnabled = enabled;
	}
	if (!getHistoryParam(request, "energyExtraMode", body, text)) return false;
	char* modeEnd = nullptr;
	const unsigned long energyMode = strtoul(text.c_str(), &modeEnd, 10);
	if (!modeEnd || modeEnd == text.c_str() || *modeEnd || energyMode > 7) return false;
	if (energyEnabled) groupConfig |= uint32_t(energyMode) << HistoryFormat::energyExtraModeShift;
	config.groupConfig = groupConfig;
	return true;
}

// Serializa unicamente las opciones del historico; config contiene el perfil activo.
static String historyConfigJson(const HistoryRecorderConfig& config)
{
	String json = "{\"enabled\":";
	json += config.enabled ? "true" : "false";
	json += ",\"samplePeriod\":";
	json += config.samplePeriod / 10.0f;
	json += ",\"voltageMode\":\"";
	json += config.groupConfig & HistoryFormat::voltageFullBit ? "full" : "basic";
	json += "\",\"activePower\":\"";
	json += historyModeName(config.groupConfig, HistoryFormat::activePowerModeShift);
	json += "\",\"reactivePower\":\"";
	json += historyModeName(config.groupConfig, HistoryFormat::reactivePowerModeShift);
	json += "\",\"apparentPower\":\"";
	json += historyModeName(config.groupConfig, HistoryFormat::apparentPowerModeShift);
	json += "\",\"powerFactor\":";
	json += config.groupConfig & HistoryFormat::powerFactorBit ? "true" : "false";
	json += ",\"voltageThd\":";
	json += config.groupConfig & HistoryFormat::voltageThdBit ? "true" : "false";
	json += ",\"currentThd\":";
	json += config.groupConfig & HistoryFormat::currentThdBit ? "true" : "false";
	json += ",\"frequency\":\"";
	json += historyModeName(config.groupConfig, HistoryFormat::frequencyModeShift);
	json += "\",\"voltageAngles\":";
	json += config.groupConfig & HistoryFormat::voltageAnglesBit ? "true" : "false";
	json += ",\"battery\":";
	json += config.groupConfig & HistoryFormat::batteryBit ? "true" : "false";
	json += ",\"energy\":";
	json += config.groupConfig & HistoryFormat::energyBit ? "true" : "false";
	json += ",\"energyExtraMode\":";
	json += config.groupConfig & HistoryFormat::energyBit ?
		((config.groupConfig >> HistoryFormat::energyExtraModeShift) & 0x07U) : 0;
	json += "}";
	return json;
}

// Agrega metricas de una operacion NAND; json recibe el objeto y operation selecciona sus acumulados.
static void appendNandOperationMetrics(String& json, const char* name, const NandIoMetrics& metrics, NandOperation operation)
{
	const uint8_t index = uint8_t(operation);
	json += "\"";
	json += name;
	json += "\":{\"completed\":";
	json += metrics.completed[index];
	json += ",\"failed\":";
	json += metrics.failed[index];
	json += ",\"timeouts\":";
	json += metrics.timeouts[index];
	json += ",\"lastBytes\":";
	json += metrics.lastTransferredBytes[index];
	json += ",\"lastElapsedUs\":";
	json += metrics.lastElapsedUs[index];
	json += ",\"lastBytesPerSecond\":";
	json += nandRateBytesPerSecond(metrics.lastTransferredBytes[index], metrics.lastElapsedUs[index]);
	json += ",\"averageBytesPerSecond\":";
	json += nandRateBytesPerSecond(
		operation == NandOperation::readPage ? metrics.bytesRead :
		operation == NandOperation::programPage ? metrics.bytesWritten : 0,
		metrics.totalElapsedUs[index]);
	const uint32_t operationCount = metrics.completed[index] + metrics.failed[index];
	json += ",\"averageElapsedUs\":";
	json += operationCount ? uint32_t(metrics.totalElapsedUs[index] / operationCount) : 0;
	json += ",\"maxElapsedUs\":";
	json += metrics.maxElapsedUs[index];
	json += "}";
}


WSeventFunctionCallback meterEvents = nullptr, screenEvents = nullptr;		//Punteros a los callback de los eventos de los websockets
//

enum class BackupOptionType : uint8_t { invalid, string, boolean, signedInt, unsignedInt, floatingPoint };

struct BackupOptionValue {
	String key;
	String text;
	BackupOptionType type;
	int32_t signedValue;
	uint32_t unsignedValue;
	float floatValue;
	bool boolValue;
};

// Comprueba si una clave pertenece a una lista cerrada.
// key: clave NVS. keys: lista permitida. count: cantidad de elementos.
static bool backupKeyInList(const String& key, const char* const* keys, size_t count)
{
	for (size_t i = 0; i < count; ++i) if (key == keys[i]) return true;
	return false;
}

// Resuelve el tipo persistente permitido para una opcion importada.
// nameSpace: namespace NVS. key: clave que se desea importar.
static BackupOptionType backupOptionType(const String& nameSpace, const String& key)
{
	static const char* const wifiBool[] = {"staticIp", "ap-hide", "ap-custom-ip", "ota-enabled", "time-sync", "debug-enabled", "mqtt-enabled"};
	static const char* const wifiSigned[] = {"wifi-mode", "time-dsttime", "time-minwest"};
	static const char* const wifiUnsigned[] = {"time-start", "mqtt-port"};
	static const char* const wifiStrings[] = {"sta-ssid", "sta-pass", "sta-ip", "sta-mask", "sta-gw", "sta-dns1", "sta-dns2",
		"ap-ssid", "ap-pass", "ap-ip", "ap-mask", "ap-gw", "ota-name", "ota-pass", "time-server",
		"debug-name", "debug-pass", "mqtt-server", "mqtt-user", "mqtt-pass"};
	static const char* const meterBool[] = {"dipEnabled", "swellEnabled", "overcurrent"};
	static const char* const meterFloat[] = {"ocCurrent", "ocHyst", "rWattH", "rVarH", "rVaH",
		"sWattH", "sVarH", "sVaH", "tWattH", "tVarH", "tVaH", "WattH", "VarH", "VaH"};
	static const char* const meterSigned[] = {"dipMs", "swellMs", "ocMs"};
	static const char* const calibration[] = {"APHCAL0", "BPHCAL0", "CPHCAL0", "NPHCAL", "AIGAIN", "BIGAIN", "CIGAIN", "NIGAIN",
		"AVGAIN", "BVGAIN", "CVGAIN", "AIRMSOS", "BIRMSOS", "CIRMSOS", "NIRMSOS", "AVRMSOS", "BVRMSOS", "CVRMSOS",
		"AIFRMSOS", "BIFRMSOS", "CIFRMSOS", "AVFRMSOS", "BVFRMSOS", "CVFRMSOS", "AIRMSONEOS", "BIRMSONEOS", "CIRMSONEOS",
		"NIRMSONEOS", "AVRMSONEOS", "BVRMSONEOS", "CVRMSONEOS", "AIRMS1012OS", "BIRMS1012OS", "CIRMS1012OS", "NIRMS1012OS",
		"AVRMS1012OS", "BVRMS1012OS", "CVRMS1012OS"};
	if (nameSpace == "wifi-stuff") {
		if (backupKeyInList(key, wifiBool, sizeof(wifiBool) / sizeof(wifiBool[0]))) return BackupOptionType::boolean;
		if (backupKeyInList(key, wifiSigned, sizeof(wifiSigned) / sizeof(wifiSigned[0]))) return BackupOptionType::signedInt;
		if (backupKeyInList(key, wifiUnsigned, sizeof(wifiUnsigned) / sizeof(wifiUnsigned[0]))) return BackupOptionType::unsignedInt;
		if (backupKeyInList(key, wifiStrings, sizeof(wifiStrings) / sizeof(wifiStrings[0]))) return BackupOptionType::string;
	}
	else if (nameSpace == "meter") {
		if (backupKeyInList(key, meterBool, sizeof(meterBool) / sizeof(meterBool[0]))) return BackupOptionType::boolean;
		if (backupKeyInList(key, meterFloat, sizeof(meterFloat) / sizeof(meterFloat[0]))) return BackupOptionType::floatingPoint;
		if (backupKeyInList(key, meterSigned, sizeof(meterSigned) / sizeof(meterSigned[0]))) return BackupOptionType::signedInt;
	}
	else if (nameSpace == "ADE9000" && (key == "neutralRms" || key == "neutralFast"))
		return BackupOptionType::floatingPoint;
	else if (nameSpace == "ADE9000" && backupKeyInList(key, calibration, sizeof(calibration) / sizeof(calibration[0])))
		return BackupOptionType::signedInt;
	return BackupOptionType::invalid;
}

// Valida y convierte el valor recibido antes de tocar NVS.
// item: opcion recibida. nameSpace: namespace que define su esquema.
static bool parseBackupOption(BackupOptionValue& item, const String& nameSpace)
{
	item.type = backupOptionType(nameSpace, item.key);
	if (item.type == BackupOptionType::invalid) return false;
	if (item.type == BackupOptionType::string) return item.text.length() <= 255;
	if (item.type == BackupOptionType::boolean) {
		if (item.text == "1" || item.text.equalsIgnoreCase("true")) item.boolValue = true;
		else if (item.text == "0" || item.text.equalsIgnoreCase("false")) item.boolValue = false;
		else return false;
		return true;
	}
	char* end = nullptr;
	if (item.type == BackupOptionType::floatingPoint) {
		item.floatValue = strtof(item.text.c_str(), &end);
		if (nameSpace == "meter" && (item.key == "ocHyst" || item.key == "ocCurrent") &&
			(item.floatValue < 0 || item.floatValue > 163.82f || (item.key == "ocCurrent" && item.floatValue == 0))) return false;
		if (nameSpace == "ADE9000" && (item.floatValue < 0.1f || item.floatValue > 10)) return false;
		return end != item.text.c_str() && *end == '\0' && isfinite(item.floatValue);
	}
	if (item.type == BackupOptionType::signedInt && nameSpace == "ADE9000" &&
		(item.text.startsWith("0x") || item.text.startsWith("0X"))) {
		const unsigned long raw = strtoul(item.text.c_str(), &end, 16);
		if (end == item.text.c_str() + 2 || *end || raw > UINT32_MAX) return false;
		item.signedValue = static_cast<int32_t>(static_cast<uint32_t>(raw));
		return true;
	}
	if (item.type == BackupOptionType::signedInt) {
		const long long value = strtoll(item.text.c_str(), &end, 10);
		if (end == item.text.c_str() || *end || value < INT32_MIN || value > INT32_MAX) return false;
		if (nameSpace == "meter" && (item.key == "dipMs" || item.key == "swellMs" || item.key == "ocMs") &&
			(value < 10 || value > eventConfirmMaxMs || value % 10)) return false;
		item.signedValue = static_cast<int32_t>(value);
		return true;
	}
	const unsigned long long value = strtoull(item.text.c_str(), &end, 10);
	if (end == item.text.c_str() || *end || item.text.startsWith("-") || value > UINT32_MAX) return false;
	item.unsignedValue = static_cast<uint32_t>(value);
	return true;
}

// Identifica key de eventos que ya pertenece exclusivamente al servicio unificado.
static bool isEventOptionKey(const String& key) {
	const char* keys[] = {"dipEnabled", "swellEnabled", "overcurrent", "dipMs", "swellMs", "ocMs", "ocCurrent", "ocHyst"};
	for (const char* name : keys) if (key == name) return true;
	return false;
}

// Escribe un grupo previamente validado con los tipos que espera cada consumidor.
// request: formulario POST. nameSpace: namespace NVS. restartRequired: indica si hay que reiniciar.
static void writeBackupOptions(AsyncWebServerRequest* request, const String& nameSpace, bool restartRequired)
{
	if (nameSpace == "meter") {
		for (size_t i = 0; i < request->params(); ++i) if (isEventOptionKey(request->getParam(i)->name())) {
			request->send(410, "application/json", "{\"error\":\"use_events_config\"}"); return;
		}
	}
	BackupOptionValue values[40];
	size_t count = 0;
	for (size_t i = 0; i < request->params(); ++i) {
		const AsyncWebParameter* param = request->getParam(i);
		if (!param->isPost() || param->name() == "ns") continue;
		if (count >= sizeof(values) / sizeof(values[0])) {
			request->send(413, "application/json", "{\"ok\":false,\"error\":\"too_many_options\"}");
			return;
		}
		values[count].key = param->name();
		values[count].text = param->value();
		for (size_t j = 0; j < count; ++j) {
			if (values[j].key == values[count].key) {
				request->send(400, "application/json", "{\"ok\":false,\"error\":\"duplicate_option\"}");
				return;
			}
		}
		if (!parseBackupOption(values[count], nameSpace)) {
			request->send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_or_unsupported_option\"}");
			return;
		}
		++count;
	}
	if (!count) {
		request->send(400, "application/json", "{\"ok\":false,\"error\":\"options_required\"}");
		return;
	}
	Preferences options;
	if (!options.begin(nameSpace.c_str(), false)) {
		request->send(503, "application/json", "{\"ok\":false,\"error\":\"preferences_unavailable\"}");
		return;
	}
	size_t writtenCount = 0;
	if (nameSpace == "meter") {
		float current = options.getFloat("ocCurrent", 50), hysteresis = options.getFloat("ocHyst", 1);
		for (size_t i = 0; i < count; ++i) {
			if (values[i].key == "ocCurrent") current = values[i].floatValue;
			if (values[i].key == "ocHyst") hysteresis = values[i].floatValue;
		}
		if (hysteresis >= current) { options.end(); request->send(400, "text/plain", "Histeresis de corriente incompatible"); return; }
	}
	for (; writtenCount < count; ++writtenCount) {
		BackupOptionValue& item = values[writtenCount];
		size_t written = 0;
		switch (item.type) {
		case BackupOptionType::string: written = options.putString(item.key.c_str(), item.text.c_str()); break;
		case BackupOptionType::boolean: written = options.putBool(item.key.c_str(), item.boolValue); break;
		case BackupOptionType::signedInt: written = options.putInt(item.key.c_str(), item.signedValue); break;
		case BackupOptionType::unsignedInt: written = options.putUInt(item.key.c_str(), item.unsignedValue); break;
		case BackupOptionType::floatingPoint: written = options.putFloat(item.key.c_str(), item.floatValue); break;
		default: break;
		}
		if (!written) break;
	}
	options.end();
	if (writtenCount != count) {
		char response[96];
		snprintf(response, sizeof(response), "{\"ok\":false,\"written\":%u,\"total\":%u}", unsigned(writtenCount), unsigned(count));
		request->send(500, "application/json", response);
		return;
	}
	char response[96];
	if (restartRequired)
		snprintf(response, sizeof(response), "{\"ok\":true,\"written\":%u,\"restartRequired\":true}", unsigned(count));
	else
		snprintf(response, sizeof(response), "{\"ok\":true,\"written\":%u}", unsigned(count));
	request->send(200, "application/json", response);
}

// Codifica texto para incluirlo de forma segura en una cadena JSON.
// value: texto de Preferences.
static String quoteJsonString(const String& value)
{
	static const char hex[] = "0123456789abcdef";
	String result;
	result.reserve(value.length() + 2);
	result += '"';
	for (size_t i = 0; i < value.length(); ++i) {
		const uint8_t ch = uint8_t(value[i]);
		if (ch == '"' || ch == '\\') { result += '\\'; result += char(ch); }
		else if (ch == '\n') result += "\\n";
		else if (ch == '\r') result += "\\r";
		else if (ch == '\t') result += "\\t";
		else if (ch < 0x20) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 0x0f]; }
		else result += char(ch);
	}
	result += '"';
	return result;
}

// Agrega al JSON una opción de red persistida, respetando su tipo NVS.
// json: objeto en construcción. options: namespace abierto. key: nombre de la preferencia.
static void appendNetworkOptionJson(String& json, Preferences& options, const char* key)
{
	if (!options.isKey(key)) return;
	if (json.length() > 1) json += ',';
	json += quoteJsonString(String(key));
	json += ':';
	switch (backupOptionType("wifi-stuff", key)) {
	case BackupOptionType::string: json += quoteJsonString(options.getString(key, "")); break;
	case BackupOptionType::boolean: json += options.getBool(key, false) ? "true" : "false"; break;
	case BackupOptionType::signedInt: json += options.getInt(key, 0); break;
	case BackupOptionType::unsignedInt: json += options.getUInt(key, 0); break;
	default: json += "null"; break;
	}
}

// Devuelve los ajustes persistidos de Wi-Fi y servicios, incluidas sus credenciales.
// request: solicitud HTTP GET.
static void handleNetworkConfigGet(AsyncWebServerRequest* request)
{
	static const char* const keys[] = {
		"wifi-mode", "sta-ssid", "sta-pass", "staticIp", "sta-ip", "sta-mask", "sta-gw", "sta-dns1", "sta-dns2",
		"ap-ssid", "ap-pass", "ap-hide", "ap-custom-ip", "ap-ip", "ap-mask", "ap-gw", "ota-enabled", "ota-name", "ota-pass",
		"time-start", "time-dsttime", "time-minwest", "time-server", "time-sync", "debug-enabled", "debug-name", "debug-pass",
		"mqtt-enabled", "mqtt-server", "mqtt-port", "mqtt-user", "mqtt-pass"
	};
	Preferences options;
	if (!options.begin("wifi-stuff", true)) {
		request->send(503, "application/json", "{\"ok\":false,\"error\":\"preferences_unavailable\"}");
		return;
	}
	String json = "{";
	for (const char* key : keys) appendNetworkOptionJson(json, options, key);
	options.end();
	json += '}';
	auto* response = request->beginResponse(200, "application/json", json);
	response->addHeader("Cache-Control", "no-store");
	request->send(response);
}

// Guarda opciones generales del respaldo a partir del namespace incluido en el POST.
// request: formulario POST con ns y opciones permitidas.
static void handleBackupOptionsPost(AsyncWebServerRequest* request)
{
	if (!request->hasParam("ns", true)) {
		request->send(400, "application/json", "{\"ok\":false,\"error\":\"ns_required\"}");
		return;
	}
	const String nameSpace = request->getParam("ns", true)->value();
	if (nameSpace != "wifi-stuff" && nameSpace != "meter" && nameSpace != "ADE9000") {
		request->send(400, "application/json", "{\"ok\":false,\"error\":\"namespace_not_allowed\"}");
		return;
	}
	writeBackupOptions(request, nameSpace, false);
}

// Guarda parámetros de red enviados como formulario y avisa que requieren reinicio.
// request: formulario POST con opciones del namespace wifi-stuff.
static void handleNetworkConfigPost(AsyncWebServerRequest* request)
{
	writeBackupOptions(request, "wifi-stuff", true);
}

// Conserva la lectura antigua; request POST debe usar la configuracion unificada.
static void handleGridSettings(AsyncWebServerRequest* request) {
	if (request->method() == HTTP_POST) { request->send(410, "application/json", "{\"error\":\"use_events_config\"}"); return; }
	eventSettingsSnapshot settings; eventSettingsGet(settings);
	const gridSettings& config = settings.config.grid;
	char json[640];
	snprintf(json, sizeof(json), "{\"schemaVersion\":1,\"lowR\":%.2f,\"highR\":%.2f,"
		"\"lowS\":%.2f,\"highS\":%.2f,\"lowT\":%.2f,\"highT\":%.2f,"
		"\"noVoltage\":%.2f,\"hysteresis\":%.2f,\"lossConfirmMs\":%u,"
		"\"pointLimitMs\":%u,\"recoveryConfirmMs\":%u,\"capturePages\":%u,"
		"\"neutralHigh\":%.3f,\"neutralHysteresis\":%.3f,\"neutralConfirmMs\":%u,\"neutralRecoveryMs\":%u}",
		config.phases[0].lowVoltage, config.phases[0].highVoltage,
		config.phases[1].lowVoltage, config.phases[1].highVoltage,
		config.phases[2].lowVoltage, config.phases[2].highVoltage,
		config.noVoltage, config.hysteresis, config.lossConfirmMs,
		config.voltageConfirmMs, config.recoveryConfirmMs, config.capturePages,
		config.neutral.highVoltage, config.neutral.hysteresis,
		config.neutral.confirmMs, config.neutral.recoveryMs);
	auto* response = request->beginResponse(200, "application/json", json);
	response->addHeader("Cache-Control", "no-store");
	request->send(response);
}

void webServerSetMeterEvents(WSeventFunctionCallback func)
{
	if (func != nullptr)
		meterEvents = func;
}

AsyncWebSocket* webServerGetMeterWS()
{
	return &scopeWS;
}

// Retira clientes WebSocket desconectados y vence cursores de logs inactivos; sin parametros.
void webServerCleanupClients()
{
	scopeWS.cleanupClients();
	remoteScreen.cleanupClients();
	static uint32_t lastSessionCleanupMs = 0;
	if (uint32_t(millis() - lastSessionCleanupMs) < 1000U) return;
	lastSessionCleanupMs = millis();
	if (!diagnosticReadMutex || xSemaphoreTake(diagnosticReadMutex, 0) != pdTRUE) return;
	for (DiagnosticReadSession& session : diagnosticReadSessions)
		if (session.isExpired()) session.invalidate();
	xSemaphoreGive(diagnosticReadMutex);
}



void Init_WebServer()
{
	static bool CORS = false;
	if (!diagnosticReadMutex) diagnosticReadMutex = xSemaphoreCreateMutexStatic(&diagnosticReadControl);

	debugI("Iniciando servidor web...");
	server.reset();			//Crear el servidor web desde 0
	webFsHttpBegin(server, storageManager);

	scopeWS.onEvent(meterEvents);
	server.addHandler(&scopeWS);

	// server.addHandler(&remoteScreen);

	// Web Server Root URL
	server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) { webFsHttpHandleAsset(request); });

	server.on("/scope.html", HTTP_GET, [](AsyncWebServerRequest* request) {
		AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "");
		response->addHeader("Location", "/index.html#scopePage");
		response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		request->send(response);
		});

	server.on("/nav.html", HTTP_GET, [](AsyncWebServerRequest* request) {
		AsyncWebServerResponse* response = request->beginResponse(302, "text/plain", "");
		response->addHeader("Location", "/index.html");
		response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		request->send(response);
		});

	/*
		Prueba de como enviar un archivo especificando solo la ruta del directorio
		Se controla que la extención sea .log o .raw, sino se devuelve error.
	*/
	server.on("/test/*", HTTP_GET, [](AsyncWebServerRequest* request) {
		static String url;
		url = request->url();
		debugI("test url: %s", url.c_str());

		if (url.endsWith(".log") || url.endsWith(".raw")) {
			request->send("text/plain", 128 * 1024, [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
				//Write up to "maxLen" bytes into "buffer" and return the amount written.
				//index equals the amount of bytes that have been already sent
				//You will not be asked for more bytes once the content length has been reached.
				//Keep in mind that you can not delay or yield waiting for more data!
				//Send what you currently have and you will be asked for more again
				uint32_t count;
				for (count = 0; count < maxLen && index < (128 * 1024); count++)
					*buffer++ = '*';
				return count;
				});
		}
		else if (url.endsWith(".csv")) {
			request->send("text/plain", 16 * 1024 * 1024, [](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
				//Write up to "maxLen" bytes into "buffer" and return the amount written.
				//index equals the amount of bytes that have been already sent
				//You will not be asked for more bytes once the content length has been reached.
				//Keep in mind that you can not delay or yield waiting for more data!
				//Send what you currently have and you will be asked for more again
				uint32_t count;
				for (count = 0; count < maxLen && index < (16 * 1024 * 1024); count++)
					*buffer++ = '*';
				return count;
				});
		}
		else {
			request->send(404, "text/plain", "Page Not Found");
		}

		});

	server.on("/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "ok");
		});

	server.on("/restart", HTTP_POST, [](AsyncWebServerRequest* request) {
		request->send(202, "application/json", "{\"ok\":true,\"restarting\":true}");
		MeterWriteEnergyRegisters();
		delay(100);
		ESP.restart();
		});

	server.on("/energy/reset", HTTP_POST, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "ok");
		MeterWriteEnergyRegisters(true);		//Reiniciar contadores de energía
		});

	server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		String status;
		getStatus(status);
		request->send(200, "application/json", status);
		});

	server.on("/debug/recent", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/debug/recent")) return;
		uint32_t after = 0, before = 0, requestedCount = 16;
		const bool hasAfter = getUint32Parameter(request, "after", after);
		const bool hasBefore = getUint32Parameter(request, "before", before);
		const bool hasCount = getUint32Parameter(request, "count", requestedCount);
		if ((request->hasParam("after") && !hasAfter) || (request->hasParam("before") && !hasBefore) ||
			(request->hasParam("count") && !hasCount) || (after && before) || !requestedCount || requestedCount > 16) {
			request->send(400, "application/json", "{\"error\":\"invalid_cursor_or_count\"}");
			return;
		}
		DiagnosticRecentChunk items[16];
		uint32_t oldest = 0, newest = 0;
		bool gap = false;
		const uint8_t count = diagnosticLogger.getRecentChunks(after, before, items, uint8_t(requestedCount), oldest, newest, gap);
		String json;
		json.reserve(4200);
		json = "{\"oldest\":";
		json += oldest;
		json += ",\"newest\":";
		json += newest;
		json += ",\"gap\":";
		json += gap ? "true" : "false";
		json += ",\"logs\":[";
		for (uint8_t index = 0; index < count; ++index) {
			const DiagnosticChunk& chunk = items[index].chunk;
			if (index) json += ',';
			json += "{\"i\":";
			json += items[index].sequence;
			json += ",\"u\":";
			managerAppendUint64(json, chunk.uptimeUs);
			json += ",\"t\":";
			managerAppendUint64(json, chunk.unixUs);
			json += ",\"m\":";
			json += chunk.moduleId;
			json += ",\"n\":";
			const char* name = "global";
			for (uint8_t module = 0; module < DiagnosticLogger::getModuleCount(); ++module) {
				uint32_t moduleId;
				const char* moduleName;
				if (DiagnosticLogger::getModuleInfo(module, moduleId, moduleName) && moduleId == chunk.moduleId) {
					name = moduleName;
					break;
				}
			}
			appendJsonString(json, name, strlen(name));
			json += ",\"l\":";
			appendJsonString(json, diagnosticLevelName(chunk.level), 3);
			json += ",\"s\":";
			json += chunk.sourceLine;
			json += ",\"f\":";
			json += chunk.flags;
			json += ",\"p\":";
			json += chunk.partIndex;
			json += ",\"x\":";
			appendJsonString(json, chunk.text, chunk.textLength);
			json += '}';
		}
		json += "]}";
		request->send(200, "application/json", json);
	});

	server.on("/debug/nand", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/debug/nand")) return;
		uint32_t countValue = 16, blockValue = 0, pageValue = 0, offsetValue = 0;
		uint32_t anchorBlockValue = 0, anchorPageValue = 0;
		const bool hasCount = getUint32Parameter(request, "count", countValue);
		const bool hasBlock = getUint32Parameter(request, "b", blockValue);
		const bool hasPage = getUint32Parameter(request, "p", pageValue);
		const bool hasOffset = getUint32Parameter(request, "o", offsetValue);
		const bool hasAnchorBlock = getUint32Parameter(request, "ab", anchorBlockValue);
		const bool hasAnchorPage = getUint32Parameter(request, "ap", anchorPageValue);
		const bool hasCursor = hasBlock || hasPage || hasOffset;
		const bool hasAnchor = hasAnchorBlock || hasAnchorPage;
		if ((request->hasParam("count") && !hasCount) || (request->hasParam("b") && !hasBlock) ||
			(request->hasParam("p") && !hasPage) || (request->hasParam("o") && !hasOffset) ||
			(request->hasParam("ab") && !hasAnchorBlock) || (request->hasParam("ap") && !hasAnchorPage) ||
			!countValue || countValue > 16 || (hasCursor && (!hasBlock || !hasPage || !hasOffset || !hasAnchorBlock || !hasAnchorPage)) ||
			(!hasCursor && hasAnchor) || blockValue >= NandPartition::invalidBlock || pageValue >= WinbondSpiNand::pagesPerBlock ||
			offsetValue > UINT16_MAX || anchorBlockValue >= NandPartition::invalidBlock ||
			anchorPageValue >= WinbondSpiNand::pagesPerBlock) {
			request->send(400, "application/json", "{\"error\":\"invalid_cursor_or_count\"}");
			return;
		}
		if (!diagnosticReadMutex || xSemaphoreTake(diagnosticReadMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
			request->send(503, "application/json", "{\"error\":\"diagnostic_reader_unavailable\"}");
			return;
		}
		if (diagnosticLogger.isFormatRunning()) {
			xSemaphoreGive(diagnosticReadMutex);
			request->send(409, "application/json", "{\"error\":\"diagnostic_format_in_progress\"}");
			return;
		}
		DiagnosticStoredChunk items[16];
		uint8_t count = 0, nextPage = 0, nextAnchorPage = 0;
		uint16_t nextBlock = NandPartition::invalidBlock, nextOffset = 0;
		uint16_t nextAnchorBlock = NandPartition::invalidBlock;
		bool done = false;
		const bool readOk = storageManager.diagnostics().readPrevious(items, uint8_t(countValue), hasCursor,
			uint16_t(blockValue), uint8_t(pageValue), uint16_t(offsetValue), hasAnchor,
			uint16_t(anchorBlockValue), uint8_t(anchorPageValue), count, nextBlock, nextPage,
			nextOffset, nextAnchorBlock, nextAnchorPage, done);
		xSemaphoreGive(diagnosticReadMutex);
		if (!readOk) {
			request->send(503, "application/json", "{\"error\":\"diagnostic_storage_busy_or_unavailable\"}");
			return;
		}
		String json;
		json.reserve(4600);
		json = "{\"done\":";
		json += done ? "true" : "false";
		json += ",\"next\":";
		if (done) json += "null";
		else {
			json += "{\"b\":"; json += nextBlock;
			json += ",\"p\":"; json += nextPage;
			json += ",\"o\":"; json += nextOffset;
			json += ",\"ab\":"; json += nextAnchorBlock;
			json += ",\"ap\":"; json += nextAnchorPage;
			json += '}';
		}
		json += ",\"logs\":[";
		for (uint8_t index = 0; index < count; ++index) {
			const DiagnosticChunk& chunk = items[index].chunk;
			if (index) json += ',';
			json += "{\"g\":"; json += items[index].pageSequence;
			json += ",\"r\":"; json += items[index].recordIndex;
			json += ",\"u\":"; managerAppendUint64(json, chunk.uptimeUs);
			json += ",\"t\":"; managerAppendUint64(json, chunk.unixUs);
			json += ",\"m\":"; json += chunk.moduleId;
			json += ",\"n\":";
			const char* name = "global";
			for (uint8_t module = 0; module < DiagnosticLogger::getModuleCount(); ++module) {
				uint32_t moduleId;
				const char* moduleName;
				if (DiagnosticLogger::getModuleInfo(module, moduleId, moduleName) && moduleId == chunk.moduleId) {
					name = moduleName;
					break;
				}
			}
			appendJsonString(json, name, strlen(name));
			json += ",\"l\":"; appendJsonString(json, diagnosticLevelName(chunk.level), 3);
			json += ",\"s\":"; json += chunk.sourceLine;
			json += ",\"f\":"; json += chunk.flags;
			json += ",\"p\":"; json += chunk.partIndex;
			json += ",\"x\":"; appendJsonString(json, chunk.text, chunk.textLength);
			json += '}';
		}
		json += "]}";
		request->send(200, "application/json", json);
	});

	server.on("/debug/binary", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/debug/binary")) return;
		const bool continuing = request->hasParam("session");
		const bool refreshing = request->hasParam("refresh");
		uint32_t token = 0, before = 0, refreshToken = 0;
		if ((continuing && (!getUint32Parameter(request, "session", token) || !token ||
			!getUint32Parameter(request, "before", before) || before >= NandPartition::invalidBlock)) ||
			(refreshing && (!getUint32Parameter(request, "refresh", refreshToken) || !refreshToken)) ||
			(continuing && refreshing) || (!continuing && request->hasParam("before"))) {
			request->send(400, "application/json", "{\"error\":\"invalid_diagnostic_cursor\"}");
			return;
		}
		if (!diagnosticReadMutex || xSemaphoreTake(diagnosticReadMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
			request->send(503, "application/json", "{\"error\":\"diagnostic_reader_unavailable\"}");
			return;
		}
		if (diagnosticLogger.isFormatRunning()) {
			xSemaphoreGive(diagnosticReadMutex);
			request->send(409, "application/json", "{\"error\":\"diagnostic_format_in_progress\"}");
			return;
		}
		DiagnosticReadSession* session = nullptr;
		for (DiagnosticReadSession& candidate : diagnosticReadSessions) {
			if (continuing ? candidate.matches(token) :
				refreshing ? candidate.matches(refreshToken) : candidate.isExpired()) {
				session = &candidate;
				break;
			}
		}
		if (!session || !session->isAvailable()) {
			xSemaphoreGive(diagnosticReadMutex);
			request->send(429, "application/json", "{\"error\":\"diagnostic_reader_busy\"}");
			return;
		}
		bool done = false;
		const bool started = continuing
			? session->beginPrevious(storageManager.diagnostics(), token, uint16_t(before), done)
			: session->beginLatest(diagnosticLogger, storageManager.diagnostics(),
				storageManager.partition(StoragePartitionId::diagnostics));
		xSemaphoreGive(diagnosticReadMutex);
		if (!started) {
			request->send(503, "application/json", "{\"error\":\"diagnostic_reader_unavailable\"}");
			return;
		}
		if (done) {
			request->send(204);
			return;
		}
		const uint32_t sessionId = session->getId();
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream",
			session->contentLength(), [session, sessionId](uint8_t* data, size_t capacity, size_t offset) -> size_t {
				return session->copyChunk(data, capacity, offset, sessionId);
			});
		request->onDisconnect([session, sessionId]() { session->cancel(sessionId); });
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});

	server.on("/debug/format", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/debug/format")) return;
		if (!diagnosticReadMutex || xSemaphoreTake(diagnosticReadMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
			request->send(503, "application/json", "{\"error\":\"diagnostic_reader_unavailable\"}");
			return;
		}
		if (diagnosticLogger.isFormatRunning()) {
			xSemaphoreGive(diagnosticReadMutex);
			request->send(409, "application/json", "{\"error\":\"diagnostic_format_in_progress\"}");
			return;
		}
		for (DiagnosticReadSession& session : diagnosticReadSessions) {
			if (!session.isAvailable()) {
				xSemaphoreGive(diagnosticReadMutex);
				request->send(409, "application/json", "{\"error\":\"diagnostic_read_in_progress\"}");
				return;
			}
		}
		for (DiagnosticReadSession& session : diagnosticReadSessions) session.invalidate();
		const bool started = storageManager.startPartitionFormat(StoragePartitionId::diagnostics);
		xSemaphoreGive(diagnosticReadMutex);
		request->send(started ? 202 : 503, "application/json", started
			? "{\"ok\":true,\"formatting\":true}"
			: "{\"ok\":false,\"error\":\"format_start_failed\"}");
	});

	server.on("/debug/config", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/debug/config")) return;
		const bool hasGlobal = request->hasParam("globalLevel", true);
		const bool hasModule = request->hasParam("moduleId", true) || request->hasParam("level", true);
		const bool hasSerial = request->hasParam("serial", true);
		const bool hasNand = request->hasParam("nand", true);
		if (!hasGlobal && !hasModule && !hasSerial && !hasNand) {
			request->send(400, "application/json", "{\"ok\":false,\"reason\":\"empty_config\"}");
			return;
		}
		DiagnosticLoggerConfig config;
		diagnosticLogger.getConfig(config);
		DiagnosticLevel globalLevel = config.globalLevel, moduleLevel = DiagnosticLevel::info;
		bool serialEnabled = config.serialEnabled, nandEnabled = config.nandEnabled;
		uint32_t moduleId = 0;
		bool inherit = false, validModule = false;
		if (hasGlobal && !parseDiagnosticLevel(request->getParam("globalLevel", true)->value(), globalLevel)) {
			request->send(400, "application/json", "{\"ok\":false,\"reason\":\"invalid_level\"}");
			return;
		}
		if (hasModule) {
			if (!request->hasParam("moduleId", true) || !request->hasParam("level", true)) {
				request->send(400, "application/json", "{\"ok\":false,\"reason\":\"missing_module_option\"}");
				return;
			}
			const String idText = request->getParam("moduleId", true)->value();
			char* end = nullptr;
			moduleId = strtoul(idText.c_str(), &end, 10);
			for (uint8_t index = 0; index < DiagnosticLogger::getModuleCount(); ++index) {
				uint32_t knownId;
				const char* name;
				if (DiagnosticLogger::getModuleInfo(index, knownId, name) && knownId == moduleId) validModule = true;
			}
			const String levelText = request->getParam("level", true)->value();
			inherit = levelText.equalsIgnoreCase("inherit");
			if (!idText.length() || !end || *end || !validModule ||
				(!inherit && !parseDiagnosticLevel(levelText, moduleLevel))) {
				request->send(400, "application/json", "{\"ok\":false,\"reason\":\"invalid_module\"}");
				return;
			}
		}
		if ((hasSerial && !parseHistoryBool(request->getParam("serial", true)->value(), serialEnabled)) ||
			(hasNand && !parseHistoryBool(request->getParam("nand", true)->value(), nandEnabled))) {
			request->send(400, "application/json", "{\"ok\":false,\"reason\":\"invalid_output\"}");
			return;
		}
		const bool saved = (!hasGlobal || diagnosticLogger.setGlobalLevel(globalLevel)) &&
			(!hasModule || (inherit ? diagnosticLogger.clearModuleLevel(moduleId) :
				diagnosticLogger.setModuleLevel(moduleId, moduleLevel))) &&
			(!hasSerial && !hasNand || diagnosticLogger.setOutputs(serialEnabled, nandEnabled));
		request->send(saved ? 200 : 500, "application/json", saved ? "{\"ok\":true}" :
			"{\"ok\":false,\"reason\":\"nvs_write_failed\"}");
	});

	server.on("/debug", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/debug")) return;
		DiagnosticLoggerConfig config;
		DiagnosticLoggerMetrics loggerMetrics;
		DiagnosticStoreMetrics storeMetrics;
		diagnosticLogger.getConfig(config);
		diagnosticLogger.getMetrics(loggerMetrics);
		storageManager.diagnostics().getMetrics(storeMetrics);
		const uint32_t usedPercent = storeMetrics.capacityBytes
			? min(100U, storeMetrics.usedBytes * 100U / storeMetrics.capacityBytes) : 0;
		String json;
		json.reserve(512);
		json = "{\"globalLevel\":" + String(uint8_t(config.globalLevel)) +
			",\"serial\":" + (config.serialEnabled ? "true" : "false") +
			",\"nand\":" + (config.nandEnabled ? "true" : "false") +
			",\"queue\":{\"capacity\":" + String(DiagnosticLogger::queueLength) + ",\"used\":" + String(loggerMetrics.queueUsed) +
			",\"peak\":" + String(loggerMetrics.queueHighWater) +
			",\"full\":" + String(loggerMetrics.queueFull) +
			",\"lostParts\":" + String(loggerMetrics.lostChunks) +
			",\"incomplete\":" + String(loggerMetrics.incompleteMessages) +
			",\"truncated\":" + String(loggerMetrics.truncatedMessages) +
			"},\"serialDrops\":" + String(loggerMetrics.serialDrops) +
			",\"nandDrops\":" + String(loggerMetrics.nandDrops) +
			",\"storage\":{\"ready\":" + (storeMetrics.ready ? "true" : "false") +
			",\"writtenPages\":" + String(storeMetrics.writtenPages) +
			",\"failedWrites\":" + String(storeMetrics.failedWrites) +
			",\"recoveredPages\":" + String(storeMetrics.recoveredPages) +
			",\"block\":" + String(storeMetrics.currentBlock) +
			",\"page\":" + String(storeMetrics.currentPage) +
			",\"activeBytes\":" + String(storeMetrics.activeBytes) +
			",\"recordsThisRun\":" + String(storeMetrics.recordsThisRun) +
			",\"usedBlocks\":" + String(storeMetrics.usedBlocks) +
			",\"usedPages\":" + String(storeMetrics.usedPages) +
			",\"usedBytes\":" + String(storeMetrics.usedBytes) +
			",\"capacityBytes\":" + String(storeMetrics.capacityBytes) +
			",\"usedPercent\":" + String(usedPercent) +
			",\"formatting\":" + (storeMetrics.formatting ? "true" : "false") +
			",\"formatDone\":" + (storeMetrics.formatDone ? "true" : "false") +
			",\"formatSuccess\":" + (storeMetrics.formatSuccess ? "true" : "false") +
			",\"formatBlocksDone\":" + String(storeMetrics.formatBlocksDone) +
			",\"formatBlocksTotal\":" + String(storeMetrics.formatBlocksTotal) +
			",\"formatBytesDone\":" + String(storeMetrics.formatBytesDone) +
			",\"formatBytesErased\":" + String(storeMetrics.formatBytesErased) +
			",\"formatBytesTotal\":" + String(storeMetrics.formatBytesTotal) +
			",\"formatPercent\":" + String(storeMetrics.formatPercent) + "},\"modules\":[";
		for (uint8_t index = 0; index < DiagnosticLogger::getModuleCount(); ++index) {
			uint32_t moduleId;
			const char* name;
			if (!DiagnosticLogger::getModuleInfo(index, moduleId, name)) continue;
			if (index) json += ',';
			json += "{\"id\":" + String(moduleId) + ",\"name\":\"" + name +
				"\",\"level\":" + String(config.moduleLevels[index]) + '}';
		}
		json += "]}";
		request->send(200, "application/json", json);
	});

	server.on("/manager/discovery", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetDiscoveryJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/manager/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetStatusJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/storage/test", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage/test")) return;
		if (!flashDetected) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"success\":false,\"reason\":\"nand_not_detected\"}");
			return;
		}
		if (storageManager.state() != StorageState::unformatted) {
			request->send(409, "application/json", "{\"schemaVersion\":1,\"success\":false,\"reason\":\"storage_active\"}");
			return;
		}
		if (!nandDiagnosticTryAcquire()) {
			request->send(409, "application/json", "{\"schemaVersion\":1,\"success\":false,\"reason\":\"diagnostic_busy\"}");
			return;
		}

		const WinbondSpiNandGeometry& geometry = eventFlash.geometry();
		uint16_t testBlock = geometry.blockCount - 1;
		if (request->hasParam("block", true)) {
			const long requestedBlock = request->getParam("block", true)->value().toInt();
			if (requestedBlock < 0 || requestedBlock >= geometry.blockCount) {
				nandDiagnosticRelease();
				request->send(400, "application/json", "{\"schemaVersion\":1,\"success\":false,\"reason\":\"invalid_block\"}");
				return;
			}
			testBlock = uint16_t(requestedBlock);
		}
		WinbondSpiNandTestResult result;
		eventFlash.runBlockTest(testBlock, result);
		nandDiagnosticRelease();

		char json[640];
		snprintf(json, sizeof(json), "{\"schemaVersion\":1,\"success\":%s,\"model\":\"%s\","
			"\"capacityBytes\":%u,\"blockCount\":%u,\"pageDataSize\":%u,\"spareSize\":%u,\"block\":%u,"
			"\"badBlock\":%s,\"badBlockMarker\":\"%02X\","
			"\"registers\":{\"protectionBefore\":\"%02X\",\"protectionDuring\":\"%02X\","
			"\"configurationBefore\":\"%02X\",\"configurationDuring\":\"%02X\"},"
			"\"erase\":{\"ok\":%s,\"status\":\"%02X\",\"timeUs\":%u},"
			"\"program\":{\"ok\":%s,\"status\":\"%02X\",\"timeUs\":%u},"
			"\"read\":{\"ok\":%s,\"status\":\"%02X\",\"timeUs\":%u},"
			"\"dataMatch\":%s,\"expectedCrc32\":\"%08X\",\"readCrc32\":\"%08X\"}",
			result.success ? "true" : "false", eventFlash.modelName(), eventFlash.capacityBytes(),
			geometry.blockCount, geometry.pageDataSize, geometry.spareSize, result.block,
			result.badBlock ? "true" : "false", result.badBlockMarker,
			result.protectionBefore, result.protectionDuring,
			result.configurationBefore, result.configurationDuring,
			result.eraseOk ? "true" : "false", result.eraseStatus, result.eraseUs,
			result.programOk ? "true" : "false", result.programStatus, result.programUs,
			result.readOk ? "true" : "false", result.readStatus, result.readUs,
			result.dataMatch ? "true" : "false", result.expectedCrc32, result.readCrc32);
		auto* response = request->beginResponse(result.success ? 200 : 500, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/storage/benchmark", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage/benchmark")) return;
		if (request->method() == HTTP_POST) {
			const String modeText = request->hasParam("mode", true) ? request->getParam("mode", true)->value() : "quick";
			const uint8_t speedMHz = request->hasParam("speedMHz", true) ? request->getParam("speedMHz", true)->value().toInt() : 20;
			const uint16_t startBlock = request->hasParam("startBlock", true) ? request->getParam("startBlock", true)->value().toInt() : 0;
			const uint16_t blockCount = request->hasParam("blockCount", true) ? request->getParam("blockCount", true)->value().toInt() : 0;
			if (modeText != "quick" && modeText != "full" && modeText != "verify") {
				request->send(400, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"invalid_mode\"}"); return;
			}
			const NandBenchmarkMode mode = modeText == "full" ? NandBenchmarkMode::full :
				modeText == "verify" ? NandBenchmarkMode::verify : NandBenchmarkMode::quick;
			if (!nandBenchmarkStart(mode, speedMHz, startBlock, blockCount)) {
				request->send(409, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"invalid_speed_or_busy\"}"); return;
			}
		}
		String json;
		nandBenchmarkGetJson(json);
		auto* response = request->beginResponse(request->method() == HTTP_POST ? 202 : 200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/storage/benchmark/cancel", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage/benchmark/cancel")) return;
		request->send(nandBenchmarkCancel() ? 202 : 409, "application/json",
			"{\"schemaVersion\":1,\"cancelRequested\":true}");
		});

	server.on("/storage/partition/format", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage/partition/format")) return;
		String confirm, name;
		if (request->hasParam("confirm", true)) confirm = request->getParam("confirm", true)->value();
		if (request->hasParam("partition", true)) name = request->getParam("partition", true)->value();
		if (confirm != "FORMAT") {
			request->send(400, "application/json", "{\"ok\":false,\"error\":\"confirmation_required\"}");
			return;
		}
		StoragePartitionId id;
		if (name == "history") id = StoragePartitionId::history;
		else if (name == "waveEvents") id = StoragePartitionId::waveEvents;
		else if (name == "gridEvents") id = StoragePartitionId::gridEvents;
		else if (name == "diagnostics") id = StoragePartitionId::diagnostics;
		else if (name == "webFsA") id = StoragePartitionId::webFsA;
		else if (name == "webFsB") id = StoragePartitionId::webFsB;
		else {
			request->send(400, "application/json", "{\"ok\":false,\"error\":\"unknown_partition\"}");
			return;
		}
		if (id == StoragePartitionId::diagnostics) {
			if (!diagnosticReadMutex || xSemaphoreTake(diagnosticReadMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
				request->send(503, "application/json", "{\"ok\":false,\"error\":\"diagnostic_reader_unavailable\"}");
				return;
			}
			if (diagnosticLogger.isFormatRunning()) {
				xSemaphoreGive(diagnosticReadMutex);
				request->send(409, "application/json", "{\"ok\":false,\"error\":\"format_in_progress\"}");
				return;
			}
			for (DiagnosticReadSession& session : diagnosticReadSessions) {
				if (!session.isAvailable()) {
					xSemaphoreGive(diagnosticReadMutex);
					request->send(409, "application/json", "{\"ok\":false,\"error\":\"diagnostic_read_in_progress\"}");
					return;
				}
			}
			for (DiagnosticReadSession& session : diagnosticReadSessions) session.invalidate();
			const bool started = storageManager.startPartitionFormat(id);
			xSemaphoreGive(diagnosticReadMutex);
			request->send(started ? 202 : 409, "application/json", started
				? "{\"ok\":true,\"formatting\":true}"
				: "{\"ok\":false,\"error\":\"storage_busy_or_unavailable\"}");
			return;
		}
		const bool started = storageManager.startPartitionFormat(id);
		request->send(started ? 202 : 409, "application/json", started
			? "{\"ok\":true,\"formatting\":true}"
			: "{\"ok\":false,\"error\":\"storage_busy_or_unavailable\"}");
	});

	// Mantener antes de /storage: el servidor tambien hace coincidir rutas por prefijo.
	server.on("/storage/test-download", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage/test-download")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		const size_t defaultSize = 3U * 1024U * 1024U;
		size_t size = request->hasParam("size") ? strtoul(request->getParam("size")->value().c_str(), nullptr, 10) : defaultSize;
		if (status.state != StorageState::ready || !size || size > status.history.usableBytes) {
			request->send(400, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"invalid_size_or_storage_unavailable\"}");
			return;
		}
		if (!nandReadSession.begin(storageManager.partition(StoragePartitionId::history), size)) {
			request->send(409, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"download_busy\"}");
			return;
		}
		const uint32_t sessionId = nandReadSession.getId();
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", size,
			[sessionId](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
				return nandReadSession.copyChunk(buffer, maxLen, index, sessionId);
			});
		request->onDisconnect([sessionId]() { nandReadSession.cancel(sessionId); });
		response->addHeader("Content-Disposition", "attachment; filename=nand-history-test.bin");
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/storage", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		const bool includeDetails = request->hasParam("detail") && request->getParam("detail")->value() == "1";
		HistoryReadSessionMetrics historyReadMetrics{};
		if (includeDetails) HistoryReadSession::getMetrics(historyReadMetrics);
		String json;
		json.reserve(includeDetails ? 3600 : 1300);
		json += "{\"state\":\"";
		json += storageStateName(status.state);
		json += "\",\"nand\":{\"blocks\":";
		json += status.nandBlockCount;
		json += ",\"capacityBytes\":";
		json += status.capacityBytes;
		json += "},\"partitions\":{\"history\":";
		appendPartitionStatus(json, status.history, status.historyFormat);
		json += ",\"waveEvents\":";
		appendPartitionStatus(json, status.waveEvents, status.waveEventsFormat);
		json += ",\"gridEvents\":";
		appendPartitionStatus(json, status.gridEvents, status.gridEventsFormat);
		json += ",\"diagnostics\":";
		appendPartitionStatus(json, status.diagnostics, status.diagnosticsFormat);
		json += ",\"webFsA\":";
		appendPartitionStatus(json, status.webFsA, status.webFsAFormat);
		json += ",\"webFsB\":";
		appendPartitionStatus(json, status.webFsB, status.webFsBFormat);
		json += "},\"historyStore\":{\"writtenPages\":";
		json += status.historyStore.writtenPages;
		if (includeDetails) {
			json += ",\"acceptedSamples\":";
			json += status.historyStore.acceptedSamples;
			json += ",\"rejectedSamples\":";
			json += status.historyStore.rejectedSamples;
			json += ",\"failedPages\":";
			json += status.historyStore.failedPages;
			json += ",\"verificationFailures\":";
			json += status.historyStore.verificationFailures;
			json += ",\"activeSamples\":";
			json += status.historyStore.activeSamples;
			json += ",\"block\":";
			json += status.historyStore.currentBlock;
			json += ",\"page\":";
			json += status.historyStore.currentPage;
			json += ",\"writePending\":";
			json += status.historyStore.writePending ? "true" : "false";
		}
		json += "},\"gridStore\":{\"recoveredEvents\":";
		json += status.gridStore.recoveredEvents;
		json += ",\"acceptedEvents\":";
		json += status.gridStore.acceptedEvents;
		if (includeDetails) {
			json += ",\"discardedCandidates\":";
			json += status.gridStore.discardedCandidates;
			json += ",\"writtenPages\":";
			json += status.gridStore.writtenPages;
			json += ",\"failedPages\":";
			json += status.gridStore.failedPages;
			json += ",\"failedEvents\":";
			json += status.gridStore.failedEvents;
			json += ",\"nextEventId\":";
			json += status.gridStore.nextEventId;
			json += ",\"captureActive\":";
			json += status.gridStore.captureActive ? "true" : "false";
			json += ",\"rootPending\":";
			json += status.gridStore.rootPending ? "true" : "false";
		}
		json += "},\"format\":{\"active\":";
		json += status.format.active ? "true" : "false";
		json += ",\"completed\":";
		json += status.format.completed ? "true" : "false";
		json += ",\"failed\":";
		json += status.format.failed ? "true" : "false";
		json += ",\"currentBlock\":";
		json += status.format.currentBlock;
		json += ",\"totalBlocks\":";
		json += status.format.totalBlocks;
		json += ",\"scannedBlocks\":";
		json += status.format.scannedBlocks;
		json += ",\"erasedBlocks\":";
		json += status.format.erasedBlocks;
		json += ",\"usableBlocks\":";
		json += status.format.usableBlocks;
		json += ",\"badBlocks\":";
		json += status.format.badBlocks;
		json += ",\"failedBlocks\":";
		json += status.format.failedBlocks;
		json += "}";
		if (includeDetails) {
			const uint32_t recentAgeUs = status.ioMetrics.recentWindowStartedUs ?
				micros() - status.ioMetrics.recentWindowStartedUs : UINT32_MAX;
			const bool recentActive = recentAgeUs < 1000000U;
			const uint32_t recentReadBytes = recentActive ? status.ioMetrics.recentReadBytes : 0;
			const uint32_t recentWrittenBytes = recentActive ? status.ioMetrics.recentWrittenBytes : 0;
			json += ",\"system\":{\"uptimeMs\":";
			json += millis();
			json += ",\"resetReason\":\"";
			json += resetReasonName(esp_reset_reason());
			json += "\",\"freeHeap\":";
			json += ESP.getFreeHeap();
			json += ",\"minFreeHeap\":";
			json += ESP.getMinFreeHeap();
			json += ",\"free8\":";
			json += heap_caps_get_free_size(MALLOC_CAP_8BIT);
			json += ",\"largest8\":";
			json += heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
			json += ",\"min8\":";
			json += heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
			json += "},\"historyRecovery\":{\"headerReads\":";
			json += status.historyRecovery.headerReads;
			json += ",\"validHeaders\":";
			json += status.historyRecovery.validHeaders;
			json += ",\"elapsedUs\":";
			json += status.historyRecovery.elapsedUs;
			json += ",\"historyFound\":";
			json += status.historyRecovery.historyFound ? "true" : "false";
			json += "},\"historyRead\":{\"emptyBufferCalls\":";
			json += historyReadMetrics.emptyBufferCalls;
			json += ",\"emptyBufferEpisodes\":";
			json += historyReadMetrics.emptyBufferEpisodes;
			json += ",\"totalBufferWaitUs\":";
			managerAppendUint64(json, historyReadMetrics.totalBufferWaitUs);
			json += ",\"lastBufferWaitUs\":";
			json += historyReadMetrics.lastBufferWaitUs;
			json += ",\"maxBufferWaitUs\":";
			json += historyReadMetrics.maxBufferWaitUs;
			json += "},\"io\":{\"readBytes\":";
			json += status.ioMetrics.bytesRead;
			json += ",\"writtenBytes\":";
			json += status.ioMetrics.bytesWritten;
			json += ",\"highQueueMax\":";
			json += status.ioMetrics.maxQueueDepth[uint8_t(StoragePriority::high)];
			json += ",\"mediumQueueMax\":";
			json += status.ioMetrics.maxQueueDepth[uint8_t(StoragePriority::medium)];
			json += ",\"lowQueueMax\":";
			json += status.ioMetrics.maxQueueDepth[uint8_t(StoragePriority::low)];
			json += ",\"lastOperation\":\"";
			json += nandOperationName(status.ioMetrics.lastOperation);
			json += "\",\"lastError\":\"";
			json += nandIoErrorName(status.ioMetrics.lastError);
			json += "\",\"recent\":{\"active\":";
			json += recentActive ? "true" : "false";
			json += ",\"windowAgeUs\":";
			json += recentActive ? recentAgeUs : 0;
			json += ",\"readBytes\":";
			json += recentReadBytes;
			json += ",\"writtenBytes\":";
			json += recentWrittenBytes;
			json += ",\"readBytesPerSecond\":";
			json += recentActive ? nandRateBytesPerSecond(recentReadBytes, recentAgeUs) : 0;
			json += ",\"writeBytesPerSecond\":";
			json += recentActive ? nandRateBytesPerSecond(recentWrittenBytes, recentAgeUs) : 0;
			json += "},\"operations\":{";
			appendNandOperationMetrics(json, "read", status.ioMetrics, NandOperation::readPage);
			json += ",";
			appendNandOperationMetrics(json, "program", status.ioMetrics, NandOperation::programPage);
			json += ",";
			appendNandOperationMetrics(json, "erase", status.ioMetrics, NandOperation::eraseBlock);
			json += ",";
			appendNandOperationMetrics(json, "badBlockMarker", status.ioMetrics, NandOperation::readBadBlockMarker);
			json += "}}";
		}
		json += "}";
		auto* response = request->beginResponse(200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/storage/format", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/storage/format")) return;
		if (!flashDetected) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"nand_not_detected\"}");
			return;
		}
		String confirm;
		if (request->hasParam("confirm", true)) confirm = request->getParam("confirm", true)->value();
		if (confirm != "FORMAT") {
			request->send(400, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"confirmation_required\"}");
			return;
		}
		if (!storageManager.startFormat()) {
			request->send(409, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"storage_unavailable_or_busy\"}");
			return;
		}
		request->send(202, "application/json", "{\"schemaVersion\":1,\"started\":true,\"state\":\"formatting\"}");
		});

	server.on("/telemetry", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetTelemetryCurrentJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/history/config", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/history/config")) return;
		HistoryRecorderConfig config;
		historyRecorder.getConfig(config);
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", historyConfigJson(config));
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});

	server.on("/history/estimate", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/history/estimate")) return;
		HistoryRecorderConfig config;
		historyRecorder.getConfig(config);
		if (!parseHistoryOptions(request, false, false, config)) {
			request->send(400, "application/json", "{\"ok\":false,\"reason\":\"invalid_history_options\"}");
			return;
		}
		if (!HistoryRecorder::isConfigSupported(config)) {
			request->send(422, "application/json", "{\"ok\":false,\"reason\":\"unsupported_history_config\"}");
			return;
		}
		StorageManagerStatus status;
		storageManager.getStatus(status);
		if (status.state != StorageState::ready) {
			request->send(503, "application/json", "{\"ok\":false,\"reason\":\"storage_unavailable\"}");
			return;
		}
		String json = "{";
		appendHistoryEstimate(json, config, status);
		json += "}";
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});

	server.on("/history/config", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/history/config")) return;
		HistoryRecorderConfig config;
		historyRecorder.getConfig(config);
		if (!parseHistoryOptions(request, true, true, config)) {
			request->send(400, "application/json", "{\"ok\":false,\"reason\":\"invalid_history_options\"}");
			return;
		}
		if (!historyRecorder.setConfig(config)) {
			request->send(422, "application/json", "{\"ok\":false,\"reason\":\"unsupported_history_config\"}");
			return;
		}
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", historyConfigJson(config));
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});

	// Registra la ruta raíz después de /history/data porque el servidor también coincide por prefijo.
	auto registerHistoryRoot = []() {
	server.on("/history", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/history")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		if (request->hasParam("from") || request->hasParam("to") || request->hasParam("last")) {
			if (status.state != StorageState::ready) {
				request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"storage_unavailable\"}");
				return;
			}
			uint32_t fromTimestamp = 0, toTimestamp = 0;
			const HistoryRangeStatus rangeStatus = resolveHistoryRange(request, fromTimestamp, toTimestamp);
			if (rangeStatus != HistoryRangeStatus::ready) {
				request->send(rangeStatus == HistoryRangeStatus::empty ? 404 : 400, "application/json",
					rangeStatus == HistoryRangeStatus::empty ? "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"history_empty\"}" :
					"{\"schemaVersion\":1,\"ok\":false,\"reason\":\"invalid_time_range\"}");
				return;
			}
			uint8_t* activeSnapshot = new (std::nothrow) uint8_t[HistoryFormat::pageSize];
			if (!activeSnapshot) {
				request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"insufficient_memory\"}");
				return;
			}
			HistoryReadPlan plan;
			const HistoryReadPlanStatus planStatus = storageManager.history().buildReadPlan(fromTimestamp, toTimestamp, plan,
				activeSnapshot, HistoryFormat::pageSize);
			delete[] activeSnapshot;
			if (planStatus != HistoryReadPlanStatus::ready) {
				const char* reason = planStatus == HistoryReadPlanStatus::noData ? "history_empty_in_range" :
					planStatus == HistoryReadPlanStatus::rangeTooLarge ? "range_too_large" : "history_read_failed";
				String json = String("{\"schemaVersion\":1,\"ok\":false,\"reason\":\"") + reason + "\",\"searchElapsedUs\":";
				json += plan.searchElapsedUs;
				json += ",\"searchHeaderReads\":";
				json += plan.searchHeaderReads;
				json += "}";
				request->send(planStatus == HistoryReadPlanStatus::noData ? 404 :
					planStatus == HistoryReadPlanStatus::rangeTooLarge ? 413 : 503, "application/json",
					json);
				return;
			}
			const uint32_t validPages = plan.storedPageCount + (plan.includesActivePage ? 1U : 0U);
			const uint32_t skippedPages = plan.physicalPageCount - plan.storedPageCount;
			String json = "{\"schemaVersion\":1,\"from\":";
			json += fromTimestamp;
			json += ",\"to\":";
			json += toTimestamp;
			json += ",\"searchElapsedUs\":";
			json += plan.searchElapsedUs;
			json += ",\"searchHeaderReads\":";
			json += plan.searchHeaderReads;
			json += ",\"validPages\":";
			json += validPages;
			json += ",\"nandPages\":";
			json += plan.storedPageCount;
			json += ",\"physicalPages\":";
			json += plan.physicalPageCount;
			json += ",\"skippedInvalidPages\":";
			json += skippedPages;
			json += ",\"includesActivePage\":";
			json += plan.includesActivePage ? "true" : "false";
			json += ",\"samples\":";
			json += plan.sampleCount + plan.activeSampleCount;
			json += ",\"downloadBytes\":";
			json += (plan.physicalPageCount + (plan.includesActivePage ? 1U : 0U)) * HistoryFormat::pageSize;
			json += ",\"hasGaps\":";
			json += skippedPages ? "true" : "false";
			json += "}";
			AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
			response->addHeader("Cache-Control", "no-store");
			request->send(response);
			return;
		}
		uint32_t oldestTimestamp = 0, newestTimestamp = 0;
		storageManager.history().getTimestampRange(oldestTimestamp, newestTimestamp);
		HistoryRecorderConfig historyConfig;
		historyRecorder.getConfig(historyConfig);
		const uint32_t capacityPages = uint32_t(status.history.usableBlockCount) * WinbondSpiNand::pagesPerBlock;
		const uint32_t committedPages = status.historyStore.hasCommittedPages ? min(status.historyStore.latestSequence + 1U, capacityPages) : 0;
		String json = "{\"schemaVersion\":1,\"ready\":";
		json += status.state == StorageState::ready ? "true" : "false";
		json += ",\"oldestTimestamp\":";
		json += oldestTimestamp;
		json += ",\"newestTimestamp\":";
		json += newestTimestamp;
		json += ",\"committedPages\":";
		json += committedPages;
		json += ",\"committedBytes\":";
		json += uint32_t(committedPages * HistoryFormat::pageSize);
		json += ",\"activeSamples\":";
		json += status.historyStore.activeSamples;
		json += ",\"activeBytes\":";
		json += uint32_t(status.historyStore.activeSamples) * HistoryFormat::sampleSize(historyConfig.groupConfig);
		json += ",\"writePending\":";
		json += status.historyStore.writePending ? "true" : "false";
		json += ",\"pageSize\":";
		json += HistoryFormat::pageSize;
		json += "}";
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});
	};

	server.on("/history/data", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/history/data")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		if (status.state != StorageState::ready) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"storage_unavailable\"}");
			return;
		}
		uint32_t fromTimestamp = 0, toTimestamp = 0;
		const HistoryRangeStatus rangeStatus = resolveHistoryRange(request, fromTimestamp, toTimestamp);
		if (rangeStatus != HistoryRangeStatus::ready) {
			request->send(rangeStatus == HistoryRangeStatus::empty ? 404 : 400, "application/json",
				rangeStatus == HistoryRangeStatus::empty ? "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"history_empty\"}" :
				"{\"schemaVersion\":1,\"ok\":false,\"reason\":\"invalid_time_range\"}");
			return;
		}
		HistoryReadSession* session = findHistoryReadSession();
		if (!session) {
			request->send(429, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"too_many_history_downloads\"}");
			return;
		}
		uint8_t* activeSnapshot = new (std::nothrow) uint8_t[HistoryFormat::pageSize];
		if (!activeSnapshot) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"insufficient_memory\"}");
			return;
		}
		HistoryReadPlan plan;
		const HistoryReadPlanStatus planStatus = storageManager.history().buildReadPlan(fromTimestamp, toTimestamp, plan,
			activeSnapshot, HistoryFormat::pageSize);
		if (planStatus != HistoryReadPlanStatus::ready) {
			delete[] activeSnapshot;
			const char* reason = planStatus == HistoryReadPlanStatus::noData ? "history_empty_in_range" :
				planStatus == HistoryReadPlanStatus::rangeTooLarge ? "range_too_large" : "history_read_failed";
			request->send(planStatus == HistoryReadPlanStatus::noData ? 404 :
				planStatus == HistoryReadPlanStatus::rangeTooLarge ? 413 : 503, "application/json",
				String("{\"schemaVersion\":1,\"ok\":false,\"reason\":\"") + reason + "\"}");
			return;
		}
		if (!session->begin(storageManager.partition(StoragePartitionId::history), plan, activeSnapshot)) {
			delete[] activeSnapshot;
			request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"history_session_unavailable\"}");
			return;
		}
		delete[] activeSnapshot;
		const size_t dataSize = size_t(plan.physicalPageCount + (plan.includesActivePage ? 1U : 0U)) * HistoryFormat::pageSize;
		const uint32_t sessionId = session->getId();
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", dataSize,
			[session, sessionId](uint8_t* buffer, size_t maxLength, size_t offset) -> size_t {
				return session->copyChunk(buffer, maxLength, offset, sessionId);
			});
		request->onDisconnect([session, sessionId]() { session->cancel(sessionId); });
		response->addHeader("Content-Disposition", "attachment; filename=power-meter-history.bin");
		response->addHeader("Cache-Control", "no-store");
		response->addHeader("X-History-From", String(fromTimestamp));
		response->addHeader("X-History-To", String(toTimestamp));
		response->addHeader("X-History-Page-Size", String(HistoryFormat::pageSize));
		request->send(response);
		});
	registerHistoryRoot();

	// Registra la raíz después de las subrutas para que no las intercepte.
	auto registerGridEventsRoot = []() {
	server.on("/grid-events", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/grid-events")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		GridEventCatalog catalog;
		storageManager.gridEvents().getCatalog(catalog);
		String json = "{\"schemaVersion\":1,\"ready\":";
		json += status.state == StorageState::ready && status.gridEvents.enabled ? "true" : "false";
		json += ",\"pageSize\":";
		json += GridEventFormat::pageSize;
		json += ",\"partitionBytes\":";
		json += status.gridEvents.usableBytes;
		json += ",\"eventCount\":";
		json += catalog.eventCount;
		json += ",\"oldestId\":";
		json += catalog.oldestId;
		json += ",\"newestId\":";
		json += catalog.newestId;
		json += ",\"captureActive\":";
		json += status.gridStore.captureActive ? "true" : "false";
		json += ",\"writtenPages\":";
		json += status.gridStore.writtenPages;
		json += ",\"points\":";
		json += status.gridStore.completedPoints;
		json += ",\"limited\":";
		json += status.gridStore.limitedPoints;
		json += ",\"dropped\":";
		json += status.gridStore.droppedRecords;
		json += "}";
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});
	};

	// Las rutas especificas van antes de sus prefijos: evita que otra ruta intercepte la descarga.
	server.on("/capture-cache", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/capture-cache")) return;
		uint64_t fast = 0, wave = 0;
		const bool ready = storageManager.isReady() && !storageManager.partition(StoragePartitionId::gridEvents).isFormatting() &&
			!storageManager.partition(StoragePartitionId::waveEvents).isFormatting() && storageManager.getEventCacheEpochs(fast, wave);
		char json[224];
		snprintf(json, sizeof(json), "{\"schemaVersion\":1,\"ready\":%s,\"deviceId\":\"%012llx\",\"fastEpoch\":\"%016llx\",\"waveEpoch\":\"%016llx\"}",
			ready ? "true" : "false", ESP.getEfuseMac() & 0xFFFFFFFFFFFFULL, fast, wave);
		auto* response = request->beginResponse(200, "application/json", json);
		response->addHeader("Cache-Control", "no-store"); request->send(response);
	});
	server.on("/wave-events/data", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/wave-events/data")) return;
		uint32_t block = 0, id = 0, page = 0, formatVersion = 0;
		const bool legacyRequest = request->params() == 2;
		const bool baseParamsValid = getUint32Parameter(request, "block", block) && getUint32Parameter(request, "waveId", id);
		const bool layoutParamsValid = legacyRequest ||
			(getUint32Parameter(request, "page", page) && getUint32Parameter(request, "version", formatVersion));
		if (!baseParamsValid || !layoutParamsValid || (!legacyRequest && request->params() != 4) ||
			block >= 320 || !id || (page != 0 && page != 32) ||
			(formatVersion != waveEventFormat::legacyVersion && formatVersion != waveEventFormat::version)) {
			request->send(400, "text/plain", "invalid_query"); return;
		}
		if (legacyRequest) formatVersion = waveEventFormat::legacyVersion;
		auto& store = storageManager.waveEvents();
		// FAST y Wave comparten las dos paginas de lectura; otra descarga recibe ocupado.
		if (nandReadSession.isActive() || !store.pin(uint16_t(block), uint8_t(page), id, uint8_t(formatVersion))) {
			request->send(409, "text/plain", "wave_unavailable_or_busy"); return;
		}
		const uint32_t pageCount = formatVersion == waveEventFormat::legacyVersion ? 64 : 32;
		const size_t contentLength = size_t(pageCount) * 2048;
		if (!nandReadSession.begin(storageManager.partition(StoragePartitionId::waveEvents), uint16_t(block), uint8_t(page), pageCount,
			[]() { storageManager.waveEvents().unpin(); })) {
			store.unpin(); request->send(503, "text/plain", "wave_reader_unavailable"); return;
		}
		const uint32_t sessionId = nandReadSession.getId();
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", contentLength,
			[sessionId](uint8_t* buffer, size_t length, size_t offset) -> size_t { return nandReadSession.copyChunk(buffer, length, offset, sessionId); });
		request->onDisconnect([sessionId]() { nandReadSession.cancel(sessionId); });
		response->addHeader("Cache-Control", "no-store");
		addCaptureCacheIdentity(response, true);
		request->send(response);
	});
	server.on("/wave-events", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/wave-events")) return;
		uint32_t eventId = 0;
		uint64_t unixUs = 0;
		if (!getUint32Parameter(request, "eventId", eventId) || !eventId ||
			!getUint64Parameter(request, "unixUs", unixUs) || request->params() != 2) {
			request->send(400, "text/plain", "eventId_and_unixUs_required"); return;
		}
		waveIndexSnapshot index;
		const auto state = storageManager.waveEvents().lookup(eventId, unixUs, index);
		if (state != waveIndexStatus::ready) {
			const bool pending = state == waveIndexStatus::pending;
			const char* reason = pending ? "wave_index_pending" : state == waveIndexStatus::busy ? "wave_index_busy" :
				state == waveIndexStatus::failed ? "wave_index_read_failed" : "wave_index_unavailable";
			char json[96]; snprintf(json, sizeof(json), "{\"ready\":false,\"reason\":\"%s\"}", reason);
			auto* response = request->beginResponse(pending ? 202 : state == waveIndexStatus::failed ? 500 : 503, "application/json", json);
			response->addHeader("Cache-Control", "no-store");
			if (!pending) response->addHeader("Retry-After", "1");
			request->send(response); return;
		}
		char fastIdentity[32] = {}, waveIdentity[32] = {};
		captureCacheIdentity(false, fastIdentity, sizeof(fastIdentity)); captureCacheIdentity(true, waveIdentity, sizeof(waveIdentity));
		String json = "{\"fastIdentity\":\"";
		json += fastIdentity; json += "\",\"waveIdentity\":\""; json += waveIdentity;
		json += "\",\"eventTimestamp\":"; json += String(double(unixUs) / 1000.0, 1); json += ",\"captures\":[";
		for (uint8_t count = 0; count < index.count; ++count) {
			const auto& capture = index.entries[count];
			if (count) json += ',';
			char item[160];
			snprintf(item, sizeof(item), "{\"id\":%lu,\"block\":%u,\"page\":%u,\"version\":%u,\"triggerUs\":%llu,\"endUs\":%llu}",
				(unsigned long)capture.id, capture.block, capture.page, capture.version, capture.triggerUs, capture.endUs);
			json += item;
		}
		json += "]}";
		request->send(200, "application/json", json);
	});
	eventsHttpBegin(server);
	server.on("/wave/config", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/wave/config")) return;
		waveStoreMetrics metrics; storageManager.waveEvents().getMetrics(metrics);
		eventSettingsSnapshot settings; eventSettingsGet(settings);
		const uint16_t requested = settings.config.preMs; const bool saveEnabled = settings.config.waveEnabled;
		const uint8_t pointMask = settings.config.waveMask;
		char json[720];
		snprintf(json, sizeof(json), "{\"preMs\":%u,\"actualPreMs\":%u,\"reservedBytes\":%lu,\"enabled\":%s,\"capturing\":%s,"
			"\"captures\":%lu,\"committed\":%lu,\"orphans\":%lu,\"gaps\":%lu,\"droppedTriggers\":%lu,"
			"\"failedWrites\":%lu,\"erasedBlocks\":%u,\"pendingLinks\":%lu,\"publishedSlices\":%lu,\"wordHeapBytes\":%lu,\"stackFreeBytes\":%u,\"saveEnabled\":%s,\"occupiedSlots\":%u,\"totalSlots\":%u,\"storedCaptures\":%u,\"pointMask\":%u}", requested,
			metrics.actualPreMs, (unsigned long)metrics.reservedBytes, metrics.enabled ? "true" : "false", metrics.capturing ? "true" : "false",
			(unsigned long)metrics.captures, (unsigned long)metrics.committed, (unsigned long)metrics.orphans, (unsigned long)metrics.gaps,
			(unsigned long)metrics.droppedTriggers, (unsigned long)metrics.failedWrites, metrics.erasedBlocks,
			(unsigned long)metrics.pendingLinks, (unsigned long)metrics.publishedSlices, (unsigned long)metrics.wordHeapBytes, metrics.stackFreeBytes,
			saveEnabled ? "true" : "false", metrics.occupiedSlots, metrics.totalSlots, metrics.storedCaptures, pointMask);
		request->send(200, "application/json", json);
	});
	server.on("/wave/config", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/wave/config")) return;
		request->send(410, "application/json", "{\"error\":\"use_events_config\"}");
	});
	server.on("/grid-events/data", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/grid-events/data")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		if (status.state != StorageState::ready) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"storage_unavailable\"}");
			return;
		}
		uint32_t eventId = 0;
		if (!getUint32Parameter(request, "eventId", eventId) || !eventId || request->params() != 1) {
			request->send(400, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"eventId_required\"}");
			return;
		}
		GridEventIndexEntry entry;
		if (!storageManager.gridEvents().findEntry(eventId, entry)) {
			request->send(404, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"event_not_found\"}");
			return;
		}
		if (nandReadSession.isActive() || !nandReadSession.begin(storageManager.partition(StoragePartitionId::gridEvents), entry.block, entry.page, entry.header.pageCount)) {
			request->send(429, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"grid_download_busy\"}");
			return;
		}
		const size_t size = size_t(entry.header.pageCount) * GridEventFormat::pageSize;
		const uint32_t sessionId = nandReadSession.getId();
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", size,
			[sessionId](uint8_t* buffer, size_t maxLength, size_t offset) -> size_t { return nandReadSession.copyChunk(buffer, maxLength, offset, sessionId); });
		request->onDisconnect([sessionId]() { nandReadSession.cancel(sessionId); });
		response->addHeader("Content-Disposition", "attachment; filename=power-meter-grid-event-" + String(eventId) + ".bin");
		response->addHeader("Cache-Control", "no-store");
		addCaptureCacheIdentity(response, false);
		request->send(response);
		});

	server.on("/grid-events/list", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/grid-events/list")) return;
		StorageManagerStatus status;
		storageManager.getStatus(status);
		if (status.state != StorageState::ready) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"storage_unavailable\"}");
			return;
		}
		uint32_t fromId = 0, toId = 0, last = 0;
		const bool hasFrom = getUint32Parameter(request, "from", fromId);
		const bool hasTo = getUint32Parameter(request, "to", toId);
		const bool hasLast = getUint32Parameter(request, "last", last);
		if ((request->hasParam("from") && !hasFrom) ||
			(request->hasParam("to") && !hasTo) || (request->hasParam("last") && !hasLast)) {
			request->send(400, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"invalid_query\"}");
			return;
		}
		if ((hasLast && (hasFrom || hasTo)) || (!hasLast && (!hasFrom || !hasTo))) {
			request->send(400, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"use_last_or_from_to\"}");
			return;
		}
		if (hasLast && !last) last = 64;
		if (hasLast) { fromId = 0; toId = 0; }
		else if (fromId > toId) { request->send(200, "application/json", "{\"schemaVersion\":1,\"events\":[]}"); return; }
		GridEventIndexEntry* entries = new (std::nothrow) GridEventIndexEntry[64];
		if (!entries) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"insufficient_memory\"}");
			return;
		}
		GridEventCatalog catalog;
		const uint8_t count = storageManager.gridEvents().getEntries(fromId, toId, hasLast, entries, 64, catalog);
		String json = "{\"schemaVersion\":1,\"eventCount\":";
		json += catalog.eventCount;
		json += ",\"oldestId\":";
		json += catalog.oldestId;
		json += ",\"newestId\":";
		json += catalog.newestId;
		json += ",\"events\":[";
		for (uint8_t index = 0; index < count; ++index) {
			if (index) json += ',';
			appendGridEventJson(json, entries[index]);
		}
		json += "]}";
		delete[] entries;
		request->send(200, "application/json", json);
		});


	server.on("/grid-events/test", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!requireExactPath(request, "/grid-events/test")) return;
		const String type = request->hasParam("type", true) ? request->getParam("type", true)->value() : "mixed";
		const uint8_t code = type.equalsIgnoreCase("state") ? 1 : type.equalsIgnoreCase("dip") ? 2 :
			type.equalsIgnoreCase("swell") ? 3 : type.equalsIgnoreCase("overcurrent") ? 4 : type.equalsIgnoreCase("mixed") ? 5 : 0;
		if (!code) { request->send(400, "application/json", "{\"error\":\"invalid_type\"}"); return; }
		if (!gridEventStoreRequestTest(code)) { request->send(409, "application/json", "{\"error\":\"test_pending\"}"); return; }
		request->send(202, "application/json", "{\"schemaVersion\":1,\"accepted\":true}");
		});
	registerGridEventsRoot();

	server.on("/network/config", HTTP_GET, handleNetworkConfigGet);
	server.on("/network/config", HTTP_POST, handleNetworkConfigPost);

	server.on("/network/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
		String status;
		scanWifiGetJson(status);
		request->send(200, "application/json", status);
		});

	server.on("/meter", HTTP_GET, [](AsyncWebServerRequest* request) {
		char value[1280];
		bool valid = true;
		if (request->params() > 0) {
			const String& param = request->getParam(0)->name();
			if (param == "energy")
				valid = Meter.getJsonEnergy(value, sizeof(value));
			else if (param == "fast")
				valid = Meter.getJsonFastMeasures(value, sizeof(value));
			else if (param == "timing") {
				String timing;
				meterTimingGetJson(timing);
				request->send(200, "application/json", timing);
				return;
			}
			else if (param == "phaseR")
				valid = Meter.phaseR.getJson(value, sizeof(value));
			else if (param == "phaseS")
				valid = Meter.phaseS.getJson(value, sizeof(value));
			else if (param == "phaseT")
				valid = Meter.phaseT.getJson(value, sizeof(value));
			else if (param == "neutral")
				valid = Meter.neutral.getJson(value, sizeof(value));
			else if (param == "angles")
				valid = Meter.getJsonAngles(value, sizeof(value));
			else if (param == "thd")
				valid = Meter.getJsonTHD(value, sizeof(value));
			else
			{
				request->send(404, "text/plain", "Page Not Found");
				return;
			}
		}
		else
			valid = Meter.getJsonBasic(value, sizeof(value));
		if (!valid) {
			request->send(500, "application/json", "{\"error\":\"json_buffer_too_small\"}");
			return;
		}
		request->send(200, "application/json", value);
		});


	server.on("/clock/rtc", HTTP_GET, [](AsyncWebServerRequest* request) {
		rtcClockInfo info;
		rtcClockGetInfo(info);
		String json = "{\"present\":";
		json += info.present ? "true" : "false";
		json += ",\"valid\":";
		json += info.valid ? "true" : "false";
		json += ",\"lostPower\":";
		json += info.lostPower ? "true" : "false";
		json += ",\"rtc\":" + String(info.rtcEpoch);
		json += ",\"system\":" + String(info.systemEpoch);
		json += ",\"driftSec\":" + String(info.driftSeconds);
		json += ",\"source\":\"" + String(managerClockSource()) + "\"}";
		request->send(200, "application/json", json);
		});

	server.on("/clock", HTTP_POST | HTTP_GET, [](AsyncWebServerRequest* request) {
		if (request->hasParam("time", true)) {
			const int32_t requestedTime = request->getParam("time", true)->value().toInt();
			setTimeTo(requestedTime);
			rtcClockSyncFromSystem();
			managerSetClockSynchronized("rtc");
		}
		uint32_t time = getTime(nullptr);
		request->send(200, "text/plain", String(time));
		});

	server.on("/calibration/neutral", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest* request) {
		if (request->method() == HTTP_POST) {
			String phase = request->hasParam("phase", true) ? request->getParam("phase", true)->value() : "r";
			phase.toLowerCase();
			if (phase != "r" && phase != "s" && phase != "t") {
				request->send(400, "application/json", "{\"error\":\"invalid_phase\"}");
				return;
			}
			if (!bl0942.startCalibration(phase == "r" ? 0 : phase == "s" ? 1 : 2)) {
				request->send(409, "application/json", "{\"error\":\"busy_or_unavailable\"}");
				return;
			}
			request->send(202, "application/json", "{\"status\":\"queued\"}");
			return;
		}
		const neutralCalibration result = bl0942.getCalibration();
		char json[320];
		snprintf(json, sizeof(json), "{\"status\":\"%s\",\"error\":\"%s\",\"phase\":\"%c\",\"samples\":%u,\"rmsSamples\":%u,\"reference\":%.4f,\"rmsFactor\":%.8f,\"fastFactor\":%.8f}",
			result.status, result.error, "rst"[result.phase], result.samples, result.rmsSamples,
			result.reference, result.rmsFactor, result.fastFactor);
		request->send(200, "application/json", json);
		});

	server.on("/calibration/restore", HTTP_POST, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "restaurando calibracion... reiniciar para aplicar cambios!");
		ade.restoreCalibration();
		});

	server.on("/options/reload", HTTP_POST, [](AsyncWebServerRequest* request) {
		request->send(410, "application/json", "{\"error\":\"use_events_config\"}");
		});
	server.on("/grid/config", HTTP_GET | HTTP_POST, handleGridSettings);


	server.on("/options", HTTP_POST, handleBackupOptionsPost);
	server.on("/options", HTTP_GET, [](AsyncWebServerRequest* request) {
		uint32_t time = micros();
		String res = "{";
		if (request->params() > 1) {
			Preferences opts;
			const String& nameSpace = request->getParam(0)->value();
			if (nameSpace == "meter") for (size_t i = 1; i < request->params(); ++i) if (isEventOptionKey(request->getParam(i)->name())) {
				request->send(410, "application/json", "{\"error\":\"use_events_config\"}"); return;
			}
			if (request->getParam(0)->name() == "ns" && opts.begin(nameSpace.c_str())) {
				for (int32_t i = 1; i < request->params(); i++) {
					String value;
					const String& key = request->getParam(i)->name();
					if (key == "_") continue;
					PreferenceType type = opts.getType(key.c_str());
					if (opts.isKey(key.c_str())) {
						if (type == PT_I8)
							value = opts.getChar(key.c_str());
						else if (type == PT_U8)
							value = opts.getUChar(key.c_str());
						else if (type == PT_I16)
							value = opts.getShort(key.c_str());
						else if (type == PT_U16)
							value = opts.getUShort(key.c_str());
						else if (type == PT_I32)
							value = opts.getInt(key.c_str());
						else if (type == PT_U32)
							value = opts.getUInt(key.c_str());
						else if (type == PT_I64)
							value = (float)opts.getLong64(key.c_str());
						else if (type == PT_U64)
							value = (float)opts.getULong64(key.c_str());
						else if (type == PT_BLOB) {
							int32_t size = opts.getBytesLength(key.c_str());
							if (size == sizeof(float))
								value = String(opts.getFloat(key.c_str()), key == "neutralRms" || key == "neutralFast" ? 9 : 2);
							else if (size == sizeof(double))
								value = opts.getDouble(key.c_str());
							else
								value = "\"invalid type\"";
						}
						else if (type == PT_STR)
							value = quoteJsonString(opts.getString(key.c_str()));	//Prepapararlo para Json
						else
							value = "\"invalid type\"";
					}
					else 
						value = "\"key no existe\"";
					if (res.length() > 3) res += ", ";
					res += "\"" + key + "\": " + value;
				}
				res += "}";
			}
			else
				res = "{\"error\":\"no se puede abrir el namespace o no existe\"}";
			opts.end();
		}
		else
			res = "{\"error\":\"se debe indicar el nombre del namespace y una clave a leer o escribir\"}";
		time = micros() - time;
		debugI("Tiempo de ejecucion opts: %Uus", time);
		request->send(200, "application/json", res);
		});


	// Para habilitar CORS. Al simular desde un servidor local no se puede llamar a la API. Con esto se soluciona.
	if (!CORS) {
		CORS = true;	//Solo agregarlos una vez
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
	}

	server.onNotFound([](AsyncWebServerRequest* request) {
		if (request->method() == HTTP_GET) webFsHttpHandleAsset(request);
		else request->send(404, "text/plain", "Page Not Found");
		});

	//Start server
	server.begin();
}


void getStatus(String& str) {
	StorageManagerStatus storageStatus;
	storageManager.getStatus(storageStatus);
	const uint32_t recentAgeUs = storageStatus.ioMetrics.recentWindowStartedUs ?
		micros() - storageStatus.ioMetrics.recentWindowStartedUs : UINT32_MAX;
	const bool recentActive = recentAgeUs < 1000000U;
	const uint32_t readBytesPerSecond = recentActive ? nandRateBytesPerSecond(storageStatus.ioMetrics.recentReadBytes, recentAgeUs) : 0;
	const uint32_t writeBytesPerSecond = recentActive ? nandRateBytesPerSecond(storageStatus.ioMetrics.recentWrittenBytes, recentAgeUs) : 0;
	char data[200];
	snprintf(data, sizeof(data), "{\"ssid\":\"%s\",\"rssi\":%i,\"ip\":\"%s\",\"heap\":%i,\"free\":%i,\"nand\":{\"readBytesPerSecond\":%lu,\"writeBytesPerSecond\":%lu}}",
		getWiFiSSID().c_str(), getWiFiRSSI(), WiFi.localIP().toString().c_str(), ESP.getHeapSize(), ESP.getFreeHeap(),
		static_cast<unsigned long>(readBytesPerSecond), static_cast<unsigned long>(writeBytesPerSecond));
	str = data;
}


int32_t scanWifiGetJson(String& List)
{
	static String list = "";
	static bool first = true;
	int32_t count = WIFI_SCAN_RUNNING;

	if (first) {
		wifiScanStart();
		count = wifiScanStatus();
		first = false;
	}
	else {
		String ssid;
		int32_t rssi;
		uint8_t encType;
		char item[128];

		count = wifiScanStatus();
		if (count >= 0) {	//Una vez terminado el escaneo, crear un array de objetos con la info de la red
			list = "";	//Limpiar lista
			for (int32_t i = 0; i < count; i++) {
				if (wifiScanGetNetwork(i, ssid, encType, rssi)) {	//Obtener info de la red
					snprintf(item, sizeof(item), "{\"ssid\":\"%s\", \"rssi\":%i,\"auth\":\"%s\"}", ssid.c_str(), rssi, getWifiAuthMode(encType));
					list += item;
					if (i < (count - 1)) list += ",";
				}
			}
			first = true;
		}
	}
	if (count == WIFI_SCAN_FAILED) {
		List = "{\"scan\":\"failed\", \"list\":[]}";
		first = true;
	}
	else if (count == WIFI_SCAN_RUNNING)
		List = "{\"scan\":\"running\", \"list\":[" + list + "]}";
	else
		List = "{\"scan\":\"completed\", \"list\":[" + list + "]}";
	return count;
}


const char* getWifiAuthMode(uint8_t mode)
{
	switch (mode)
	{
	case WIFI_AUTH_OPEN:
		return "OPEN";
	case WIFI_AUTH_WEP:
		return "WEP";
	case WIFI_AUTH_WPA_PSK:
		return "WPA_PSK";
	case WIFI_AUTH_WPA2_PSK:
		return "WPA2_PSK";
	case WIFI_AUTH_WPA_WPA2_PSK:
		return "WPA_WPA2_PSK";
	case WIFI_AUTH_WPA2_ENTERPRISE:
		return "WPA2_ENTERPRISE";
	case WIFI_AUTH_WPA3_PSK:
		return "WPA3_PSK";
	case WIFI_AUTH_WPA2_WPA3_PSK:
		return "WPA2_WPA3_PSK";
	case WIFI_AUTH_WAPI_PSK:
		return "WAPI_PSK";
	default:
		return "";
	}
}
