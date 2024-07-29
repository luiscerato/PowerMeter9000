// 
// 
// 

#include "sound.h"

#define LOG_LOCAL_LEVEL ESP_LOGE
#include <esp_log.h>
static const char* TAG = "sound";

uint8_t Sound::PWM_ChannelUsed = 0;


Sound::Sound()
{
	Pin = Channel = -1;
	Tone = PlayTime = 0;
	LastTime = 0;
}

Sound::~Sound()
{
	if (Channel < 0)
		return;
	ledcDetachPin(Pin);
	pinMode(Pin, INPUT_PULLUP);
}

Sound::Sound(uint32_t Pin)
{
	Init(Pin, PWM_ChannelUsed++);
}

Sound::Sound(uint32_t Pin, uint8_t Channel)
{
	Init(Pin, Channel);
}

uint16_t Sound::getTimeOut()
{
	return PlayTime;
}

uint16_t Sound::getElapsed()
{
	if (!LastTime || !PlayTime)
		return 0;
	return millis() - LastTime;
}

void Sound::setTimeOut(uint16_t Time)
{
	PlayTime = Time;
	if (Time == 0)
		LastTime = 0;
	else
		LastTime = millis();
}

void Sound::Run()
{
	if (!LastTime || !PlayTime)
		return;

	if (millis() - LastTime >= PlayTime) {
		ESP_LOGD(TAG, "timeout %u", millis() - LastTime);
		PlayTone(0);
		LastTime = PlayTime = 0;
	}
}


bool Sound::Init(uint8_t Pin)
{
	return Init(Pin, PWM_ChannelUsed++);
}


bool Sound::Init(uint8_t Pin, int8_t Channel)
{
	Sound();
	ESP_LOGD(TAG, "Iniciando! Pin: %u, Channel %i", Pin, Channel);

	if (!GPIO_IS_VALID_OUTPUT_GPIO(Pin) || Channel < 0 || Channel > 15) {
		ESP_LOGE(TAG, "Pin o Channel invalido");
		return false;
	}

	this->Pin = Pin;
	this->Channel = Channel;

	digitalWrite(Pin, false);
	pinMode(Pin, OUTPUT);
	ledcAttachPin(Pin, Channel);
	PlayTime = LastTime = 0;
	ESP_LOGD(TAG, "Listo!");
	return true;
}

void Sound::PlayTone(uint16_t Freq)
{
	ESP_LOGD(TAG, "Tone: %u hz", Freq);
	ledcWriteTone(Channel, Freq);
	setTimeOut(0);
}

void Sound::PlayTone(uint16_t Freq, uint16_t Ms)
{
	ESP_LOGD(TAG, "Tone: %u hz, time: %u", Freq, Ms);
	ledcWriteTone(Channel, Freq);
	setTimeOut(Ms);
}

void Sound::PlayNote(note_t Note, uint8_t Octave)
{
	ESP_LOGD(TAG, "Note: %u, octave: %u", Note, Octave);
	ledcWriteNote(Channel, Note, Octave);
	setTimeOut(0);
}

void Sound::PlayNote(note_t Note, uint8_t Octave, uint16_t Ms)
{
	ESP_LOGD(TAG, "Note: %u, octave: %u, time: %u", Note, Octave, Ms);
	ledcWriteNote(Channel, Note, Octave);
	setTimeOut(Ms);
}
