#include "ade9000/ADE9000.h"
#include "AsyncWebSocket.h"
#include "time.h"
#include "wifiUtils.h"

#define fastDataSamplesCount 200

typedef struct {
	uint16_t timeStamp;		//Ms de la muestra
	uint16_t VrmsR;			//Voltaje rms de fase R con 2 decimales (rango 0-655.35Vac)
	uint16_t VrmsS;			//Voltaje rms de fase S con 2 decimales (rango 0-655.35Vac)
	uint16_t VrmsT;			//Voltaje rms de fase T con 2 decimales (rango 0-655.35Vac)
	uint16_t IrmsR;			//Corriente rms de fase R con 2 decimales (rango 0-655.35A)
	uint16_t IrmsS;			//Corriente rms de fase S con 2 decimales (rango 0-655.35A)
	uint16_t IrmsT;			//Corriente rms de fase T con 2 decimales (rango 0-655.35A)
	uint16_t IrmsN;			//Corriente rms de neutro con 2 decimales (rango 0-655.35A)
} fastRMSData_t;

class capturedEvent {
	fastRMSData_t* data;
	uint32_t maxSamples;
	uint32_t sampleCount;
	String events;
	uint32_t eventCounter;
	uint32_t prevSampleQty;				//Cantidad de muestras previas
	bool capturing, canDownload, canStart;
	timeval startDate, endDate;
	uint32_t dipCycles = 5, swellCycles = 5;
	float dipVoltage, swellVoltage, overCurrent;

	void init(uint32_t maxSamples) {
		data = nullptr;
		this->maxSamples = maxSamples;
		sampleCount = 0;
		events = "";
		capturing = false;
		canDownload = false;
		canStart = true;
		eventCounter = 0;
	};

public:

	void setDipEventParam(float vLevel, uint32_t halfCycles) {dipVoltage = vLevel; dipCycles = halfCycles;};
	void seSwellEventParam(float vLevel, uint32_t halfCycles) {swellVoltage = vLevel; swellCycles = halfCycles;}
	void setOverCurrentParam(float iLevel) {overCurrent = iLevel; };

	capturedEvent(uint16_t maxSamples){ init(maxSamples);};

	~capturedEvent() {
		if (data) free(data);
		events.~String();
	}

	const char *getBuffer() {if (data) return (const char*)data; return nullptr; };

	bool isCapturing() {return capturing;};

	uint32_t getSampleCount() {return sampleCount;};

	bool isSampleBufferFull() {return sampleCount == (maxSamples-1);};

	// Iniciar la clase para emperzar a capturar datos. Inicia el buffer según la cantidad de muestras máximas a guardar.
	bool startCapture(uint32_t prevSampleQty) {
		uint32_t size = sizeof(fastRMSData_t) * maxSamples;
		if (capturing || (data != nullptr) || !canStart ) {debugE("Captura en curso!"); return false;};
		data = (fastRMSData_t*)malloc(size);
		if (data == nullptr) {debugE("Fallo al intentar asignar %d bytes", size); return false;};
		debugI("Iniciando captura... Asignados %u bytes para %u samples", size, maxSamples);
		memset(data, 0, size);	//Iniciar todo a 0
		sampleCount = 0;
		events = "";	//Crear un array Json para los eventos de texto
		capturing = true;
		canDownload = canStart = false;
		eventCounter++;		//Incrementar contador de evento
		gettimeofday(&startDate, nullptr);
		this->prevSampleQty = prevSampleQty;
		return true;
	};

	bool isDataReady() {return canDownload; };

	void stopCapture() {
		if (!capturing) return;
		debugI("Terminando captura... %d samples capturadas", sampleCount);
		capturing = false;
		canDownload = true;
		canStart = false;
		gettimeofday(&endDate, nullptr);
	};

	void reset() {
		if (capturing) stopCapture();
		if (data) free(data);
		data = nullptr;
		events = "";
		canDownload = false;
		canStart = true;
 	};

	void printSamples(uint32_t start, uint32_t end) {
		if (!canDownload) return;
		if (start > end) return;
		if (start >= sampleCount) return;
		if (end > (sampleCount-1)) end = sampleCount-1;
 
		for (uint32_t i = start; i < end; i++) {
			fastRMSData_t sample = data[i];
			debugI("[%03d] %6d -> %3.2f %3.2f %3.2f %3.2f %3.2f %3.2f %3.2f", i, sample.timeStamp,
			(float)sample.VrmsR*0.01, (float)sample.VrmsS*0.01,(float)sample.VrmsT*0.01,
			(float)sample.IrmsR*0.01, (float)sample.IrmsS*0.01, (float)sample.IrmsT*0.01, (float)sample.IrmsN*0.01);
		}
	};

	//[[123, 225.05, 225.05, 225.05, 15.05, 10.08, 9.86, 10.02],]

	bool getEvents(String &event) {
		if (!canDownload) return false;

		char info[300];
		snprintf(info, 128, "{\"event\": %d, \"sampleCount\": %d, \"triggerSample\": %d, \"start\": %u%u, \"end\": %u%u, \"list\":", 
			eventCounter, sampleCount, prevSampleQty, startDate.tv_sec, startDate.tv_usec/1000, endDate.tv_sec, endDate.tv_usec/1000);
		
		event.reserve(events.length() + 300);
		event = info;
		event += "[" + events + "]}";
		return true;
	};

	uint32_t pushSamples(fastRMSData_t* samples) {
		if (!capturing) return 0;
		if (sampleCount >= maxSamples) return 0;
		if (data == nullptr) return 0;

		data[sampleCount++] = *samples;
		return 1;
	};

	uint32_t pushSamples(fastRMSData_t* samples, uint32_t count) {
		if (!capturing) return 0;
		if (data == nullptr || samples == nullptr) return 0;
		uint32_t i = 0;
		for (i = 0; i < count; i++) {
			if (sampleCount >= maxSamples) return i;
			*data++ = *samples++;
			sampleCount++;
		}
		return i;
	};

	void pushEvent(const char * event) {
		if (!capturing) return;
		debugI("Evento: %s", event);
		if (events.length() ) events += ", ";
		events += event;
	};
};

enum class eventType {
	voltageDip,
	voltageDwell,
	overCurrent
};

enum class phaseName {
	phaseR,
	phaseS,
	phaseT,
	neutral
};

class phaseEvent_t {
public:
	bool active = false;
	float value = 0.0;
	timeval start;
	timeval end;
	phaseName name = phaseName::phaseR;
	eventType type = eventType::voltageDip;

	void setProperties(phaseName phaseName, eventType eventType) {
		name = phaseName;
		type = eventType;
	};

	static const char* eventType2String(eventType event) {
		if (event == eventType::voltageDip)
			return "Dip";
		else if (event == eventType::voltageDwell)
			return "Dwell";
		return "Overcurrent";
	};

	static const char* phaseName2String(phaseName phase) {
		if (phase == phaseName::phaseR)
			return "Fase R";
		else if (phase == phaseName::phaseS)
			return "Fase S";
		else if (phase == phaseName::phaseT)
			return "Fase T";
		return "Neutro";
	};

	void inline setStatus(bool status) {
		lastActive = active;
		active = status;

		if (active && !lastActive) 	//Entrando...
			gettimeofday(&start, nullptr);
		else if (!active && lastActive) //Saliendo...
			gettimeofday(&end, nullptr);
	};

	void inline setStatus(bool status, float level) {
		setStatus(status);
		if (hasChanged() && status) value = level;	//Actualizar el valor solo cuando entra 
	};

	bool inline hasChanged() {
		return lastActive != active;
	}

	String getJson() {
		char str[128];
		timeval timeStamp;
		const char *action;
		if (active) {
			timeStamp = start;
			action = "start";
		}
		else {
			timeStamp = end;
			action = "end";
		}

		snprintf(str, 128, "{\"phase\": \"%s\", \"type\": \"%s\",\"action\": \"%s\", \"time\": %d%d, \"val\": %.2f}", 
			phaseName2String(name), eventType2String(type), action, timeStamp.tv_sec, timeStamp.tv_usec / 1000, value);
		return String(str);
	};

	void printEvent() {
		// debugI("now: %d, last:%d, changed?: %d", active, lastActive, hasChanged());
		// if (active) {
		// 	debugI("Evento %s en fase %s iniciado a %d%d, value: %.2f", eventType2String(type), phaseName2String(name), start.tv_sec, start.tv_usec / 1000, value);
		// }
		// else {
		// 	uint64_t s = (uint64_t)start.tv_sec * 1000 + start.tv_usec / 1000;
		// 	uint64_t e = (uint64_t)end.tv_sec * 1000 + end.tv_usec / 1000;
		// 	debugI("Evento %s en %s terminado a %d%d, last value: %.2f, duracion: %d ms", eventType2String(type), phaseName2String(name), end.tv_sec, end.tv_usec / 1000, value, e - s);
		// }
	};

private:
	bool lastActive = false;
};

struct phaseValues
{
	/* data */
	char Name[8];		//Nombre de fase
	float Vrms;			//Voltaje RMS
	float VVrms;		//Voltaje entre fase siguiente
	float Irms;			//Corriente RMS
	float FastVrms;		//Voltaje RMS rápida
	float FastIrms;		//Corriente RMS rápida
	float Watt;			//Potencia Activa
	float VAR;			//Potencia Reactiva
	float VA;			//Potencia Aparente
	float PowerFactor; 	//Factor de potencia
	float Vthd;			//Distorción de voltaje
	float Ithd; 		//Distorción de corriente
	float AngleVI;		//Ángulo entre corriente y voltaje
	float AngleV;		//Ángulo con la fase siguiente
	float AngleI;		//Ángulo de corriente con la fase siguiente
	float Freq;			//Frecuencia de fase 
	float Watt_H;		//Energía activa
	float VAR_H;		//Energía reactiva
	float VA_H;			//Energía aparente

	phaseEvent_t voltageDip;
	phaseEvent_t voltageSwell;
	phaseEvent_t overCurrent;

	phaseValues() {
		Vrms = Irms = Watt = VAR = VA = PowerFactor = Vthd = Ithd = AngleVI = AngleV = AngleI = 0.0;
		Name[0] = 0;
	};

	phaseValues(const char* name) {
		Vrms = Irms = Watt = VAR = VA = PowerFactor = Vthd = Ithd = AngleVI = 0.0;
		strncpy(Name, name, sizeof(Name) - 1);
		Name[7] = 0;

		//Buscar en el nombre de la fase la letra que la identifica y asignarla las propiedades a los eventos
		phaseName phase;
		for (int32_t i = (strlen(Name)-1); i >= 0; i--) {
			char f = toUpperCase(Name[i]);
			if (f == 'R') {
				phase = phaseName::phaseR;
				break;
			}
			else if (f == 'S') {
				phase = phaseName::phaseS;
				break;
			}
			else if (f == 'T') {
				phase = phaseName::phaseT;
				break;
			}
			else if (f == 'N') {
				phase = phaseName::neutral;
				break;
			}
		};
		voltageDip.setProperties(phase, eventType::voltageDip);
		voltageSwell.setProperties(phase, eventType::voltageDwell);
		overCurrent.setProperties(phase, eventType::overCurrent);
	};

	String getJson() {
		const uint32_t strSize = 1024;
		String res;
		char* str = (char*)malloc(strSize);
		if (str == nullptr) return res;

		snprintf(str, strSize, "{\"name\":\"%s\",\"vrms\":%.4f,\"vvrms\":%.4f,\"irms\":%.4f,\"fvrms\":%.4f,\"firms\":%.4f,"
			"\"freq\":%.2f,\"pf\":%.4f,\"angle\":%.2f,\"vthd\":%.2f,\"ithd\":%.2f,"
			"\"watt\":%.4f,\"var\":%.4f,\"va\":%.4f,"
			"\"watth\":%.4f,\"varh\":%.4f,\"vah\":%.4f}",
			Name, Vrms, VVrms, Irms, FastVrms, FastIrms,
			Freq, PowerFactor, AngleVI, Vthd, Ithd,
			Watt, VAR, VA,
			Watt_H, VAR_H, VA_H);
		res = str;
		free(str);
		return res;
	};
};

struct meterValues
{
	phaseValues phaseR;
	phaseValues phaseS;
	phaseValues phaseT;
	phaseValues neutral;
	struct {
		float Watt;
		float VAR;
		float VA;
	} power;

	struct {
		double Watt_H;
		double VAR_H;
		double VA_H;
	} energy;
	struct {
		float Irms;			//Voltaje RMS promedio de todas las fases
		float Vrms;			//Corriente RMS promedio de todas las fases
		float VVrms;			//Corriente RMS promedio de todas las fases
		float FastVrms;		//Voltaje RMS rápida promedio de todas las fases
		float FastIrms;		//Corriente RMS rápida promedio de todas las fases
		float Watt;			//Potencia Activa promedio de todas las fases
		float VAR;			//Potencia Reactiva promedio de todas las fases
		float VA;			//Potencia Aparente promedio de todas las fases
		float PowerFactor;	//Factor de potencia promedio de todas las fases
		float Vthd;			//Distorción de voltaje promedio de todas las fases
		float Ithd; 		//Distorción de corriente promedio de todas las fases
		float Freq;			//Frecuencia de fase promedio de todas las fases
	} average;

	ADE_OISTATUS_t currentEvents;		//Estado de los eventos de corriente
	ADE_EVENT_STATUS_t voltageEvents;	//Estado de los eventos de voltaje


	meterValues() : phaseR("FASE R"), phaseS("FASE S"), phaseT("FASE T"), neutral("NEUTRO") {
		power.Watt = power.VAR = power.VA = 0;
	};

	bool isVoltageEventsActive() {
		return voltageEvents.DIPA || voltageEvents.DIPB || voltageEvents.DIPC || voltageEvents.SWELLA || voltageEvents.SWELLB || voltageEvents.SWELLC;
	};

	bool isCurrentEventsActive() {
		return currentEvents.OIPHASE;
	};

	String getJson() {
		const uint32_t strSize = 1024;
		String res;
		char* str = (char*)malloc(strSize);
		if (str == nullptr) return res;

		snprintf(str, strSize, "{\"vrms\":%.3f,\"irms\":%.3f,\"fvrms\":%.3f,\"firms\":%.3f,"
			"\"freq\":%.2f,\"pf\":%.4f,\"vthd\":%.2f,\"ithd\":%.2f,"
			"\"watt\":%.3f,\"var\":%.3f,\"va\":%.3f,"
			"\"watth\":%.3f,\"varh\":%.3f,\"vah\":%.3f}",
			average.Vrms, average.Irms, average.FastVrms, average.FastIrms,
			average.Freq, average.PowerFactor, average.Vthd, average.Ithd,
			power.Watt, power.VAR, power.VA,
			energy.Watt_H, energy.VAR_H, energy.VA_H);
		res = str;
		free(str);
		return res;
	}

	void getJsonBasic(String& dest) {
		const uint32_t strSize = 1280;
		char* str = (char*)malloc(strSize);
		if (str == nullptr) {
			dest = "{}";
			return;
		};

		snprintf(str, strSize, "{\"vrms\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"n\":%.3f,\"avg\":%.3f},"
			"\"vvrms\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"avg\":%.3f},"
			"\"irms\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"n\":%.3f,\"avg\":%.3f},"
			"\"watt\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"avg\":%.3f,\"tot\":%.3f},"
			"\"var\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"avg\":%.3f,\"tot\":%.3f},"
			"\"va\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"avg\":%.3f,\"tot\":%.3f},"
			"\"wattH\":%.3f,\"varH\":%.3f,\"vaH\":%.3f,\"freq\":%.2f}",
			phaseR.Vrms, phaseS.Vrms, phaseT.Vrms, neutral.Vrms, average.Vrms,
			phaseR.VVrms, phaseS.VVrms, phaseT.VVrms, average.VVrms,
			phaseR.Irms, phaseS.Irms, phaseT.Irms, neutral.Irms, average.Irms,
			phaseR.Watt, phaseS.Watt, phaseT.Watt, average.Watt, power.Watt,
			phaseR.VAR, phaseS.VAR, phaseT.VAR, average.VAR, power.VAR,
			phaseR.VA, phaseS.VA, phaseT.VA, average.VA, power.VA,
			energy.Watt_H, energy.VAR_H, energy.VA_H, average.Freq);
		dest = str;
		free(str);
	}

	void getJsonFastMeasures(String& dest) {
		char str[128];
		snprintf(str, sizeof(str), "{\"fastV\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"n\":%.3f},"
			"\"fastI\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"n\":%.3f}}",
			phaseR.FastVrms, phaseS.FastVrms, phaseT.FastVrms, neutral.Vrms,
			phaseR.FastIrms, phaseS.FastIrms, phaseT.FastIrms, neutral.FastIrms);
		dest = str;
	}

	void getJsonEnergy(String& dest) {
		char str[256];
		snprintf(str, sizeof(str), "{\"wattH\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"tot\":%.3f},"
			"\"varH\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"tot\":%.3f},"
			"\"vaH\":{\"r\":%.3f,\"s\":%.3f,\"t\":%.3f,\"tot\":%.3f}}",
			phaseR.Watt_H, phaseS.Watt_H, phaseT.Watt_H, energy.Watt_H,
			phaseR.VAR_H, phaseS.VAR_H, phaseT.VAR_H, energy.VAR_H,
			phaseR.VA_H, phaseS.VA_H, phaseT.VA_H, energy.VA_H);
		dest = str;
	}

	void getJsonAngles(String& dest) {
		char str[256];
		snprintf(str, sizeof(str), "{\"voltage\":{\"r\":%.2f,\"s\":%.2f,\"t\":%.2f},"
			"\"vi\":{\"r\":%.2f,\"s\":%.2f,\"t\":%.2f},"
			"\"current\":{\"r\":%.2f,\"s\":%.2f,\"t\":%.2f}}",
			phaseR.AngleV, phaseS.AngleV, phaseT.AngleV,
			phaseR.AngleVI, phaseS.AngleVI, phaseT.AngleVI,
			phaseR.AngleI, phaseS.AngleI, phaseT.AngleI);
		dest = str;
	}

	void getJsonTHD(String& dest) {
		char str[256];
		snprintf(str, sizeof(str), "{\"voltageTHD\":{\"r\":%.2f,\"s\":%.2f,\"t\":%.2f},"
			"\"currentTHD\":{\"r\":%.2f,\"s\":%.2f,\"t\":%.2f}}",
			phaseR.Vthd, phaseS.Vthd, phaseT.Vthd,
			phaseR.Ithd, phaseS.Ithd, phaseT.Ithd);
		dest = str;
	}
};

typedef struct {
	float voltageScale;
	float currentScale;
	int32_t sampleFreq;

	String toString() {
		return String();
	};
} scopeInfo_t;


void MeterInit();


void MeterLoadOptions();

void MeterLoop();

void MeterTask(void* arg);

void readWaveBuffer();

void scaleBuffer(WFBFixedDataRate_t* samplesBuffer, int32_t samplesCount, float voltage, float current);

void compressWaveBuffer12(int32_t* waveBufferRaw, uint8_t* waveBuffer, uint16_t part);


void scopeWSevents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);


void acEventsWSevents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);


void meterCheckEvents();

void meterFastDataSamplePush();

uint16_t meterFastDataSampleGetIndex();


fastRMSData_t *meterGetFastDataSample(uint32_t sample);

void meterReadWaveBuffer();

void meterReadRMS();

void meterReadHalfRMS();

void meterRead1012RMS();

void meterReadPower();

void meterReadTHD();

void meterReadPowerFactor();

void meterReadPeriods();

void meterReadAngles();

void meterReadEnergy();

ADE_EVENT_STATUS_t meterReadDipSwell();

ADE_OISTATUS_t meterReadOverCurrent();

float sumVoltages(float v1, float deg1, float v2, float deg2);


extern ADE9000 ade;

extern meterValues Meter;