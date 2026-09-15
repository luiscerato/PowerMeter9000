#pragma once

#include <Arduino.h>

// Inicializa la cola temporal de eventos almacenada solamente en RAM.
void eventStoreInit();

// Genera un identificador unico durante el arranque actual.
String eventStoreNextEventId();

// Genera un evento sintetico completo sin leer el ADE; eventId y reason reciben el resultado.
bool eventStoreCreateRampTest(String& eventId, String& reason);

// Crea una captura privada de MeterTask, todavia invisible para HTTP.
// eventId: identificador comun. timestampMs: Unix ms o 0 si no hay reloj valido.
void eventStoreBeginEvent(const String& eventId, uint64_t timestampMs);

// Registra la metadata de la transicion que origino el evento.
// eventId: identificador comun. type: tipo serializado. phase: fase origen.
// value: valor principal. unit: unidad. timestampMs: Unix ms o 0.
// durationMs: duracion real. eventEnded: true solo cuando la condicion termino.
void eventStoreAddMeta(const String& eventId, const char* type, const char* phase, float value, const char* unit, uint64_t timestampMs, uint64_t durationMs, bool eventEnded);

// Copia Fast RMS desde el loop principal y entrega su propiedad a MeterTask sin esperar.
// eventId: identificador comun. rmsData: muestras binarias. rmsDataSize: bytes de muestras.
// triggerSample: indice estimado de inicio, o -1 si antecede a las muestras retenidas.
// Devuelve false si se debe reintentar antes de liberar el buffer original.
bool eventStoreFinishFastRms(const String& eventId, const uint8_t* rmsData, size_t rmsDataSize, int32_t triggerSample);

// Recibe Fast RMS y entrega el evento completo al loop; llamar solo desde MeterTask, sin parametros.
void eventStoreServiceCapture();

// Inicia la captura de onda comprimida reservando sus dos primeros bloques.
// eventId: identificador comun. timestampMs: Unix ms o 0.
// triggerOffset: inicio relativo al frame en curso; puede ser negativo si ocurrio en el anterior.
bool eventStoreStartWave(const String& eventId, uint64_t timestampMs, int64_t triggerOffset);

// Marca que la onda no puede capturarse para un evento.
// eventId: identificador comun. reason: causa breve para el Manager.
void eventStoreSetWaveUnavailable(const String& eventId, const char* reason);

// Marca el cierre del evento origen y solicita un bloque de onda posterior.
// eventId: identificador comun. type: tipo que finalizo. phase: fase que finalizo.
void eventStoreEndWave(const String& eventId, const char* type, const char* phase);

// Copia el frame de 96 ms a la captura privada activa, sin mutex, CRC ni MQTT; solo MeterTask.
// frame: datos de 8064 bytes. frameSize: cantidad de bytes. scopeActive: prioridad del osciloscopio.
void eventStoreProcessWaveFrame(const uint8_t* frame, size_t frameSize, bool scopeActive);

// Publica periodicamente las notificaciones finales de eventos aun disponibles.
void eventStoreLoop();

// Construye la lista de eventos finales disponibles.
// dest: String donde se escribe el JSON.
void eventStoreGetListJson(String& dest);

// Construye el JSON final de un evento listo para descargar.
// eventId: identificador solicitado. dest: String donde se escribe el JSON.
bool eventStoreGetEventJson(const String& eventId, String& dest);

// Devuelve el tamano Fast RMS de un evento listo.
// eventId: identificador solicitado. dataSize: bytes disponibles.
bool eventStoreGetFastRmsSize(const String& eventId, size_t& dataSize);

// Devuelve el tamano Wave de un evento listo.
// eventId: identificador solicitado. dataSize: bytes disponibles.
bool eventStoreGetWaveSize(const String& eventId, size_t& dataSize);

// Copia un fragmento Fast RMS al buffer que pertenece al servidor web.
// eventId: identificador solicitado. index: offset. dest: destino. maxLen: capacidad de destino.
size_t eventStoreCopyFastRmsChunk(const String& eventId, size_t index, uint8_t* dest, size_t maxLen);

// Copia un fragmento Wave al buffer que pertenece al servidor web.
// eventId: identificador solicitado. index: offset. dest: destino. maxLen: capacidad de destino.
size_t eventStoreCopyWaveChunk(const String& eventId, size_t index, uint8_t* dest, size_t maxLen);

// Libera todos los buffers asociados a un evento descargado.
// eventId: identificador solicitado.
bool eventStoreClearEvent(const String& eventId);

// Devuelve la cantidad de eventos finales retenidos.
uint32_t eventStoreEventCount();

// Devuelve la cantidad de eventos finales que incluyen captura Wave.
uint32_t eventStoreWaveCount();
