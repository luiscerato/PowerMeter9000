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

	battery.begin();
	battery.setPreCharge_Current_Limit(0.15);        //Primer ciclo de carga
	battery.setFast_Charge_Current_Limit(0.5);       //Corriente de carga cuando está en corriente constante
	battery.setTermination_Current_Limit(0.065);     //Corriente a la que finaliza el ciclo de carga
	battery.setBoost_Voltage(4.8);                   //VOltaje de saldia en modo BOOST (salida de 5V)
	battery.setInput_Current_Limit(0.90);            //Limita la corriente total de circuito a 1V (capacidad máxima de la fuente)
	battery.setChargeEnable(true);                   //Habilita la carga de la batería

	minVolt = 3.5;
	maxVolt = 4.15;
}

void Battery::Loop()
{
	static uint32_t timer = 0;

	if (millis() - timer > 999) {
		timer = millis();
		battery.clearTimer();


		uint32_t tbatt = micros();
		battery.properties();
		tbatt = micros() - tbatt;
		debugD("Battery updated: %d us", tbatt);

		getPercent();
	}
}

float Battery::getPercent()
{
	float volt = battery.getVBAT();
	float percent = sigmoidal(volt, minVolt, maxVolt);

	debugD("Battery -> volt: %.2fV, percent: %.0f%%", volt, percent);
	return percent;
}

Battery Batt;