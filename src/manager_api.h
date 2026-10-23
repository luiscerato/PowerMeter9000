#pragma once

#include <Arduino.h>

static const char* managerMqttBaseTopic = "PowerMeter9000";

// Devuelve el uptime del firmware en milisegundos.
uint64_t managerUptimeMs();

// Devuelve Unix Epoch UTC en milisegundos, o 0 si el reloj no esta sincronizado.
uint64_t managerNowMs();

// Devuelve true si existe una hora UTC valida.
bool managerClockSynchronized();

// Marca el reloj como sincronizado.
// source: origen de la hora valida, por ejemplo "sntp" o "rtc".
void managerSetClockSynchronized(const char* source);

// Devuelve el ultimo instante de sincronizacion correcto en Unix ms.
uint64_t managerLastSyncTimestampMs();

// Devuelve el origen actual del reloj.
const char* managerClockSource();

// Devuelve un id estable para el dispositivo.
String managerGetDeviceId();

// Devuelve el topic base MQTT usado por el firmware.
const char* managerGetMqttBaseTopic();

// Devuelve la URL del broker MQTT sin usuario ni clave.
String managerGetMqttBrokerUrl();

// Publica y guarda una muestra de telemetria con metadata de Manager.
// relativeTopic: topic relativo a managerMqttBaseTopic.
// dataPayload: JSON original de la medicion, sin metadata.
// timestampMs: Unix ms de adquisicion, o 0 si no hay reloj valido.
void managerPublishTelemetry(const char* relativeTopic, const char* dataPayload, uint64_t timestampMs);

// Devuelve el proximo contador de publicacion MQTT.
uint32_t managerNextSequence();

// Arma la respuesta de discovery para Power Meter Manager.
// dest: String donde se escribe el JSON.
void managerGetDiscoveryJson(String& dest);

// Arma la respuesta de estado para Power Meter Manager.
// dest: String donde se escribe el JSON.
void managerGetStatusJson(String& dest);

// Arma la respuesta de telemetria actual para Power Meter Manager.
// dest: String donde se escribe el JSON.
void managerGetTelemetryCurrentJson(String& dest);

// Escribe un entero uint64_t decimal en el String.
// dest: String destino.
// value: valor a serializar.
void managerAppendUint64(String& dest, uint64_t value);
