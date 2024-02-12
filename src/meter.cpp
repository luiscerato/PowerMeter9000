#include "meter.h"
#include "ade9000/ADE9000RegMap.h"
#include "pins.h"


ADE9000 ade(15000000, pinAdeSS, VSPI);

const uint8_t NOMINAL_INPUT_VOLTAGE = 220;
const float NOMINAL_INPUT_CURRENT = 1;/* Current nominal RMS for Input in Amperes */
const uint8_t INPUT_FREQUENCY = 50;/* Frequency in Hertz */
const uint8_t CALIBRATION_ANGLE_DEGREES = 60;/* Used for funtion transform current in Ohm */
const float BURDEN_RESISTOR = 11.24;/* Current nominal RMS for Input in Amperes */
const uint16_t TURNS_RATIO_TRANSFORMER = 4400;/* Used for funtion transform current */
const float ATTEUNATION_FACTOR = 1001;/* The defaul atteunation factor on board used funtion transform in Ohm/Ohm ((R1 + R2)/ R2) */
const uint8_t ACCUMULATION_TIME = 5;/* Accumulation time in seconds when EGY_TIME=7999, accumulation mode= sample based */


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



	if (millis() - rms > 39) {	//Actualizar lecturas rms rápidas 
		rms = millis();

		VoltageRMSRegs volts; CurrentRMSRegs curr;

		ade.readVoltageRMSRegs(&volts);
		ade.readCurrentRMSRegs(&curr);

		Meter.phaseR.FastVrms = volts.VoltageRMS_A;
		Meter.phaseS.FastVrms = volts.VoltageRMS_B;
		Meter.phaseT.FastVrms = volts.VoltageRMS_C;
		Meter.phaseR.FastIrms = curr.CurrentRMS_A;
		Meter.phaseS.FastIrms = curr.CurrentRMS_B;
		Meter.phaseT.FastIrms = curr.CurrentRMS_C;
		Meter.neutral.FastIrms = curr.CurrentRMS_N;
	}

	if (millis() - avg > 199) {
		avg = millis();

		VoltageRMSRegs volts; CurrentRMSRegs curr;
		ade.ReadTen12VoltageRMSRegs(&volts);
		ade.ReadTen12CurrentRMSRegs(&curr);

		Meter.phaseR.Vrms = volts.VoltageRMS_A;
		Meter.phaseS.Vrms = volts.VoltageRMS_B;
		Meter.phaseT.Vrms = volts.VoltageRMS_C;
		Meter.phaseR.Irms = curr.CurrentRMS_A;
		Meter.phaseS.Irms = curr.CurrentRMS_B;
		Meter.phaseT.Irms = curr.CurrentRMS_C;
		Meter.neutral.Irms = curr.CurrentRMS_N;
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

		Meter.power.Watt = Meter.phaseR.Watt + Meter.phaseS.Watt + Meter.phaseT.Watt;
		Meter.power.VAR = Meter.phaseR.VAR + Meter.phaseS.VAR + Meter.phaseT.VAR;
		Meter.power.VA = Meter.phaseR.VA + Meter.phaseS.VA + Meter.phaseT.VA;

		Serial.printf("Fase R: %10.3fW, %10.3fVA, %10.3fVAR. Raw: %10d, %10d, %10d. Conversion: %10.6f\n", watt.ActivePower_A, var.ReactivePower_A, va.ApparentPower_A,
			watt.ActivePowerReg_A, var.ReactivePowerReg_A, va.ApparentPowerReg_A, CAL_POWER_CC);

		Serial.printf("Fase S: %10.3fW, %10.3fVA, %10.3fVAR. Raw: %10d, %10d, %10d. Conversion: %10.6f\n", watt.ActivePower_B, var.ReactivePower_B, va.ApparentPower_B,
			watt.ActivePowerReg_B, var.ReactivePowerReg_B, va.ApparentPowerReg_B, CAL_POWER_CC);

		Serial.printf("Fase T: %10.3fW, %10.3fVA, %10.3fVAR. Raw: %10d, %10d, %10d. Conversion: %10.6f\n", watt.ActivePower_C, var.ReactivePower_C, va.ApparentPower_C,
			watt.ActivePowerReg_C, var.ReactivePowerReg_C, va.ApparentPowerReg_C, CAL_POWER_CC);
	}

	if (millis() - thd > 1023) {
		thd = millis();
		CurrentTHDRegs curr; VoltageTHDRegs volt;

		ade.ReadVoltageTHDRegsnValues(&volt);
		ade.ReadCurrentTHDRegsnValues(&curr);

		Meter.phaseR.Ithd = curr.CurrentTHDValue_A;
		Meter.phaseS.Ithd = curr.CurrentTHDValue_B;
		Meter.phaseT.Ithd = curr.CurrentTHDValue_C;
		Meter.neutral.Ithd = 0;

		Meter.phaseR.Vthd = volt.VoltageTHDValue_A;
		Meter.phaseS.Vthd = volt.VoltageTHDValue_A;
		Meter.phaseT.Vthd = volt.VoltageTHDValue_A;
		Meter.neutral.Vthd = 0;
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