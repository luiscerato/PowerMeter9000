#pragma once

#include <Arduino.h>
#include <esp_wifi.h>
#include <ArduinoOTA.h>
#include <RemoteDebug.h>
#include <AsyncMqttClient.h>
#include "esp_sntp.h"
#include <WiFiMulti.h>

extern RemoteDebug Debug;	//Acceso a las funciones de debugger remoto
extern AsyncMqttClient mqtt;

enum class Wifi_Mode : int32_t
{
	Off = WIFI_MODE_NULL,
	Station = WIFI_MODE_STA,
	SoftAP = WIFI_MODE_AP,
	Station_AP = WIFI_MODE_APSTA,
};

struct wifiStationConfig
{
	bool enabled;
	bool staticIp;
	String ssid;
	String password;
	String ip;
	String mask;
	String gateway;
	String dns1;
	String dns2;
};

// Marca consumidores pendientes: services bit 0 Wi-Fi y bit 1 NTP; cada solicitud se consume una vez.
void networkQueueApply(uint8_t services);
// Devuelve wait/apply/ok/fail segun el cambio pendiente; sin parametros.
const char* networkApplyStatus();

struct networkSettingsGuard {
	bool locked;
	// Reserva el acceso a NVS de red si needed es true; locked indica si se obtuvo sin esperar.
	explicit networkSettingsGuard(bool needed = true);
	// Libera solamente la reserva obtenida por esta instancia; sin parametros.
	~networkSettingsGuard();
	networkSettingsGuard(const networkSettingsGuard&) = delete;
	networkSettingsGuard& operator=(const networkSettingsGuard&) = delete;
private:
	bool needed;
};

/*
		Listado de los caracteres creados con la función Create_WiFi_Chars()
*/
enum WiFiChar : char
{
	WiFiChar_Off = 0x1,  // Símbolo que representa el Wifi apagado
	WiFiChar_Lock,       // Símbolo que representa un candado
	WiFiChar_RSSI_1 = 4, // Símbolo que representa la intensidad mínima de recepción de Wifi (RSSI < -80)
	WiFiChar_RSSI_2,     // Símbolo que representa la intensidad media de recepción de Wifi (RSSI < -70)
	WiFiChar_RSSI_3,     // Símbolo que representa la intensidad media alta de recepción de Wifi (RSSI < -60)
	WiFiChar_RSSI_4,     // Símbolo que representa la intensidad máxima de recepción de Wifi (RSSI  > -60)
};


void MQTTStart();

void UtilsInitSettings();


bool UtilsLoadDeafultSettings();

void UtilsLoop();

void DebugStart();


void WifiStart();

void WifiReconnect();

// Indica si el AP temporal de recuperacion esta activo; no recibe parametros.
bool wifiRecoveryApActive();

// Inicia o reutiliza el escaneo asíncrono. No recibe parámetros.
void wifiScanStart();

// Devuelve el estado del escaneo Wi-Fi. No recibe parámetros.
int32_t wifiScanStatus();

// Lee una red: index es su índice; ssid, auth y rssi reciben sus datos.
bool wifiScanGetNetwork(int32_t index, String& ssid, uint8_t& auth, int32_t& rssi);

// Guarda las credenciales y conecta respetando la politica IP actual.
// ssid es el nombre de red y password es su clave, que puede estar vacia.
bool wifiSaveStationCredentials(const String& ssid, const String& password);

// Lee de NVS al abrir la edicion de estacion; config recibe todos sus campos.
bool wifiLoadStationConfig(wifiStationConfig& config);

// Guarda y aplica la configuracion de estacion; config contiene modo, red e IP.
bool wifiSaveStationConfig(const wifiStationConfig& config);

void OTAStart();

void OTAEnd();

void TimeStart();

// Prepara fecha UTC desde RTC/NVS y zona local sin iniciar Wi-Fi ni SNTP; sin parametros.
void timeInitClock();

bool TimeSetServer();



int32_t getWiFiRSSI();
// char* getWiFiSSID();
String getWiFiSSID();
char getWiFiRSSICode();

char getWiFiRSSICode(int32_t RSSI);
char getWiFiRSSICode();

time_t getTime(time_t* _timer);

struct tm getTime();

void setTimeTo(uint32_t Secs);

String setTimeZone(timezone* TimeZone);

String setTimeZone(int32_t offset, int32_t daylight);

void printLocalTime();

const char* getWifiEventName(WiFiEvent_t event);
