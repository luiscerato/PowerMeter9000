#include <Arduino.h>
#if defined(ESP32)
#include "esp32-hal.h"
#include "esp_system.h"
#endif  // ESP32

#include <queue>

#ifndef _KEYBOARD_h
#define _KEYBOARD_h

#ifndef Keyboard_MaxButtons		
#define Keyboard_MaxButtons	8	//Cantidad máxima de botones que tiene el teclado
#endif

#ifndef Keyboard_MaxCodes		
#define Keyboard_MaxCodes	16	//Cantidad máxima de códigos que se pueden almacenar
#endif


enum class ButtonMode :int32_t{
	Disabled = 0,				//Tecla deshabilitada
	OneShot,					//Devuelve un pulso luego de Time1, espera a que se suelte
	OneShotHold,				//Devuelve un pulso luego de soltar la tecla, si el tiempo es mayor a Time1
	OneShot2Code,				//Igual al anterior, solo que si el tiempo es mayor a Time2 devuelve otro pulso (misma tecla, dos funciones)
	MultiShot,					//Igual a KeyMode_OneShot, solo que despu�s de Time2 repite cada vez que pasa Time3
	MultiShotSpeedUp,			//Igual a KeyMode_MultiShot, solo que Time3 es cada vez menor
	ShotWhilePressed			//Devulve el valor de entrada
};

enum class Keys :int32_t {
	None = 0,
	Up,
	Down,
	Left,
	Right,
	Esc,
	Enter,
	Ok,
	Next,
	Key_0 = '0', Key_1, Key_2, Key_3, Key_4, Key_5, Key_6, Key_7, Key_8, Key_9,
};


#if defined(ESP8266)
#define CheckPinValid(x) ((x > 16) || (x < 0))
#elif defined(ESP32)
#define CheckPinValid(x) digitalPinIsValid(x)
#endif  // ESP32

class KeyTiming
{
public:
	uint32_t Debounce;		//Tiempo mínimo de tecla presionada para validar tecla en ms
	uint32_t Wait;			//Tiempo de espera para repetir código en ms
	uint32_t Repeat;		//Tiempo de repetición en ms
	uint32_t LongWait;		//Tiempo de espera para segundo código (en ms)
	uint32_t FilterHigh;	//Tiempo mínimo en 1 (en ms) para ser considerado 1
	uint32_t FilterLow;		//Tiempo mínimo en 0 (en ms) para ser considerado 0

	KeyTiming() {
		Debounce = 30;
		Wait = 750;
		LongWait = 1000;
		Repeat = 250;
		FilterHigh = 5;
		FilterLow = 5;
	};
};

class button;

class keyboard
{
private:
	unsigned long LastTime;		//Variable de tiempo
	unsigned long ScanTime = 5;	//Tiempo de escaneo de las teclas
	KeyTiming Timing;			//Timing del teclado

	button *buttons[Keyboard_MaxButtons];	//Botones del teclado
	Keys KeyCodes[Keyboard_MaxCodes];		//Códigos capturados 
	void(*onKeyEvent)();					//

public:

	keyboard();

	void scan();

	bool PushKey(Keys Code);

	Keys PopKey();

	bool RemoveKey(Keys Code);

	bool AddButton(button *data);

	bool RemoveButton(button *data);

	bool RemoveButton(uint32_t Index);

	bool isKeyDown(Keys Code);

	bool isKeyDown(int32_t Index);

	bool isKeyUp(Keys Code);

	bool isKeyUp(int32_t Index);

	bool isKeyPressed(int32_t code);

	bool isKeyPressed(Keys code);

	Keys getNextKey();

	bool isSomeKeyPressed();

	void ClearKeys();

	// bool operator[](Keys Code) {
	// };

	friend class button;
};


/*
	Class Button

	Es una clase prototipo para luego definir los tipos de entradas que se quieran.
	Contiene métodos y propiedades básicas para definir el funcionamiento de un pulsador.

*/
class button {

public:
	bool State;					//Estado del botón
	Keys Code = Keys::None;		//Código devuelto cuando la tecla es presionada
	bool isLocked;				//La tecla está bloqueada, se ignora el valor de la tecla, siempre devuelve 0

	button() {
		State = false;
		isLocked = false;
	};

	virtual void setCode(Keys BtnCode) {
		Code = BtnCode;
	};

	virtual Keys getCode() {
		return Code;
	};

	virtual bool isPressed() {
		return State;
	};

	virtual void setLock(bool Lock) {
		isLocked = Lock;
	};

	virtual void Lock() {
		isLocked = true;
	};

	virtual void Unlock() {
		isLocked = false;
	};

	virtual void setState(bool State) {
		this->State = State;
	};

	virtual bool getState() {
		return State;
	};

	virtual Keys Update() {
		if (State && !isLocked)
			return Code;
		else
			return Keys::None;
	};

	friend class keyboard;
};

/*
	class TactSwitch

	Por medio de esta clase se puede leer el estado de un swicth y devolver el código correspondiente.
	El estado que se lee puede ser un pin o directamente un obtenerlo desde una llamada a función.
*/

class TactSwitch: public button {

private:
	ButtonMode Mode = ButtonMode::MultiShot;	//Modo de trabajo de la tecla
	bool isInverted = true;			//La tecla trabaja de manera invertida. (Activo con valor)
	int32_t Pin = -1;					//Pin en el que está conectada la tecla
	bool(*getPinState)() = nullptr;		//Puntero a función que devuelve el estado del pulsador

	Keys Code2 = Keys::None; 			//Código alternativo devuelto cuando la tecla es presionada
	KeyTiming *Timing;					//Puntero a estructura de tiempos

	uint32_t LastEvent = 0;				//Tiempo desde que ocurrió el último cambio de estado del pulsador
	bool LastState = false;				//Último estado del pulsador

	uint32_t TimeOn = 0;				//Tiempo de tecla presionada 
	bool Wait = false;					//Indica que se debe esperar antes de devolver otro código

	void Init();

public:

	TactSwitch();

	void begin(ButtonMode Mode, Keys Code, int32_t Pin, bool Inverted) {
		setMode(Mode);
		this->Code = Code;
		setPin(Pin, Inverted);
	};

	void begin(ButtonMode Mode, Keys Code, int32_t Pin) {
		setMode(Mode);
		this->Code = Code;
		setPin(Pin);
	};

	void begin(ButtonMode Mode, Keys Code, bool(*getState)()) {
		setMode(Mode);
		this->Code = Code;
		setFunction(getState);
	};

	void begin(Keys Code, Keys Code2, int32_t Pin, bool Inverted = false) {
		setMode(ButtonMode::OneShot2Code);
		this->Code = Code;
		this->Code2 = Code2;
		setPin(Pin, Inverted);
	};

	void begin(Keys Code, Keys Code2, bool(*getState)()) {
		setMode(ButtonMode::OneShot2Code);
		this->Code = Code;
		this->Code2 = Code2;
		setFunction(getState);
	};

	void begin(Keys Code, int32_t Pin, bool Inverted = false) {
		begin(ButtonMode::MultiShot, Code, Pin, Inverted);
	};

	void begin(Keys Code, bool(*getState)()) {
		begin(ButtonMode::MultiShot, Code, getState);
	};

	void setPin(int32_t Pin);

	void setPin(int32_t Pin, bool Inverted);

	void setFunction(bool(*getState)());

	void setMode(ButtonMode Mode);

	Keys Update();

	friend class button;
};


#endif