#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "Preferences.h"
#include "SPIFFS.h"
#include <stdarg.h>
#include "debug.h"
#include "device_base.h"
#include "json_writer.h"
#include "speaker.h"
#include "ui/lcd_ui.h"
#include "webserver.h"

static constexpr const char DEBUG_MODULE[] = "webserver";
static constexpr size_t OLED_BUFFER_SIZE = 128U * 64U / 8U;
static constexpr size_t OLED_RAW_PACKET_SIZE = OLED_BUFFER_SIZE + 1U;
static constexpr uint8_t OLED_TILE_COLUMNS = 16;
static constexpr uint8_t OLED_TILE_ROWS = 8;
static constexpr uint8_t OLED_TILE_BYTES = 8;
static constexpr uint8_t OLED_TILE_COUNT = OLED_TILE_COLUMNS * OLED_TILE_ROWS;
static constexpr size_t OLED_TILE_PACKET_SIZE = 1U + OLED_TILE_COUNT * (1U + OLED_TILE_BYTES);
static constexpr uint32_t OLED_WS_STATS_INTERVAL_MS = 500;
static lcdUi* remoteUi = nullptr;
static webServerJsonCallback hardwareStatusJsonCallback = nullptr;
static webServerJsonCallback applicationStatusJsonCallback = nullptr;
static webServerEndpointsCallback applicationEndpointsCallback = nullptr;

AsyncWebServer server(80);
AsyncWebSocket debugWS("/debugws");
AsyncWebSocket remoteScreen("/screenws");

static uint8_t lastOledPacket[OLED_RAW_PACKET_SIZE] = {};
static uint8_t lastOledFrame[OLED_BUFFER_SIZE] = {};
static size_t lastOledPacketSize = 0;
static bool oledBufferValid = false;
static bool oledWsNeedsFull = true;
static volatile bool oledFramePending = false;
static uint32_t lastOledBroadcastMs = 0;
static uint32_t lastWsCleanupMs = 0;
static constexpr uint32_t OLED_BROADCAST_INTERVAL_MS = 100;
static bool debugWsReady = false;
static uint32_t oledWsStatsStartMs = 0;
static uint32_t oledWsStatsFrames = 0;
static uint32_t oledWsStatsFullFrames = 0;
static uint32_t oledWsStatsTileFrames = 0;
static uint32_t oledWsStatsTiles = 0;
static uint32_t oledWsStatsBytesSent = 0;
static uint32_t oledWsStatsRawBytes = 0;

struct DebugWsMessage {
	char text[256];
};

static QueueHandle_t debugWsQueue = nullptr;

// Acumula estadisticas de paquetes OLED enviados por WebSocket.
// packetSize es el tamano real de la trama y packetType indica full o tiles.
static void addOledWsStats(size_t packetSize, uint8_t packetType)
{
	const uint32_t now = millis();
	if (oledWsStatsStartMs == 0)
		oledWsStatsStartMs = now;

	oledWsStatsFrames++;
	oledWsStatsBytesSent += packetSize;
	oledWsStatsRawBytes += OLED_BUFFER_SIZE;
	if (packetType == 2) {
		oledWsStatsTileFrames++;
		oledWsStatsTiles += packetSize > 1 ? (packetSize - 1) / 9 : 0;
	}
	else
		oledWsStatsFullFrames++;

	if (now - oledWsStatsStartMs < OLED_WS_STATS_INTERVAL_MS)
		return;

	const uint32_t savedBytes = oledWsStatsRawBytes > oledWsStatsBytesSent
		? oledWsStatsRawBytes - oledWsStatsBytesSent
		: 0;
	const uint32_t savedPercent = oledWsStatsRawBytes > 0
		? savedBytes * 100UL / oledWsStatsRawBytes
		: 0;

	debugD("oled ws frames=%lu full=%lu tileFrames=%lu tiles=%lu bytes=%lu/%lu saved=%lu%%",
		static_cast<unsigned long>(oledWsStatsFrames),
		static_cast<unsigned long>(oledWsStatsFullFrames),
		static_cast<unsigned long>(oledWsStatsTileFrames),
		static_cast<unsigned long>(oledWsStatsTiles),
		static_cast<unsigned long>(oledWsStatsBytesSent),
		static_cast<unsigned long>(oledWsStatsRawBytes),
		static_cast<unsigned long>(savedPercent));

	oledWsStatsStartMs = now;
	oledWsStatsFrames = 0;
	oledWsStatsFullFrames = 0;
	oledWsStatsTileFrames = 0;
	oledWsStatsTiles = 0;
	oledWsStatsBytesSent = 0;
	oledWsStatsRawBytes = 0;
}

// Prepara el paquete binario de pantalla remota usando los tiles ya detectados por U8G2Clip.
// changedTiles contiene los indices de tile cambiados y fullFrame fuerza resincronizacion.
static void buildOledPacket(const uint8_t* buffer, const uint8_t* changedTiles, uint8_t changedCount, bool fullFrame)
{
	if (!oledWsNeedsFull && !fullFrame && changedTiles != nullptr) {
		const size_t tilePacketSize = 1U + static_cast<size_t>(changedCount) * (1U + OLED_TILE_BYTES);
		if (changedCount == 0) {
			lastOledPacketSize = 0;
			return;
		}
		if (tilePacketSize < OLED_RAW_PACKET_SIZE) {
			lastOledPacket[0] = 2;
			size_t writeIndex = 1;
			for (uint8_t i = 0; i < changedCount; ++i) {
				const uint8_t tileIndex = changedTiles[i];
				if (tileIndex >= OLED_TILE_COUNT)
					continue;
				const size_t offset = static_cast<size_t>(tileIndex) * OLED_TILE_BYTES;
				lastOledPacket[writeIndex++] = tileIndex;
				memcpy(lastOledPacket + writeIndex, buffer + offset, OLED_TILE_BYTES);
				writeIndex += OLED_TILE_BYTES;
			}
			lastOledPacketSize = writeIndex;
			return;
		}
	}

	lastOledPacket[0] = 0;
	memcpy(lastOledPacket + 1, buffer, OLED_BUFFER_SIZE);
	lastOledPacketSize = OLED_RAW_PACKET_SIZE;
}


// Indica si key pertenece a la configuración publicada por la web.
// Construye la configuracion de debug en buffer y devuelve false si no alcanza.
static bool buildDebugConfigJson(char* buffer, size_t capacity, bool includeType = false)
{
	JsonWriter writer(buffer, capacity);
	if (includeType)
		writer.appendFormat("{\"type\":\"debugConfig\",\"level\":%d,\"modules\":[", static_cast<int>(currentDebugLevel));
	else
		writer.appendFormat("{\"level\":%d,\"modules\":[", static_cast<int>(currentDebugLevel));
	const uint8_t count = getDebugModuleCount();
	bool first = true;
	for (uint8_t i = 0; i < count; ++i) {
		const char* name = getDebugModuleName(i);
		if (name == nullptr)
			continue;
		writer.appendFormat("%s{\"name\":", first ? "" : ",");
		first = false;
		writer.appendQuoted(name);
		writer.appendFormat(",\"level\":%d}", static_cast<int>(getDebugLevel(name)));
	}
	writer.append("]}");
	return writer.valid();
}

// Envía buffer como JSON o informa que el buffer previsto fue insuficiente.
static void sendJson(AsyncWebServerRequest* request, const char* buffer, bool valid)
{
	if (!valid) {
		request->send(500, "application/json", "{\"error\":\"Respuesta JSON demasiado grande\"}");
		return;
	}
	request->send(200, "application/json", buffer);
}

static bool buildStatusJson(char* buffer, size_t capacity);
static bool buildWifiScanJson(char* buffer, size_t capacity, bool restart);

// Ejecuta una prueba corta del JsonWriter para detectar errores de estructura.
// No recibe parametros; devuelve false si falla la escritura, composicion o validacion.
static bool runJsonWriterSmokeTest()
{
	char buffer[256];
	JsonWriter json(buffer, sizeof(buffer));
	json.objBegin();
	json.objAppendString("state", "ok");
	json.objAppendBool("enabled", true);
	json.objAppendFloatArray("values", 3, 2, 1.25, 2.50, 3.75);
	json.arrBegin("items");
	json.objBegin();
	json.objAppendString("name", "a");
	json.objAppendInt("count", 2);
	json.objEnd();
	json.arrEnd();
	json.objEnd();

	const char* expected = "{\"state\":\"ok\",\"enabled\":true,\"values\":[1.25,2.50,3.75],\"items\":[{\"name\":\"a\",\"count\":2}]}";
	if (!json.valid() || strcmp(json.c_str(), expected) != 0)
		return false;

	JsonWriter child(64);
	child.objBegin();
	child.objAppendString("child", "ok");
	child.objEnd();

	JsonWriter parent(buffer, sizeof(buffer));
	parent.objBegin();
	parent.objAppendJson("nested", child);
	parent.objEnd();
	if (!parent.valid() || strcmp(parent.c_str(), "{\"nested\":{\"child\":\"ok\"}}") != 0)
		return false;

	JsonWriter broken(buffer, sizeof(buffer));
	broken.objBegin();
	broken.objAppendString("open", "true");
	return !broken.valid() && broken.ok() && broken.depth() == 1;
}

static void onRemoteScreenEvent(AsyncWebSocket* ws, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
	if (type == WS_EVT_CONNECT) {
		client->text("{\"type\":\"display\",\"width\":128,\"height\":64}");
		oledWsNeedsFull = true;
		if (oledBufferValid)
			buildOledPacket(lastOledFrame, nullptr, 0, true);
		oledFramePending = oledBufferValid;
		return;
	}

	if (type != WS_EVT_DATA)
		return;

	AwsFrameInfo* info = static_cast<AwsFrameInfo*>(arg);
	if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT)
		return;

	String key;
	key.reserve(len + 1);
	for (size_t i = 0; i < len; ++i)
		key += static_cast<char>(data[i]);

	if (remoteUi != nullptr && remoteUi->injectKey(key.c_str()))
		client->text("{\"type\":\"key\",\"ok\":true}");
	else
		client->text("{\"type\":\"key\",\"ok\":false}");
}

// Atiende conexiones del WebSocket de debug y envia la configuracion inicial.
// client es el cliente conectado al canal de debug.
static void onDebugWsEvent(AsyncWebSocket* ws, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
	if (type != WS_EVT_CONNECT)
		return;

	char config[2048];
	if (buildDebugConfigJson(config, sizeof(config), true))
		client->text(config);
	client->text("[INFO][webserver] Canal de debug conectado");
}

// Asocia la interfaz que recibira las teclas enviadas por WebSocket.
void webServerSetUi(lcdUi& ui)
{
	remoteUi = &ui;
}

// Registra el callback que arma la respuesta JSON de /hardware.
void webServerSetHardwareStatusJsonCallback(webServerJsonCallback callback)
{
	hardwareStatusJsonCallback = callback;
}

// Registra el callback que arma la respuesta JSON de /app.
void webServerSetApplicationStatusJsonCallback(webServerJsonCallback callback)
{
	applicationStatusJsonCallback = callback;
}

// Registra endpoints propios de la aplicacion para agregarlos al iniciar el servidor.
void webServerSetApplicationEndpointsCallback(webServerEndpointsCallback callback)
{
	applicationEndpointsCallback = callback;
}

void webServerBegin()
{
	static bool corsConfigured = false;

	debugI("Iniciando servidor web...");
	if (!runJsonWriterSmokeTest())
		debugE("JsonWriter smoke test fallo");
	if (debugWsQueue == nullptr)
		debugWsQueue = xQueueCreate(12, sizeof(DebugWsMessage));
	debugWsReady = false;
	server.reset();
	SPIFFS.begin(false);

	server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
		AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/index.html", "text/html");
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});

	server.on("/ping", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "ok");
	});

	server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "ok");
		delay(100);
		ESP.restart();
	});

	server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		char status[2048];
		sendJson(request, status, buildStatusJson(status, sizeof(status)));
	});

	server.on("/hardware", HTTP_GET, [](AsyncWebServerRequest* request) {
		char status[2048];
		if (hardwareStatusJsonCallback == nullptr) {
			snprintf(status, sizeof(status), "{}");
			request->send(200, "application/json", status);
			return;
		}
		sendJson(request, status, hardwareStatusJsonCallback(status, sizeof(status)));
	});

	server.on("/app", HTTP_GET, [](AsyncWebServerRequest* request) {
		char status[2048];
		if (applicationStatusJsonCallback == nullptr) {
			snprintf(status, sizeof(status), "{}");
			request->send(200, "application/json", status);
			return;
		}
		sendJson(request, status, applicationStatusJsonCallback(status, sizeof(status)));
	});

	server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
		char status[2048];
		const bool restart = request->hasParam("refresh") && request->getParam("refresh")->value() == "1";
		if (restart)
			device.requestWifiScan(true);
		sendJson(request, status, buildWifiScanJson(status, sizeof(status), restart));
	});

	server.on("/time", HTTP_POST | HTTP_GET, [](AsyncWebServerRequest* request) {
		if (request->hasParam("body", true))
			device.setTimeTo(request->getParam("body", true)->value().toInt());
		request->send(200, "text/plain", String(device.timeNow(nullptr)));
	});

	server.on("/config", HTTP_GET, [](AsyncWebServerRequest* request) {
		char response[4096];
		sendJson(request, response, device.getConfigJson(response, sizeof(response)));
	});

	server.on("/config", HTTP_POST, [](AsyncWebServerRequest* request) {
		for (size_t i = 0; i < request->params(); ++i) {
			const AsyncWebParameter* param = request->getParam(i);
			if (!param->isPost())
				continue;
			if (!device.updateSettingField(param->name().c_str(), param->value().c_str())) {
				request->send(400, "application/json", "{\"error\":\"Parametro de configuracion invalido\"}");
				return;
			}
		}

		char response[4096];
		sendJson(request, response, device.getConfigJson(response, sizeof(response)));
	});

	server.on("/debug/config", HTTP_GET, [](AsyncWebServerRequest* request) {
		char response[2048];
		sendJson(request, response, buildDebugConfigJson(response, sizeof(response)));
	});

	server.on("/debug/config", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (request->hasParam("clear", true) && request->hasParam("module", true)) {
			clearDebugLevel(request->getParam("module", true)->value().c_str());
		}
		else if (request->hasParam("level", true)) {
			const int level = constrain(request->getParam("level", true)->value().toInt(), 0, 5);
			if (request->hasParam("module", true))
				setDebugLevel(request->getParam("module", true)->value().c_str(), static_cast<DebugLevel>(level));
			else
				setDebugLevel(static_cast<DebugLevel>(level));
		}
		char response[2048];
		sendJson(request, response, buildDebugConfigJson(response, sizeof(response)));
	});

	if (applicationEndpointsCallback != nullptr)
		applicationEndpointsCallback(server);

	// Evita que el navegador conserve JavaScript o estilos de un firmware anterior.
	AsyncStaticWebHandler& staticFiles = server.serveStatic("/", SPIFFS, "/");
	staticFiles.setCacheControl("no-store, no-cache, must-revalidate, max-age=0");

	remoteScreen.onEvent(onRemoteScreenEvent);
	debugWS.onEvent(onDebugWsEvent);
	server.addHandler(&remoteScreen);
	server.addHandler(&debugWS);
	debugWsReady = true;

	if (!corsConfigured) {
		corsConfigured = true;
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
	}

	server.onNotFound([](AsyncWebServerRequest* request) {
		request->send(404, "text/plain", "Page Not Found");
		debugW("File not found: %s", request->url().c_str());
	});

	server.begin();
}

void webServerLoop()
{
	const uint32_t now = millis();
	if (now - lastWsCleanupMs >= 1000) {
		lastWsCleanupMs = now;
		debugWS.cleanupClients();
		remoteScreen.cleanupClients();
	}

	if (oledFramePending && oledBufferValid && lastOledPacketSize > 0 && remoteScreen.count() > 0
		&& now - lastOledBroadcastMs >= OLED_BROADCAST_INTERVAL_MS
		&& remoteScreen.availableForWriteAll()) {
		if (remoteScreen.binaryAll(lastOledPacket, lastOledPacketSize) != AsyncWebSocket::DISCARDED) {
			addOledWsStats(lastOledPacketSize, lastOledPacket[0]);
			oledWsNeedsFull = false;
			lastOledBroadcastMs = now;
			oledFramePending = false;
		}
	}

	if (debugWsQueue == nullptr)
		return;

	DebugWsMessage message;
	if (debugWS.count() == 0)
		return;

	for (uint8_t sent = 0; sent < 2 && debugWS.availableForWriteAll(); ++sent) {
		if (xQueueReceive(debugWsQueue, &message, 0) != pdTRUE)
			break;
		debugWS.textAll(message.text);
	}
}

void webServerBroadcastOled(const uint8_t* buffer, size_t size, const uint8_t* changedTiles, uint8_t changedCount, bool fullFrame)
{
	if (buffer == nullptr || size != OLED_BUFFER_SIZE)
		return;

	if (oledFramePending)
		oledWsNeedsFull = true;
	memcpy(lastOledFrame, buffer, OLED_BUFFER_SIZE);
	buildOledPacket(buffer, changedTiles, changedCount, fullFrame);
	oledBufferValid = true;
	oledFramePending = lastOledPacketSize > 0;
}

void webServerPlayRtttl(const char* rtttl, bool loop)
{
	if (rtttl == nullptr || rtttl[0] == '\0' || remoteScreen.count() == 0 || !remoteScreen.availableForWriteAll())
		return;

	char message[Speaker::maxRtttlLength + 80];
	JsonWriter writer(message, sizeof(message));
	writer.append("{\"type\":\"rtttl\",\"data\":");
	writer.appendQuoted(rtttl);
	writer.append(",\"loop\":");
	writer.append(loop ? "true" : "false");
	writer.append("}");
	if (writer.valid())
		remoteScreen.textAll(message);
}

void webServerStopRtttl()
{
	if (remoteScreen.count() == 0 || !remoteScreen.availableForWriteAll())
		return;

	remoteScreen.textAll("{\"type\":\"rtttlStop\"}");
}

void webServerQueueDebugMessage(const char* message)
{
	if (debugWsQueue == nullptr || message == nullptr)
		return;

	DebugWsMessage queued = {};
	strncpy(queued.text, message, sizeof(queued.text) - 1);
	if (xQueueSend(debugWsQueue, &queued, 0) != pdTRUE) {
		DebugWsMessage discarded;
		xQueueReceive(debugWsQueue, &discarded, 0);
		xQueueSend(debugWsQueue, &queued, 0);
	}
}

void webServerBroadcastDebugConfig()
{
	if (!debugWsReady || debugWS.count() == 0 || !debugWS.availableForWriteAll())
		return;

	char config[2048];
	if (buildDebugConfigJson(config, sizeof(config), true))
		debugWS.textAll(config);
}

// Construye el estado general manejado por la base dentro de buffer.
static bool buildStatusJson(char* buffer, size_t capacity)
{
	const String ssid = device.wifiSsid();
	const String ip = WiFi.localIP().toString();
	const MqttStatus mqtt = device.mqttStatus();
	JsonWriter writer(buffer, capacity);
	writer.objBegin();
	writer.objAppendString("ssid", ssid.c_str());
	writer.objAppendInt("rssi", device.wifiRssi());
	writer.objAppendString("ip", ip.c_str());
	writer.objAppendUInt("heap", ESP.getHeapSize());
	writer.objAppendUInt("free", ESP.getFreeHeap());
	writer.objAppendUInt("uptime", millis() / 1000UL);
	writer.objBegin("mqtt");
	writer.objAppendBool("enabled", mqtt.state != MqttConnectionState::Disabled);
	writer.objAppendBool("connected", device.isMqttConnected());
	writer.objAppendString("state", DeviceBase::mqttConnectionStateName(mqtt.state));
	if (mqtt.reasonCode >= 0) {
		writer.objAppendInt("reasonCode", mqtt.reasonCode);
		writer.objAppendString("reason", DeviceBase::mqttDisconnectReasonName(mqtt.reasonCode));
	}
	else {
		writer.objAppendNull("reasonCode");
		writer.objAppendNull("reason");
	}
	writer.objAppendUInt("disconnects", mqtt.disconnectCount);
	writer.objAppendUInt("connectAttempts", mqtt.connectAttempts);
	writer.objAppendUInt("subscriptions", mqtt.subscriptions);
	writer.objAppendUInt("messages", mqtt.messages);
	if (mqtt.lastMessageAtMs != 0)
		writer.objAppendUInt("lastMessageAgeSeconds", (millis() - mqtt.lastMessageAtMs) / 1000UL);
	else
		writer.objAppendNull("lastMessageAgeSeconds");
	writer.objAppendUInt("ageSeconds", (millis() - mqtt.changedAtMs) / 1000UL);
	writer.objEnd();
	writer.objEnd();
	return writer.valid();
}

// Construye el estado y resultado del escaneo WiFi dentro de buffer.
static bool buildWifiScanJson(char* buffer, size_t capacity, bool restart)
{
	WifiScanNetwork found[WifiScanMaxNetworks] = {};
	uint8_t count = 0;
	const WifiScanState state = device.wifiScanSnapshot(found, WifiScanMaxNetworks, count);
	JsonWriter writer(buffer, capacity);

	if (state == WifiScanState::Idle)
		device.requestWifiScan(false);

	if (state == WifiScanState::Failed) {
		writer.objBegin();
		writer.objAppendString("scan", "failed");
		writer.arrBegin("list");
		writer.arrEnd();
		writer.objEnd();
	}
	else if (restart || state == WifiScanState::Idle || state == WifiScanState::Requested || state == WifiScanState::Running) {
		writer.objBegin();
		writer.objAppendString("scan", "running");
		writer.arrBegin("list");
		writer.arrEnd();
		writer.objEnd();
	}
	else {
		writer.objBegin();
		writer.objAppendString("scan", "completed");
		writer.arrBegin("list");
		for (uint8_t i = 0; i < count; ++i) {
			writer.objBegin();
			writer.objAppendString("ssid", found[i].ssid);
			writer.objAppendInt("rssi", found[i].rssi);
			writer.objAppendString("auth", getWifiAuthMode(found[i].auth));
			writer.objEnd();
		}
		writer.arrEnd();
		writer.objEnd();
	}
	return writer.valid();
}

const char* getWifiAuthMode(uint8_t mode)
{
	switch (mode) {
	case WIFI_AUTH_OPEN: return "OPEN";
	case WIFI_AUTH_WEP: return "WEP";
	case WIFI_AUTH_WPA_PSK: return "WPA_PSK";
	case WIFI_AUTH_WPA2_PSK: return "WPA2_PSK";
	case WIFI_AUTH_WPA_WPA2_PSK: return "WPA_WPA2_PSK";
	case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2_ENTERPRISE";
	case WIFI_AUTH_WPA3_PSK: return "WPA3_PSK";
	case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2_WPA3_PSK";
	case WIFI_AUTH_WAPI_PSK: return "WAPI_PSK";
	default: return "";
	}
}

AsyncWebSocket* webServerGetDebugWS()
{
	return &debugWS;
}

uint32_t webServerClientCount()
{
	return remoteScreen.count() + debugWS.count();
}
