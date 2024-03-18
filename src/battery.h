#ifndef _BATTERY_H
#define _BATTERY_H

#include "BQ25896.h"


extern BQ25896 battery;

typedef BQ25896::CHG_STAT ChargeState;
typedef BQ25896::BQ_FAULT ChargeFault;

enum class PowerSource {
	USB = 1,
	Line = 2,
	Battery = 3
};

enum class BatteryFault {
	None = 0,
	NoBattery = 1,
	InputVoltage,
	TimerExpired,
	BatteryHot,
	BatteryCold,
	ThermalShutdown,
	BatteryOverVoltage
};


class Battery {

public:

	void Init();

	void Loop();

	float getPercent();

	void getJson(String& dest);

	bool isBatteryPresent();

	BatteryFault getFault();

	PowerSource getSource();

	ChargeState getChargeState();

	bool isCharging();


private:
	float minVolt = 3.5, maxVolt = 4.2;
	bool batteryPresent = false;
	uint32_t chargeTime = 0;

	PowerSource powerSource = PowerSource::Line;
	ChargeState chargeState = ChargeState::NOT_CHARGING;
	ChargeFault chargerFault;


	float sigmoidal(float voltage, float minVoltage, float maxVoltage);
};


extern Battery Batt;

#endif