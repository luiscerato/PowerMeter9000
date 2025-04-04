#include "battery.h"
#include "RemoteDebug.h"
#include "pins.h"


extern RemoteDebug Debug;
BQ25896  battery(Wire);


void Battery::Init()
{
	debugI("Iniciando configuracion de cargador de bateria!");

	Wire.begin(pinSDA, pinSCL);
	Wire.setClock(400000);

	battery.reset();

	battery.begin();
	battery.setPreCharge_Current_Limit(0.15);        //Primer ciclo de carga
	battery.setFast_Charge_Current_Limit(0.5);       //Corriente de carga cuando está en corriente constante
	battery.setTermination_Current_Limit(0.065);     //Corriente a la que finaliza el ciclo de carga
	battery.setBoost_Voltage(4.9);                   //VOltaje de saldia en modo BOOST (salida de 5V)
	battery.setInput_Current_Limit(0.95);            //Limita la corriente total de circuito a 1V (capacidad máxima de la fuente)
	battery.setChargeEnable(true);                   //Habilita la carga de la batería
	battery.setVINDPM(4.7);

	//battery.write_(BQ25896::REG::ADC_CTRL, 0x40);

	minVolt = 3.5;
	maxVolt = 4.10;
}

void Battery::Loop()
{
	static uint32_t timer = 0, counter = 0;
	static bool change2OTG = false;
	bool enLoad, enCharge;

	if (millis() - timer > 999) {
		timer = millis();

		battery.properties();
		BQ25896::VBUS_STAT vbusStat = battery.getVBUS_STATUS();
		chargeState = battery.getCHG_STATUS();
		chargerFault = battery.getBQFault();
		if (chargerFault.raw != 0) debugE("BQ25895 fault: 0x%x", chargerFault.raw);

		//Chequear desde donde se alimenta
		if (vbusStat == BQ25896::VBUS_STAT::OTG) {
			if (change2OTG == false) {		//Determinar si recién se cambió a OTG
				change2OTG = true;
				battery.setBatLoad(false);
			}
			powerSource = PowerSource::Battery;
			if (battery.getVBUS() > 3.5)
				debugW("Inconsistencia en deteccion de powerSource. BQ25895=OTG, pero VBUS=%.2fV > 3.5V", battery.getVBUS());
		}
		else {
			change2OTG = false;
			if (battery.getVBUS() > 4.9)
				powerSource = PowerSource::Line;
			else if (battery.getVBUS() > 4.2)
				powerSource = PowerSource::USB;
		}

		/*
			Detectar si la batería está presente. El BQ25895 no tiene una medición directa de presencia de batería.
			Si la batería no está, el cargador pasa rápidamente por las etapas de carga y carga terminada. En este caso, el ciclo completo
			dura unos 5s, mientras que con una batería cargada dura al menos 2 minutos. La duda de si hay presencia de batería se da una
			vez terminada la carga, ya que mientras está en modo OTG y hay corriente de carga mayor a 0, es evidente que hay batería.
			Se mide el tiempo de carga y si es menor a 15s, se incremente un contador hasta que llegue a 3, en este momento se deshabilita
			la carga, se activa una carga de batería (15ma) para evitar que el capacitor del pin VBAT se carge, y se indica la ausencia.
			Mientras no hay batería se mide el voltaje, si supera los 3V (sin batería marca 2.3V) es porque hay una.
		*/
		if (powerSource != PowerSource::Battery) {
			static BQ25896::CHG_STAT stat, lastStat = BQ25896::CHG_STAT::NOT_CHARGING;
			static uint32_t chargeCounter = 0;

			stat = battery.getCHG_STATUS();
			if (lastStat != stat) {
				lastStat = stat;
				debugD("Cargador cambio a %d", stat);

				if (stat == BQ25896::CHG_STAT::CHARGE_DONE) {	//Si se terminó la carga se debe controlar...
					chargeTime = millis() - chargeTime;
					debugD("Tiempo de carga: %u s", chargeTime / 1000);
					if (chargeTime < 15000) {
						if (chargeCounter++ > 2) {
							batteryPresent = false;
							enLoad = true;		//Se activa carga en batería para evitar que se cargue el capacitor y reporte mal el voltaje
							enCharge = false;	//Deshabilitar carga para terminar con el ciclo y evitar tensión en VBAT
							battery.setREG03(enCharge, true, true, enLoad);	//Escribir hibilitacion de carga, de carga de bateria y reset wd
							debugW("Se han cumplido 3 ciclos de carga en muy poco tiempo, se asume que no hay bateria...");
						}
					}
				}
				else if (stat == BQ25896::CHG_STAT::FAST_CHARGE) {
					chargeTime = millis();
				}
				else if (stat == BQ25896::CHG_STAT::PRE_CHARGE) {
					chargeTime = millis();
					chargeCounter = 0;
				}
				else
					chargeCounter = 0;
			}
		}
		else
			batteryPresent = true;

		//Si no hay batería, solo leer el voltaje de la misma a ver si supera los 3v, eso indica que hay una
		if (counter++ > 2) {
			counter = 0;
			if (!batteryPresent) {
				chargeTime = millis();
				enLoad = true;
				if (battery.getVBAT() > 3.0) {
					debugD("No hay bateria, pero el voltaje indica que se puso una: %.2fV, se habilita carga!", battery.getVBAT());
					enCharge = true;
					batteryPresent = true;
				}
				else
					enCharge = false;
			}
			else {
				enCharge = true;
				enLoad = false;
			}
			battery.setREG03(enCharge, true, true, enLoad);	//Escribir hibilitacion de carga, de carga de bateria y reset wd

		}
	}
}


ChargeState Battery::getChargeState()
{
	return chargeState;
}

bool Battery::isCharging()
{
	if (chargeState == ChargeState::PRE_CHARGE || chargeState == ChargeState::FAST_CHARGE) return true;
	return false;
}


float Battery::getPercent()
{
	float volt = battery.getVBAT();
	float percent = sigmoidal(volt, minVolt, maxVolt);

	// debugD("Battery -> volt: %.2fV, percent: %.0f%%", volt, percent);
	return percent;
}


bool Battery::isBatteryPresent()
{
	return batteryPresent;
}


PowerSource Battery::getSource()
{
	return powerSource;
}


BatteryFault Battery::getFault()
{
	BatteryFault res = BatteryFault::None;
	BQ25896::BQ_FAULT fault = battery.getBQFault();

	if (fault.BAT_FAULT)
		res = BatteryFault::BatteryOverVoltage;
	else if (fault.WATCHDOG_FAULT)
		res = BatteryFault::TimerExpired;
	else if (fault.NTC_FAULT == 1)
		res = BatteryFault::BatteryCold;
	else if (fault.NTC_FAULT == 2)
		res = BatteryFault::BatteryHot;
	else if (fault.CHRG_FAULT == 1)
		res = BatteryFault::InputVoltage;
	else if (fault.CHRG_FAULT == 2)
		res = BatteryFault::ThermalShutdown;
	else if (fault.CHRG_FAULT == 3)
		res = BatteryFault::TimerExpired;
	
	if (!batteryPresent)
		res = BatteryFault::NoBattery;

	return res;
}


void Battery::getJson(String& dest)
{
	char str[256];

	const char* src = "line", * charge = "standby", * fault = "none", * battState = "ok";
	if (powerSource == PowerSource::Battery)
		src = "battery";
	else if (powerSource == PowerSource::USB)
		src = "usb";

	if (chargeState == ChargeState::PRE_CHARGE)
		charge = "precharge";
	else if (chargeState == ChargeState::FAST_CHARGE)
		charge = "charging";
	else if (chargeState == ChargeState::CHARGE_DONE)
		charge = "ready";

	if (chargerFault.CHRG_FAULT == 1)
		fault = "input fault";
	else if (chargerFault.CHRG_FAULT == 2)
		fault = "thermal";
	else if (chargerFault.CHRG_FAULT == 3)
		fault = "timer";

	if (chargerFault.BAT_FAULT) battState = "bad";

	snprintf(str, sizeof(str), "{\"state\":\"%s\",\"charge\":\"%s\",\"percent\":%d,\"vbatt\":%.3f,\"ibatt\":%.3f,\"vbus\":%.3f,\"source\":\"%s\",\"fault\":\"%s\"}",
		battState, charge, (uint32_t)getPercent(), battery.getVBAT(), battery.getICHG(), battery.getVBUS(), src, fault);

	dest = str;
}


float Battery::sigmoidal(float voltage, float minVoltage, float maxVoltage)
{
	float result = 105.0 - (105.0 / (1 + pow(1.724 * (voltage - minVoltage) / (maxVoltage - minVoltage), 5.5)));
	result = constrain(result, 0, 100);
	if (isnanf(result)) result = 0.0;
	return result;
}


Battery Batt;