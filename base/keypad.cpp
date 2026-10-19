#include "keypad.h"

#include "debug.h"

static constexpr const char DEBUG_MODULE[] = "keypad";

static constexpr uint8_t KeyMaxButtons = 5;

static AceButton btnEscape(nullptr, 0);
static AceButton btnDown(nullptr, 1);
static AceButton btnUp(nullptr, 2);
static AceButton btnRight(nullptr, 3);
static AceButton btnEnter(nullptr, 4);
static AceButton* const buttonsList[KeyMaxButtons] = { &btnEscape, &btnDown, &btnUp, &btnRight, &btnEnter };
static uint16_t buttonsVLevels[KeyMaxButtons + 1] = {};
static keypadLadderConfig keypadConfig = {};
static LadderButtonConfig* buttonConfig = nullptr;
static TimerHandle_t buttonsTimer = nullptr;
static bool keypadStarted = false;

volatile uint32_t buttonEvents[KeyMaxButtons] = {};
volatile uint32_t newKeyEvent = 0;

// Indica si los niveles analogicos estan en orden creciente.
static bool keypadLevelsAreValid(uint8_t levelCount)
{
	for (uint8_t i = 1; i < levelCount; ++i) {
		if (buttonsVLevels[i] <= buttonsVLevels[i - 1])
			return false;
	}
	return true;
}

// Convierte un evento de AceButton al tipo comun de UI.
// eventType es el codigo generado por la libreria AceButton.
static uiKeyEventType keypadEventTypeFromAce(uint32_t eventType)
{
	if (eventType == AceButton::kEventRepeatPressed)
		return uiKeyEventType::Repeat;
	return uiKeyEventType::Press;
}

// Indica si un evento de AceButton debe enviarse hacia la UI.
// eventType es el codigo generado por la libreria AceButton.
static bool keypadShouldPublishAceEvent(uint8_t eventType)
{
	return eventType == AceButton::kEventPressed
		|| eventType == AceButton::kEventRepeatPressed;
}

// Ejecuta el chequeo periodico del ladder analogico.
void buttonsTimerCallback(TimerHandle_t xTimer)
{
	(void)xTimer;
	if (buttonConfig != nullptr)
		buttonConfig->checkButtons();
}

// Configura un boton dentro de la estructura comun.
// config es la configuracion destino, index la posicion, key el codigo y levelMv el nivel analogico.
static void keypadSetButton(keypadLadderConfig& config, uint8_t index, Keys key, uint16_t levelMv)
{
	if (index >= KeyMaxButtons)
		return;
	config.keys[index] = key;
	config.levelsMv[index] = levelMv;
}

// Configura un teclado analogico de tres botones.
void keypadConfigureThreeKeys(uint8_t pin, Keys key1, uint16_t key1Mv, Keys key2, uint16_t key2Mv, Keys key3, uint16_t key3Mv, uint16_t releasedMv, uint16_t toleranceMv)
{
	keypadLadderConfig config;
	config.pin = pin;
	config.buttonCount = 3;
	keypadSetButton(config, 0, key1, key1Mv);
	keypadSetButton(config, 1, key2, key2Mv);
	keypadSetButton(config, 2, key3, key3Mv);
	config.releasedMv = releasedMv;
	config.toleranceMv = toleranceMv;
	keypadConfigure(config);
}

// Configura un teclado analogico de cuatro botones.
void keypadConfigureFourKeys(uint8_t pin, uint16_t escapeMv, uint16_t downMv, uint16_t upMv, uint16_t rightMv, uint16_t releasedMv, uint16_t toleranceMv)
{
	keypadConfigureFourKeys(pin, Keys::Escape, escapeMv, Keys::Down, downMv, Keys::Up, upMv, Keys::Right, rightMv, releasedMv, toleranceMv);
}

// Configura un teclado analogico de cuatro botones con codigos explicitos.
void keypadConfigureFourKeys(uint8_t pin, Keys key1, uint16_t key1Mv, Keys key2, uint16_t key2Mv, Keys key3, uint16_t key3Mv, Keys key4, uint16_t key4Mv, uint16_t releasedMv, uint16_t toleranceMv)
{
	keypadLadderConfig config;
	config.pin = pin;
	config.buttonCount = 4;
	keypadSetButton(config, 0, key1, key1Mv);
	keypadSetButton(config, 1, key2, key2Mv);
	keypadSetButton(config, 2, key3, key3Mv);
	keypadSetButton(config, 3, key4, key4Mv);
	config.releasedMv = releasedMv;
	config.toleranceMv = toleranceMv;
	keypadConfigure(config);
}

// Configura un teclado analogico de cinco botones.
void keypadConfigureFiveKeys(uint8_t pin, uint16_t escapeMv, uint16_t downMv, uint16_t upMv, uint16_t rightMv, uint16_t enterMv, uint16_t releasedMv, uint16_t toleranceMv)
{
	keypadConfigureFiveKeys(pin, Keys::Escape, escapeMv, Keys::Down, downMv, Keys::Up, upMv, Keys::Right, rightMv, Keys::Enter, enterMv, releasedMv, toleranceMv);
}

// Configura un teclado analogico de cinco botones con codigos explicitos.
void keypadConfigureFiveKeys(uint8_t pin, Keys key1, uint16_t key1Mv, Keys key2, uint16_t key2Mv, Keys key3, uint16_t key3Mv, Keys key4, uint16_t key4Mv, Keys key5, uint16_t key5Mv, uint16_t releasedMv, uint16_t toleranceMv)
{
	keypadLadderConfig config;
	config.pin = pin;
	config.buttonCount = 5;
	keypadSetButton(config, 0, key1, key1Mv);
	keypadSetButton(config, 1, key2, key2Mv);
	keypadSetButton(config, 2, key3, key3Mv);
	keypadSetButton(config, 3, key4, key4Mv);
	keypadSetButton(config, 4, key5, key5Mv);
	config.releasedMv = releasedMv;
	config.toleranceMv = toleranceMv;
	keypadConfigure(config);
}

// Configura el teclado analogico desde una estructura comun.
void keypadConfigure(const keypadLadderConfig& config)
{
	if (config.buttonCount < 3 || config.buttonCount > 5) {
		keypadDisablePhysical();
		return;
	}

	keypadConfig = config;
	for (uint8_t i = 0; i < config.buttonCount; ++i)
		buttonsVLevels[i] = config.levelsMv[i];
	buttonsVLevels[config.buttonCount] = config.releasedMv;

	if (!keypadLevelsAreValid(config.buttonCount + 1)) {
		debugE("Keypad invalido: los niveles deben estar en orden creciente");
		keypadDisablePhysical();
	}
}

// Desactiva la lectura fisica; quedan disponibles solo teclas inyectadas.
void keypadDisablePhysical()
{
	keypadConfig = {};
	if (buttonsTimer != nullptr) {
		xTimerStop(buttonsTimer, 0);
		xTimerDelete(buttonsTimer, 0);
		buttonsTimer = nullptr;
	}
	if (buttonConfig != nullptr) {
		delete buttonConfig;
		buttonConfig = nullptr;
	}
	keypadClearEvents();
	keypadStarted = false;
}

// Inicializa los botones fisicos configurados.
void keypadBegin()
{
	if (keypadStarted || keypadConfig.buttonCount == 0)
		return;

	pinMode(keypadConfig.pin, INPUT);
	analogReadResolution(12);
	adcAttachPin(keypadConfig.pin);
	analogSetPinAttenuation(keypadConfig.pin, ADC_11db);

	if (buttonConfig != nullptr)
		delete buttonConfig;
	buttonConfig = new LadderButtonConfig(keypadConfig.pin, keypadConfig.buttonCount + 1, buttonsVLevels, keypadConfig.buttonCount, buttonsList);
	if (buttonConfig == nullptr) {
		debugE("No se pudo crear LadderButtonConfig");
		return;
	}

	buttonConfig->setFeature(ButtonConfig::kFeatureRepeatPress);
	buttonConfig->setFeature(ButtonConfig::kFeatureSuppressAfterRepeatPress);
	buttonConfig->setDebounceDelay(60);

	buttonConfig->setEventHandler([](AceButton* button, uint8_t eventType, uint8_t buttonState) {
		(void)buttonState;
		if (millis() < 1000)
			return;
		if (!keypadShouldPublishAceEvent(eventType))
			return;

		const uint32_t vPin = button->getPin();
		if (vPin >= keypadConfig.buttonCount)
			return;
		const Keys key = keypadConfig.keys[vPin];
		if (eventType == AceButton::kEventRepeatPressed && key != Keys::Up && key != Keys::Down)
			return;

		buttonEvents[vPin] = eventType;
		newKeyEvent++;
		debugD("Evento %s, key=%d, vPin=%i, adc=%u mV", AceButton::eventName(eventType),
			static_cast<int32_t>(key), vPin, analogReadMilliVolts(keypadConfig.pin));
	});

	buttonsTimer = xTimerCreate(
		"buttonsTimer",
		4 / portTICK_PERIOD_MS,
		pdTRUE,
		nullptr,
		buttonsTimerCallback
	);

	if (buttonsTimer != nullptr)
		xTimerStart(buttonsTimer, 0);
	keypadStarted = true;
	debugI("Keypad analogico iniciado: pin=%u, botones=%u", keypadConfig.pin, keypadConfig.buttonCount);
}

// Devuelve un evento pendiente del teclado fisico, o false si no hay ninguno.
// event recibe el codigo de tecla y el tipo de evento.
bool keypadTakeEvent(keypadEvent& event)
{
	if (millis() < 500)
		keypadClearEvents();

	if (newKeyEvent == 0)
		return false;

	for (uint8_t i = 0; i < keypadConfig.buttonCount; ++i) {
		const uint32_t rawEvent = buttonEvents[i];
		if (rawEvent == AceButton::kButtonStateUnknown)
			continue;

		buttonEvents[i] = AceButton::kButtonStateUnknown;
		if (newKeyEvent > 0)
			newKeyEvent--;

		event.key = keypadConfig.keys[i];
		event.type = keypadEventTypeFromAce(rawEvent);
		return true;
	}

	newKeyEvent = 0;
	return false;
}

void keypadClearEvents()
{
	for (uint8_t i = 0; i < KeyMaxButtons; ++i)
		buttonEvents[i] = AceButton::kButtonStateUnknown;
	newKeyEvent = 0;
}
