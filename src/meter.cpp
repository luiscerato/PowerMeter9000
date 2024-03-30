#include "meter.h"
#include "ade9000/ADE9000RegMap.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "webserver.h"


ADE9000 ade(15000000, pinAdeSS, VSPI);

const uint8_t NOMINAL_INPUT_VOLTAGE = 220;
const float NOMINAL_INPUT_CURRENT = 1;/* Current nominal RMS for Input in Amperes */
const uint8_t INPUT_FREQUENCY = 50;/* Frequency in Hertz */
const uint8_t CALIBRATION_ANGLE_DEGREES = 60;/* Used for funtion transform current in Ohm */
const float BURDEN_RESISTOR = 11.24;/* Current nominal RMS for Input in Amperes */
const uint16_t TURNS_RATIO_TRANSFORMER = 4400;/* Used for funtion transform current */
const float ATTEUNATION_FACTOR = 1001;/* The defaul atteunation factor on board used funtion transform in Ohm/Ohm ((R1 + R2)/ R2) */
const uint8_t ACCUMULATION_TIME = 1;/* Accumulation time in seconds when EGY_TIME=7999, accumulation mode= sample based */


meterValues meterVals;		//Contiene los datos leidos por taskMeter, por lo que no se deben usar fuera de esa tarea
meterValues Meter;			//Contiene datos que se pueden leer de manera segura por otro task
struct TotalEnergyVals MeterEnergy;

TaskHandle_t meterHandle = NULL;

WFBFixedDataRate_t readBuffer[WFB_SAMPLES_PER_CHANNEL / 2];		//Buffer temporal para leer los datos del wavebuffer (7ch*128 samples)
uint8_t outputSamples[8064];	//Buffer para 96ms de datos de 12 bits. 96ms ->768 samples por canal. 768*7*(12/8) => 8064 bytes

scopeInfo_t scopeInfo;
uint32_t meterUpdated = 0;
portMUX_TYPE meterMutex = portMUX_INITIALIZER_UNLOCKED;


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

	scopeInfo.currentScale = 1.0;
	scopeInfo.voltageScale = 1.0;
	scopeInfo.sampleFreq = 8000;
	webServerSetMeterEvents(scopeWSevents);

	xTaskCreatePinnedToCore(MeterTask, "meterTask", 8192, NULL, 10, &meterHandle, APP_CPU_NUM);

	/*
		Asignar los pines de interrupciones a una función que se encargue de desbloquear la tarea que lee el ADE9000
	*/

	//Interrupción 0 se va a encargar de desbloquear por eventos de medición
	ade.setupInterruption0(ADE_MASK0_BITS_EGYRDY | ADE_MASK0_BITS_PAGE_FULL | ADE_MASK0_BITS_RMSONERDY);

	attachInterrupt(pinAdeInt0, []() {
		BaseType_t switchToMeterTask = pdFALSE; //
		vTaskNotifyGiveFromISR(meterHandle, &switchToMeterTask);
		portYIELD_FROM_ISR(switchToMeterTask);
		}, FALLING);
	ade.clearStatusBit0();


	//Configurar los niveles de disparo de los eventos en las señales
	ade.setDipDetectionLevels(210.0, 5);
	ade.setSwellDetectionLevels(238.0, 5);
	ade.enableOverCurrentDetection(5.0);

	//Interrupcion 1 va a desbloquear por eventos en las señales medidas
	ade.setupInterruption1(ADE_MASK1_BITS_DIPA | ADE_MASK1_BITS_DIPB | ADE_MASK1_BITS_DIPC |
		ADE_MASK1_BITS_SWELLA | ADE_MASK1_BITS_SWELLB | ADE_MASK1_BITS_SWELLC |
		ADE_MASK1_BITS_OI);	//Limpiar flags de interrupciones

	attachInterrupt(pinAdeInt1, []() {
		BaseType_t switchToMeterTask = pdFALSE; //
		vTaskNotifyGiveFromISR(meterHandle, &switchToMeterTask);
		portYIELD_FROM_ISR(switchToMeterTask);
		}, FALLING);

	ade.clearStatusBit1();	//Limpiar flags de interrupciones

	Serial.println("Meter inicializado!");
}


void MeterTask(void* arg)
{
	uint32_t waitingTime = 0, runningTime = 0, startTime = 0, stopTime = 0, realTime = 0;
	ADE_EVENT_STATUS_t lastEvent, event;
	ADE_OISTATUS_t lastOI, oi;
	uint32_t EventsCount;

	while (millis() < 250)
		vTaskDelay(25 / portTICK_RATE_MS);	//Esperar que todo esté listo


	while (1) {
		stopTime = micros();
		runningTime += stopTime - startTime;

		EventsCount = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));	//Esperar a que se produzca una interrupcion (al menos cada 10ms)
		startTime = micros();
		waitingTime += startTime - stopTime;

		if (micros() - realTime > 999999) {
			realTime = micros() - realTime;
			// Serial.printf("CPU -> realTime=%dus, runningTime=%dus, waitingTime=%dus  ==>> CPU=%.2f%%\n", realTime, runningTime, waitingTime,
			// 	(float)runningTime / (float)realTime * 100.0);
			realTime = micros();
			waitingTime = runningTime = 0;
		}

		if (EventsCount == 0)
			Serial.println("Algo ha ocurrido y no se recibieron interrupciones dentro los 50ms de espera!");

		/*
			Usar un bloque while para leer los flags de las interrupciones porque se da el caso de que se produce una nueva interrupcion
			entre que se leyó el estado actual y se limpió los bits, entonces el pin de interrupción sigue activo y no se genera la interrupcion
			correspondiente. De esta manera se asegura que se limpien todos los bits y se sigen generando interrupciones.
		*/
		while (digitalRead(pinAdeInt0) == 0) {		//Leer que causó la interrupcion
			ADE_STATUS0_t status0 = ade.readStatus0();
			ade.clearStatusBit0(status0.raw);
			// if (EventsCount == 0) Serial.printf("Status0=0x%x\n", status0.raw);

			// Serial.printf("Status 0= 0x%X. time: %uus\n", status0, t);
			if (status0.PAGE_FULL) {
				meterReadWaveBuffer();
				meterUpdated++;
			}
			if (status0.RMSONERDY) {
				meterReadHalfRMS();
				meterUpdated++;
			}
			if (status0.RMS1012RDY) {
				meterRead1012RMS();
				meterReadRMS();
				meterReadPower();
				meterUpdated++;
			}
			if (status0.THD_PF_RDY) {
				meterReadTHD();
				meterReadPowerFactor();
				meterReadAngles();
				meterReadPeriods();
				meterUpdated++;
			}
			if (status0.EGYRDY) {
				meterReadEnergy();
				meterUpdated++;
			}
		}

		while (digitalRead(pinAdeInt1) == 0) {		//Leer que causó la interrupcion
			ADE_STATUS1_t status1 = ade.readStatus1();
			if (status1.raw) ade.clearStatusBit1(status1.raw);

			meterReadDipSwell();
			meterReadOverCurrent();
			meterUpdated++;
		}
	}
}


void scopeWSevents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
	if (type == WS_EVT_CONNECT) {
		Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
		client->ping();
		server->textAll("");
	}
	else if (type == WS_EVT_DISCONNECT) {
		Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
	}
	else if (type == WS_EVT_ERROR) {
		Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
	}
	else if (type == WS_EVT_PONG) {
		Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char*)data : "");
	}
	else if (type == WS_EVT_DATA) {
		AwsFrameInfo* info = (AwsFrameInfo*)arg;
		String msg = "";
		if (info->final && info->index == 0 && info->len == len) {
			Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

			if (info->opcode == WS_TEXT) {
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)data[i];
				}
			}
			Serial.printf("%s\n", msg.c_str());
			//Buscar informacion en el mensaje
			float value;

			if (sscanf((const char*)data, "scaleVoltage=%f", &value) == 1) {
				if (value > 0.0 && value < 100.0) scopeInfo.voltageScale = value;
				Serial.printf("Cambiando escala de voltaje a %.3f V/bit\n", value);
			}
			else if (sscanf((const char*)data, "scaleCurrent=%f", &value) == 1) {
				if (value > 0.0 && value < 100.0) scopeInfo.currentScale = value;
				Serial.printf("Cambiando escala de corriente a %.3f A/bit\n", value);
			}
		}
	}
}



void MeterLoop()
{
	static uint32_t fast = 0, rms = 0, power = 0, thd = 0, angles = 0, energy_time = 0;

	if (meterUpdated) {
		taskENTER_CRITICAL(&meterMutex);
		meterUpdated = 0;
		Meter = meterVals;
		taskEXIT_CRITICAL(&meterMutex);
	}

	if (millis() - rms > 199) {
		rms = millis();
	}

	if (millis() - fast > 39) {	//Actualizar lecturas rms rápidas 
		fast = millis();
	}

	if (millis() - power > 499) {
		power = millis();
	}

	if (millis() - thd > 1023) {
		thd = millis();
	}

	if (millis() - angles > 499) {
		angles = millis();
	}

	if (millis() - energy_time > 499) {
		energy_time = millis();
	}
}


void meterReadWaveBuffer()
{
	static uint32_t lastTime = 0, part = 0, timeRead, timeScale, timeCompress, timeSend;
	timeRead = micros();
	readWaveBuffer();
	timeRead = micros() - timeRead;

	timeScale = micros();
	scaleBuffer(readBuffer, WFB_SAMPLES_PER_CHANNEL / 2, scopeInfo.voltageScale, scopeInfo.currentScale);
	timeScale = micros() - timeScale;

	timeCompress = micros();
	compressWaveBuffer12(readBuffer[0].buffer, outputSamples, part++);
	timeCompress = micros() - timeCompress;

	if (part == 6) {	//Buffer de 96ms comprimido listo para enviar
		part = 0;

		timeSend = micros();
		size_t size = sizeof(outputSamples);
		webServerGetMeterWS()->binaryAll(outputSamples, size);

		timeSend = micros() - timeSend;

		// Serial.printf("Tiempo de lectura: %uus, tiempo escalado: %uus, tiempo de compresion: %uus, tiempo de envio: %uus, free heap: %u bytes\n",
		// 	timeRead, timeScale, timeCompress, timeSend, ESP.getFreeHeap());
	}
}


void meterReadRMS()
{
	VoltageRMSRegs volts; CurrentRMSRegs curr;
	ade.readVoltageRMSRegs(&volts);
	ade.readCurrentRMSRegs(&curr);

	meterVals.phaseR.Vrms = volts.VoltageRMS_A;
	meterVals.phaseS.Vrms = volts.VoltageRMS_B;
	meterVals.phaseT.Vrms = volts.VoltageRMS_C;

	meterVals.phaseR.Irms = curr.CurrentRMS_A;
	meterVals.phaseS.Irms = curr.CurrentRMS_B;
	meterVals.phaseT.Irms = curr.CurrentRMS_C;
	meterVals.neutral.Irms = curr.CurrentRMS_N;
	//Calcular voltajes entre fase y fase
	meterVals.phaseR.VVrms = sumVoltages(meterVals.phaseR.Vrms, 0, meterVals.phaseS.Vrms, meterVals.phaseR.AngleV);
	meterVals.phaseS.VVrms = sumVoltages(meterVals.phaseS.Vrms, 0, meterVals.phaseT.Vrms, meterVals.phaseS.AngleV);
	meterVals.phaseT.VVrms = sumVoltages(meterVals.phaseT.Vrms, 0, meterVals.phaseR.Vrms, meterVals.phaseT.AngleV);

	meterVals.average.Vrms = (meterVals.phaseR.Vrms + meterVals.phaseS.Vrms + meterVals.phaseT.Vrms) / 3.0;
	meterVals.average.VVrms = (meterVals.phaseR.VVrms + meterVals.phaseS.VVrms + meterVals.phaseT.VVrms) / 3.0;
	meterVals.average.Irms = (meterVals.phaseR.Irms + meterVals.phaseS.Irms + meterVals.phaseT.Irms) / 3.0;
}

void meterReadHalfRMS()
{
	VoltageRMSRegs volts; CurrentRMSRegs curr;

	ade.ReadHalfVoltageRMSRegs(&volts);
	ade.ReadHalfCurrentRMSRegs(&curr);

	meterVals.phaseR.FastVrms = volts.VoltageRMS_A;
	meterVals.phaseS.FastVrms = volts.VoltageRMS_B;
	meterVals.phaseT.FastVrms = volts.VoltageRMS_C;
	meterVals.phaseR.FastIrms = curr.CurrentRMS_A;
	meterVals.phaseS.FastIrms = curr.CurrentRMS_B;
	meterVals.phaseT.FastIrms = curr.CurrentRMS_C;
	meterVals.neutral.FastIrms = curr.CurrentRMS_N;

	meterVals.average.FastVrms = (meterVals.phaseR.FastVrms + meterVals.phaseS.FastVrms + meterVals.phaseT.FastVrms) / 3.0;
	meterVals.average.FastIrms = (meterVals.phaseR.FastIrms + meterVals.phaseS.FastIrms + meterVals.phaseT.FastIrms) / 3.0;
}

void meterRead1012RMS()
{

}

void meterReadPower()
{
	ActivePowerRegs watt; ReactivePowerRegs var; ApparentPowerRegs va;

	ade.readActivePowerRegs(&watt);
	ade.readReactivePowerRegs(&var);
	ade.readApparentPowerRegs(&va);

	meterVals.phaseR.Watt = watt.ActivePower_A;
	meterVals.phaseS.Watt = watt.ActivePower_B;
	meterVals.phaseT.Watt = watt.ActivePower_C;
	meterVals.neutral.Watt = 0;

	meterVals.phaseR.VAR = var.ReactivePower_A;
	meterVals.phaseS.VAR = var.ReactivePower_B;
	meterVals.phaseT.VAR = var.ReactivePower_C;
	meterVals.neutral.VAR = 0;

	meterVals.phaseR.VA = va.ApparentPower_A;
	meterVals.phaseS.VA = va.ApparentPower_B;
	meterVals.phaseT.VA = va.ApparentPower_C;
	meterVals.neutral.VA = 0;

	meterVals.power.Watt = meterVals.phaseR.Watt + meterVals.phaseS.Watt + meterVals.phaseT.Watt;
	meterVals.power.VAR = meterVals.phaseR.VAR + meterVals.phaseS.VAR + meterVals.phaseT.VAR;
	meterVals.power.VA = meterVals.phaseR.VA + meterVals.phaseS.VA + meterVals.phaseT.VA;

	meterVals.average.Watt = (meterVals.phaseR.Watt + meterVals.phaseS.Watt + meterVals.phaseT.Watt) / 3.0;
	meterVals.average.VAR = (meterVals.phaseR.VAR + meterVals.phaseS.VAR + meterVals.phaseT.VAR) / 3.0;
	meterVals.average.VA = (meterVals.phaseR.VA + meterVals.phaseS.VA + meterVals.phaseT.VA) / 3.0;
}

void meterReadTHD()
{
	CurrentTHDRegs curr; VoltageTHDRegs volt;

	ade.ReadVoltageTHDRegsnValues(&volt);
	ade.ReadCurrentTHDRegsnValues(&curr);

	meterVals.phaseR.Ithd = curr.CurrentTHDValue_A;
	meterVals.phaseS.Ithd = curr.CurrentTHDValue_B;
	meterVals.phaseT.Ithd = curr.CurrentTHDValue_C;
	meterVals.neutral.Ithd = 0;

	meterVals.phaseR.Vthd = volt.VoltageTHDValue_A;
	meterVals.phaseS.Vthd = volt.VoltageTHDValue_B;
	meterVals.phaseT.Vthd = volt.VoltageTHDValue_C;
	meterVals.neutral.Vthd = 0;

	meterVals.average.Ithd = (meterVals.phaseR.Ithd + meterVals.phaseS.Ithd + meterVals.phaseT.Ithd) / 3.0;
	meterVals.average.Vthd = (meterVals.phaseR.Vthd + meterVals.phaseS.Vthd + meterVals.phaseT.Vthd) / 3.0;
}

void meterReadPowerFactor()
{
	PowerFactorRegs pf;
	ade.readPowerFactorRegsnValues(&pf);

	meterVals.phaseR.PowerFactor = pf.PowerFactorValue_A;
	meterVals.phaseS.PowerFactor = pf.PowerFactorValue_B;
	meterVals.phaseT.PowerFactor = pf.PowerFactorValue_C;
	meterVals.neutral.PowerFactor = 0;

	meterVals.average.PowerFactor = (meterVals.phaseR.PowerFactor + meterVals.phaseS.PowerFactor + meterVals.phaseT.PowerFactor) / 3.0;
}

void meterReadPeriods()
{
	PeriodRegs period;
	ade.readPeriodRegsnValues(&period);

	meterVals.phaseR.Freq = period.FrequencyValue_A;
	meterVals.phaseS.Freq = period.FrequencyValue_B;
	meterVals.phaseT.Freq = period.FrequencyValue_C;
	meterVals.average.Freq = (meterVals.phaseR.Freq + meterVals.phaseS.Freq + meterVals.phaseT.Freq) / 3.0;
}

void meterReadAngles()
{
	AngleRegs ang;
	ade.readAngleRegsnValues(&ang);

	meterVals.phaseR.AngleVI = ang.AngleValue_VA_IA;
	meterVals.phaseS.AngleVI = ang.AngleValue_VB_IB;
	meterVals.phaseT.AngleVI = ang.AngleValue_VC_IC;

	meterVals.phaseR.AngleV = ang.AngleValue_VA_VB;
	meterVals.phaseS.AngleV = ang.AngleValue_VB_VC;
	meterVals.phaseT.AngleV = 360 - ang.AngleValue_VA_VC;

	meterVals.phaseR.AngleI = ang.AngleValue_IA_IB;
	meterVals.phaseS.AngleI = ang.AngleValue_IB_IC;
	meterVals.phaseT.AngleI = 360 - ang.AngleValue_IA_IC;

	if (meterVals.phaseR.Irms < 0.2) meterVals.phaseR.AngleVI = meterVals.phaseR.AngleI = 0.0;
	if (meterVals.phaseS.Irms < 0.2) meterVals.phaseS.AngleVI = meterVals.phaseS.AngleI = 0.0;
	if (meterVals.phaseT.Irms < 0.2) meterVals.phaseT.AngleVI = meterVals.phaseT.AngleI = 0.0;
}

void meterReadEnergy()
{
	ade.readAccEnergyRegister(&MeterEnergy);

	meterVals.phaseR.Watt_H = MeterEnergy.PhaseR.Watt_H;
	meterVals.phaseS.Watt_H = MeterEnergy.PhaseS.Watt_H;
	meterVals.phaseT.Watt_H = MeterEnergy.PhaseT.Watt_H;

	meterVals.phaseR.VAR_H = MeterEnergy.PhaseR.VAR_H;
	meterVals.phaseS.VAR_H = MeterEnergy.PhaseS.VAR_H;
	meterVals.phaseT.VAR_H = MeterEnergy.PhaseT.VAR_H;

	meterVals.phaseR.VA_H = MeterEnergy.PhaseR.VA_H;
	meterVals.phaseS.VA_H = MeterEnergy.PhaseS.VA_H;
	meterVals.phaseT.VA_H = MeterEnergy.PhaseT.VA_H;

	meterVals.energy.Watt_H = MeterEnergy.PhaseR.Watt_H + MeterEnergy.PhaseS.Watt_H + MeterEnergy.PhaseT.Watt_H;
	meterVals.energy.VAR_H = MeterEnergy.PhaseR.VAR_H + MeterEnergy.PhaseS.VAR_H + MeterEnergy.PhaseT.VAR_H;
	meterVals.energy.VA_H = MeterEnergy.PhaseR.VA_H + MeterEnergy.PhaseS.VA_H + MeterEnergy.PhaseT.VA_H;
}

void meterReadDipSwell()
{
	ADE_EVENT_STATUS_t event;
	struct VoltageRMSRegs volts;

	event = ade.readEventStatus();
	if (event.DIPA || event.DIPB || event.DIPC) ade.readDipLevels(&volts);	//Leer solo si hay un evento activo

	meterVals.phaseR.voltageDip.setStatus(event.DIPA, volts.VoltageRMS_A);
	meterVals.phaseS.voltageDip.setStatus(event.DIPB, volts.VoltageRMS_B);
	meterVals.phaseT.voltageDip.setStatus(event.DIPC, volts.VoltageRMS_C);

	if (meterVals.phaseR.voltageDip.hasChanged()) meterVals.phaseR.voltageDip.printEvent("dip", "R");
	if (meterVals.phaseS.voltageDip.hasChanged()) meterVals.phaseS.voltageDip.printEvent("dip", "S");
	if (meterVals.phaseT.voltageDip.hasChanged()) meterVals.phaseT.voltageDip.printEvent("dip", "T");

	if (event.SWELLA || event.SWELLB || event.SWELLC) ade.readSwellLevels(&volts);	//Leer solo si hay un evento activo
	meterVals.phaseR.voltageSwell.setStatus(event.SWELLA, volts.VoltageRMS_A);
	meterVals.phaseS.voltageSwell.setStatus(event.SWELLB, volts.VoltageRMS_B);
	meterVals.phaseT.voltageSwell.setStatus(event.SWELLC, volts.VoltageRMS_C);

	if (meterVals.phaseR.voltageSwell.hasChanged()) meterVals.phaseR.voltageSwell.printEvent("swell", "R");
	if (meterVals.phaseS.voltageSwell.hasChanged()) meterVals.phaseS.voltageSwell.printEvent("swell", "S");
	if (meterVals.phaseT.voltageSwell.hasChanged()) meterVals.phaseT.voltageSwell.printEvent("swell", "T");
}


void meterReadOverCurrent()
{
	ADE_OISTATUS_t status;
	struct CurrentRMSRegs current;

	status = ade.checkOverCurrentStatus();
	if (status.OIPHASE) ade.readOverCurrentLevels(&current);	//Leer solo si hay un evento activo

	meterVals.phaseR.overCurrent.setStatus(status.OIPHASEA, current.CurrentRMS_A);
	meterVals.phaseS.overCurrent.setStatus(status.OIPHASEB, current.CurrentRMS_B);
	meterVals.phaseT.overCurrent.setStatus(status.OIPHASEC, current.CurrentRMS_C);
	meterVals.neutral.overCurrent.setStatus(status.OIPHASEN, current.CurrentRMS_N);

	if (meterVals.phaseR.overCurrent.hasChanged()) meterVals.phaseR.overCurrent.printEvent("overCurrent", "R");
	if (meterVals.phaseS.overCurrent.hasChanged()) meterVals.phaseS.overCurrent.printEvent("overCurrent", "S");
	if (meterVals.phaseT.overCurrent.hasChanged()) meterVals.phaseT.overCurrent.printEvent("overCurrent", "T");
	if (meterVals.neutral.overCurrent.hasChanged()) meterVals.neutral.overCurrent.printEvent("overCurrent", "N");
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
	ade.SPI_Burst_Read_FixedDT_Buffer(lastPage * 128, WFB_SAMPLES_PER_CHANNEL / 2, readBuffer);
	time = micros() - time;
}

/*
	Escalar el buffer de entrada para ajustar un bit al valor indicado en voltage o current
*/
void scaleBuffer(WFBFixedDataRate_t* samplesBuffer, int32_t samplesCount, float voltage, float current)
{
	if (samplesCount < 0 || samplesCount >= WFB_SAMPLES_PER_CHANNEL) return;
	if (voltage == 0.0 || current == 0.0) return;

	current = 1.0 / (CAL_I_PCF * current) * 65536;
	voltage = 1.0 / (CAL_V_PCF * voltage) * 65536;

	for (uint32_t i = 0; i < samplesCount; i++) {
		samplesBuffer[i].IA *= current;
		samplesBuffer[i].IB *= current;
		samplesBuffer[i].IC *= current;
		samplesBuffer[i].IN *= current;
		samplesBuffer[i].VA *= voltage;
		samplesBuffer[i].VB *= voltage;
		samplesBuffer[i].VC *= voltage;
	}
}

void compressWaveBuffer12(int32_t* waveBufferRaw, uint8_t* waveBuffer, uint16_t part)
{
	uint32_t time = micros();
	int32_t* raw = waveBufferRaw;
	uint8_t* buf = &waveBuffer[part * 1344];  //2688 es la cantidad de bytes resultantes de comprimir el buffer de 1024 words 
	// uint8_t* buf = &waveBuffer[part * 2688];  //2688 es la cantidad de bytes resultantes de comprimir el buffer de 2048 words 


	int32_t val1, val2, k = 0, bytes = 0;
	for (int32_t i = 0; i < 896; i++) {	//896 muestras en el buffer 128*7=896
		/*
			Como se comprime y se guardan 12 bits, se deben juntar de a 2 muestras y guardar 3 bytes
			Siempre se guarda después de leer la segunda muestra
		*/
		if (!(k++ & 1)) {
			val1 = (*raw++) >> 16;  //Comprimir a 12 bits
			if (val1 > 2047) val1 = 2047;
			else if (val1 < -2048) val1 = -2048;
		}
		else {  //Solo guardar cada 2 words leidas
			val2 = (*raw++) >> 16;  //Comprimir a 12 bits
			if (val2 > 2047) val2 = 2047;
			else if (val2 < -2048) val2 = -2048;

			uint8_t a, b, c;

			*buf++ = a = val1 & 0xFF;
			*buf++ = b = uint8_t((val1 & 0x0F00) >> 8) | uint8_t((val2 & 0x000F) << 4);
			*buf++ = c = (val2 & 0xFF0) >> 4;
			bytes += 3;
		}
	}

	time = micros() - time;
	// Serial.printf("Tiempo de compresion: %u us, k: %d, bytes: %d\n", time, k, bytes);
}

float sumVoltages(float v1, float deg1, float v2, float deg2)
{
	deg2 -= deg1;		//Diferencia en grados entre cada fase
	//c2 = a^2 + b^2 − 2 a b cos(θ)
	float sum = v1 * v1 + v2 * v2 - 2 * v1 * v2 * cosf(radians(deg2));
	return sqrtf(sum);
}