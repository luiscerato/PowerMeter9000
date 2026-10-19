#pragma once

#include "grid_state.h"

// Inicializa la cola estatica y restaura el ultimo estado conocido de red.
void gridEventsInit();

// Procesa estados confirmados y genera transiciones; sampleMs es el instante monotonico de lectura.
void gridEventsUpdate(const phaseState& r, const phaseState& s, const phaseState& t,
	gridCondition overall, float vr, float vs, float vt, uint32_t sampleMs);

// Indica si ya termino la guarda de arranque y se pueden capturar cambios reales de red.
bool gridEventsReadyForStorage();

// Devuelve el estado persistido antes del arranque; slot identifica R, S, T o red.
uint8_t gridEventsStartupState(uint8_t slot);

// Guarda como maximo un estado pendiente en NVS por pasada; llamar solo desde el loop principal.
void gridEventsPersistLoop();

// Intenta publicar el siguiente cambio pendiente sin bloquear el loop.
void gridEventNotificationLoop();

// Confirma una publicación pendiente; packetId es el identificador MQTT recibido en PUBACK.
void gridEventNotificationAck(uint16_t packetId);

// Marca la publicación en curso para reintentar después de una desconexión MQTT.
void gridEventNotificationDisconnected();
