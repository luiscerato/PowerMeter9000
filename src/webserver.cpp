#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "webserver.h"
#include "wifiUtils.h"
#include "meter.h"


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket scopeWS("/ws");
AsyncWebSocket remoteScreen("/screen");

WSeventFunctionCallback meterEvents = nullptr, screenEvents = nullptr;

void webServerSetMeterEvents(WSeventFunctionCallback func)
{
	if (func != nullptr)
		meterEvents = func;
}

AsyncWebSocket* webServerGetMeterWS()
{
	return &scopeWS;
}

void Init_WebServer()
{
	debugI("Iniciando servidor web...");
	scopeWS.onEvent(meterEvents);
	server.addHandler(&scopeWS);
	server.addHandler(&remoteScreen);

	// Web Server Root URL
	server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(SPIFFS, "/index.html", "text/html");
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
				debugI("Enviando %s Pos=%d", url.c_str(), index);

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

	server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		String status;
		getStatus(status);
		request->send(200, "application/json", status);
		});

	server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
		String status;
		scanWifiGetJson(status);
		request->send(200, "application/json", status);
		});

	server.on("/meter", HTTP_GET, [](AsyncWebServerRequest* request) {
		Serial.printf("params: %i\n", request->params());
		for (int x = 0; x < request->params(); x++) {
			Serial.printf(" p%i-> %i '%s'='%s'\n", x,
				request->getParam(x)->isPost(),
				request->getParam(x)->name().c_str(),
				request->getParam(x)->value().c_str());
		}
		String Value = "";
		if (request->params() > 0) {
			const String& param = request->getParam(0)->name();
			if (param == "energy")
				Meter.getJsonEnergy(Value);
			else if (param == "fast")
				Meter.getJsonFastMeasures(Value);
			else if (param == "phaseR")
				Value = Meter.phaseR.getJson();
			else if (param == "phaseS")
				Value = Meter.phaseS.getJson();
			else if (param == "phaseT")
				Value = Meter.phaseT.getJson();
			else if (param == "neutral")
				Value = Meter.neutral.getJson();
			else
				request->send(404, "text/plain", "Page Not Found");
		}
		else {
			Meter.getJsonBasic(Value);
		}
		request->send(200, "text/plain", Value);
		});


	server.on("/time", HTTP_POST | HTTP_GET, [](AsyncWebServerRequest* request) {
		// Serial.printf("params: %i\n", request->params());
		// for (int x = 0; x < request->params(); x++) {
		// 	Serial.printf(" p%i-> %i '%s'='%s'\n", x,
		// 		request->getParam(x)->isPost(),
		// 		request->getParam(x)->name().c_str(),
		// 		request->getParam(x)->value().c_str());
		// }
		if (request->hasParam("body", true)) {
			AsyncWebParameter* body = request->getParam("body", true);
			// Serial.printf(" '%s' = '%s'\n", body->name().c_str(), body->value().c_str());
			int32_t time = body->value().toInt();
			setTimeTo(time);
		}
		uint32_t time = getTime(nullptr);
		request->send(200, "text/plain", String(time));
		});

	server.serveStatic("/", SPIFFS, "/").setCacheControl("max-age=31536000");

	// Para habilitar CORS. Al simular desde un servidor local no se puede llamar a la API. Con esto se soluciona.
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
	DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

	server.onNotFound([](AsyncWebServerRequest* request) {
		request->send(404, "text/plain", "Page Not Found");

		Serial.printf("File not found\n");
		});

	//Start server
	server.begin();
}


void getStatus(String& str) {
	char data[128];
	snprintf(data, sizeof(data), "{\"ssid\":\"%s\", \"rssi\":%i,\"ip\":\"%s\", \"heap\":%i, \"free\":%i}",
		getWiFiSSID().c_str(), getWiFiRSSI(), WiFi.localIP().toString().c_str(), ESP.getHeapSize(), ESP.getFreeHeap());
	str = data;
}


int32_t scanWifiGetJson(String& List)
{
	static String list = "";
	static bool first = true;
	int32_t count;

	if (first) {
		count = WiFi.scanNetworks(true);
		first = false;
	}
	else {
		String ssid;
		int32_t rssi, channel, count;
		uint8_t encType, * bssid;
		char item[128];

		if (count = WiFi.scanComplete() >= 0) {	//Una vez terminado el escaneo, crear un array de objetos con la info de la red
			list = "";	//Limpiar lista
			for (int32_t i = 0; i < count; i++) {
				if (WiFi.getNetworkInfo(i, ssid, encType, rssi, bssid, channel)) {	//Obtener info de la red
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