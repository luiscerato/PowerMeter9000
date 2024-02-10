#include "meter.h"
#include "ade9000/ADE9000RegMap.h"
#include "pins.h"


ADE9000 ade(15000000, pinAdeSS, VSPI);

meterValues Meter;

void MeterInit()
{
	Serial.println("Iniciando ADE9000!");
	ade.initADE9000(pinAdeClk, pinAdeSdi, pinAdeSdo);
	ade.setupADE9000();              // Initialize ADE9000 registers according to values in ADE9000API.h
}

void MeterLoop()
{
	static uint32_t rms = 0, avg = 0, power = 0, thd = 0, angles = 0, energy = 0;

	if (millis() - rms > 32) {	//Actualizar lecturas rms rápidas 
		rms = millis();

		VoltageRMSRegs volts; CurrentRMSRegs curr;

		ade.readVoltageRMSRegs(&volts);
		ade.readCurrentRMSRegs(&curr);

		Meter.phaseR.Vrms = volts.VoltageRMS_A;
		Meter.phaseS.Vrms = volts.VoltageRMS_B;
		Meter.phaseT.Vrms = volts.VoltageRMS_C;
		Meter.phaseR.Irms = curr.CurrentRMS_A;
		Meter.phaseS.Irms = curr.CurrentRMS_B;
		Meter.phaseT.Irms = curr.CurrentRMS_C;
		Meter.neutral.Irms = curr.CurrentRMS_N;
	}

	if (millis() - avg > 199) {
		avg = millis();


	}

	if (millis() - power > 499) {
		power = millis();

		ActivePowerRegs watt; ReactivePowerRegs var; ApparentPowerRegs va; PowerFactorRegs pf;

		ade.readActivePowerRegs(&watt);
		ade.readReactivePowerRegs(&var);
		ade.readApparentPowerRegs(&va);
		ade.readPowerFactorRegsnValues(&pf);

		Meter.phaseR.Watt = watt.ActivePower_A;
		Meter.phaseS.Watt = watt.ActivePower_B;
		Meter.phaseT.Watt = watt.ActivePower_C;
		Meter.neutral.Watt = 0;

		Meter.phaseR.VAR = var.ReactivePower_A;
		Meter.phaseS.VAR = var.ReactivePower_B;
		Meter.phaseT.VAR = var.ReactivePower_C;
		Meter.neutral.VAR = 0;

		Meter.phaseR.VA = va.ApparentPower_A;
		Meter.phaseS.VA = va.ApparentPower_B;
		Meter.phaseT.VA = va.ApparentPower_C;
		Meter.neutral.VA = 0;

		Meter.phaseR.PowerFactor = pf.PowerFactorValue_A;
		Meter.phaseS.PowerFactor = pf.PowerFactorValue_B;
		Meter.phaseT.PowerFactor = pf.PowerFactorValue_C;
		Meter.neutral.PowerFactor = 0;
	}

	if (millis() - thd > 1023) {
		thd = millis();


	}

	if (millis() - angles > 499) {
		angles = millis();

		AngleRegs ang; PeriodRegs period;
		ade.readPeriodRegsnValues(&period);
		ade.readAngleRegsnValues(&ang);

		Meter.phaseR.Freq = period.FrequencyValue_A;
		Meter.phaseS.Freq = period.FrequencyValue_B;
		Meter.phaseT.Freq = period.FrequencyValue_C;

		Meter.phaseR.AngleVI = ang.AngleValue_VA_IA;
		Meter.phaseS.AngleVI = ang.AngleValue_VB_IB;
		Meter.phaseT.AngleVI = ang.AngleValue_VC_IC;
	}

	if (millis() - energy > 499) {
		energy = millis();


	}
}