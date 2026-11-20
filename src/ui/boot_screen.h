#pragma once

#include <stdint.h>
class ST7920_SPI;

// Muestra el bitmap de 128x60 y la barra vacia; screen es la pantalla ya inicializada.
void bootScreenShow(ST7920_SPI& screen);
// Agrega pixels a la barra y envia solo el tramo modificado; screen conserva el framebuffer existente.
void bootScreenAdvance(ST7920_SPI& screen, uint8_t pixels = 12);
// Completa la barra e inicia una permanencia visual no bloqueante de 500 ms; screen es la pantalla de arranque.
void bootScreenFinish(ST7920_SPI& screen);
// Indica si debe postergarse el dibujo de la UI, sin detener otras tareas; sin parametros.
bool bootScreenHold();
