#pragma once

#include "lcd_ui.h"

// Dibuja el menu de estacion Wi-Fi, el escaneo de redes y el editor de IP.
// ui: interfaz LCD activa; action: ciclo de vida de la pantalla.
bool drawWifiSettings(lcd_ui* ui, UI_Action action);
