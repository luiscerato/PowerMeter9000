#include "Arduino.h"
#include "ESPAsyncWebServer.h"
#include "Preferences.h"
#include "SPIFFS.h"
#include "webserver.h"
#include "wifiUtils.h"
#include "meter.h"
#include "manager_api.h"
#include "event_store.h"
#include "w25n02kv.h"
#include "nand_benchmark.h"
#include <math.h>
#include <atomic>


// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
AsyncWebSocket scopeWS("/ws");
AsyncWebSocket remoteScreen("/screen");
AsyncWebSocket acEventsWS("/acevents");
extern W25N02Kv eventFlash;
extern bool flashDetected;
static std::atomic<bool> nandTestRunning(false);


WSeventFunctionCallback meterEvents = nullptr, screenEvents = nullptr, acEvents = nullptr;		//Punteros a los callback de los eventos de los websockets
//

// Convierte texto decimal o hexadecimal a entero con signo.
// value: texto recibido por HTTP.
static int64_t parseSignedOption(const String& value)
{
	return strtoll(value.c_str(), nullptr, 0);
}

// Convierte texto decimal o hexadecimal a entero sin signo.
// value: texto recibido por HTTP.
static uint64_t parseUnsignedOption(const String& value)
{
	return strtoull(value.c_str(), nullptr, 0);
}

// Atiende lectura/guardado del formulario de red; request contiene los 11 campos completos al guardar.
static void handleGridSettings(AsyncWebServerRequest* request) {
	gridSettings config;
	meterGetGridSettings(config);
	if (request->method() == HTTP_POST) {
		const char* keys[] = {"lowR", "highR", "lowS", "highS", "lowT", "highT",
			"noVoltage", "hysteresis", "lossConfirmMs", "voltageConfirmMs", "recoveryConfirmMs"};
		double values[11];
		for (unsigned i = 0; i < 11; ++i) {
			if (!request->hasParam(keys[i], true)) {
				request->send(400, "text/plain", "Faltan ajustes de red"); return;
			}
			const String& text = request->getParam(keys[i], true)->value();
			char* end;
			values[i] = strtod(text.c_str(), &end);
			if (end == text.c_str() || *end || !isfinite(values[i]) || values[i] < 0 ||
				values[i] > 600000 || (i >= 8 && floor(values[i]) != values[i])) {
				request->send(400, "text/plain", "Valor de red invalido"); return;
			}
		}
		for (unsigned i = 0; i < 3; ++i) {
			config.phases[i].lowVoltage = values[i * 2];
			config.phases[i].highVoltage = values[i * 2 + 1];
		}
		config.noVoltage = values[6]; config.hysteresis = values[7];
		config.lossConfirmMs = values[8]; config.voltageConfirmMs = values[9];
		config.recoveryConfirmMs = values[10];
		if (!validGridSettings(config)) {
			request->send(400, "text/plain", "Limites o tiempos de red incompatibles"); return;
		}
		if (!meterSaveGridSettings(config)) {
			request->send(500, "text/plain", "No se pudieron guardar los ajustes"); return;
		}
	}
	char json[384];
	snprintf(json, sizeof(json), "{\"schemaVersion\":1,\"lowR\":%.2f,\"highR\":%.2f,"
		"\"lowS\":%.2f,\"highS\":%.2f,\"lowT\":%.2f,\"highT\":%.2f,"
		"\"noVoltage\":%.2f,\"hysteresis\":%.2f,\"lossConfirmMs\":%u,"
		"\"voltageConfirmMs\":%u,\"recoveryConfirmMs\":%u}",
		config.phases[0].lowVoltage, config.phases[0].highVoltage,
		config.phases[1].lowVoltage, config.phases[1].highVoltage,
		config.phases[2].lowVoltage, config.phases[2].highVoltage,
		config.noVoltage, config.hysteresis, config.lossConfirmMs,
		config.voltageConfirmMs, config.recoveryConfirmMs);
	auto* response = request->beginResponse(200, "application/json", json);
	response->addHeader("Cache-Control", "no-store");
	request->send(response);
}

// Obtiene el identificador requerido por las rutas de artefactos de eventos.
// request: solicitud HTTP. eventId: destino del identificador validado.
static bool getEventIdParam(AsyncWebServerRequest* request, String& eventId)
{
	if (!request->hasParam("eventId")) return false;
	eventId = request->getParam("eventId")->value();
	return eventId.length() > 0;
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
		AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/index.html", "text/html");
		response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		response->addHeader("Pragma", "no-cache");
		response->addHeader("Expires", "0");
		request->send(response);
		});

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
		MeterWriteEnergyRegisters();		//Forzar escritura de registros de energía antes de reiniciar
		delay(100);
		ESP.restart();
		});

	server.on("/resetEnergy", HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "ok");
		MeterWriteEnergyRegisters(true);		//Reiniciar contadores de energía
		});

	server.on("/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		String status;
		getStatus(status);
		request->send(200, "application/json", status);
		});

	server.on("/api/v1/manager/discovery", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetDiscoveryJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/api/v1/status", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetStatusJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/api/v1/debug/nand-test", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (!flashDetected) {
			request->send(503, "application/json", "{\"schemaVersion\":1,\"success\":false,\"reason\":\"nand_not_detected\"}");
			return;
		}
		bool expected = false;
		if (!nandTestRunning.compare_exchange_strong(expected, true)) {
			request->send(409, "application/json", "{\"schemaVersion\":1,\"success\":false,\"reason\":\"test_already_running\"}");
			return;
		}

		constexpr uint16_t testBlock = W25N02Kv::blockCount - 1;
		W25N02KvTestResult result;
		eventFlash.runBlockTest(testBlock, result);
		nandTestRunning = false;

		char json[640];
		snprintf(json, sizeof(json), "{\"schemaVersion\":1,\"success\":%s,\"block\":%u,"
			"\"badBlock\":%s,\"badBlockMarker\":\"%02X\",\"erase\":{\"ok\":%s,\"status\":\"%02X\",\"timeUs\":%u},"
			"\"program\":{\"ok\":%s,\"status\":\"%02X\",\"timeUs\":%u},"
			"\"read\":{\"ok\":%s,\"status\":\"%02X\",\"timeUs\":%u},"
			"\"dataMatch\":%s,\"expectedCrc32\":\"%08X\",\"readCrc32\":\"%08X\"}",
			result.success ? "true" : "false", result.block, result.badBlock ? "true" : "false", result.badBlockMarker,
			result.eraseOk ? "true" : "false", result.eraseStatus, result.eraseUs,
			result.programOk ? "true" : "false", result.programStatus, result.programUs,
			result.readOk ? "true" : "false", result.readStatus, result.readUs,
			result.dataMatch ? "true" : "false", result.expectedCrc32, result.readCrc32);
		auto* response = request->beginResponse(result.success ? 200 : 500, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/api/v1/debug/nand-benchmark", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest* request) {
		if (request->method() == HTTP_POST) {
			const String modeText = request->hasParam("mode") ? request->getParam("mode")->value() : "quick";
			const uint8_t speedMHz = request->hasParam("speedMHz") ? request->getParam("speedMHz")->value().toInt() : 20;
			if (modeText != "quick" && modeText != "full") {
				request->send(400, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"invalid_mode\"}"); return;
			}
			if (!nandBenchmarkStart(modeText == "full" ? NandBenchmarkMode::full : NandBenchmarkMode::quick, speedMHz)) {
				request->send(409, "application/json", "{\"schemaVersion\":1,\"started\":false,\"reason\":\"invalid_speed_or_busy\"}"); return;
			}
		}
		String json;
		nandBenchmarkGetJson(json);
		auto* response = request->beginResponse(request->method() == HTTP_POST ? 202 : 200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/api/v1/debug/nand-benchmark/cancel", HTTP_POST, [](AsyncWebServerRequest* request) {
		request->send(nandBenchmarkCancel() ? 202 : 409, "application/json",
			"{\"schemaVersion\":1,\"cancelRequested\":true}");
		});

	server.on("/api/v1/telemetry/current", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetTelemetryCurrentJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/api/v1/events/test", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (request->hasParam("pattern")) {
			if (request->getParam("pattern")->value() != "ramp") {
				request->send(400, "application/json", "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"unknown_pattern\"}");
				return;
			}
			String eventId, reason;
			bool created = eventStoreCreateRampTest(eventId, reason);
			String payload = created
				? "{\"schemaVersion\":1,\"ok\":true,\"eventId\":\"" + eventId + "\",\"testPattern\":\"triangle-int12-v1\"}"
				: "{\"schemaVersion\":1,\"ok\":false,\"reason\":\"" + reason + "\"}";
			AsyncWebServerResponse* response = request->beginResponse(created ? 201 : 503, "application/json", payload);
			response->addHeader("Cache-Control", "no-store");
			request->send(response);
			return;
		}
		MeterTriggerTestEvent();
		String response = "{\"ok\":true,\"message\":\"test event capture requested\"}";
		request->send(200, "application/json", response);
		});

	server.on("/api/v1/events/fast-rms", HTTP_GET, [](AsyncWebServerRequest* request) {
		String eventId;
		if (!getEventIdParam(request, eventId)) {
			request->send(400, "application/json", "{\"error\":\"eventId is required\"}");
			return;
		}
		size_t dataSize = 0;
		if (!eventStoreGetFastRmsSize(eventId, dataSize)) {
			request->send(404, "application/json", "{\"error\":\"fast RMS data not available\"}");
			return;
		}
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", dataSize, [eventId](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
			return eventStoreCopyFastRmsChunk(eventId, index, buffer, maxLen);
			});
		response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		request->send(response);
		});

	server.on("/api/v1/events/wave", HTTP_GET, [](AsyncWebServerRequest* request) {
		String eventId;
		if (!getEventIdParam(request, eventId)) {
			request->send(400, "application/json", "{\"error\":\"eventId is required\"}");
			return;
		}
		size_t dataSize = 0;
		if (!eventStoreGetWaveSize(eventId, dataSize)) {
			request->send(404, "application/json", "{\"error\":\"wave data not available\"}");
			return;
		}
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", dataSize, [eventId](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
			return eventStoreCopyWaveChunk(eventId, index, buffer, maxLen);
			});
		response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
		request->send(response);
		});

	server.on("/api/v1/events/wave-diagnostics", HTTP_GET, [](AsyncWebServerRequest* request) {
		String payload;
		meterGetWaveDiagnostics(payload);
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", payload);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
		});

	server.on("/api/v1/events/clear", HTTP_POST, [](AsyncWebServerRequest* request) {
		String eventId;
		if (!getEventIdParam(request, eventId)) {
			request->send(400, "application/json", "{\"error\":\"eventId is required\"}");
			return;
		}
		if (!eventStoreClearEvent(eventId)) {
			request->send(404, "application/json", "{\"error\":\"event not available\"}");
			return;
		}
		request->send(200, "application/json", "{\"ok\":true}");
		});

	server.on("/api/v1/events", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		eventStoreGetListJson(response);
		request->send(200, "application/json", response);
		});

	server.on("/api/v1/history", HTTP_GET, [](AsyncWebServerRequest* request) {
		String response;
		managerGetNotImplementedJson(response, "history");
		request->send(501, "application/json", response);
		});

	server.on("/api/v1/configuration", HTTP_GET | HTTP_PATCH, [](AsyncWebServerRequest* request) {
		String response;
		managerGetNotImplementedJson(response, "configuration");
		request->send(501, "application/json", response);
		});

	server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest* request) {
		String status;
		scanWifiGetJson(status);
		request->send(200, "application/json", status);
		});

	server.on("/meter", HTTP_GET, [](AsyncWebServerRequest* request) {
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
			managerSetClockSynchronized("rtc");
		}
		uint32_t time = getTime(nullptr);
		request->send(200, "text/plain", String(time));
		});

	server.on("/restorecal", HTTP_POST | HTTP_GET, [](AsyncWebServerRequest* request) {
		request->send(200, "text/plain", "restaurando calibracion... reiniciar para aplicar cambios!");
		ade.restoreCalibration();
		});

	server.on("/reloadopts", HTTP_POST | HTTP_GET, [](AsyncWebServerRequest* request) {
		MeterLoadOptions();
		request->send(200, "text/plain", "opciones recargadas");
		});
	server.on("/gridopts", HTTP_GET | HTTP_POST, handleGridSettings);


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
						debugI("Escribiendo %s:%s", key.c_str(), val.c_str());
						/*Todo: poner los valores para los tipos UINT*/
						if (type == PT_I8)
							opts.putChar(key.c_str(), parseSignedOption(val));
						else if (type == PT_U8)
							opts.putUChar(key.c_str(), parseUnsignedOption(val));
						else if (type == PT_I16)
							opts.putShort(key.c_str(), parseSignedOption(val));
						else if (type == PT_U16)
							opts.putUShort(key.c_str(), parseUnsignedOption(val));
						else if (type == PT_I32)
							opts.putInt(key.c_str(), parseSignedOption(val));
						else if (type == PT_U32)
							opts.putUInt(key.c_str(), parseUnsignedOption(val));
						else if (type == PT_I64)
							opts.putLong64(key.c_str(), parseSignedOption(val));
						else if (type == PT_U64)
							opts.putULong64(key.c_str(), parseUnsignedOption(val));
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
	server.serveStatic("/", SPIFFS, "/").setCacheControl("no-store, no-cache, must-revalidate, max-age=0");


	// Para habilitar CORS. Al simular desde un servidor local no se puede llamar a la API. Con esto se soluciona.
	if (!CORS) {
		CORS = true;	//Solo agregarlos una vez
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
		DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, PATCH");
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
	int32_t count = WIFI_SCAN_RUNNING;

	if (first) {
		count = WiFi.scanNetworks(true);
		first = false;
	}
	else {
		String ssid;
		int32_t rssi, channel;
		uint8_t encType, * bssid;
		char item[128];

		count = WiFi.scanComplete();
		if (count >= 0) {	//Una vez terminado el escaneo, crear un array de objetos con la info de la red
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
