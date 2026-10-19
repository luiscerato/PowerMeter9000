#pragma once
#include <Arduino.h>
#include "ui/lcd_ui/ui_types.h"
#include "AceButton.h"

using namespace ace_button;

struct keypadLadderConfig {
	uint8_t pin = 0;
	uint8_t buttonCount = 0;
	Keys keys[5] = { Keys::Escape, Keys::Down, Keys::Up, Keys::Right, Keys::Enter };
	uint16_t levelsMv[5] = {};
	uint16_t releasedMv = 4095;
	uint16_t toleranceMv = 0;
};

struct keypadEvent {
	Keys key = Keys::None;
	uiKeyEventType type = uiKeyEventType::Press;
};

// API de teclado (botones)
// Configura un teclado analogico de tres botones.
void keypadConfigureThreeKeys(uint8_t pin, Keys key1, uint16_t key1Mv, Keys key2, uint16_t key2Mv, Keys key3, uint16_t key3Mv, uint16_t releasedMv = 4095, uint16_t toleranceMv = 0);
// Configura un teclado analogico de cuatro botones.
void keypadConfigureFourKeys(uint8_t pin, uint16_t escapeMv, uint16_t downMv, uint16_t upMv, uint16_t rightMv, uint16_t releasedMv = 4095, uint16_t toleranceMv = 0);
// Configura un teclado analogico de cuatro botones con codigos explicitos.
void keypadConfigureFourKeys(uint8_t pin, Keys key1, uint16_t key1Mv, Keys key2, uint16_t key2Mv, Keys key3, uint16_t key3Mv, Keys key4, uint16_t key4Mv, uint16_t releasedMv = 4095, uint16_t toleranceMv = 0);
// Configura un teclado analogico de cinco botones.
void keypadConfigureFiveKeys(uint8_t pin, uint16_t escapeMv, uint16_t downMv, uint16_t upMv, uint16_t rightMv, uint16_t enterMv, uint16_t releasedMv = 4095, uint16_t toleranceMv = 0);
// Configura un teclado analogico de cinco botones con codigos explicitos.
void keypadConfigureFiveKeys(uint8_t pin, Keys key1, uint16_t key1Mv, Keys key2, uint16_t key2Mv, Keys key3, uint16_t key3Mv, Keys key4, uint16_t key4Mv, Keys key5, uint16_t key5Mv, uint16_t releasedMv = 4095, uint16_t toleranceMv = 0);
// Configura el teclado analogico desde una estructura comun.
void keypadConfigure(const keypadLadderConfig& config);
// Desactiva la lectura fisica; quedan disponibles solo teclas inyectadas.
void keypadDisablePhysical();
void keypadBegin();
// Devuelve un evento pendiente del teclado fisico, o false si no hay ninguno.
bool keypadTakeEvent(keypadEvent& event);
// Limpia los eventos fisicos pendientes.
void keypadClearEvents();
