#include "device_base.h"

#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>
#include <ArduinoOTA.h>
#include <AsyncMqttClient.h>
#include <esp_wifi.h>
#include "esp_sntp.h"

#include "debug.h"
#include "json_writer.h"
#include "utils.h"
#include "ui/lcd_ui.h"

static constexpr const char DEBUG_MODULE[] = "base/device";

DeviceBase device;

namespace {

const char* safeText(const char* value, const char* fallback)
{
	return value != nullptr && value[0] != '\0' ? value : fallback;
}

constexpr uint32_t WifiScanPollIntervalMs = 100;
constexpr uint32_t WifiScanTimeoutMs = 30000;

static const char* baseConfigKeys[] = {
	BaseSettingsKeys::wifiMode,
	BaseSettingsKeys::staSsid, BaseSettingsKeys::staPass, BaseSettingsKeys::staticIp,
	BaseSettingsKeys::staIp, BaseSettingsKeys::staMask, BaseSettingsKeys::staGateway,
	BaseSettingsKeys::staDns1, BaseSettingsKeys::staDns2,
	BaseSettingsKeys::apSsid, BaseSettingsKeys::apPass, BaseSettingsKeys::apHide,
	BaseSettingsKeys::apCustomIp, BaseSettingsKeys::apIp, BaseSettingsKeys::apMask,
	BaseSettingsKeys::apGateway,
	BaseSettingsKeys::otaEnabled, BaseSettingsKeys::otaPass,
	BaseSettingsKeys::timeSync, BaseSettingsKeys::timeServer,
	BaseSettingsKeys::timeMinutesWest, BaseSettingsKeys::timeDstMinutes,
	BaseSettingsKeys::mqttEnabled, BaseSettingsKeys::mqttServer, BaseSettingsKeys::mqttPort,
	BaseSettingsKeys::mqttUser, BaseSettingsKeys::mqttPass, BaseSettingsKeys::mqttBaseTopic,
};

// Convierte texto comun de formularios a booleano.
// value contiene el texto recibido.
bool parseBoolText(const char* value)
{
	return value != nullptr
		&& (strcmp(value, "1") == 0
			|| strcasecmp(value, "true") == 0
			|| strcasecmp(value, "on") == 0
			|| strcasecmp(value, "yes") == 0);
}

// Agrega key y su valor usando el tipo almacenado en Preferences.
// writer recibe el JSON, settings es Preferences abierta y key identifica el valor.
void appendBaseConfigValue(JsonWriter& writer, Preferences& settings, const char* key)
{
	writer.appendQuoted(key);
	writer.append(":");
	switch (settings.getType(key)) {
	case PT_U8:
		writer.append(settings.getBool(key) ? "true" : "false");
		break;
	case PT_I32:
		writer.appendFormat("%ld", static_cast<long>(settings.getInt(key)));
		break;
	case PT_U32:
		writer.appendFormat("%lu", static_cast<unsigned long>(settings.getUInt(key)));
		break;
	case PT_STR: {
		char value[96] = {};
		settings.getString(key, value, sizeof(value));
		writer.appendQuoted(value);
		break;
	}
	default:
		writer.append("null");
		break;
	}
}

// Ordena redes WiFi por intensidad de senal.
// networks es el arreglo a ordenar y count indica cuantos elementos contiene.
void sortWifiNetworks(WifiScanNetwork* networks, uint8_t count)
{
	for (uint8_t i = 0; i < count; ++i) {
		for (uint8_t j = i + 1; j < count; ++j) {
			if (networks[j].rssi > networks[i].rssi) {
				const WifiScanNetwork saved = networks[i];
				networks[i] = networks[j];
				networks[j] = saved;
			}
		}
	}
}

// Copia redes detectadas por el ESP32 evitando SSID repetidos.
// networks recibe el resultado y found es la cantidad cruda devuelta por WiFi.
uint8_t collectWifiNetworks(WifiScanNetwork* networks, int16_t found)
{
	uint8_t count = 0;

	for (int16_t i = 0; i < found && count < WifiScanMaxNetworks; ++i) {
		String ssid;
		int32_t rssi = 0;
		int32_t channel = 0;
		uint8_t auth = 0;
		uint8_t* bssid = nullptr;
		if (!WiFi.getNetworkInfo(i, ssid, auth, rssi, bssid, channel) || ssid.isEmpty())
			continue;

		int8_t duplicate = -1;
		for (uint8_t current = 0; current < count; ++current) {
			if (ssid.equals(networks[current].ssid)) {
				duplicate = current;
				break;
			}
		}

		if (duplicate >= 0) {
			if (rssi > networks[duplicate].rssi) {
				networks[duplicate].rssi = rssi;
				networks[duplicate].auth = auth;
				networks[duplicate].channel = channel;
			}
			continue;
		}

		ssid.toCharArray(networks[count].ssid, sizeof(networks[count].ssid));
		networks[count].rssi = rssi;
		networks[count].auth = auth;
		networks[count].channel = channel;
		count++;
	}

	sortWifiNetworks(networks, count);
	return count;
}

}

// Prepara los defaults base y permite que la app los personalice.
// callback recibe una copia editable de los valores genericos.
void DeviceBase::configureSettings(BaseSettingsDefaultsCallback callback)
{
	settingsConfig_ = BaseSettingsConfig();
	if (callback != nullptr)
		callback(settingsConfig_);
	setMainTopic(settingsConfig_.mqttBaseTopic);
}

// Devuelve la configuracion base activa.
const BaseSettingsConfig& DeviceBase::settingsConfig() const
{
	return settingsConfig_;
}

// Devuelve el nombre de Preferences usado por la configuracion base.
const char* DeviceBase::preferencesNamespace() const
{
	return safeText(settingsConfig_.preferencesNamespace, "base-config");
}

// Devuelve el hostname usado por WiFi, OTA y servicios remotos.
const char* DeviceBase::hostName() const
{
	return safeText(settingsConfig_.hostName, "esp32-device");
}

// Devuelve el topico principal MQTT activo en memoria.
const char* DeviceBase::mainTopic() const
{
	return safeText(mainTopic_, hostName());
}

// Actualiza el topico principal MQTT activo en memoria.
void DeviceBase::setMainTopic(const char* topic)
{
	String text = topic != nullptr ? topic : "";
	text.trim();
	snprintf(mainTopic_, sizeof(mainTopic_), "%s", safeText(text.c_str(), hostName()));
}

// Publica una copia estable del ultimo escaneo WiFi.
// state es el estado nuevo, networks contiene redes opcionales y count indica cuantas copiar.
void DeviceBase::publishWifiScan(WifiScanState state, const WifiScanNetwork* networks, uint8_t count)
{
	portENTER_CRITICAL(&wifiScanMux_);
	wifiScanPublishedState_ = state;
	wifiScanPublishedCount_ = min(count, WifiScanMaxNetworks);
	if (networks != nullptr && wifiScanPublishedCount_ > 0)
		memcpy(wifiScanPublishedNetworks_, networks, wifiScanPublishedCount_ * sizeof(WifiScanNetwork));
	portEXIT_CRITICAL(&wifiScanMux_);
}

// Inicia un escaneo WiFi asincronico.
// No recibe parametros; actualiza el estado publicado del escaneo.
void DeviceBase::startWifiScan()
{
	const int16_t result = WiFi.scanNetworks(true);
	if (result != WIFI_SCAN_RUNNING) {
		wifiScanRunning_ = false;
		publishWifiScan(WifiScanState::Failed);
		debugE("No se pudo iniciar el escaneo WiFi: %d", result);
		return;
	}

	wifiScanRunning_ = true;
	wifiScanStartedMs_ = millis();
	wifiScanLastPollMs_ = 0;
	publishWifiScan(WifiScanState::Running);
	debugI("Escaneo WiFi iniciado");
}

/*
	Chequea que exista la configuración de WifiUtils
	Se debe llamar al principio del programa, despues de haber llamado a debugBegin
*/
void DeviceBase::beginSettings()
{
	const char* settingsName = preferencesNamespace();
	debugI("Iniciando '%s' settings...", settingsName);
	if (settings_.begin(settingsName, false) == false) {
		debugE("Fallo al cargar '%s' desde Preferences", settingsName);
	}
	else {
		debugI("Cargando '%s' desde Preferences", settingsName);
		if (settings_.isKey(BaseSettingsKeys::settings) == false) {	//Controlar si existe la clave con valor true, si no está, es porque la configuración está vacía
			settings_.end();	//Cerrar settings_ porque en la proxima función se vuelve a abrir

			if (loadDefaultSettings() == false) { // Cargar configuración por defecto
				debugE("Fallo al guardar '%s' en Preferences", settingsName);
				return;
			}
		}
	}
	settings_.end();
}

void DeviceBase::loopBaseServices()
{
	static uint32_t timerWifi, timerMQTT;

	if (WiFi.isConnected()) {
		timerWifi = millis();
		if (usingMQTT_) {
			if (mqtt_.connected())
				timerMQTT = millis();
			else {
				if (millis() - timerMQTT > 9999) {
					timerMQTT = millis();
					recordMqttConnectAttempt();
					debugI("MQTT reconectando: intento=%lu",
						static_cast<unsigned long>(mqttStatus().connectAttempts));
					mqtt_.connect();
				}
			}
		}
	}
	else {
		if (usingMQTT_)
			publishMqttStatus(MqttConnectionState::WaitingWifi);
		if (millis() - timerWifi > 9999) {
			timerWifi = millis();
			reconnectWifi();
		}
		timerMQTT = millis();
	}

	// if (usingDebug_)Debug.handle();
	if (usingOTA_) ArduinoOTA.handle();
}

void DeviceBase::beginDebug()
{
	debugI("Iniciando servicio de RemoteDebug...");

	/*
		Para iniciar RemoteDebugger primero debemos estar conectados al Wifi
		Agregar una función al evento wifi para que lo inicialice una vez conectado
	*/
	WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
		debugI("Debugger listo!");
		usingDebug_ = true;
		}, ARDUINO_EVENT_WIFI_STA_CONNECTED);
}


void DeviceBase::beginMqtt()
{
	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	settings_.begin(settingsName, false);
	static char server[64], user[32], pass[32];

	bool enabled = settings_.getBool(BaseSettingsKeys::mqttEnabled, config.mqttEnabled);
	if (enabled) {
		debugI("Iniciando servicio MQTT");
		usingMQTT_ = true;
		publishMqttStatus(MqttConnectionState::WaitingWifi);
		String url = settings_.getString(BaseSettingsKeys::mqttServer, config.mqttServer);
		uint32_t port = settings_.getUInt(BaseSettingsKeys::mqttPort, config.mqttPort);
		snprintf(server, sizeof(server), "%s", url.c_str());

		String usr = settings_.getString(BaseSettingsKeys::mqttUser, config.mqttUser);
		String pas = settings_.getString(BaseSettingsKeys::mqttPass, config.mqttPass);
		snprintf(user, sizeof(user), "%s", usr.c_str());
		snprintf(pass, sizeof(pass), "%s", pas.c_str());
		String topic = settings_.getString(BaseSettingsKeys::mqttBaseTopic, config.mqttBaseTopic);
		topic.trim();
		setMainTopic(topic.c_str());

		mqtt_.setServer(server, port);
		if (usr.length() > 0) mqtt_.setCredentials(user, pas.length() > 0 ? pass : nullptr);

		WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
			debugI("Conectando a servidor MQTT %s, base topic '%s'...", server, mainTopic());
			recordMqttConnectAttempt();
			mqtt_.connect();
			}, ARDUINO_EVENT_WIFI_STA_CONNECTED);

		mqtt_.onConnect([this](bool sessionPresent) {
			publishMqttStatus(MqttConnectionState::Connected);
			debugI("MQTT conectado: sesionPrevia=%u intento=%lu", sessionPresent,
				static_cast<unsigned long>(mqttStatus().connectAttempts));
			if (mqttConnectedCallback_ != nullptr)
				mqttConnectedCallback_(mqtt_);
			});

		mqtt_.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
			const int8_t code = static_cast<int8_t>(reason);
			publishMqttStatus(MqttConnectionState::Disconnected, code, true);
			debugW("MQTT desconectado: codigo=%d, motivo=%s", code,
				mqttDisconnectReasonName(code));
			});

		mqtt_.onSubscribe([this](uint16_t packetId, uint8_t qos) {
			recordMqttSubscription();
			debugI("MQTT suscripcion confirmada: packetId=%u qos=%u total=%lu",
				packetId, qos, static_cast<unsigned long>(mqttStatus().subscriptions));
			});

		mqtt_.onUnsubscribe([](uint16_t packetId) {
			debugI("MQTT desuscripcion confirmada: packetId=%u", packetId);
			});

		mqtt_.onMessage([this](char* topic, char*, AsyncMqttClientMessageProperties,
			size_t, size_t index, size_t total) {
			if (index != 0)
				return;
			recordMqttMessage();
			debugD("MQTT mensaje: topic='%s' bytes=%u totalSesion=%lu", topic,
				static_cast<unsigned>(total), static_cast<unsigned long>(mqttStatus().messages));
			});
	}
	else {
		usingMQTT_ = false;
		publishMqttStatus(MqttConnectionState::Disabled);
		debugI("Servicio MQTT desactivado");
	}
	settings_.end();
}

// Actualiza el estado MQTT conservando el ultimo error durante la reconexion.
// state es el estado nuevo, reasonCode reemplaza el error si es no negativo y
// countDisconnect incrementa el contador de fallos observado en RAM.
void DeviceBase::publishMqttStatus(MqttConnectionState state, int8_t reasonCode,
	bool countDisconnect)
{
	const uint32_t now = millis();
	portENTER_CRITICAL(&mqttStatusMux_);
	if (mqttStatus_.state != state)
		mqttStatus_.changedAtMs = now;
	mqttStatus_.state = state;
	if (state == MqttConnectionState::Connected) {
		mqttStatus_.reasonCode = -1;
		mqttStatus_.subscriptions = 0;
		mqttStatus_.messages = 0;
		mqttStatus_.lastMessageAtMs = 0;
	}
	else if (state == MqttConnectionState::Disabled) {
		mqttStatus_.reasonCode = -1;
		mqttStatus_.connectAttempts = 0;
		mqttStatus_.subscriptions = 0;
		mqttStatus_.messages = 0;
		mqttStatus_.lastMessageAtMs = 0;
	}
	else if (reasonCode >= 0)
		mqttStatus_.reasonCode = reasonCode;
	if (countDisconnect)
		mqttStatus_.disconnectCount++;
	portEXIT_CRITICAL(&mqttStatusMux_);
}

// Inicia el diagnostico de un intento real de conexion MQTT.
void DeviceBase::recordMqttConnectAttempt()
{
	const uint32_t now = millis();
	portENTER_CRITICAL(&mqttStatusMux_);
	if (mqttStatus_.state != MqttConnectionState::Connecting)
		mqttStatus_.changedAtMs = now;
	mqttStatus_.state = MqttConnectionState::Connecting;
	mqttStatus_.connectAttempts++;
	portEXIT_CRITICAL(&mqttStatusMux_);
}

// Cuenta una suscripcion confirmada por el broker en la sesion actual.
void DeviceBase::recordMqttSubscription()
{
	portENTER_CRITICAL(&mqttStatusMux_);
	mqttStatus_.subscriptions++;
	portEXIT_CRITICAL(&mqttStatusMux_);
}

// Cuenta un mensaje recibido y conserva el instante para diagnostico web.
void DeviceBase::recordMqttMessage()
{
	portENTER_CRITICAL(&mqttStatusMux_);
	mqttStatus_.messages++;
	mqttStatus_.lastMessageAtMs = millis();
	portEXIT_CRITICAL(&mqttStatusMux_);
}

/*
		Iniciar el Wifi según la configuración que se lee
*/
void DeviceBase::beginWifi()
{
	WiFi.disconnect(true, true);

	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	settings_.begin(settingsName, false);

	debugI("Iniciando Wifi...\n");
	Wifi_Mode mode = static_cast<Wifi_Mode>(settings_.getInt(BaseSettingsKeys::wifiMode, config.wifiMode));
	WiFi.mode(static_cast<wifi_mode_t>(mode));
	if (mode == Wifi_Mode::Station)
		debugI("Wifi modo estacion");
	else if (mode == Wifi_Mode::SoftAP)
		debugI("Wifi modo soft AP");
	else if (mode == Wifi_Mode::Station_AP)
		debugI("Wifi modo soft AP y estacion");
	else {
		debugI("Wifi modo off");
		settings_.end();
		return;
	}

	WiFi.setSleep(false);
	WiFi.setTxPower(WIFI_POWER_8_5dBm);
	debugI("Potencia WiFi TX configurada en 8.5 dBm");
	WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
		if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
			debugI("Conectado a AP (SSID): %s", wifiSsid().c_str());
		}
		else if (event == (WiFiEvent_t)ARDUINO_EVENT_WIFI_STA_GOT_IP) {
			debugI("Se obtuvo direccion IP: %s", WiFi.localIP().toString().c_str());
		}
		else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
			const wifi_err_reason_t reason = static_cast<wifi_err_reason_t>(info.wifi_sta_disconnected.reason);
			debugW("WiFi desconectado: motivo=%u, descripcion=%s",
				static_cast<unsigned>(reason), WiFi.disconnectReasonName(reason));
		}
		else {
			debugI("Evento wifi! code: %d, descripcion: %s", event, wifiEventName(event));
		}
		});

	IPAddress ip, mask, gateway, dns1, dns2;
	bool StaticIP = settings_.getBool(BaseSettingsKeys::staticIp, config.staticIp);
	ip.fromString(settings_.getString(BaseSettingsKeys::staIp, config.staIp));
	mask.fromString(settings_.getString(BaseSettingsKeys::staMask, config.staMask));
	gateway.fromString(settings_.getString(BaseSettingsKeys::staGateway, config.staGateway));
	dns1.fromString(settings_.getString(BaseSettingsKeys::staDns1, config.staDns1));
	dns2.fromString(settings_.getString(BaseSettingsKeys::staDns2, config.staDns2));

	if (StaticIP) {
		debugI("Seteando direccion IP estatica... \n");
		debugI("Direccion IP:    %s", ip.toString().c_str());
		debugI("Mascara de red:  %s", mask.toString().c_str());
		debugI("Gateway:         %s", gateway.toString().c_str());
		debugI("DNS preferiado:  %s", dns1.toString().c_str());
		debugI("DNS alternativo: %s", dns2.toString().c_str());

		if (WiFi.config(ip, gateway, mask, dns1, dns2))
			debugI("IP iniciada correctamente!\n");
		else
			debugW("Error al setear IP!\n");
	}

	String ssid = settings_.getString(BaseSettingsKeys::staSsid, config.staSsid);
	String pass = settings_.getString(BaseSettingsKeys::staPass, config.staPass);
	// String ssid1 = settings_.getString("sta-ssid1", "");
	// String pass1 = settings_.getString("sta-pass1", "");

	ssid.trim();
	if (!ssid.isEmpty()) {
		debugI("Intentando conectar a: %s...", ssid.c_str());
		if (WiFi.begin(ssid.c_str(), pass.c_str()))
			debugI("Ok!");
		else
			debugE("Error!");
	}
	else {
		debugI("WiFi Station sin SSID; queda disponible para escaneo");
	}

	if (mode == Wifi_Mode::SoftAP || mode == Wifi_Mode::Station_AP) {
		// Eventos de modo softAP
		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			debugI("Info: WiFi AP iniciado"); },
			ARDUINO_EVENT_WIFI_AP_START);

		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			debugI("Info: WiFi AP terminado"); }, ARDUINO_EVENT_WIFI_AP_STOP);

		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			debugI("Info: WiFi AP direccion IP asignada: %s", WiFi.softAPIP().toString().c_str()); }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

		IPAddress ip, mask, gateway;
		ip.fromString(settings_.getString(BaseSettingsKeys::apIp, config.apIp));
		mask.fromString(settings_.getString(BaseSettingsKeys::apMask, config.apMask));
		gateway.fromString(settings_.getString(BaseSettingsKeys::apGateway, config.apGateway));
		String ssid = settings_.getString(BaseSettingsKeys::apSsid, config.apSsid);
		String pass = settings_.getString(BaseSettingsKeys::apPass, config.apPass);
		bool hide = settings_.getBool(BaseSettingsKeys::apHide, config.apHide);
		bool customIp = settings_.getBool(BaseSettingsKeys::apCustomIp, config.apCustomIp);

		// Configuracion soft ap
		debugI("Configurando el punto de acceso (AP)...");
		if (WiFi.softAP(ssid.c_str(), pass.c_str(), 1, hide))
			debugI("Ok!");
		else
			debugE("Error!");

		if (customIp) {
			debugI("Usar direccion IP definida por usuario para AP");
			debugI("Direccion IP:    %s", ip.toString().c_str());
			debugI("Mascara de red:  %s", mask.toString().c_str());
			debugI("Gateway:         %s", gateway.toString().c_str());

			if (WiFi.softAPConfig(ip, gateway, mask))
				debugI("IP iniciada correctamente!");
			else
				debugI("Error al setear IP!");
		}
			WiFi.softAPsetHostname(hostName());
		WiFi.enableAP(true);
	}
	debugI("Configuracion Wifi terminada correctamente\n");
	settings_.end();
}

void DeviceBase::reconnectWifi()
{
	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	settings_.begin(settingsName, false);
	Wifi_Mode mode = static_cast<Wifi_Mode>(settings_.getInt(BaseSettingsKeys::wifiMode, config.wifiMode));

	if (mode == Wifi_Mode::Station || mode == Wifi_Mode::Station_AP) {
		String ssid = settings_.getString(BaseSettingsKeys::staSsid, config.staSsid);
		String pass = settings_.getString(BaseSettingsKeys::staPass, config.staPass);
		ssid.trim();
		if (!ssid.isEmpty()) {
			debugI("Reconectando wifi...");
			WiFi.begin(ssid.c_str(), pass.c_str());
		}
	}
	settings_.end();
}

/*
		Evento de conexión de Wifi
*/
// Solicita un nuevo escaneo WiFi.
// force permite reemplazar un pedido o escaneo pendiente.
void DeviceBase::requestWifiScan(bool force)
{
	portENTER_CRITICAL(&wifiScanMux_);
	if (!wifiScanRequested_ && !wifiScanRunning_) {
		wifiScanRequested_ = true;
		wifiScanPublishedState_ = WifiScanState::Requested;
	}
	else if (force && wifiScanRunning_) {
		wifiScanPublishedState_ = WifiScanState::Running;
	}
	portEXIT_CRITICAL(&wifiScanMux_);
}

// Avanza el escaneo WiFi sin bloquear el loop principal.
// No recibe parametros; publica Completed o Failed cuando termina.
void DeviceBase::loopWifiScan()
{
	bool requested = false;
	portENTER_CRITICAL(&wifiScanMux_);
	requested = wifiScanRequested_;
	wifiScanRequested_ = false;
	portEXIT_CRITICAL(&wifiScanMux_);

	if (requested)
		startWifiScan();
	if (!wifiScanRunning_)
		return;

	const uint32_t now = millis();
	if (now - wifiScanLastPollMs_ < WifiScanPollIntervalMs)
		return;
	wifiScanLastPollMs_ = now;

	const int16_t result = WiFi.scanComplete();
	if (result == WIFI_SCAN_RUNNING) {
		if (now - wifiScanStartedMs_ <= WifiScanTimeoutMs)
			return;
		WiFi.scanDelete();
		wifiScanRunning_ = false;
		publishWifiScan(WifiScanState::Failed);
		debugE("El escaneo WiFi supero el tiempo maximo");
		return;
	}

	if (result < 0) {
		WiFi.scanDelete();
		wifiScanRunning_ = false;
		publishWifiScan(WifiScanState::Failed);
		debugE("El escaneo WiFi fallo: %d", result);
		return;
	}

	WifiScanNetwork networks[WifiScanMaxNetworks] = {};
	const uint8_t count = collectWifiNetworks(networks, result);
	WiFi.scanDelete();
	wifiScanRunning_ = false;
	publishWifiScan(WifiScanState::Completed, networks, count);
	debugI("Escaneo WiFi completo: %u redes", count);
}

// Devuelve una copia consistente del ultimo escaneo.
// networks recibe las redes, capacity es su tamano y count devuelve cuantas se copiaron.
WifiScanState DeviceBase::wifiScanSnapshot(WifiScanNetwork* networks, uint8_t capacity, uint8_t& count)
{
	portENTER_CRITICAL(&wifiScanMux_);
	const WifiScanState state = wifiScanPublishedState_;
	count = min(wifiScanPublishedCount_, capacity);
	if (networks != nullptr && count > 0)
		memcpy(networks, wifiScanPublishedNetworks_, count * sizeof(WifiScanNetwork));
	portEXIT_CRITICAL(&wifiScanMux_);
	return state;
}

/*
		Métodos de actualización OTA (over the air)
*/
void DeviceBase::beginOta()
{
	/*
			Iniciar el servicio de actualización por OTA
	*/
	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	settings_.begin(settingsName, false);
	bool enabled = settings_.getBool(BaseSettingsKeys::otaEnabled, config.otaEnabled);
	String pass = settings_.getString(BaseSettingsKeys::otaPass, config.otaPass);

	if (enabled) {
		debugI("Iniciando servicio de actualizacion de software OTA");
		usingOTA_ = true;

		ArduinoOTA.setHostname(hostName());

		if (pass.length() > 0)
			ArduinoOTA.setPassword(pass.c_str());

		ArduinoOTA.onStart([]() {
			// lcd.setGfxMode(false);
			// lcd.printTxt(LCD_LINE0, " Actualizacion  ");

			// if (ArduinoOTA.getCommand() == U_FLASH) {
			// 	debugW("Iniciando actualizacion de firware via OTA...");
			// 	lcd.printTxt(LCD_LINE1, "OTA de firmware ");
			// }
			// else {// U_SPIFFS
			// 	debugW("Iniciando actualizacion de sistema de archivos via OTA...");
			// 	lcd.printTxt(LCD_LINE1, "OTA de archivos ");
			// }
			});

		ArduinoOTA.onEnd([]() {
			debugI("Actualizacion terminada");
			// lcd.printTxt(LCD_LINE2, "    Listo!!!    ");
			// lcd.printTxt(LCD_LINE3, "Reiniciando.....");
			delay(2000);
			}
		);

		ArduinoOTA.onProgress([](uint32_t progress, uint32_t total) {
			static int32_t lastProg = -1, prog;
			prog = (float)progress / (float)total * 100;
			if (prog != lastProg) {
				// lastProg = prog;
				// debugI("Progreso OTA: %d%%", prog);

				// char str[32];
				// snprintf(str, sizeof(str), "      % 3d%%     ", prog);
				// lcd.printTxt(LCD_LINE2, str);

				// uint32_t pos = (float)progress / (float)total * 16;
				// if (pos > 16) pos = 16;
				// for (uint32_t x = 0; x < pos; x++)
				// 	str[x] = 1;
				// for (uint32_t x = pos; x < 16; x++)
				// 	str[x] = 7;
				// str[16] = 0;
				// lcd.printTxt(LCD_LINE3, str);
			}});

			ArduinoOTA.onError([](ota_error_t error) {
				char OTA_ErrorMsg[32];
				if (error == OTA_AUTH_ERROR)
					strcpy(OTA_ErrorMsg, "Error de autenticacion");
				else if (error == OTA_BEGIN_ERROR)
					strcpy(OTA_ErrorMsg, "Error al iniciar");
				else if (error == OTA_CONNECT_ERROR)
					strcpy(OTA_ErrorMsg, "Error de conexion");
				else if (error == OTA_RECEIVE_ERROR)
					strcpy(OTA_ErrorMsg, "Error de recepcion");
				else if (error == OTA_END_ERROR)
					strcpy(OTA_ErrorMsg, "Error al terminar");

				// lcd.printTxt(LCD_LINE2, "Error!");
				// lcd.printTxt(LCD_LINE3, OTA_ErrorMsg);
				delay(2000);

				debugE("OTA Error[%u]: %s", error, OTA_ErrorMsg);
				});

			ArduinoOTA.begin();
	}
	else {
		debugI("Iniciando servicio de actualizacion de software OTA... no esta activo");
		ArduinoOTA.end();
		usingOTA_ = false;
	}
	settings_.end();
}

void DeviceBase::beginTime()
{
	debugI("Iniciando RTC...");

	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	settings_.begin(settingsName, false);
	time_t now = settings_.getUInt(BaseSettingsKeys::timeStart, config.timeStart);
	localTimeZone_.tz_dsttime = settings_.getInt(BaseSettingsKeys::timeDstMinutes, config.timeDstMinutes);
	localTimeZone_.tz_minuteswest = settings_.getInt(BaseSettingsKeys::timeMinutesWest, config.timeMinutesWest);

	setTimeZone(&localTimeZone_);

	setTimeTo(now);

	//Verificar si se usa servidor ntp
	usingTimeServer_ = settings_.getBool(BaseSettingsKeys::timeSync, config.timeSync);
	settings_.end();

	if (usingTimeServer_) {
		WiFi.onEvent([this](WiFiEvent_t event, WiFiEventInfo_t info) {
			setTimeServer();
			}, ARDUINO_EVENT_WIFI_STA_CONNECTED);
	}

	printLocalTime();
}

void DeviceBase::setTimeServer()
{
	static char server[48];
	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	settings_.begin(settingsName, false);

	usingTimeServer_ = settings_.getBool(BaseSettingsKeys::timeSync, config.timeSync);
	String url = settings_.getString(BaseSettingsKeys::timeServer, config.timeServer);
	snprintf(server, sizeof(server), "%s", url.c_str());

	debugI("Iniciando servidor de hora. Server: '%s'...", server);

	settings_.end();

	if (usingTimeServer_ == false) {
		debugW("La actualizacion de hora desde internet esta desactivada.");
		return;
	}
	// Setear el servidor de tiempo
	configTime(localTimeZone_.tz_minuteswest * 60, localTimeZone_.tz_dsttime * 60, server);

	//Agregar un callback para cuando se sincronice la hora 
	sntp_set_time_sync_notification_cb([](struct timeval* tv) {
		debugI("Fecha y hora sincronizada con servidor!!");
		device.printLocalTime();
		});

	/*
			Una vez seteado el servidor sntp la hora se sincroniza automáticamente
			cada una hora.
			https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html
	*/
}



bool DeviceBase::loadDefaultSettings()
{
	uint32_t time = micros();
	const char* settingsName = preferencesNamespace();
	const BaseSettingsConfig& config = settingsConfig();
	debugI("Cargando configuracion por defecto!!");

	if (settings_.begin(settingsName, false) == false) {
		debugE("Fallo al cargar '%s' desde Preferences", settingsName);
		return false;
	}
	settings_.clear();
	settings_.putBool(BaseSettingsKeys::settings, true);

	//Wifi
	settings_.putInt(BaseSettingsKeys::wifiMode, config.wifiMode);
	settings_.putString(BaseSettingsKeys::staSsid, config.staSsid);
	settings_.putString(BaseSettingsKeys::staPass, config.staPass);
	settings_.putBool(BaseSettingsKeys::staticIp, config.staticIp);
	settings_.putString(BaseSettingsKeys::staIp, config.staIp);
	settings_.putString(BaseSettingsKeys::staMask, config.staMask);
	settings_.putString(BaseSettingsKeys::staGateway, config.staGateway);
	settings_.putString(BaseSettingsKeys::staDns1, config.staDns1);
	settings_.putString(BaseSettingsKeys::staDns2, config.staDns2);

	//Wifi AP
	settings_.putString(BaseSettingsKeys::apSsid, config.apSsid);
	settings_.putString(BaseSettingsKeys::apPass, config.apPass);
	settings_.putBool(BaseSettingsKeys::apHide, config.apHide);
	settings_.putBool(BaseSettingsKeys::apCustomIp, config.apCustomIp);
	settings_.putString(BaseSettingsKeys::apIp, config.apIp);
	settings_.putString(BaseSettingsKeys::apMask, config.apMask);
	settings_.putString(BaseSettingsKeys::apGateway, config.apGateway);

	//OTA
	settings_.putBool(BaseSettingsKeys::otaEnabled, config.otaEnabled);
	settings_.putString(BaseSettingsKeys::otaPass, config.otaPass);

	//Time
	settings_.putUInt(BaseSettingsKeys::timeStart, config.timeStart);
	settings_.putInt(BaseSettingsKeys::timeDstMinutes, config.timeDstMinutes);
	settings_.putInt(BaseSettingsKeys::timeMinutesWest, config.timeMinutesWest);

	//NTP servers
	settings_.putString(BaseSettingsKeys::timeServer, config.timeServer);
	settings_.putBool(BaseSettingsKeys::timeSync, config.timeSync);

	//MQTT
	settings_.putBool(BaseSettingsKeys::mqttEnabled, config.mqttEnabled);
	settings_.putString(BaseSettingsKeys::mqttServer, config.mqttServer);
	settings_.putUInt(BaseSettingsKeys::mqttPort, config.mqttPort);
	settings_.putString(BaseSettingsKeys::mqttUser, config.mqttUser);
	settings_.putString(BaseSettingsKeys::mqttPass, config.mqttPass);
	settings_.putString(BaseSettingsKeys::mqttBaseTopic, config.mqttBaseTopic);
	setMainTopic(config.mqttBaseTopic);

	settings_.end();	//Cerrar por las dudas
	time = micros() - time;
	debugI("Configuracion cargada! time: %uus", time);
	return true;
}

bool DeviceBase::isSettingsKey(const char* key) const
{
	if (key == nullptr)
		return false;

	for (const char* configKey : baseConfigKeys) {
		if (strcmp(key, configKey) == 0)
			return true;
	}
	return false;
}

bool DeviceBase::updateSettingField(const char* key, const char* value)
{
	if (!isSettingsKey(key) || value == nullptr)
		return false;

	Preferences settings;
	if (!settings.begin(preferencesNamespace(), false))
		return false;

	bool saved = false;
	switch (settings.getType(key)) {
	case PT_U8:
		saved = settings.putBool(key, parseBoolText(value)) > 0;
		break;
	case PT_I32:
		saved = settings.putInt(key, String(value).toInt()) > 0;
		break;
	case PT_U32:
		saved = settings.putUInt(key, strtoul(value, nullptr, 10)) > 0;
		break;
	case PT_STR:
		saved = settings.putString(key, value) > 0 || value[0] == '\0';
		break;
	default:
		saved = false;
		break;
	}
	settings.end();

	if (saved && strcmp(key, BaseSettingsKeys::mqttBaseTopic) == 0)
		setMainTopic(value);
	return saved;
}

bool DeviceBase::updateSettingsFromForm(const char* formText)
{
	UrlFormDecoder decoder;
	UrlFormToken token;
	if (!decoder.load(formText))
		return false;

	while (decoder.next(token)) {
		if (!updateSettingField(token.key.c_str(), token.value.c_str()))
			return false;
	}
	return decoder.ok();
}

bool DeviceBase::getConfigJson(char* buffer, size_t capacity) const
{
	Preferences settings;
	if (!settings.begin(preferencesNamespace(), true))
		return false;

	JsonWriter writer(buffer, capacity);
	writer.append("{");
	for (size_t i = 0; i < sizeof(baseConfigKeys) / sizeof(baseConfigKeys[0]); ++i) {
		writer.append(i == 0 ? "" : ",");
		appendBaseConfigValue(writer, settings, baseConfigKeys[i]);
	}
	writer.append("}");
	settings.end();
	return writer.valid();
}


time_t DeviceBase::timeNow(time_t* timer)
{
	// time_t now = time(_timer);
	// return now + (localTimeZone_.tz_minuteswest * 60) + (localTimeZone_.tz_dsttime * 60);
	struct timeval now;
	gettimeofday(&now, nullptr);
	return now.tv_sec;
}


struct tm DeviceBase::localTime()
{
	struct timeval now;
	gettimeofday(&now, nullptr);
	return *localtime((time_t*)&now.tv_sec);
}


void DeviceBase::setTimeTo(uint32_t seconds)
{
	struct timeval now = { (time_t)seconds, 0 };
	settimeofday(&now, nullptr);
}

String DeviceBase::setTimeZone(timezone* timeZone)
{
	return setTimeZone(timeZone->tz_minuteswest * 60, timeZone->tz_dsttime * 60);
}

String DeviceBase::setTimeZone(int32_t offset, int32_t daylight)
{
	char cst[17] = { 0 }, cdt[17] = { 0 }, tz[33] = { 0 };

	debugI("Zona horaria: %i, daylight :%i\n", offset, daylight);
	offset *= -1;

	if (offset % 3600) {
		sprintf(cst, "UTC%ld:%02u:%02u", offset / 3600, abs((offset % 3600) / 60), abs(offset % 60));
	}
	else {
		sprintf(cst, "UTC%ld", offset / 3600);
	}
	if (daylight) {
		long tz_dst = offset - daylight;
		if (tz_dst % 3600) {
			sprintf(cdt, "DST%ld:%02u:%02u", tz_dst / 3600, abs((tz_dst % 3600) / 60), abs(tz_dst % 60));
		}
		else {
			sprintf(cdt, "DST%ld", tz_dst / 3600);
		}
	}
	sprintf(tz, "%s%s", cst, cdt);
	debugI("Time Zone: %s", tz);
	setenv("TZ", tz, 1);
	tzset();
	return String(tz);
}

void DeviceBase::printLocalTime()
{
	struct tm now;
	struct timeval time;

	gettimeofday(&time, nullptr);
	now = *localtime((time_t*)&time.tv_sec);

	debugI("Fecha y hora: %02d/%02d/%04d %02d:%02d:%02d", now.tm_mday, now.tm_mon + 1, now.tm_year + 1900, now.tm_hour, now.tm_min, now.tm_sec);
}




String getElapsedTime(time_t time, bool format)
{
	int dias, horas, minutos, segundos;
	dias = time / (24 * 60 * 60);
	if (dias)
		time %= dias * (24 * 60 * 60);

	horas = time / (60 * 60);
	if (horas)
		time %= horas * (60 * 60);

	minutos = time / 60;
	if (minutos)
		time %= minutos * 60;
	segundos = time;

	String res = "";
	if (dias == 1)
		res += String(dias) + " dia ";
	else if (dias > 1)
		res += String(dias) + " dias ";
	if (format) {
		if (horas > 1)
			res += String(horas) + " horas ";
		else if (horas > 0)
			res += String(horas) + " hora ";
		if (minutos > 1)
			res += String(minutos) + " minutos ";
		else if (minutos > 0)
			res += String(minutos) + " minuto ";
		if (segundos > 1 || segundos == 0)
			res += String(segundos) + " segundos ";
		else if (segundos > 0)
			res += String(segundos) + " segundo ";
	}
	else {
		char str[16];
		sprintf(str, "%02d:%02d:%02d", horas, minutos, segundos);
		res += String(str);
	}
	res.trim();
	return res;
}

int32_t DeviceBase::wifiRssi()
{
	wifi_ap_record_t wifidata;
	// uint64_t time = micros();
	if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
		// time = micros() - time;
		// printf("rssi:%d in %u us\n", wifidata.rssi, time);
		return wifidata.rssi;
	}
	return 0;
}

// char* wifiSsid()
// {
// 	static wifi_ap_record_t wifidata;
// 	if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
// 		return (char*)wifidata.ssid;
// 	}
// 	return nullptr;
// }


String DeviceBase::wifiSsid()
{
	static wifi_ap_record_t wifidata;
	if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
		return String((char*)wifidata.ssid);
	}
	return String("");
}


char DeviceBase::wifiRssiCode(int32_t rssi)
{
	char c = ' ';
	if (rssi < -80)
		c = WiFiChar_RSSI_1;
	else if (rssi < -70)
		c = WiFiChar_RSSI_2;
	else if (rssi < -60)
		c = WiFiChar_RSSI_3;
	else // if (RSSI >= -60)
		c = WiFiChar_RSSI_4;
	return c;
}

char DeviceBase::wifiRssiCode() { return wifiRssiCode(wifiRssi()); }

uint16_t DeviceBase::wifiLevelCode()
{
	int32_t rssi = wifiRssi();

	if (!WiFi.isConnected())
		return WiFiLevel_Off; // icono wifi_off
	else if (rssi >= -60)
		return WiFiLevel_RSSI_3; // icono wifi_ok
	else if (rssi >= -80)
		return WiFiLevel_RSSI_2; // icono wifi_mid

	return WiFiLevel_RSSI_1; // icono wifi_low
}

const char* arduino_event_nms[] = {
		"WIFI_READY",
		"SCAN_DONE",
		"STA_START", "STA_STOP", "STA_CONNECTED", "STA_DISCONNECTED", "STA_AUTHMODE_CHANGE", "STA_GOT_IP", "STA_GOT_IP6", "STA_LOST_IP",
		"AP_START", "AP_STOP", "AP_STACONNECTED", "AP_STADISCONNECTED", "AP_STAIPASSIGNED", "AP_PROBEREQRECVED", "AP_GOT_IP6",
		"FTM_REPORT",
		"ETH_START", "ETH_STOP", "ETH_CONNECTED", "ETH_DISCONNECTED", "ETH_GOT_IP", "ETH_GOT_IP6",
		"WPS_ER_SUCCESS", "WPS_ER_FAILED", "WPS_ER_TIMEOUT", "WPS_ER_PIN", "WPS_ER_PBC_OVERLAP",
		"SC_SCAN_DONE", "SC_FOUND_CHANNEL", "SC_GOT_SSID_PSWD", "SC_SEND_ACK_DONE",
		"PROV_INIT", "PROV_DEINIT", "PROV_START", "PROV_END", "PROV_CRED_RECV", "PROV_CRED_FAIL", "PROV_CRED_SUCCESS"
};

const char* DeviceBase::wifiEventName(WiFiEvent_t event) const
{
	if (event >= ARDUINO_EVENT_MAX) return nullptr;
	return arduino_event_nms[event];
}


const char* wifiEventDescription[] = {
	"ESP32 WiFi ready", 										/*!< SYSTEM_EVENT_WIFI_READY  */
	"ESP32 finish scanning AP",                					/*!< SYSTEM_EVENT_SCAN_DONE  */
	"ESP32 station start",                						/*!< SYSTEM_EVENT_STA_START*/
	"ESP32 station stop",                 						/*!< SYSTEM_EVENT_STA_STOP */
	"ESP32 station connected to AP",            				/*!< SYSTEM_EVENT_STA_CONNECTED */
	"ESP32 station disconnected from AP",         				/*!< SYSTEM_EVENT_STA_DISCONNECTED */
	"the auth mode of AP connected by ESP32 station changed",   /*!< SYSTEM_EVENT_STA_AUTHMODE_CHANGE */
	"ESP32 station got IP from connected AP",               	/*!< SYSTEM_EVENT_STA_GOT_IP */
	"ESP32 station lost IP and the IP is reset to 0",           /*!< SYSTEM_EVENT_STA_LOST_IP */
	"ESP32 station connected BSS rssi goes below threshold",    /*!< SYSTEM_EVENT_STA_BSS_RSSI_LOW */
	"ESP32 station wps succeeds in enrollee mode",       		/*!< SYSTEM_EVENT_STA_WPS_ER_SUCCESS */
	"ESP32 station wps fails in enrollee mode",        			/*!< SYSTEM_EVENT_STA_WPS_ER_FAILED */
	"ESP32 station wps timeout in enrollee mode",       		/*!< SYSTEM_EVENT_STA_WPS_ER_TIMEOUT */
	"ESP32 station wps pin code in enrollee mode",           	/*!< SYSTEM_EVENT_STA_WPS_ER_PIN */
	"ESP32 station wps overlap in enrollee mode",   			/*!< SYSTEM_EVENT_STA_WPS_ER_PBC_OVERLAP */
	"ESP32 soft-AP start",                 						/*!< SYSTEM_EVENT_AP_START */
	"ESP32 soft-AP stop",                  						/*!< SYSTEM_EVENT_AP_STOP */
	"a station connected to ESP32 soft-AP",          			/*!< SYSTEM_EVENT_AP_STACONNECTED */
	"a station disconnected from ESP32 soft-AP",       			/*!< SYSTEM_EVENT_AP_STADISCONNECTED */
	"ESP32 soft-AP assign an IP to a connected station",        /*!< SYSTEM_EVENT_AP_STAIPASSIGNED */
	"Receive probe request packet in soft-AP interface",        /*!< SYSTEM_EVENT_AP_PROBEREQRECVED */
	"Receive status of Action frame transmitted",         		/*!< SYSTEM_EVENT_ACTION_TX_STATUS */
	"Indicates the completion of Remain-on-Channel operation status" ,/*!< SYSTEM_EVENT_ROC_DONE*/
	"ESP32 station beacon timeout",       						/*!< SYSTEM_EVENT_STA_BEACON_TIMEOUT */
	"Receive report of FTM procedure",               			/*!< SYSTEM_EVENT_FTM_REPORT */
	"ESP32 station or ap or ethernet interface v6IP addr is preferred", /*!< SYSTEM_EVENT_GOT_IP6 */
	"ESP32 ethernet start",                						/*!< SYSTEM_EVENT_ETH_START */
	"ESP32 ethernet stop",                 						/*!< SYSTEM_EVENT_ETH_STOP */
	"ESP32 ethernet phy link up",            					/*!< SYSTEM_EVENT_ETH_CONNECTED */
	"ESP32 ethernet phy link down",         					/*!< SYSTEM_EVENT_ETH_DISCONNECTED */
	"ESP32 ethernet got IP from connected AP",               	/*!< SYSTEM_EVENT_ETH_GOT_IP */
	"ESP32 ethernet lost IP and the IP is reset to 0",          /*!< SYSTEM_EVENT_ETH_LOST_IP */
};

// Envia eventos de sonido emitidos por la UI a la instancia global de base.
// event identifica el sonido semantico emitido por lcdUi.
static void deviceSoundEventBridge(uiSoundEvent event)
{
	device.playSound(event);
}

// Inicializa los servicios base usando defaults genericos y la UI remota indicada.
bool DeviceBase::begin(lcdUi& ui, BaseSettingsDefaultsCallback defaultsCallback)
{
	ui_ = &ui;
	configureSettings(defaultsCallback);
	if (!ensureSoundQueue())
		return false;

	applyRegisteredCallbacks();
	prepareUi(ui);
	beginServices();
	return true;
}

// Ejecuta las tareas periodicas de conectividad y servidor web.
void DeviceBase::loop()
{
	loopInput();
	loopSound();
	loopBaseServices();
	loopWifiScan();
	webServerLoop();
}

// Registra el callback que entrega el JSON de estado de hardware.
void DeviceBase::setHardwareStatusJsonCallback(webServerJsonCallback callback)
{
	hardwareStatusJsonCallback_ = callback;
	webServerSetHardwareStatusJsonCallback(callback);
}

// Registra el callback que entrega el JSON de estado de aplicacion.
void DeviceBase::setApplicationStatusJsonCallback(webServerJsonCallback callback)
{
	applicationStatusJsonCallback_ = callback;
	webServerSetApplicationStatusJsonCallback(callback);
}

// Registra endpoints propios de la aplicacion en el servidor web base.
void DeviceBase::setApplicationEndpointsCallback(webServerEndpointsCallback callback)
{
	applicationEndpointsCallback_ = callback;
	webServerSetApplicationEndpointsCallback(callback);
}

// Registra la funcion llamada cada vez que MQTT conecta.
void DeviceBase::setMqttConnectedCallback(mqttConnectedCallback callback)
{
	mqttConnectedCallback_ = callback;
}

// Registra una fuente de eventos de teclado para inyectarlos en la UI.
void DeviceBase::setKeypadEventProvider(KeypadEventProvider provider, const uiKeyBehavior& behavior, uiInputSource source)
{
	keypadEventProvider_ = provider;
	keypadBehavior_ = behavior;
	keypadSource_ = source;
}

// Registra el destino fisico de audio usado por la base.
void DeviceBase::setSpeaker(Speaker& speaker)
{
	speaker_ = &speaker;
}

// Encola un sonido de UI para rutearlo a los destinos disponibles.
bool DeviceBase::playSound(uiSoundEvent event, bool loop)
{
	const char* rtttl = rtttlForSoundEvent(event);
	if (rtttl == nullptr)
		return false;

	return playSound(rtttl, loop);
}

// Encola una melodia RTTTL para rutearla a los destinos disponibles.
bool DeviceBase::playSound(const char* rtttl, bool loop)
{
	if (rtttl == nullptr || rtttl[0] == '\0' || !ensureSoundQueue())
		return false;

	if (!loop && speaker_ != nullptr && speaker_->isRepeating()) {
		debugD("Sonido descartado: hay una reproduccion en repeat activa");
		return false;
	}

	SoundCommand command = {};
	command.type = SoundCommandType::Play;
	command.rtttl = rtttl;
	command.loop = loop;
	return xQueueSend(soundQueue_, &command, 0) == pdTRUE;
}

// Detiene el sonido actual y cancela cualquier loop activo.
bool DeviceBase::stopSound()
{
	if (!ensureSoundQueue())
		return false;

	SoundCommand command = {};
	command.type = SoundCommandType::Stop;
	return xQueueSend(soundQueue_, &command, 0) == pdTRUE;
}

// Devuelve true si la interfaz WiFi esta conectada.
bool DeviceBase::isWifiConnected() const
{
	return WiFi.isConnected();
}

// Devuelve true si el cliente MQTT esta conectado.
bool DeviceBase::isMqttConnected() const
{
	return mqtt_.connected();
}

// Devuelve una copia consistente del diagnostico MQTT almacenado en RAM.
MqttStatus DeviceBase::mqttStatus() const
{
	portENTER_CRITICAL(&mqttStatusMux_);
	const MqttStatus snapshot = mqttStatus_;
	portEXIT_CRITICAL(&mqttStatusMux_);
	return snapshot;
}

// Convierte el estado MQTT interno en un identificador estable para la API.
const char* DeviceBase::mqttConnectionStateName(MqttConnectionState state)
{
	switch (state) {
	case MqttConnectionState::WaitingWifi: return "waitingWifi";
	case MqttConnectionState::Connecting: return "connecting";
	case MqttConnectionState::Connected: return "connected";
	case MqttConnectionState::Disconnected: return "disconnected";
	case MqttConnectionState::Disabled:
	default: return "disabled";
	}
}

// Convierte un codigo de AsyncMqttClient en una causa legible para la API.
const char* DeviceBase::mqttDisconnectReasonName(int8_t reasonCode)
{
	switch (reasonCode) {
	case 0: return "tcpDisconnected";
	case 1: return "unacceptableProtocol";
	case 2: return "identifierRejected";
	case 3: return "serverUnavailable";
	case 4: return "malformedCredentials";
	case 5: return "notAuthorized";
	case 6: return "notEnoughMemory";
	case 7: return "badFingerprint";
	default: return "none";
	}
}

// Devuelve el cliente MQTT usado por la base.
AsyncMqttClient& DeviceBase::mqttClient()
{
	return mqtt_;
}

// Devuelve la cantidad de clientes conectados a servicios web remotos.
uint32_t DeviceBase::webClientCount() const
{
	return webServerClientCount();
}

// Aplica callbacks configurados antes de begin().
void DeviceBase::applyRegisteredCallbacks()
{
	if (applicationEndpointsCallback_ != nullptr)
		webServerSetApplicationEndpointsCallback(applicationEndpointsCallback_);

	if (hardwareStatusJsonCallback_ != nullptr)
		webServerSetHardwareStatusJsonCallback(hardwareStatusJsonCallback_);

	if (applicationStatusJsonCallback_ != nullptr)
		webServerSetApplicationStatusJsonCallback(applicationStatusJsonCallback_);
}

// Prepara la UI para trabajar con la base.
void DeviceBase::prepareUi(lcdUi& ui)
{
	ui.clearKeyEvents();
	webServerSetUi(ui);
	ui.setSoundEventCallback(deviceSoundEventBridge);
}

// Inicializa los servicios base.
void DeviceBase::beginServices()
{
	beginDebug();
	beginSettings();
	beginWifi();
	beginTime();
	beginOta();
	beginMqtt();
	webServerBegin();
}

// Procesa la entrada registrada.
void DeviceBase::loopInput()
{
	if (keypadEventProvider_ == nullptr || ui_ == nullptr)
		return;

	keypadEvent rawEvent;
	while (keypadEventProvider_(rawEvent)) {
		uiKeyEvent event;
		event.key = rawEvent.key;
		event.type = rawEvent.type;
		event.source = keypadSource_;
		event.behavior = keypadBehavior_;
		ui_->injectKeyEvent(event);
	}
}

// Procesa el sonido local/remoto.
void DeviceBase::loopSound()
{
	processSoundQueue();
	if (speaker_ != nullptr)
		speaker_->run();
}

// Crea la cola de sonido si todavia no existe.
bool DeviceBase::ensureSoundQueue()
{
	if (soundQueue_ == nullptr)
		soundQueue_ = xQueueCreate(8, sizeof(SoundCommand));
	return soundQueue_ != nullptr;
}

// Procesa los comandos de sonido pendientes.
void DeviceBase::processSoundQueue()
{
	if (soundQueue_ == nullptr)
		return;

	// La cola transporta punteros RTTTL. No se copian para evitar limitar
	// melodias largas; el llamador debe garantizar la vida util documentada.
	SoundCommand command = {};
	while (xQueueReceive(soundQueue_, &command, 0) == pdTRUE) {
		if (command.type == SoundCommandType::Stop) {
			webServerStopRtttl();
			if (speaker_ != nullptr)
				speaker_->stop();
			continue;
		}

		if (command.rtttl == nullptr || command.rtttl[0] == '\0')
			continue;

		if (!command.loop && speaker_ != nullptr && speaker_->isRepeating()) {
			debugD("Sonido de cola descartado: hay una reproduccion en repeat activa");
			continue;
		}

		routeRtttl(command.rtttl, command.loop);
	}
}

// Envia una melodia RTTTL a todos los destinos configurados.
void DeviceBase::routeRtttl(const char* rtttl, bool loop)
{
	if (rtttl == nullptr || rtttl[0] == '\0')
		return;

	webServerPlayRtttl(rtttl, loop);
	if (speaker_ != nullptr && speaker_->isEnabled())
		speaker_->playRtttl(rtttl, loop);
}

// Devuelve la melodia RTTTL asociada a un evento de UI.
const char* DeviceBase::rtttlForSoundEvent(uiSoundEvent event) const
{
	switch (event) {
	case uiSoundEvent::Escape:    return SpeakerSounds::escape;
	case uiSoundEvent::Up:        return SpeakerSounds::clickHigh;
	case uiSoundEvent::Down:      return SpeakerSounds::clickLow;
	case uiSoundEvent::Left:      return SpeakerSounds::left;
	case uiSoundEvent::Right:     return SpeakerSounds::right;
	case uiSoundEvent::Enter:     return SpeakerSounds::accept;
	case uiSoundEvent::EndOfList: return SpeakerSounds::endOfList;
	case uiSoundEvent::LimitLow:  return SpeakerSounds::limitLow;
	case uiSoundEvent::LimitHigh: return SpeakerSounds::limitHigh;
	case uiSoundEvent::Saved:     return SpeakerSounds::saved;
	case uiSoundEvent::Warning:   return SpeakerSounds::warning;
	case uiSoundEvent::Error:     return SpeakerSounds::error;
	case uiSoundEvent::None:
	default:
		return nullptr;
	}
}
