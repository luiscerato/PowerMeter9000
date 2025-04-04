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

void OTAStart();

void OTAEnd();

void TimeStart();

void TimeSetServer();



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