#include "ade9000/ADE9000.h"
#include "AsyncWebSocket.h"

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



void meterReadRMS();

void meterReadHalfRMS();

void meterRead1012RMS();

void meterReadPower();

void meterReadTHD();

void meterReadPowerFactor();

void meterReadPeriods();

void meterReadAngles();

void meterReadEnergy();

void meterReadDip();

void meterReadSwell();

void meterReadOverCurrent();


extern ADE9000 ade;

extern meterValues Meter;