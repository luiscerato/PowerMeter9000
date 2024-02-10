#include "keyboard.h"

static volatile KeyTiming Timings;
/*
	Clase button: clase que se encarga de la lógica de detección de los pulsos de cada botón. Esta clase 
	es la encargada de devolver un código cuando se detecte una pulsación.
	
	Un botón tiene algunas propiedades y métodos:
		El pin donde está conectado y su polaridad
		Su modo de funcionamiento.
		Un código que devuelve cuando es presionado, puede tener dos.
	Un botón distingue dos estados:
		Down: cuando el botón está presionado.
		Up: cuando el botón está suelto.
	El evento Pressed se genera cuando se cumple cierta condición, que puede ser distinta al estado actual.
	Cuando un botón está bloqueado se ignora el estado del mismo y no devuelve ningún evento.

	Clase Keyboard: permite instalar un teclado con algunos botones y simplificar su funcionamiento.
	Los botones son agregados u eliminados de la clase y son escaneados automáticamente, cada vez que 
	se detecta una 	pulsación, los códigos asociados son cargados en una lista de eventos, para luego 
	ser leídos por la aplicación del usuario.
	También se permite la generación de eventos, como ser cuando se pulsa o suelta un botón, o cuando un botón
	genera un nuevo evento.

	Esta clase tiene una instancia de tiempos que se aplicarán a cada botón una vez agregados.

*/

/*
	Inicializa la clase
*/
keyboard::keyboard()
{
	for (int32_t i = 0; i < Keyboard_MaxCodes; i++) {
		buttons[i] = nullptr;
	}
	ClearKeys();
}

/*
	Escanea todos los botones en busca de cambios
*/
void keyboard::scan()
{
	if (millis() - LastTime < ScanTime)	//Limitar escaneo a 5ms
		return;
	LastTime = millis();

	unsigned long time = micros();
	for (int32_t i = 0; i < Keyboard_MaxButtons; i++) {
		Keys Code;
		if (buttons[i]) {
			Code = buttons[i]->Update();
			if (Code != Keys::None) {
				PushKey(Code);
			}
		}
	}
	//Serial.printf("Time: %u us\n", micros() - time);
}

/*	
	Agrega un botón a la lista de botones
*/
bool keyboard::AddButton(button * data)
{
	if (data == nullptr)
		return false;
	for (int32_t i = 0; i < Keyboard_MaxButtons; i++) {
		if (buttons[i] == nullptr) {
			buttons[i] = data;
			return true;
		}
	}
	return false;
}

/*
	Elimina un botón de la lista de botones
*/
bool keyboard::RemoveButton(button * data)
{
	for (int32_t i = 0; i < Keyboard_MaxButtons; i++) {
		if (buttons[i] == data)
			buttons[i] = nullptr;
	}
	return false;
}


/*
	Elimina un botón de la lista de botones
*/
bool keyboard::RemoveButton(uint32_t Index)
{
	if (Index >= Keyboard_MaxButtons)
		return false;

	buttons[Index] = nullptr;
	return true;
}


/*
	Devuelve una tecla en la lista de eventos
*/
Keys keyboard::getNextKey()
{
	return PopKey();
}


/*
	Determina si la tecla está apretada
*/
bool keyboard::isKeyDown(Keys Code)
{
	for (int32_t i = 0; i < Keyboard_MaxButtons; i++) {
		if (buttons[i]) {
			if (buttons[i]->Code == Code) {
				return buttons[i]->isPressed();
			}
		}
	}
	return false;
}


/*
	Determina si la tecla está apretada 
*/
bool keyboard::isKeyDown(int32_t Index)
{
	if (Index >= Keyboard_MaxButtons)
		return false;
	if (buttons[Index]) {
		return buttons[Index]->isPressed();
	}
	return false;
}


/*
	Determina si la tecla está suelta (no presionada)
*/
bool keyboard::isKeyUp(Keys Code)
{
	return !isKeyDown(Code);
}


/*
	Determina si la tecla está suelta (no presionada)
*/
bool keyboard::isKeyUp(int32_t Index)
{
	return !isKeyDown(Index);
}


/*
	Determina si la tecla indicada generó un evento y lo elimina de la lista
*/
bool keyboard::isKeyPressed(int32_t code)
{
	return isKeyPressed(static_cast<Keys>(code));
}

/*
	Determina si la tecla indicada generó un evento y lo elimina de la lista
*/
bool keyboard::isKeyPressed(Keys code)
{
	if (code == Keys::None)
		return false;

	for (int32_t i = 0; i < Keyboard_MaxCodes; i++) {
		if (code == KeyCodes[i]) {
			RemoveKey(code);
			return true;
		}
	}
	return false;
}

/*
	Indica si hay una tecla en la lista de eventos
*/
bool keyboard::isSomeKeyPressed()
{
	for (int32_t i = 0; i < Keyboard_MaxCodes; i++) {
		if (KeyCodes[i] != Keys::None)
			return true;
	}
	return false;
}


/*
	Borra todas las teclas de la lista de eventos
*/
void keyboard::ClearKeys()
{
	for (int32_t i = 0; i < Keyboard_MaxCodes; i++) {
		KeyCodes[i] = Keys::None;
	}
}

/*
	Métodos privados
*/

/*
	Agregar una tecla en la lista de eventos
*/
bool keyboard::PushKey(Keys Code)
{
	int32_t Index;
	if (Code == Keys::None)
		return false;

	for (Index = 0; Index < Keyboard_MaxCodes; Index++) { //Buscar el primer lugar libre
		if (KeyCodes[Index] == Keys::None) {
			KeyCodes[Index] = Code;
			return true;
		}
	}
	return false;	//No hay espacio
}

/*
	Devolver y eliminar el primer elemento de la lista
*/
Keys keyboard::PopKey()
{
	int32_t Index;
	Keys res = KeyCodes[0];

	for (Index = 1; Index < Keyboard_MaxCodes; Index++) { //Buscar el primer lugar libre
		KeyCodes[Index - 1] = KeyCodes[Index];
		if (KeyCodes[Index] == Keys::None)
			break;
	}
	KeyCodes[Index-1] = Keys::None;
	return res;
}

/*
	Borrar la primer ocurrencia en la lista (si se encuentra)
*/
bool keyboard::RemoveKey(Keys Code)
{
	int32_t Index;
	for (Index = 0; Index < Keyboard_MaxCodes; Index++) { //Buscar el codigo indicado
		if (KeyCodes[Index] == Keys::None) //Terminó la lista
			return false;	//Terminó la lista
		else if (KeyCodes[Index] == Code)
			break;
	}
	if (Index == Keyboard_MaxCodes)
		return false; //No se encontró la tecla

	for (; Index < Keyboard_MaxCodes - 1;) {
		KeyCodes[Index] = KeyCodes[++Index];
		if (KeyCodes[Index] == Keys::None)
			break;	//final de lista
	}
	KeyCodes[Index] = Keys::None; //Terminar la lista
	return true;
}



/***********************************
	Class TactSwitch
*/


TactSwitch::TactSwitch()
{
	Init();
}

void TactSwitch::Init()
{
	Mode = ButtonMode::MultiShot;
	TimeOn = 0;
	isInverted = false;
	isLocked = false;
	LastState = false;
	Pin = -1;
	Code = Keys::None;
	Timing = (KeyTiming*)&Timings;
}


void TactSwitch::setFunction(bool(*getState)()) {
	getPinState = getState;
	isInverted = false;
}


void TactSwitch::setPin(int32_t Pin) {
	setPin(Pin, false);
}


void TactSwitch::setPin(int32_t Pin, bool Inverted) {
	if (!CheckPinValid(Pin)) {
		this->Pin = -1;
		return;
	}
	this->Pin = Pin;
	isInverted = Inverted;

#if defined(ESP32)
	if (!isInverted)
		pinMode(Pin, INPUT_PULLUP);
	else
		pinMode(Pin, INPUT_PULLDOWN);
#else
	pinMode(Pin, INPUT_PULLUP);
#endif
}


void TactSwitch::setMode(ButtonMode Mode) {
	this->Mode = Mode;
	if (this->Mode > ButtonMode::ShotWhilePressed)
		this->Mode = ButtonMode::OneShot;
}


Keys TactSwitch::Update()
{
	Keys res = Keys::None;
	uint32_t TimePressed, mils;
	bool PinState = false;

	if (isLocked) {
		TimeOn = 0;
		LastState = 0;
		return Keys::None;
	}

	if (getPinState)
		PinState = getPinState();
	else if (Pin > -1)
		PinState = digitalRead(Pin);
	else
		return res;

	if (!isInverted) PinState = !PinState;

	/*
		Filtrar los pulsos
	*/
	mils = millis();
	if (LastState != PinState) {		//Rising edge
		LastState = PinState;
		LastEvent = mils;
	}
	else {
		if (LastState) {
			if (mils - LastEvent > Timing->FilterHigh) {
				if (!State)
					TimeOn = LastEvent;
				State = true;
			}
		}
		else {
			if (State == false && TimeOn)
				TimeOn = 0;
			else if (mils - LastEvent > Timing->FilterLow)
				State = false;
		}
	}
	TimePressed = mils - TimeOn;
	if (TimeOn == 0)
		TimePressed = 0;

	switch (Mode) {
	case ButtonMode::Disabled:
		res = Keys::None;
		break;

	case ButtonMode::OneShot:
		if (State) {
			if (TimePressed >= Timing->Debounce && !Wait) {
				res = Code;
				Wait = true;
			}
		}
		else
			Wait = false;
		break;

	case ButtonMode::OneShot2Code:
		if (State) {
			if (TimePressed > Timing->LongWait && !Wait) {
				res = Code2;
				Wait = true;
			}
		}
		else {
			if ((TimePressed > Timing->Debounce) && (TimePressed < Timing->LongWait)) {
				res = Code;
			}
			Wait = false;
		}
		break;

	case ButtonMode::MultiShot:
		if (State) {
			if (TimePressed >= Timing->Debounce && !Wait) {
				res = Code;
				Wait = true;
			}
			else if (TimePressed > Timing->Debounce + Timing->Wait) {
				Wait = false;
				res = Code;
				TimeOn += Timing->Repeat;
				//Serial.printf("ButtonMode::MultiShot -> %lu ms", millis());
			}
		}
		else
			Wait = false;
		break;
	}

	//if (res)
	//	Serial.printf("Pin %u: %u\n", Pin, res);
	return res;
}
