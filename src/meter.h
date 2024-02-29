#include "ade9000/ADE9000.h"
#include "AsyncWebSocket.h"
#include "time.h"

typedef struct {
	bool active;
	float value;
	timeval start;
	timeval end;

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
		value = level;
	};

	bool inline hasChanged() {
		return lastActive != active;
	}

	void printEvent(const char* Event, const char* phase) {
		if (active) {
			Serial.printf("Evento %s en fase %s iniciado a %d.%d, value: %.2f\n", Event, phase, start.tv_sec, start.tv_usec / 1000, value);
		}
		else {
			uint64_t s = (uint64_t)start.tv_sec * 1000 + start.tv_usec / 1000;
			uint64_t e = (uint64_t)end.tv_sec * 1000 + end.tv_usec / 1000;
			Serial.printf("Evento %s en fase %s terminado a %d.%d, last value: %.2f, duracion: %d ms\n", Event, phase, end.tv_sec, end.tv_usec / 1000, value, e - s);
		}
	}

private:
	bool lastActive;
} phaseEvent_t;

struct phaseValues
{
	/* data */
	char Name[8];		//Nombre de fase
	float Vrms;			//Voltaje RMS
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
	float Freq;			//Frecuencia de fase 
	float Watt_H;		//Energía activa
	float VAR_H;		//Energía reactiva
	float VA_H;			//Energía aparente

	phaseValues() {
		Vrms = Irms = Watt = VAR = VA = PowerFactor = Vthd = Ithd = AngleVI = 0.0;
		Name[0] = 0;
	};

	phaseValues(const char* name) {
		Vrms = Irms = Watt = VAR = VA = PowerFactor = Vthd = Ithd = AngleVI = 0.0;
		strncpy(Name, name, 7);
		Name[7] = 0;
	};

	phaseEvent_t voltageDip;
	phaseEvent_t voltageSwell;
	phaseEvent_t overCurrent;

	String getJson() {
		const uint32_t strSize = 1024;
		String res;
		char* str = (char*)malloc(strSize);
		if (str == nullptr) return res;
		//"{\"Zero\":%.0f, \"Conversion\":%.0f, \"Weight\":%.3f, \"CalTemp\": %.2f, \"Date\": %d}"

		snprintf(str, strSize, "{\"name\":\"%s\",\"vrms\":%.4f,\"irms\":%.4f,\"fvrms\":%.4f,\"firms\":%.4f,"
			"\"freq\":%.2f,\"pf\":%.4f,\"angle\":%.2f,\"vthd\":%.2f,\"ithd\":%.2f,"
			"\"watt\":%.4f,\"var\":%.4f,\"va\":%.4f,"
			"\"watth\":%.4f,\"varh\":%.4f,\"vah\":%.4f}",
			Name, Vrms, Irms, FastVrms, FastIrms,
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

	meterValues() : phaseR("FASE R"), phaseS("FASE S"), phaseT("FASE T"), neutral("NEUTRO") {
		power.Watt = power.VAR = power.VA = 0;
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

void MeterInitScope();

void MeterLoop();

void MeterTask(void* arg);

void readWaveBuffer();

void scaleBuffer(WFBFixedDataRate_t* samplesBuffer, int32_t samplesCount, float voltage, float current);

void compressWaveBuffer12(int32_t* waveBufferRaw, uint8_t* waveBuffer, uint16_t part);


void scopeWSevents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len);


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

void meterReadDipSwell();

void meterReadOverCurrent();


extern ADE9000 ade;

extern meterValues Meter;