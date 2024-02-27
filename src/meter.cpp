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


meterValues Meter;
struct TotalEnergyVals MeterEnergy;

TaskHandle_t meterHandle = NULL;

WFBFixedDataRate_t readBuffer[WFB_SAMPLES_PER_CHANNEL / 2];		//Buffer temporal para leer los datos del wavebuffer (7ch*128 samples)
uint8_t outputSamples[8064];	//Buffer para 96ms de datos de 12 bits. 96ms ->768 samples por canal. 768*7*(12/8) => 8064 bytes

scopeInfo_t scopeInfo;


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

	// ade.setupInterruption1(ADE_MASK1_BITS_OI);
	// ade.setupInterruption1(ADE_MASK1_BITS_DIPA | ADE_MASK1_BITS_DIPB | ADE_MASK1_BITS_DIPC);

	ade.enableDipDetection(230.0, 5);
	ade.enableSwellDetection(238.0, 10);
	ade.enableOverCurrentDetection(5.0);
}

void MeterInitScope()
{

}

void MeterTask(void* arg)
{
	uint32_t lastTime = 0, part = 0, timeRead, timeScale, timeCompress, timeSend;
	ADE_EVENT_STATUS_t lastEvent, event;
	ADE_OISTATUS_t lastOI, oi;

	while (millis() < 250);	//Esperar que todo esté listo


	while (1) {
		if (digitalRead(pinAdeInt0) == 0) {		//Leer que causó la interrupcion
			uint32_t t = micros() - lastTime;
			lastTime = micros();

			ADE_STATUS0_t status0 = ade.readStatus0();
			// Serial.printf("Status 0= 0x%X. time: %uus\n", status0, t);
			if (status0.PAGE_FULL) {
				ade.clearStatusBit0(ADE_STATUS0_BITS_PAGE_FULL);

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
				// for (int32_t x = 0; x < 128; x++) {
				// 	Serial.printf("%d     %d\n", readBuffer[x*7], readBuffer[x*7+1]);

				// }
			}
		}

		if (digitalRead(pinAdeInt1) == 0) {		//Leer que causó la interrupcion
			ADE_STATUS1_t status1 = ade.readStatus1();
			if (status1.raw) ade.clearStatusBit1(status1.raw);
		}

		struct VoltageRMSRegs volts;
		struct CurrentRMSRegs curr;
		event = ade.readEventStatus();
		if (event.DIPA || event.DIPB || event.DIPC) ade.readDipEventLevels(&volts);
		if (event.DIPA && !lastEvent.DIPA)
			Serial.printf("Entering DIP event on phase A! -> %.2fV\n", volts.VoltageRMS_A);
		else if (!event.DIPA && lastEvent.DIPA)
			Serial.printf("Exiting DIP event on phase A!\n");

		if (event.DIPB && !lastEvent.DIPB)
			Serial.printf("Entering DIP event on phase B! -> %.2fV\n", volts.VoltageRMS_B);
		else if (!event.DIPB && lastEvent.DIPB)
			Serial.printf("Exiting DIP event on phase B!\n");

		if (event.DIPC && !lastEvent.DIPC)
			Serial.printf("Entering DIP event on phase C! -> %.2fV\n", volts.VoltageRMS_C);
		else if (!event.DIPC && lastEvent.DIPC)
			Serial.printf("Exiting DIP event on phase C!\n");



		if (event.SWELLA || event.SWELLB || event.SWELLC) ade.readSwellEventLevels(&volts);
		if (event.SWELLA && !lastEvent.SWELLA)
			Serial.printf("Entering SWELL event on phase A! -> %.2fV\n", volts.VoltageRMS_A);
		else if (!event.SWELLA && lastEvent.SWELLA)
			Serial.printf("Exiting SWELL event on phase A!\n");

		if (event.SWELLB && !lastEvent.SWELLB)
			Serial.printf("Entering SWELL event on phase B! -> %.2fV\n", volts.VoltageRMS_B);
		else if (!event.SWELLB && lastEvent.SWELLB)
			Serial.printf("Exiting SWELL event on phase B!\n");

		if (event.SWELLC && !lastEvent.SWELLC)
			Serial.printf("Entering SWELL event on phase C! -> %.2fV\n", volts.VoltageRMS_C);
		else if (!event.SWELLC && lastEvent.SWELLC)
			Serial.printf("Exiting SWELL event on phase C!\n");


		oi = ade.checkOverCurrentStatus();
		if (oi.OIPHASE) ade.readOverCurrentLevels(&curr);
		if (oi.OIPHASEA && !lastOI.OIPHASEA)
			Serial.printf("Entering Over Current event on phase A! -> %.2fA\n", curr.CurrentRMS_A);
		else if (!oi.OIPHASEA && lastOI.OIPHASEA)
			Serial.printf("Exiting Over Current event on phase A!\n");

		if (oi.OIPHASEB && !lastOI.OIPHASEB)
			Serial.printf("Entering Over Current event on phase B! -> %.2fA\n", curr.CurrentRMS_B);
		else if (!oi.OIPHASEB && lastOI.OIPHASEB)
			Serial.printf("Exiting Over Current event on phase B!\n");

		if (oi.OIPHASEC && !lastOI.OIPHASEC)
			Serial.printf("Entering Over Current event on phase C! -> %.2fA\n", curr.CurrentRMS_C);
		else if (!oi.OIPHASEC && lastOI.OIPHASEC)
			Serial.printf("Exiting Over Current event on phase C!\n");

		if (oi.OIPHASEN && !lastOI.OIPHASEN)
			Serial.printf("Entering Over Current event on phase N! -> %.2fA\n", curr.CurrentRMS_N);
		else if (!oi.OIPHASEN && lastOI.OIPHASEN)
			Serial.printf("Exiting Over Current event on phase N!\n");


		lastEvent = event;
		lastOI = oi;
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
	ade.SPI_Burst_Read_FixedDT_Buffer(lastPage * 128, WFB_SAMPLES_PER_CHANNEL / 2, readBuffer);
	time = micros() - time;

	// Serial.printf("Leyendo pagina %d... time: %uus\n", lastPage, time);

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