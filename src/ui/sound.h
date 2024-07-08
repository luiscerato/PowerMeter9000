// sound.h

#ifndef _SOUND_h
#define _SOUND_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

#include "esp32-hal.h"
#include "esp_system.h"

class Sound {
	static uint8_t PWM_ChannelUsed;

	int8_t Pin, Channel;
	uint16_t Tone, PlayTime;
	uint32_t LastTime;

public:
	Sound();

	~Sound();

	Sound(uint32_t Pin);

	Sound(uint32_t Pin, uint8_t Channel);

	bool Init(uint8_t Pin);

	bool Init(uint8_t Pin, int8_t Channel);

	inline uint16_t getTimeOut();

	inline uint16_t getElapsed();

	inline void setTimeOut(uint16_t Time);

	void Run();

	void PlayTone(uint16_t Freq);

	void PlayTone(uint16_t Freq, uint16_t Ms);

	void PlayNote(note_t Note, uint8_t Octave);

	void PlayNote(note_t Note, uint8_t Octave, uint16_t Ms);
};


#endif

