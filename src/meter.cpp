#include "meter.h"
#include "ade9000/ADE9000RegMap.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


ADE9000 ade(15000000, pinAdeSS, VSPI);

const uint8_t NOMINAL_INPUT_VOLTAGE = 220;
const float NOMINAL_INPUT_CURRENT = 1;/* Current nominal RMS for Input in Amperes */
const uint8_t INPUT_FREQUENCY = 50;/* Frequency in Hertz */
const uint8_t CALIBRATION_ANGLE_DEGREES = 60;/* Used for funtion transform current in Ohm */
const float BURDEN_RESISTOR = 11.24;/* Current nominal RMS for Input in Amperes */
const uint16_t TURNS_RATIO_TRANSFORMER = 4400;/* Used for funtion transform current */
const float ATTEUNATION_FACTOR = 1001;/* The defaul atteunation factor on board used funtion transform in Ohm/Ohm ((R1 + R2)/ R2) */
const uint8_t ACCUMULATION_TIME = 1;/* Accumulation time in seconds when EGY_TIME=7999, accumulation mode= sample based */


meterValues Meter;
struct TotalEnergyVals MeterEnergy;

TaskHandle_t meterHandle = NULL;
void MeterTask(void* arg);
void readWaveBuffer();

int32_t readBuffer[1024];		//Buffer temporal para leer los datos del wavebuffer (8ch*128 samples)

void MeterInit()
{
	Serial.println("Iniciando ADE9000!");
	pinMode(pinAdeInt0, INPUT_PULLUP);
	pinMode(pinAdeInt1, INPUT_PULLUP);

	ade.initADE9000(pinAdeClk, pinAdeSdi, pinAdeSdo);
	ade.ADC_Redirect(adeChannel_IA, adeChannel_IC);	//Cruzar los canales A con C
	ade.ADC_Redirect(adeChannel_IC, adeChannel_IA);
	ade.ADC_Redirect(adeChannel_VA, adeChannel_VC);
	ade.ADC_Redirect(adeChannel_VC, adeChannel_VA);
	ade.setupADE9000();              // Initialize ADE9000 registers according to values in ADE9000API.h
	//5 vueltas de cable por cada trafo
	ade.setNoPowerCutoff(0.5);		//0.5mA es 0A

	xTaskCreatePinnedToCore(MeterTask, "meterTask", 8192, NULL, 10, &meterHandle, APP_CPU_NUM);
}

void MeterTask(void* arg)
{
	uint32_t lastTime = 0;
	while (millis() < 250);	//Esperar que todo esté listo

	while (1) {
		if (digitalRead(pinAdeInt0) == 0) {		//Leer que causó la interrupcion
			uint32_t t = micros() - lastTime;
			lastTime = micros();

			uint32_t status0 = ade.readStatus0();
			Serial.printf("Status 0= 0x%X. time: %uus\n", status0, t);
			if (status0 & 0x20000) {
				ade.clearStatusBit0(0x20000);
				readWaveBuffer();

				for (int32_t x = 0; x < 128; x++) {
					Serial.printf("%d     %d\n", readBuffer[x*7], readBuffer[x*7+1]);
					
				}
			}
		}
		MeterLoop();
		vTaskDelay(2 / portTICK_RATE_MS);
	}
}

void readWaveBuffer()
{
	uint32_t time = micros();
	int32_t lastPage = ade.SPI_Read_16(ADDR_WFB_TRG_STAT);
	lastPage >>= 12;

	if (lastPage >= 14 || lastPage < 3)		//Se terminó de escribir la página 15. leer de la 8 a la 15
		lastPage = 8;
	else
		lastPage = 0;
	ade.SPI_Burst_Read_FixedDT_Buffer(lastPage * 128, 1024, readBuffer);
	time = micros() - time;

	Serial.printf("Leyendo pagina %d... time: %uus\n", lastPage, time);

}

void MeterLoop()
{
	static uint32_t fast = 0, rms = 0, power = 0, thd = 0, angles = 0, energy_time = 0;

	if (millis() - rms > 199) {
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

		Meter.average.Vrms = (Meter.phaseR.Vrms + Meter.phaseS.Vrms + Meter.phaseT.Vrms) / 3.0;
		Meter.average.Irms = (Meter.phaseR.Irms + Meter.phaseS.Irms + Meter.phaseT.Irms) / 3.0;
	}

	if (millis() - fast > 39) {	//Actualizar lecturas rms rápidas 
		fast = millis();

		VoltageRMSRegs volts; CurrentRMSRegs curr;

		ade.ReadHalfVoltageRMSRegs(&volts);
		ade.ReadHalfCurrentRMSRegs(&curr);

		Meter.phaseR.FastVrms = volts.VoltageRMS_A;
		Meter.phaseS.FastVrms = volts.VoltageRMS_B;
		Meter.phaseT.FastVrms = volts.VoltageRMS_C;
		Meter.phaseR.FastIrms = curr.CurrentRMS_A;
		Meter.phaseS.FastIrms = curr.CurrentRMS_B;
		Meter.phaseT.FastIrms = curr.CurrentRMS_C;
		Meter.neutral.FastIrms = curr.CurrentRMS_N;

		Meter.average.FastVrms = (Meter.phaseR.FastVrms + Meter.phaseS.FastVrms + Meter.phaseT.FastVrms) / 3.0;
		Meter.average.FastIrms = (Meter.phaseR.FastIrms + Meter.phaseS.FastIrms + Meter.phaseT.FastIrms) / 3.0;
	}

	if (millis() - power > 499) {
		power = millis();

		ActivePowerRegs watt; ReactivePowerRegs var; ApparentPowerRegs va;

		ade.readActivePowerRegs(&watt);
		ade.readReactivePowerRegs(&var);
		ade.readApparentPowerRegs(&va);

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

		Meter.power.Watt = Meter.phaseR.Watt + Meter.phaseS.Watt + Meter.phaseT.Watt;
		Meter.power.VAR = Meter.phaseR.VAR + Meter.phaseS.VAR + Meter.phaseT.VAR;
		Meter.power.VA = Meter.phaseR.VA + Meter.phaseS.VA + Meter.phaseT.VA;

		Meter.average.Watt = (Meter.phaseR.Watt + Meter.phaseS.Watt + Meter.phaseT.Watt) / 3.0;
		Meter.average.VAR = (Meter.phaseR.VAR + Meter.phaseS.VAR + Meter.phaseT.VAR) / 3.0;
		Meter.average.VA = (Meter.phaseR.VA + Meter.phaseS.VA + Meter.phaseT.VA) / 3.0;

		// Serial.printf("Fase R: %10.3fW, %10.3fVA, %10.3fVAR. Raw: %10d, %10d, %10d. Conversion: %10.6f\n", watt.ActivePower_A, var.ReactivePower_A, va.ApparentPower_A,
		// 	watt.ActivePowerReg_A, var.ReactivePowerReg_A, va.ApparentPowerReg_A, CAL_POWER_CC);

		// Serial.printf("Fase S: %10.3fW, %10.3fVA, %10.3fVAR. Raw: %10d, %10d, %10d. Conversion: %10.6f\n", watt.ActivePower_B, var.ReactivePower_B, va.ApparentPower_B,
		// 	watt.ActivePowerReg_B, var.ReactivePowerReg_B, va.ApparentPowerReg_B, CAL_POWER_CC);

		// Serial.printf("Fase T: %10.3fW, %10.3fVA, %10.3fVAR. Raw: %10d, %10d, %10d. Conversion: %10.6f\n", watt.ActivePower_C, var.ReactivePower_C, va.ApparentPower_C,
		// 	watt.ActivePowerReg_C, var.ReactivePowerReg_C, va.ApparentPowerReg_C, CAL_POWER_CC);
	}

	if (millis() - thd > 1023) {
		thd = millis();
		CurrentTHDRegs curr; VoltageTHDRegs volt; PowerFactorRegs pf;

		ade.ReadVoltageTHDRegsnValues(&volt);
		ade.ReadCurrentTHDRegsnValues(&curr);
		ade.readPowerFactorRegsnValues(&pf);

		Meter.phaseR.Ithd = curr.CurrentTHDValue_A;
		Meter.phaseS.Ithd = curr.CurrentTHDValue_B;
		Meter.phaseT.Ithd = curr.CurrentTHDValue_C;
		Meter.neutral.Ithd = 0;

		Meter.phaseR.Vthd = volt.VoltageTHDValue_A;
		Meter.phaseS.Vthd = volt.VoltageTHDValue_B;
		Meter.phaseT.Vthd = volt.VoltageTHDValue_C;
		Meter.neutral.Vthd = 0;

		Meter.phaseR.PowerFactor = pf.PowerFactorValue_A;
		Meter.phaseS.PowerFactor = pf.PowerFactorValue_B;
		Meter.phaseT.PowerFactor = pf.PowerFactorValue_C;
		Meter.neutral.PowerFactor = 0;

		Meter.average.Ithd = (Meter.phaseR.Ithd + Meter.phaseS.Ithd + Meter.phaseT.Ithd) / 3.0;
		Meter.average.Vthd = (Meter.phaseR.Vthd + Meter.phaseS.Vthd + Meter.phaseT.Vthd) / 3.0;
		Meter.average.PowerFactor = (Meter.phaseR.PowerFactor + Meter.phaseS.PowerFactor + Meter.phaseT.PowerFactor) / 3.0;
	}

	if (millis() - angles > 499) {
		angles = millis();

		AngleRegs ang; PeriodRegs period;
		ade.readPeriodRegsnValues(&period);
		ade.readAngleRegsnValues(&ang);

		Meter.phaseR.Freq = period.FrequencyValue_A;
		Meter.phaseS.Freq = period.FrequencyValue_B;
		Meter.phaseT.Freq = period.FrequencyValue_C;
		Meter.average.Freq = (Meter.phaseR.Freq + Meter.phaseS.Freq + Meter.phaseT.Freq) / 3.0;


		Meter.phaseR.AngleVI = ang.AngleValue_VA_IA;
		Meter.phaseS.AngleVI = ang.AngleValue_VB_IB;
		Meter.phaseT.AngleVI = ang.AngleValue_VC_IC;
		if (Meter.phaseR.Watt < 10) Meter.phaseR.AngleVI = 0.0;
		if (Meter.phaseS.Watt < 10) Meter.phaseS.AngleVI = 0.0;
		if (Meter.phaseT.Watt < 10) Meter.phaseT.AngleVI = 0.0;
	}

	if (digitalRead(pinAdeInt0) == 0) {	//Interrupcion por datos de energía?
		uint32_t time = micros();
		bool res = ade.updateEnergyRegister(&MeterEnergy);
		time = micros() - time;

		if (res) {
			// Serial.printf("Actualizacion de energía (%d us)! R:%.2f Wh, S:%.2f VARh, T:%.2fVAh\n", time, (float)MeterEnergy.PhaseR.Watt_H, (float)MeterEnergy.PhaseS.Watt_H, (float)MeterEnergy.PhaseT.Watt_H);

			Meter.phaseR.Watt_H = MeterEnergy.PhaseR.Watt_H;
			Meter.phaseS.Watt_H = MeterEnergy.PhaseS.Watt_H;
			Meter.phaseT.Watt_H = MeterEnergy.PhaseT.Watt_H;

			Meter.phaseR.VAR_H = MeterEnergy.PhaseR.VAR_H;
			Meter.phaseS.VAR_H = MeterEnergy.PhaseS.VAR_H;
			Meter.phaseT.VAR_H = MeterEnergy.PhaseT.VAR_H;

			Meter.phaseR.VA_H = MeterEnergy.PhaseR.VA_H;
			Meter.phaseS.VA_H = MeterEnergy.PhaseS.VA_H;
			Meter.phaseT.VA_H = MeterEnergy.PhaseT.VA_H;

			Meter.energy.Watt_H = MeterEnergy.PhaseR.Watt_H + MeterEnergy.PhaseS.Watt_H + MeterEnergy.PhaseT.Watt_H;
			Meter.energy.VAR_H = MeterEnergy.PhaseR.VAR_H + MeterEnergy.PhaseS.VAR_H + MeterEnergy.PhaseT.VAR_H;
			Meter.energy.VA_H = MeterEnergy.PhaseR.VA_H + MeterEnergy.PhaseS.VA_H + MeterEnergy.PhaseT.VA_H;
		}
	}

	if (millis() - energy_time > 499) {
		energy_time = millis();
	}
}