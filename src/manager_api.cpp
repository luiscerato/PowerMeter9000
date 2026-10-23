#include "manager_api.h"
#include "wifiUtils.h"
#include "meter.h"
#include <Preferences.h>
#include <WiFi.h>
#include <sys/time.h>
#include <atomic>
#include <string.h>

#include "storage/storage_manager.h"

extern StorageManager storageManager;

static const char* managerSettingsName = "manager";
static const char* managerFirmwareVersion = "0.9.0";
static bool clockIsSynchronized = false;
static const char* clockSource = "none";
static uint64_t clockLastSyncMs = 0;
static std::atomic<uint32_t> telemetrySequence{0};

struct managerTelemetrySample_t {
	const char* relativeTopic;
	uint64_t timestampMs;
	uint32_t sequence;
	String data;
	bool valid;
};

static managerTelemetrySample_t telemetrySamples[] = {
	{ "meter", 0, 0, "", false },
	{ "meter/energy", 0, 0, "", false },
	{ "meter/fast", 0, 0, "", false },
	{ "meter/r", 0, 0, "", false },
	{ "meter/s", 0, 0, "", false },
	{ "meter/t", 0, 0, "", false },
	{ "battery", 0, 0, "", false },
	{ "status", 0, 0, "", false },
};

// Escapa texto para incluirlo como string JSON.
// dest: String destino; value: texto a escapar.
static void managerAppendJsonString(String& dest, const String& value)
{
	dest += "\"";
	for (uint32_t i = 0; i < value.length(); i++) {
		char c = value[i];
		if (c == '"' || c == '\\') {
			dest += "\\";
			dest += c;
		}
		else if ((uint8_t)c < 0x20) {
			char buffer[7];
			snprintf(buffer, sizeof(buffer), "\\u%04x", c);
			dest += buffer;
		}
		else dest += c;
	}
	dest += "\"";
}

// Agrega un campo timestamp que puede ser null.
// dest: JSON parcial; name: nombre del campo; value: Unix ms o 0.
static void managerAppendTimestampField(String& dest, const char* name, uint64_t value)
{
	dest += "\"";
	dest += name;
	dest += "\":";
	if (value) managerAppendUint64(dest, value);
	else dest += "null";
}

// Devuelve el indice de cache para un topic relativo.
// relativeTopic: topic buscado.
static int32_t managerFindSample(const char* relativeTopic)
{
	for (uint32_t i = 0; i < sizeof(telemetrySamples) / sizeof(telemetrySamples[0]); i++) {
		if (strcmp(telemetrySamples[i].relativeTopic, relativeTopic) == 0) return i;
	}
	return -1;
}

// Guarda una muestra de telemetria en cache.
// relativeTopic: topic relativo; dataPayload: JSON original; timestampMs: Unix ms; sequence: secuencia MQTT.
static void managerStoreTelemetry(const char* relativeTopic, const char* dataPayload, uint64_t timestampMs, uint32_t sequence)
{
	int32_t index = managerFindSample(relativeTopic);
	if (index < 0 || dataPayload == nullptr) return;
	telemetrySamples[index].timestampMs = timestampMs;
	telemetrySamples[index].sequence = sequence;
	telemetrySamples[index].data = dataPayload;
	const size_t length = strlen(dataPayload);
	telemetrySamples[index].valid = length >= 2 && dataPayload[0] == '{' && dataPayload[length - 1] == '}';
}

// Arma el payload MQTT agregando metadata en la raiz.
// dest: JSON final; dataPayload: JSON original; timestampMs: Unix ms; sequence: secuencia.
static void managerBuildTelemetryPayload(String& dest, const char* dataPayload, uint64_t timestampMs, uint32_t sequence)
{
	const size_t length = strlen(dataPayload);
	dest.reserve(length + 96);
	dest = "{\"schemaVersion\":1,";
	managerAppendTimestampField(dest, "timestamp", timestampMs);
	dest += ",\"sequence\":";
	dest += String(sequence);
	if (length > 2 && dataPayload[0] == '{' && dataPayload[length - 1] == '}') {
		dest += ",";
		for (size_t index = 1; index < length - 1; ++index)
			dest += dataPayload[index];
	}
	dest += "}";
}

// Devuelve la MAC base como texto hexadecimal compacto.
// upperCase: true para mayusculas.
static String managerGetMacCompact(bool upperCase)
{
	uint64_t mac = ESP.getEfuseMac();
	char buffer[13];
	snprintf(buffer, sizeof(buffer), upperCase ? "%012llX" : "%012llx", (unsigned long long)(mac & 0xFFFFFFFFFFFFULL));
	return String(buffer);
}

uint64_t managerUptimeMs()
{
	return millis();
}

uint64_t managerNowMs()
{
	if (!clockIsSynchronized) return 0;
	struct timeval now;
	gettimeofday(&now, nullptr);
	return ((uint64_t)now.tv_sec * 1000ULL) + (now.tv_usec / 1000ULL);
}

bool managerClockSynchronized()
{
	return clockIsSynchronized;
}

void managerSetClockSynchronized(const char* source)
{
	clockIsSynchronized = true;
	clockSource = source == nullptr ? "none" : source;
	struct timeval now;
	gettimeofday(&now, nullptr);
	clockLastSyncMs = ((uint64_t)now.tv_sec * 1000ULL) + (now.tv_usec / 1000ULL);
}

uint64_t managerLastSyncTimestampMs()
{
	return clockLastSyncMs;
}

const char* managerClockSource()
{
	return clockIsSynchronized ? clockSource : "none";
}

String managerGetDeviceId()
{
	Preferences prefs;
	String deviceId;
	if (prefs.begin(managerSettingsName, false)) {
		deviceId = prefs.getString("deviceId", "");
		if (!deviceId.length()) {
			deviceId = "pm-ade9000-" + managerGetMacCompact(false);
			prefs.putString("deviceId", deviceId);
		}
		prefs.end();
	}
	if (!deviceId.length()) deviceId = "pm-ade9000-" + managerGetMacCompact(false);
	return deviceId;
}

const char* managerGetMqttBaseTopic()
{
	return managerMqttBaseTopic;
}

String managerGetMqttBrokerUrl()
{
	Preferences prefs;
	String server = "";
	uint32_t port = 1883;
	if (prefs.begin("wifi-stuff", true)) {
		server = prefs.getString("mqtt-server", "");
		port = prefs.getUInt("mqtt-port", 1883);
		prefs.end();
	}
	if (!server.length()) return "";
	return "mqtt://" + server + ":" + String(port);
}

void managerPublishTelemetry(const char* relativeTopic, const char* dataPayload, uint64_t timestampMs)
{
	if (dataPayload == nullptr) return;
	uint32_t sequence = managerNextSequence();
	managerStoreTelemetry(relativeTopic, dataPayload, timestampMs, sequence);
	if (!mqtt.connected()) return;

	String payload;
	String topic = String(managerMqttBaseTopic) + "/" + relativeTopic;
	managerBuildTelemetryPayload(payload, dataPayload, timestampMs, sequence);
	mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

uint32_t managerNextSequence()
{
	return ++telemetrySequence;
}

void managerGetDiscoveryJson(String& dest)
{
	String mac = WiFi.macAddress();
	String compactMac = managerGetMacCompact(true);
	String serial = "PM9000-" + compactMac.substring(6);
	String brokerUrl = managerGetMqttBrokerUrl();

	dest = "{\"schemaVersion\":1,\"device\":{\"id\":";
	managerAppendJsonString(dest, managerGetDeviceId());
	dest += ",\"name\":\"Medidor principal\",\"model\":\"PowerMeter ADE9000\",\"serialNumber\":";
	managerAppendJsonString(dest, serial);
	dest += ",\"macAddress\":";
	managerAppendJsonString(dest, mac);
	dest += ",\"firmwareVersion\":";
	managerAppendJsonString(dest, managerFirmwareVersion);
	dest += "},\"driver\":{\"id\":\"ade9000\",\"telemetryProfile\":\"ade9000-v1\"},\"mqtt\":{\"baseTopic\":";
	managerAppendJsonString(dest, managerMqttBaseTopic);
	dest += ",\"brokerUrl\":";
	managerAppendJsonString(dest, brokerUrl);
	dest += "},\"electrical\":{\"system\":\"three_phase\",\"phases\":[\"R\",\"S\",\"T\",\"N\"]},";
	dest += "\"capabilities\":{\"telemetry\":true,\"telemetryRest\":true,\"eventNotifications\":false,";
	dest += "\"eventCatalog\":true,\"eventDownload\":true,\"historySync\":false,\"configuration\":false,\"localWeb\":true},";
	dest += "\"links\":{\"status\":\"/manager/status\",\"telemetry\":\"/telemetry\",";
	dest += "\"events\":\"/grid-events\",\"history\":\"/history\"}}";
}

void managerGetStatusJson(String& dest)
{
	dest = "{\"schemaVersion\":1,\"deviceId\":";
	managerAppendJsonString(dest, managerGetDeviceId());
	dest += ",";
	managerAppendTimestampField(dest, "timestamp", managerNowMs());
	dest += ",\"uptimeMs\":";
	managerAppendUint64(dest, managerUptimeMs());
	dest += ",\"clock\":{\"synchronized\":";
	dest += managerClockSynchronized() ? "true" : "false";
	dest += ",\"source\":";
	managerAppendJsonString(dest, managerClockSource());
	dest += ",\"lastSyncTimestamp\":";
	if (managerLastSyncTimestampMs()) managerAppendUint64(dest, managerLastSyncTimestampMs());
	else dest += "null";
	dest += "},\"network\":{\"connected\":";
	dest += WiFi.isConnected() ? "true" : "false";
	dest += ",\"ipAddress\":";
	managerAppendJsonString(dest, WiFi.localIP().toString());
	dest += ",\"rssi\":";
	dest += String(WiFi.isConnected() ? WiFi.RSSI() : 0);
	dest += "},\"mqtt\":{\"connected\":";
	dest += mqtt.connected() ? "true" : "false";
	dest += ",\"baseTopic\":";
	managerAppendJsonString(dest, managerMqttBaseTopic);
	dest += "},\"acquisition\":{\"running\":true,\"lastSampleTimestamp\":";
	uint64_t lastSample = MeterGetLastSampleTimestampMs();
	if (lastSample) managerAppendUint64(dest, lastSample);
	else dest += "null";
	dest += "},\"storage\":{\"available\":";
	dest += storageManager.isReady() ? "true" : "false";

	dest += "}}";
}

void managerGetTelemetryCurrentJson(String& dest)
{
	dest = "{\"schemaVersion\":1,\"deviceId\":";
	managerAppendJsonString(dest, managerGetDeviceId());
	dest += ",\"generatedAt\":";
	uint64_t generatedAt = managerNowMs();
	if (generatedAt) managerAppendUint64(dest, generatedAt);
	else dest += "null";
	dest += ",\"samples\":[";

	bool first = true;
	for (uint32_t i = 0; i < sizeof(telemetrySamples) / sizeof(telemetrySamples[0]); i++) {
		if (!telemetrySamples[i].valid) continue;
		if (!first) dest += ",";
		first = false;
		dest += "{\"relativeTopic\":";
		managerAppendJsonString(dest, telemetrySamples[i].relativeTopic);
		dest += ",";
		managerAppendTimestampField(dest, "timestamp", telemetrySamples[i].timestampMs);
		dest += ",\"sequence\":";
		dest += String(telemetrySamples[i].sequence);
		dest += ",\"data\":";
		dest += telemetrySamples[i].data;
		dest += "}";
	}
	dest += "]}";
}

void managerAppendUint64(String& dest, uint64_t value)
{
	char buffer[24];
	snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
	dest += buffer;
}
