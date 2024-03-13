#ifndef _BATTERY_H
#define _BATTERY_H

#include "BQ25896.h"


extern BQ25896 battery;

class Battery {

public:
	void Init();

	void Loop();

	float getPercent();

	String getJson();


private:
	float minVolt, maxVolt;

	static inline float sigmoidal(float voltage, float minVoltage, float maxVoltage) {
		float result = 105.0 - (105.0 / (1 + pow(1.724 * (voltage - minVoltage) / (maxVoltage - minVoltage), 5.5)));
		return result >= 100.0 ? 100.0 : result;
	}

};


extern Battery Batt;

#endif