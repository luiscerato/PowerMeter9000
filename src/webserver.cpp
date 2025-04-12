#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "Preferences.h"
#include "SPIFFS.h"
#include "webserver.h"
#include "wifiUtils.h"
#include "meter.h"


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket scopeWS("/ws");
AsyncWebSocket remoteScreen("/screen");
AsyncWebSocket acEventsWS("/acevents");


WSeventFunctionCallback meterEvents = nullptr, screenEvents = nullptr, acEvents = nullptr;		//Punteros a los callback de los eventos de los websockets
//
void webServerSetMeterEvents(WSeventFunctionCallback func)
{
	if (func != nullptr)
		meterEvents = func;
}

AsyncWebSocket* webServerGetMeterWS()
{
	return &scopeWS;
}

void webServerSetAcEvents(WSeventFunctionCallback func)
{
	if (func != nullptr)
		acEvents = func;
}

AsyncWebSocket* webServerGetAcWS()
{
	return &acEventsWS;
}



void Init_WebServer()
{
	static bool CORS = false;

	debugI("Iniciando servidor web...");
	server.reset();			//Crear el servidor web desde 0

	scopeWS.onEvent(meterEvents);
	server.addHandler(&scopeWS);
	acEventsWS.onEvent(acEvents);
	server.addHandler(&acEventsWS);

	// server.addHandler(&remoteScreen);

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

	server.on("/reset", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "ok");
		ESP.restart();
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
			else if (param == "angles")
				Meter.getJsonAngles(Value);
			else if (param == "thd")
				Meter.getJsonTHD(Value);
			else
			{
				request->send(404, "text/plain", "Page Not Found");
				return;
			}
		}
		else {
			Meter.getJsonBasic(Value);
		}
		request->send(200, "application/json", Value);
		});


	server.on("/time", HTTP_POST | HTTP_GET, [](AsyncWebServerRequest* request) {
		if (request->hasParam("body", true)) {
			const AsyncWebParameter* body = request->getParam("body", true);
			// Serial.printf(" '%s' = '%s'\n", body->name().c_str(), body->value().c_str());
			int32_t time = body->value().toInt();
			setTimeTo(time);
		}
		uint32_t time = getTime(nullptr);
		request->send(200, "text/plain", String(time));
		});


	server.on("/opts", HTTP_GET, [](AsyncWebServerRequest* request) {
		//Param[0] -> Namespace
		//Param[1] -> Key:value
		uint32_t time = micros();
		String res = "{";
		if (request->params() > 1) {
			Preferences opts;
			const String& key = request->getParam(0)->name();
			const String& val = request->getParam(0)->value();

			if (opts.begin(val.c_str())) {
				for (int32_t i = 1; i < request->params(); i++) {
					String value;
					const String& key = request->getParam(i)->name();
					const String& val = request->getParam(i)->value();
					PreferenceType type = opts.getType(key.c_str());

					if (request->getParam(i)->value().length() > 0) { //Si la longitud de valor es >0, entonces se lo debe escribir
						debugI("Escribiendo %s:%s", key.c_str(), value.c_str());
						/*Todo: poner los valores para los tipos UINT*/
						if (type == PT_I8)
							opts.putChar(key.c_str(), atoi(val.c_str()));
						else if (type == PT_U8)
							opts.putUChar(key.c_str(), atoi(val.c_str()));
						else if (type == PT_I16)
							opts.putShort(key.c_str(), atoi(val.c_str()));
						else if (type == PT_U16)
							opts.putUShort(key.c_str(), atoi(val.c_str()));
						else if (type == PT_I32)
							opts.putInt(key.c_str(), atoll(val.c_str()));
						else if (type == PT_U32)
							opts.putUInt(key.c_str(), atoll(val.c_str()));
						else if (type == PT_I64)
							opts.putLong64(key.c_str(), atoll(val.c_str()));
						else if (type == PT_U64)
							opts.putULong64(key.c_str(), atoll(val.c_str()));
						else if (type == PT_BLOB) {
							int32_t size = opts.getBytesLength(key.c_str());
							if (size == sizeof(float))
								opts.putFloat(key.c_str(), atof(val.c_str()));
							else if (size == sizeof(double))
								opts.putDouble(key.c_str(), atoff(val.c_str()));
							else
								debugE("El typo de la clave '%s' no se puede identificar");
						}
						else if (type == PT_STR)
							opts.putString(key.c_str(), val.c_str());
						else {//La clave no existe
							debugW("La clave '%s' no existe o no se puede identificar, se la crea como tipo string");
							opts.putString(key.c_str(), val.c_str());
						}
					}

					//Leer el valor de la clave (si es que se escribió en el paso anterior se la envía con el valor escrito)
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
							value = opts.getUInt(key.c_str());
						else if (type == PT_U32)
							value = opts.getLong(key.c_str());
						else if (type == PT_I64)
							value = (float)opts.getLong64(key.c_str());
						else if (type == PT_U64)
							value = (float)opts.getULong64(key.c_str());
						else if (type == PT_BLOB) {
							int32_t size = opts.getBytesLength(key.c_str());
							if (size == sizeof(float))
								value = opts.getFloat(key.c_str());
							else if (size == sizeof(double))
								value = opts.getDouble(key.c_str());
							else
								value = "\"invalid type\"";
						}
						else if (type == PT_STR)
							value = "\"" + opts.getString(key.c_str()) + "\"";	//Prepapararlo para Json
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


	// server.serveStatic("/", SPIFFS, "/").setCacheControl("max-age=31536000");	//Los archivos se pueden almacenas en el servidor por un año
	server.serveStatic("/", SPIFFS, "/"); //.setCacheControl("max-age=31536000");	//En modo develop hay que actualiza a cada rato


	// Para habilitar CORS. Al simular desde un servidor local no se puede llamar a la API. Con esto se soluciona.
	if (!CORS) {
		CORS = true;	//Solo agregarlos una vez
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
	}

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