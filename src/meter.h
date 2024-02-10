#include "ade9000/ADE9000.h"

struct phaseValues
{
	/* data */
	char Name[8];		//Nombre de fase
	float Vrms;			//Voltaje RMS
	float Irms;			//Corriente RMS
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

	meterValues() : phaseR("Fase R"), phaseS("Fase S"), phaseT("Fase T"), neutral("Neutro") {
		power.Watt = power.VAR = power.VA = 0;
	};
};


void MeterInit();

void MeterLoop();



extern ADE9000 ade;

extern meterValues Meter;