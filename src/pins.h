#include <Arduino.h>

const int32_t pinLcdSS = 5;
const int32_t pinLcdBL = 15;
const int32_t pinSpiClk = 19;
const int32_t pinSpiSdo = 18;
const int32_t pinSpiSdi = 17;
const int32_t pinAdeClk = 25;
const int32_t pinAdeSdo = 33;
const int32_t pinAdeSdi = 32;
const int32_t pinAdeSS = 26;
const int32_t pinAdeInt0 = 34;
const int32_t pinAdeInt1 = 35;
const int32_t pinMemSS = 4;
const int32_t pinMem2SS = 16;
const int32_t pinRelay = 27;
const int32_t pinBuzzer = 14;
const int32_t pinPixelLed = 23;
const int32_t pinLedOk = 2;
const int32_t pinSDA = 21;
const int32_t pinSCL = 22;
const int32_t pinBtns = 36;
const int32_t pinBQInt = 39;

const int32_t pinBLTx = 12;
const int32_t pinBLRx = 13;

const int32_t pinBtnUp = 33;
const int32_t pinBtnDown = 32;
const int32_t pinBtnNext = 25;
const int32_t pinBtnEsc = 21;
const int32_t pinBtnOk = 26;

#define miliVolt_ButtonCancel 2740
#define miliVolt_ButtonDown 2200
#define miliVolt_ButtonUp 1655
#define miliVolt_ButtonNext 1110
#define miliVolt_ButtonOk 570

typedef enum {
	buttonNone = 0,
	buttonCancel = 1,
	buttonDown,
	buttonUp,
	buttonNext,
	buttonOk,
} boardButtons_t;
