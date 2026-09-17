#pragma once

#include "grid_state.h"

// Inicializa la cola estatica y restaura el ultimo estado conocido de red.
void gridEventsInit();

// Procesa estados confirmados y genera transiciones; sampleMs es el instante monotonico de lectura.
void gridEventsUpdate(const phaseState& r, const phaseState& s, const phaseState& t,
	gridCondition overall, float vr, float vs, float vt, uint32_t sampleMs);

// Intenta publicar el siguiente cambio pendiente sin bloquear el loop.
void gridEventNotificationLoop();

// Confirma una publicación pendiente; packetId es el identificador MQTT recibido en PUBACK.
void gridEventNotificationAck(uint16_t packetId);

// Marca la publicación en curso para reintentar después de una desconexión MQTT.
void gridEventNotificationDisconnected();
