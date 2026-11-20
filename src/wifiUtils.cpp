#include "wifiUtils.h"
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "AsyncMqttClient.h"
#include "ST7920_SPI.h"
#include "manager_api.h"
#include "rtc_clock.h"
#include "memory_diagnostics.h"
#include "diagnostic_logger.h"
#include "storage/wave_event_store.h"
#include <new>
#include <atomic>

Preferences Settings;
const char* settingsName = "wifi-stuff";
static Wifi_Mode configuredWifiMode = Wifi_Mode::Off;
static bool recoveryAp = false, stationHasSsid = false, apAttempted = false;
static uint32_t lastStationOnlineMs = 0, stationOnlineSinceMs = 0;
static portMUX_TYPE networkMutex = portMUX_INITIALIZER_UNLOCKED;
static uint8_t pendingNetworkServices = 0;
static bool networkNvsLocked = false;
static uint32_t networkApplyAfterMs = 0;
static std::atomic<uint8_t> networkState{0};
static std::atomic<bool> timeConnectionPending{false};
static std::atomic<bool> debugConnectionPending{false};
struct disconnectLogWindow { uint32_t lastLogMs = 0; uint16_t suppressed = 0; bool hasLogged = false; };
static disconnectLogWindow wifiDisconnectLog, mqttDisconnectLog;

// Limita el aviso a uno por minuto; moduleId filtra, label y reason describen la causa, window agrupa repeticiones.
static void logDisconnectRateLimited(uint32_t moduleId, const char* label, uint8_t reason,
	disconnectLogWindow& window)
{
	const uint32_t now = millis();
	if (!window.hasLogged || uint32_t(now - window.lastLogMs) >= 60000) {
		const uint16_t count = window.suppressed + 1;
		window.lastLogMs = now;
		window.suppressed = 0;
		window.hasLogged = true;
		DIAG_WARN(moduleId, "%s desconectado motivo=%u eventos_agrupados=%u", label, unsigned(reason), unsigned(count));
	}
	else ++window.suppressed;
}

// Serializa lectura y escritura de grupos completos; needed=false evita bloquear otros namespaces.
networkSettingsGuard::networkSettingsGuard(bool needed) : locked(!needed), needed(needed) {
	if (!needed) return;
	portENTER_CRITICAL(&networkMutex);
	if (!networkNvsLocked) { networkNvsLocked = true; locked = true; }
	portEXIT_CRITICAL(&networkMutex);
}

// Libera el acceso a NVS de esta instancia; sin parametros.
networkSettingsGuard::~networkSettingsGuard() {
	if (!needed || !locked) return;
	portENTER_CRITICAL(&networkMutex); networkNvsLocked = false; portEXIT_CRITICAL(&networkMutex);
}

// Publica services sin borrar solicitudes concurrentes y deja un segundo para entregar la respuesta HTTP.
void networkQueueApply(uint8_t services) {
	if (!services) return;
	portENTER_CRITICAL(&networkMutex);
	pendingNetworkServices |= services;
	networkApplyAfterMs = millis() + 1000U;
	networkState = 1;
	portEXIT_CRITICAL(&networkMutex);
}

// Informa el estado del ultimo cambio de servicios; sin parametros.
const char* networkApplyStatus() {
	const char* states[] = {"ok", "wait", "apply", "fail"};
	return states[networkState.load()];
}

// Reconfigura solo la radio; preserveAccess habilita el AP temporal durante un cambio de estacion.
static bool applyWifiRuntime(bool preserveAccess);

// Consume por separado Wi-Fi y hora desde el loop principal; sin parametros.
static void pollNetworkConfig() {
	if (waveCaptureBusy()) return;
	portENTER_CRITICAL(&networkMutex);
	const bool ready = pendingNetworkServices && int32_t(millis() - networkApplyAfterMs) >= 0;
	portEXIT_CRITICAL(&networkMutex);
	if (!ready) return;
	networkSettingsGuard guard;
	if (!guard.locked) return;
	uint8_t services = 0;
	portENTER_CRITICAL(&networkMutex);
	if (pendingNetworkServices && int32_t(millis() - networkApplyAfterMs) >= 0) {
		services = pendingNetworkServices; pendingNetworkServices = 0; networkState = 2;
	}
	portEXIT_CRITICAL(&networkMutex);
	if (!services) return;
	bool ok = true;
	if (services & 1U) ok = applyWifiRuntime(true);
	if (services & 2U) ok = TimeSetServer() && ok;
	portENTER_CRITICAL(&networkMutex);
	if (!pendingNetworkServices) networkState = ok ? 0 : 3;
	portEXIT_CRITICAL(&networkMutex);
}

// Inicia el AP guardado; recovery lo hace visible aunque el AP manual estuviera oculto.
static bool startConfiguredAp(bool recovery)
{
	Preferences options;
	if (!options.begin(settingsName, true)) return false;
	const String ssid = options.getString("ap-ssid", "PowerMeter9000"), password = options.getString("ap-pass", "");
	const bool hidden = !recovery && options.getBool("ap-hide", false);
	IPAddress ip(192,168,4,1), mask(255,255,255,0), gateway(192,168,4,1);
	if (options.getBool("ap-custom-ip", false)) {
		ip.fromString(options.getString("ap-ip", "192.168.4.1"));
		mask.fromString(options.getString("ap-mask", "255.255.255.0"));
		gateway.fromString(options.getString("ap-gw", "192.168.4.1"));
	}
	options.end();
	WiFi.mode(configuredWifiMode == Wifi_Mode::SoftAP ? WIFI_MODE_AP : WIFI_MODE_APSTA);
	if (!WiFi.softAPConfig(ip, gateway, mask) || !WiFi.softAP(ssid.c_str(), password.c_str(), 1, hidden)) return false;
	WiFi.softAPsetHostname("PowerMeter9000");
	debugI("AP %s: %s, IP %s%s", recovery ? "recuperacion" : "configurado", ssid.c_str(),
		WiFi.softAPIP().toString().c_str(), password.isEmpty() ? " (sin clave)" : "");
	return true;
}

// Mantiene un AP tras 30 s sin red y lo retira al recuperar la estacion sin clientes; sin parametros.
static void pollWifiRecovery()
{
	if (configuredWifiMode != Wifi_Mode::Station) return;
	const uint32_t now = millis();
	if (WiFi.isConnected()) {
		lastStationOnlineMs = now;
		if (!stationOnlineSinceMs) stationOnlineSinceMs = now;
		if (recoveryAp && uint32_t(now - stationOnlineSinceMs) >= 30000U && !WiFi.softAPgetStationNum()) {
			WiFi.softAPdisconnect(false); WiFi.mode(WIFI_MODE_STA); recoveryAp = false;
			debugI("AP de recuperacion cerrado: estacion conectada");
		}
	} else {
		stationOnlineSinceMs = 0;
		if (!recoveryAp && ((!stationHasSsid && !apAttempted) || uint32_t(now - lastStationOnlineMs) >= 30000U)) {
			networkSettingsGuard guard;
			if (!guard.locked) return;
			lastStationOnlineMs = now;
			apAttempted = true;
			recoveryAp = startConfiguredAp(true);
		}
	}
}

// Devuelve si el AP temporal esta activo; sin parametros.
bool wifiRecoveryApActive() { return recoveryAp; }

// Aplica la direccion IP de la estacion guardada o vuelve a DHCP.
// options: preferencias abiertas para esta conexion o cambio de direccion.
static bool configureStationIp(Preferences& options)
{
	if (!options.getBool("staticIp", false))
		return WiFi.config(IPAddress(), IPAddress(), IPAddress());
	IPAddress ip, mask, gateway, dns1, dns2;
	ip.fromString(options.getString("sta-ip", "192.168.1.90"));
	mask.fromString(options.getString("sta-mask", "255.255.254.0"));
	gateway.fromString(options.getString("sta-gw", "192.168.0.1"));
	dns1.fromString(options.getString("sta-dns1", "8.8.8.8"));
	dns2.fromString(options.getString("sta-dns2", "4.4.4.4"));
	return WiFi.config(ip, gateway, mask, dns1, dns2);
}

AsyncMqttClient mqtt;

RemoteDebug Debug;

timezone localTimeZone = { 0, -180 };
const char* hostName = "PowerMeter9000";

bool usingOTA = false;
bool usingDebug = false;
bool usingTimeServer = false;
bool usingMQTT = false;

/*
	Chequea que exista la configuración de WifiUtils
	Se debe llamar al principio del programa, después de haber llamado a DebugStart
*/
void UtilsInitSettings()
{
	Debug.setSerialEnabled(true);
	debugI("Iniciando '%s' settings...", settingsName);
	if (Settings.begin(settingsName, false) == false) {
		debugE("Fallo al cargar 'wifi-stuff' desde Preferences");
	}
	else {
		debugI("Cargando 'wifi-stuff' desde Preferences");
		if (Settings.isKey("settings") == false) {	//Controlar si existe la clave con valor true, si no está, es porque la configuración está vacía
			Settings.end();	//Cerrar settings porque en la proxima función se vuelve a abrir

			if (UtilsLoadDeafultSettings() == false) { // Cargar configuración por defecto
				debugE("Fallo al guardar 'wifi-stuff' en Preferences");
				return;
			}
		}
	}
	Settings.end();
}

// Inicia RemoteDebug leyendo sus ajustes solo cuando se establece una conexion; sin parametros.
static void startRemoteDebug();

void UtilsLoop()
{
	static uint32_t timerWifi, timerMQTT;
	pollNetworkConfig();
	if (timeConnectionPending.load()) {
		networkSettingsGuard guard;
		if (guard.locked && timeConnectionPending.exchange(false)) TimeSetServer();
	}
	if (debugConnectionPending.exchange(false)) startRemoteDebug();
	pollWifiRecovery();

	if (WiFi.isConnected()) {
		timerWifi = millis();
		if (usingMQTT) {
			if (mqtt.connected())
				timerMQTT = millis();
			else {
				if (millis() - timerMQTT > 9999) {
					timerMQTT = millis();
					debugI("Reconectando mqtt...");
					mqtt.connect();
				}
			}
		}
	}
	else {
		if (millis() - timerWifi > 9999) {
			timerWifi = millis();
			WifiReconnect();
		}
		timerMQTT = millis();
	}

	if (usingDebug)Debug.handle();
	if (usingOTA && memoryHasRoom(2048, 16 * 1024)) {
		try {
			ArduinoOTA.handle();
		} catch (const std::bad_alloc&) {
			memoryReportState("OTA allocation rejected", 1460);
		}
	}
}

void DebugStart()
{
	//Solo hacer esta pre-configuración del puerto serie para poder iniciar la escritura por el puerto serie usando las macros de debug
	Debug.setSerialEnabled(true);
	debugI("Iniciando servicio RemoteDebug...");

	/*
		Para iniciar RemoteDebugger primero debemos estar conectados al Wifi
		Agregar una función al evento wifi para que lo inicialice una vez conectado
	*/
	WiFi.onEvent([](arduino_event_t* event) { debugConnectionPending = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// Lee nombre y clave al conectar, los entrega a RemoteDebug y libera los temporales; sin parametros.
static void startRemoteDebug() {
	networkSettingsGuard guard;
	if (!guard.locked) { debugConnectionPending = true; return; }
	Preferences options;
	if (!options.begin(settingsName, true)) return;
	usingDebug = options.getBool("debug-enabled", true);
	if (usingDebug) {
		Debug.begin(options.getString("debug-name", hostName));
		Debug.setPassword(options.getString("debug-pass", ""));
		Debug.showProfiler(true); Debug.showColors(true);
	}
	options.end();
}

void MQTTStart()
{
	Settings.begin(settingsName, false);
	static char server[64], user[32], pass[32];

	bool enabled = Settings.getBool("mqtt-enabled", false);
	if (enabled) {
		debugI("Iniciando servicio MQTT");
		usingMQTT = true;
		String url = Settings.getString("mqtt-server");
		uint32_t port = Settings.getUInt("mqtt-port", 1883);
		snprintf(server, sizeof(server), "%s", url.c_str());

		String usr = Settings.getString("mqtt-user");
		String pas = Settings.getString("mqtt-pass");
		snprintf(user, sizeof(user), "%s", usr.c_str());
		snprintf(pass, sizeof(pass), "%s", pas.c_str());

		mqtt.setServer(server, port);
		if (usr.length() > 0) mqtt.setCredentials(user, pas.length() > 0 ? pass : nullptr);

		WiFi.onEvent([](arduino_event_t* event) {
			debugI("Conectando a servidor MQTT %s...", server);
			mqtt.connect();
			}, ARDUINO_EVENT_WIFI_STA_CONNECTED);

		mqtt.onConnect([](bool sessionPresent) {
			debugI("Conectado a servidor MQTT!");
			DIAG_INFO(DiagnosticModule::mqtt, "Conectado, sesion_previa=%u", sessionPresent ? 1 : 0);
			});

		mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
			debugI("Desconectado de servidor MQTT!");
			logDisconnectRateLimited(DiagnosticModule::mqtt, "MQTT", uint8_t(reason), mqttDisconnectLog);
			});

	}
	else {
		usingMQTT = false;
		debugI("Servicio MQTT desactivado");
	}
	Settings.end();
}

/*
		Iniciar el Wifi según la configuración que se lee
*/
void WifiStart()
{
	WiFi.persistent(false);
	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
		if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
			debugI("Conectado a AP (SSID): %s", getWiFiSSID());
			DIAG_INFO(DiagnosticModule::wifi, "Asociado al punto de acceso");
		}
		else if (event == (WiFiEvent_t)ARDUINO_EVENT_WIFI_STA_GOT_IP) {
			debugI("Se obtuvo direccion IP: %s", WiFi.localIP().toString().c_str());
			const IPAddress address = WiFi.localIP();
			DIAG_INFO(DiagnosticModule::wifi, "IP asignada=%u.%u.%u.%u RSSI=%d",
				address[0], address[1], address[2], address[3], WiFi.RSSI());
		}
		else if (event == (WiFiEvent_t)ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
			logDisconnectRateLimited(DiagnosticModule::wifi, "WiFi", uint8_t(info.wifi_sta_disconnected.reason),
				wifiDisconnectLog);
		}
		});
	if (!applyWifiRuntime(false)) debugE("No se pudo iniciar Wi-Fi");
}

// Lee ajustes al cambiar la radio; preserveAccess mantiene acceso por AP mientras reconecta la estacion.
static bool applyWifiRuntime(bool preserveAccess)
{
	Preferences options;
	if (!options.begin(settingsName, true)) return false;
	const int32_t mode = options.getInt("wifi-mode", 1);
	const String ssid = options.getString("sta-ssid", ""), password = options.getString("sta-pass", "");
	configuredWifiMode = static_cast<Wifi_Mode>(mode);
	recoveryAp = false; apAttempted = false;
	stationHasSsid = !ssid.isEmpty();
	lastStationOnlineMs = millis(); stationOnlineSinceMs = 0;
	WiFi.disconnect(false, false);
	const bool temporaryAp = preserveAccess && configuredWifiMode == Wifi_Mode::Station;
	if (!WiFi.mode(temporaryAp ? WIFI_MODE_APSTA : static_cast<wifi_mode_t>(mode))) { options.end(); return false; }
	if (configuredWifiMode == Wifi_Mode::Off) { options.end(); return true; }
	WiFi.setSleep(false);
	bool ok = true;
	if (temporaryAp) { recoveryAp = startConfiguredAp(true); apAttempted = true; ok = recoveryAp; }
	else if (configuredWifiMode == Wifi_Mode::SoftAP || configuredWifiMode == Wifi_Mode::Station_AP) ok = startConfiguredAp(false);
	if (configuredWifiMode == Wifi_Mode::Station || configuredWifiMode == Wifi_Mode::Station_AP) {
		ok = configureStationIp(options) && ok;
		if (stationHasSsid) ok = WiFi.begin(ssid.c_str(), password.c_str()) != WL_CONNECT_FAILED && ok;
	}
	options.end();
	return ok;
}

// Lee las credenciales solamente al reintentar la estacion; sin parametros.
void WifiReconnect()
{
	if (configuredWifiMode != Wifi_Mode::Station && configuredWifiMode != Wifi_Mode::Station_AP) return;
	portENTER_CRITICAL(&networkMutex);
	const bool pending = (pendingNetworkServices & 1U) != 0;
	portEXIT_CRITICAL(&networkMutex);
	if (pending) return;
	networkSettingsGuard guard;
	Preferences options;
	if (!guard.locked || !options.begin(settingsName, true)) return;
	const String ssid = options.getString("sta-ssid", ""), password = options.getString("sta-pass", "");
	options.end();
	if (!ssid.isEmpty()) WiFi.begin(ssid.c_str(), password.c_str());
}

// Guarda la red y conecta con la politica IP configurada; ssid es el nombre y password puede estar vacia.
bool wifiSaveStationCredentials(const String& ssid, const String& password)
{
	wifiStationConfig config;
	if (!wifiLoadStationConfig(config) || ssid.isEmpty()) return false;
	config.enabled = true;
	config.ssid = ssid;
	config.password = password;
	return wifiSaveStationConfig(config);
}

// Carga desde NVS el modo de estacion, credenciales y parametros IP; config recibe los valores.
bool wifiLoadStationConfig(wifiStationConfig& config)
{
	networkSettingsGuard guard;
	Preferences options;
	if (!guard.locked || !options.begin(settingsName, true)) return false;
	config.enabled = options.getInt("wifi-mode", 1) != 0;
	config.staticIp = options.getBool("staticIp", false);
	config.ssid = options.getString("sta-ssid", ""); config.password = options.getString("sta-pass", "");
	config.ip = options.getString("sta-ip", "192.168.1.90"); config.mask = options.getString("sta-mask", "255.255.254.0");
	config.gateway = options.getString("sta-gw", "192.168.0.1"); config.dns1 = options.getString("sta-dns1", "8.8.8.8");
	config.dns2 = options.getString("sta-dns2", "4.4.4.4");
	options.end();
	return true;
}

// Persiste config y marca la reconfiguracion pendiente de la estacion.
bool wifiSaveStationConfig(const wifiStationConfig& config)
{
	IPAddress ip, mask, gateway, dns1, dns2;
	if (config.staticIp && (!ip.fromString(config.ip) || !mask.fromString(config.mask) ||
		!gateway.fromString(config.gateway) || !dns1.fromString(config.dns1) ||
		!dns2.fromString(config.dns2))) return false;
	networkSettingsGuard guard;
	if (!guard.locked || !Settings.begin(settingsName, false)) return false;

	Settings.putInt("wifi-mode", static_cast<int32_t>(config.enabled ? Wifi_Mode::Station : Wifi_Mode::Off));
	Settings.putBool("staticIp", config.staticIp);
	Settings.putString("sta-ssid", config.ssid);
	Settings.putString("sta-pass", config.password);
	Settings.putString("sta-ip", config.ip);
	Settings.putString("sta-mask", config.mask);
	Settings.putString("sta-gw", config.gateway);
	Settings.putString("sta-dns1", config.dns1);
	Settings.putString("sta-dns2", config.dns2);
	Settings.end();
	networkQueueApply(1);
	return true;
}

// Inicia o reutiliza el escaneo asíncrono. No recibe parámetros.
void wifiScanStart()
{
	if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
	WiFi.scanDelete();
	WiFi.scanNetworks(true);
}

// Devuelve el estado actual del escaneo. No recibe parámetros.
int32_t wifiScanStatus()
{
	return WiFi.scanComplete();
}

// Lee una red: index es su índice; ssid, auth y rssi reciben sus datos.
bool wifiScanGetNetwork(int32_t index, String& ssid, uint8_t& auth, int32_t& rssi)
{
	int32_t channel;
	uint8_t* bssid;
	return index >= 0 && WiFi.getNetworkInfo(index, ssid, auth, rssi, bssid, channel);
}

/*
		Evento de conexión de Wifi
*/
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info)
{

	if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
		debugI("Conectado a AP (SSID): %s", getWiFiSSID());

	}
	else if (event == (WiFiEvent_t)ARDUINO_EVENT_WIFI_STA_GOT_IP) {
		debugI("Se obtuvo direccion IP: %s", WiFi.localIP().toString().c_str());
	}
	else {
		debugI("Evento wifi! code: %d, descripcion: %s", event, getWifiEventName(event));
	}
}




/*
		Métodos de actualización OTA (over the air)
*/
extern ST7920_SPI lcd;
void OTAStart()
{
	/*
			Iniciar el servicio de actualización por OTA
	*/
	Settings.begin(settingsName, false);
	bool enabled = Settings.getBool("ota-enabled", true);
	String name = Settings.getString("ota-name", "");
	String pass = Settings.getString("ota-pass", "");

	if (enabled) {
		debugI("Iniciando servicio de actualizacion de software OTA");
		usingOTA = true;

		if (name.length() > 0)
			ArduinoOTA.setHostname(name.c_str());
		else
			ArduinoOTA.setHostname("PowerMeter9000");

		if (pass.length() > 0)
			ArduinoOTA.setPassword(pass.c_str());

		ArduinoOTA.onStart([]() {
			lcd.setGfxMode(false);
			lcd.printTxt(LCD_LINE0, " Actualizacion  ");

			if (ArduinoOTA.getCommand() == U_FLASH) {
				debugW("Iniciando actualizacion de firware via OTA...");
				lcd.printTxt(LCD_LINE1, "OTA de firmware ");
			}
			else {// U_SPIFFS
				debugW("Iniciando actualizacion de sistema de archivos via OTA...");
				lcd.printTxt(LCD_LINE1, "OTA de archivos ");
			}
			});

		ArduinoOTA.onEnd([]() {
			debugI("Actualizacion terminada");
			lcd.printTxt(LCD_LINE2, "    Listo!!!    ");
			lcd.printTxt(LCD_LINE3, "Reiniciando.....");
			delay(2000);
			}
		);

		ArduinoOTA.onProgress([](uint32_t progress, uint32_t total) {
			static int32_t lastProg = -1, prog;
			prog = (float)progress / (float)total * 100;
			if (prog != lastProg) {
				lastProg = prog;
				debugI("Progreso OTA: %d%%", prog);

				char str[32];
				snprintf(str, sizeof(str), "      % 3d%%     ", prog);
				lcd.printTxt(LCD_LINE2, str);

				uint32_t pos = (float)progress / (float)total * 16;
				if (pos > 16) pos = 16;
				for (uint32_t x = 0; x < pos; x++)
					str[x] = 1;
				for (uint32_t x = pos; x < 16; x++)
					str[x] = 7;
				str[16] = 0;
				lcd.printTxt(LCD_LINE3, str);
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

			lcd.printTxt(LCD_LINE2, "Error!");
			lcd.printTxt(LCD_LINE3, OTA_ErrorMsg);
			delay(2000);

			debugE("OTA Error[%u]: %s", error, OTA_ErrorMsg);
			});

		ArduinoOTA.begin();
	}
	else {
		debugI("Iniciando servicio de actualizacion de software OTA... no esta activo");
		ArduinoOTA.end();
		usingOTA = false;
	}
	Settings.end();
}

void OTAEnd()
{
	debugI("Deteniendo el servicio de actualizacion de software OTA...");
	ArduinoOTA.end();
	usingOTA = false;
}


// Establece la hora local de arranque antes de iniciar adquisicion; no recibe parametros.
void timeInitClock()
{
	debugI("Iniciando RTC...");

	Settings.begin(settingsName, false);
	time_t now = Settings.getUInt("time-start", 1704078000); // 1/1/2024 00:00:00 GMT-03:00
	localTimeZone.tz_dsttime = Settings.getInt("time-dsttime", 0);
	localTimeZone.tz_minuteswest = Settings.getInt("time-minwest", -180);

	setTimeZone(&localTimeZone);

	uint32_t rtcEpoch = 0;
	if (rtcClockRead(rtcEpoch)) {
		setTimeTo(rtcEpoch);
		managerSetClockSynchronized("rtc");
		debugI("Hora inicial tomada del RTC: %lu", static_cast<unsigned long>(rtcEpoch));
	}
	else {
		setTimeTo(now);
		debugW("RTC sin hora valida; usando hora guardada en NVS: %lu", static_cast<unsigned long>(now));
	}

	//Verificar si se usa servidor ntp
	usingTimeServer = Settings.getBool("time-sync", false);
	Settings.end();

}

// Activa la sincronizacion de red despues de preparar la hora local; sin parametros.
void TimeStart()
{
	WiFi.onEvent([](arduino_event_t* event) { timeConnectionPending = true; }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
	TimeSetServer();

	printLocalTime();
}

// Lee NTP y zona horaria al iniciar o reconfigurar el servicio; devuelve false si falla NVS, sin parametros.
bool TimeSetServer()
{
	static char server[48];
	Preferences options;
	if (!options.begin(settingsName, true)) return false;
	usingTimeServer = options.getBool("time-sync", true);
	if (sntp_enabled()) sntp_stop();
	localTimeZone.tz_minuteswest = options.getInt("time-minwest", -180);
	localTimeZone.tz_dsttime = options.getInt("time-dsttime", 0);
	snprintf(server, sizeof(server), "%s", options.getString("time-server", "ar.pool.ntp.org").c_str());
	options.end();
	setTimeZone(&localTimeZone);

	debugI("Iniciando servidor de hora. Server: '%s'...", server);

	if (usingTimeServer == false) {
		debugW("La actualizacion de hora desde internet esta desactivada.");
		return true;
	}
	// Setear el servidor de tiempo
	configTime(0, 0, server);
	// configTime reemplaza TZ; restaurarla solo cambia localtime, no el reloj UTC ni los timestamps.
	setTimeZone(&localTimeZone);

	//Agregar un callback para cuando se sincronice la hora 
	sntp_set_time_sync_notification_cb([](struct timeval* tv) {
		debugI("Fecha y hora sincronizada con servidor!!");
		managerSetClockSynchronized("sntp");
		rtcClockSyncFromSystem();
		printLocalTime();
		});

	/*
			Una vez seteado el servidor sntp la hora se sincroniza automáticamente
			cada una hora.
			https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html
	*/
	return true;
}



bool UtilsLoadDeafultSettings()
{
	uint32_t time = micros();
	debugI("Cargando configuracion por defecto!!");

	if (Settings.begin("wifi-stuff", false) == false) {
		debugE("Fallo al cargar 'wifi-stuff' desde Preferences");
		return false;
	}
	Settings.clear();
	Settings.putBool("settings", true);

	//Wifi
	Settings.putInt("wifi-mode", static_cast<int32_t>(Wifi_Mode::Station));
	// Settings.putString("sta-ssid", "Wifi-Luis");
	// Settings.putString("sta-pass", "");
	Settings.putString("sta-ssid", "TP-LINK_883497");
	Settings.putString("sta-pass", "80255383");
	Settings.putBool("staticIp", true);
	Settings.putString("sta-ip", "192.168.1.90");
	Settings.putString("sta-mask", "255.255.254.0");
	Settings.putString("sta-gw", "192.168.0.1");
	Settings.putString("sta-dns1", "8.8.8.8");
	Settings.putString("sta-dns2", "4.4.4.4");

	//Wifi AP
	Settings.putString("ap-ssid", "ESP32-AP");
	Settings.putString("ap-pass", "");
	Settings.putBool("ap-hide", false);
	Settings.putBool("ap-custom-ip", false);
	Settings.putString("ap-ip", "192.168.1.1");
	Settings.putString("ap-mask", "255.255.255.0");
	Settings.putString("ap-gw", "192.168.1.1");

	//OTA
	Settings.putBool("ota-enabled", true);
	Settings.putString("ota-name", hostName);
	Settings.putString("ota-pass", "");

	//Time
	Settings.putUInt("time-start", 1704078000); // 1/1/2024 00:00:00 GMT-30:00
	Settings.putInt("time-dsttime", 0);
	Settings.putInt("time-minwest", -180);

	//NTP servers
	Settings.putString("time-server", "ar.pool.ntp.org");
	Settings.putBool("time-sync", true);

	//Remote debugger
	Settings.putBool("debug-enabled", true);
	Settings.putString("debug-name", hostName);
	Settings.putString("debug-pass", "");

	//MQTT
	Settings.putBool("mqtt-enabled", true);
	Settings.putString("mqtt-server", "192.168.1.30");
	Settings.putUInt("mqtt-port", 1883);
	Settings.putString("mqtt-user", "luis");
	Settings.putString("mqtt-pass", "electro");

	Settings.end();	//Cerrar por las dudas
	time = micros() - time;
	debugI("Configuracion cargada! time: %Uus", time);
	return true;
}


time_t getTime(time_t* _timer)
{
	// time_t now = time(_timer);
	// return now + (localTimeZone.tz_minuteswest * 60) + (localTimeZone.tz_dsttime * 60);
	struct timeval now;
	gettimeofday(&now, nullptr);
	return now.tv_sec;
}


struct tm getTime()
{
	struct timeval now;
	gettimeofday(&now, nullptr);
	return *localtime((time_t*)&now.tv_sec);
}


void setTimeTo(uint32_t Secs)
{
	struct timeval now = { (time_t)Secs, 0 };
	settimeofday(&now, nullptr);
}

String setTimeZone(timezone* TimeZone)
{
	return setTimeZone(TimeZone->tz_minuteswest * 60, TimeZone->tz_dsttime * 60);
}

String setTimeZone(int32_t offset, int32_t daylight)
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

void printLocalTime()
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

int32_t getWiFiRSSI()
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

// char* getWiFiSSID()
// {
// 	static wifi_ap_record_t wifidata;
// 	if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
// 		return (char*)wifidata.ssid;
// 	}
// 	return nullptr;
// }


String getWiFiSSID()
{
	static wifi_ap_record_t wifidata;
	if (esp_wifi_sta_get_ap_info(&wifidata) == 0) {
		return String((char*)wifidata.ssid);
	}
	return String("");
}


char getWiFiRSSICode(int32_t RSSI)
{
	char c = ' ';
	if (RSSI < -80)
		c = WiFiChar_RSSI_1;
	else if (RSSI < -70)
		c = WiFiChar_RSSI_2;
	else if (RSSI < -60)
		c = WiFiChar_RSSI_3;
	else // if (RSSI >= -60)
		c = WiFiChar_RSSI_4;
	return c;
}

char getWiFiRSSICode() { return getWiFiRSSICode(getWiFiRSSI()); }

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

const char* getWifiEventName(WiFiEvent_t event)
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
