#include "wifiUtils.h"
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "AsyncMqttClient.h"

Preferences Settings;
const char* settingsName = "wifi-stuff";

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

void UtilsLoop()
{
	static uint32_t timerWifi, timerMQTT;
	
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
	if (usingOTA) ArduinoOTA.handle();
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
	WiFi.onEvent([](arduino_event_t* event) {
		Settings.begin(settingsName, false);
		bool enabled = Settings.getBool("debug-enabled", true);
		if (enabled) {
			String name = Settings.getString("debug-name", hostName);
			String pass = Settings.getString("debug-pass", "");
			Debug.begin(name);

			if (pass.length() > 0) Debug.setPassword(pass);
			Debug.showProfiler(true); // Profiler (Good to measure times, to optimize codes)
			Debug.showColors(true); // Colors

			debugI("Debugger listo!");
			usingDebug = true;
		}
		else {
			debugW("Remote debugger deshabilitado!");
			usingDebug = false;
		}
		Settings.end();
		}, ARDUINO_EVENT_WIFI_STA_CONNECTED);
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
		strcpy(server, url.c_str());

		String usr = Settings.getString("mqtt-user");
		String pas = Settings.getString("mqtt-pass");
		strcpy(user, usr.c_str());
		strcpy(pass, pas.c_str());

		mqtt.setServer(server, port);
		if (usr.length() > 0) mqtt.setCredentials(user, pas.length() > 0 ? pass : nullptr);

		WiFi.onEvent([](arduino_event_t* event) {
			debugI("Conectando a servidor MQTT %s...", server);
			mqtt.connect();
			}, ARDUINO_EVENT_WIFI_STA_CONNECTED);

		mqtt.onConnect([](bool sessionPresent) {
			debugI("Conectado a servidor MQTT!");
			});

		mqtt.onDisconnect([](AsyncMqttClientDisconnectReason reason) {
			debugI("Desconectado de servidor MQTT!");
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
	WiFi.disconnect(true, true);

	Settings.begin(settingsName, false);

	debugI("Iniciando Wifi...\n");
	Wifi_Mode mode = static_cast<Wifi_Mode>(Settings.getInt("wifi-mode", static_cast<int32_t>(Wifi_Mode::Station)));
	WiFi.mode(static_cast<wifi_mode_t>(mode));
	if (mode == Wifi_Mode::Station)
		debugI("Wifi modo estacion");
	else if (mode == Wifi_Mode::SoftAP)
		debugI("Wifi modo soft AP");
	else if (mode == Wifi_Mode::Station_AP)
		debugI("Wifi modo soft AP y estacion");
	else {
		debugI("Wifi modo off");
		Settings.end();
		return;
	}

	WiFi.setSleep(false);
	WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
		if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
			debugI("Conectado a AP (SSID): %s", getWiFiSSID());
		}
		else if (event == (WiFiEvent_t)ARDUINO_EVENT_WIFI_STA_GOT_IP) {
			debugI("Se obtuvo direccion IP: %s", WiFi.localIP().toString().c_str());
		}
		else {
			debugI("Evento wifi! code: %d, descripcion: %s", event, getWifiEventName(event));
		}
		});

	IPAddress ip, mask, gateway, dns1, dns2;
	bool StaticIP = Settings.getBool("staticIp", false);
	ip.fromString(Settings.getString("sta-ip", "192.168.1.90"));
	mask.fromString(Settings.getString("sta-mask", "255.255.255.0"));
	gateway.fromString(Settings.getString("sta-gw", "192.168.1.1"));
	dns1.fromString(Settings.getString("sta-dns1", "8.8.8.8"));
	dns2.fromString(Settings.getString("sta-dns2", "4.4.4.4"));

	if (StaticIP) {
		debugI("Seteando direccion IP estatica... \n");
		debugI("Direccion IP:    %s", ip.toString());
		debugI("Mascara de red:  %s", mask.toString());
		debugI("Gateway:         %s", gateway.toString());
		debugI("DNS preferiado:  %s", dns1.toString());
		debugI("DNS alternativo: %s", dns2.toString());

		if (WiFi.config(ip, gateway, mask, dns1, dns2))
			debugI("IP iniciada correctamente!\n");
		else
			debugW("Error al setear IP!\n");
	}

	String ssid = Settings.getString("sta-ssid", "");
	String pass = Settings.getString("sta-pass", "");

	debugI("Intentando conectar a: %s...", ssid);
	if (WiFi.begin(ssid.c_str(), pass.c_str()))
		debugI("Ok!");
	else
		debugE("Error!");

	if (mode == Wifi_Mode::SoftAP || mode == Wifi_Mode::Station_AP) {
		// Eventos de modo softAP
		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			Serial.printf("Info: WiFi AP iniciado"); },
			ARDUINO_EVENT_WIFI_AP_START);

		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			Serial.printf("Info: WiFi AP terminado"); }, ARDUINO_EVENT_WIFI_AP_STOP);

		WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
			Serial.printf("Info: WiFi AP direccion IP aignada: ",
			Serial.println(WiFi.softAPIP())); }, ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED);

		IPAddress ip, mask, gateway;
		ip.fromString(Settings.getString("ap-ip", "192.168.1.1"));
		mask.fromString(Settings.getString("ap-mask", "255.255.255.0"));
		gateway.fromString(Settings.getString("ap-gw", "192.168.1.1"));
		String ssid = Settings.getString("ap-ssid", "ESP32-AP");
		String pass = Settings.getString("ap-pass", "");
		bool hide = Settings.getBool("ap-hide", false);
		bool customIp = Settings.getBool("ap-custom-ip", false);

		// Configuracion soft ap
		debugI("Configurando el punto de acceso (AP)...");
		if (WiFi.softAP(ssid.c_str(), pass.c_str(), 1, hide))
			debugI("Ok!");
		else
			debugE("Error!");

		if (customIp) {
			debugI("Usar direccion IP definida por usuario para AP");
			debugI("Direccion IP:    %s", ip.toString());
			debugI("Mascara de red:  %s", mask.toString());
			debugI("Gateway:         %s", gateway.toString());

			if (WiFi.softAPConfig(ip, gateway, mask))
				debugI("IP iniciada correctamente!");
			else
				debugI("Error al setear IP!");
		}
		WiFi.softAPsetHostname("PowerMeter9000");
		WiFi.enableAP(true);
	}
	debugI("Configuracion Wifi terminada correctamente\n");
	Settings.end();
}

void WifiReconnect()
{
	Settings.begin(settingsName, false);
	Wifi_Mode mode = static_cast<Wifi_Mode>(Settings.getInt("wifi-mode", static_cast<int32_t>(Wifi_Mode::Station)));

	if (mode == Wifi_Mode::Station || mode == Wifi_Mode::Station_AP) {
		String ssid = Settings.getString("sta-ssid", "");
		String pass = Settings.getString("sta-pass", "");
		debugI("Reconectando wifi...");
		WiFi.begin(ssid.c_str(), pass.c_str());
	}
	Settings.end();
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
			if (ArduinoOTA.getCommand() == U_FLASH)
				debugW("Iniciando actualizacion de firware via OTA...");
			else // U_SPIFFS
				debugW("Iniciando actualizacion de sistema de archivos via OTA...");

			});

		ArduinoOTA.onEnd(
			[]() {debugI("Actualizacion terminada");}
		);

		ArduinoOTA.onProgress([](uint32_t progress, uint32_t total) {
			static int32_t lastProg = -1, prog;
			prog = (float)progress / (float)total * 100;
			if (prog != lastProg) {
				lastProg = prog;
				debugI("Progreso OTA: %d%%", prog);
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


void TimeStart()
{
	debugI("Iniciando RTC...");

	Settings.begin(settingsName, false);
	time_t now = Settings.getUInt("time-start", 1704078000); // 1/1/2024 00:00:00 GMT-03:00
	localTimeZone.tz_dsttime = Settings.getInt("time-dsttime", 0);
	localTimeZone.tz_minuteswest = Settings.getInt("time-minwest", -180);

	setTimeZone(&localTimeZone);

	setTimeTo(now);

	//Verificar si se usa servidor ntp
	usingTimeServer = Settings.getBool("time-sync", false);
	Settings.end();

	if (usingTimeServer) {
		WiFi.onEvent([](arduino_event_t* event) {
			TimeSetServer();
			}, ARDUINO_EVENT_WIFI_STA_CONNECTED);
	}

	printLocalTime();
}

void TimeSetServer()
{
	static char server[48];
	Settings.begin(settingsName, false);

	usingTimeServer = Settings.getBool("time-sync", false);
	String url = Settings.getString("time-server", "ar.pool.ntp.org");
	strcpy(server, url.c_str());

	debugI("Iniciando servidor de hora. Server: '%s'...", server);

	Settings.end();

	if (usingTimeServer == false) {
		debugW("La actualizacion de hora desde internet esta desactivada.");
		return;
	}
	// Setear el servidor de tiempo
	configTime(localTimeZone.tz_minuteswest * 60, localTimeZone.tz_dsttime * 60, server);

	//Agregar un callback para cuando se sincronice la hora 
	sntp_set_time_sync_notification_cb([](struct timeval* tv) {
		debugI("Fecha y hora sincronizada con servidor!!");
		printLocalTime();
		});

	/*
			Una vez seteado el servidor sntp la hora se sincroniza automáticamente
			cada una hora.
			https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html
	*/
}



bool UtilsLoadDeafultSettings()
{
	debugI("Cargando configuracion por defecto!!");

	if (Settings.begin("wifi-stuff", false) == false) {
		debugE("Fallo al cargar 'wifi-stuff' desde Preferences");
		return false;
	}
	Settings.clear();
	Settings.putBool("settings", true);

	//Wifi
	Settings.putInt("wifi-mode", static_cast<int32_t>(Wifi_Mode::Station));
	Settings.putString("sta-ssid", "Wifi-Luis");
	Settings.putString("sta-pass", "");
	Settings.putBool("staticIp", true);
	Settings.putString("sta-ip", "192.168.1.90");
	Settings.putString("sta-mask", "255.255.255.0");
	Settings.putString("sta-gw", "192.168.1.1");
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
	debugI("Configuración cargada!");
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
