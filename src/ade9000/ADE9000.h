/*
  ADE9000API.h - Library based in ADE9000 - Energy and PQ monitoring AFE
  This library have with base the library of nchandra development by Analog Devices
  for compatibility at ESP32
  Author: Ariel Rios
  Date: 12-07-2020
*/
#pragma ONCE
#ifndef ADE9000_H
#define ADE9000_H 1

/****************************************************************************************************************
 Includes
***************************************************************************************************************/

#include "Arduino.h"
#include "Preferences.h"
#include "ADE9000RegMap.h"
#include <SPI.h>

/****************************************************************************************************************
									User Inputs
******************************************************************************************************************/
#ifndef DEBUG_MODE_OFF
#define DEBUG_MODE
#define PRINT_DEBUG(x) (Serial.print(x))
#endif

extern const uint8_t NOMINAL_INPUT_VOLTAGE;
extern const float NOMINAL_INPUT_CURRENT;				/* Current nominal RMS for Input in Amperes */
extern const uint8_t INPUT_FREQUENCY; 					/* Frequency in Hertz */
extern const uint8_t CALIBRATION_ANGLE_DEGREES; 		/* Used for funtion transform current in Ohm */
extern const float BURDEN_RESISTOR;						/* Current nominal RMS for Input in Amperes */
extern const uint16_t TURNS_RATIO_TRANSFORMER;			/* Used for funtion transform current */
extern const float ATTEUNATION_FACTOR;					/* The defaul atteunation factor on board used funtion transform in Ohm/Ohm ((R1 + R2)/ R2) */
extern const uint8_t ACCUMULATION_TIME;					/* Accumulation time in seconds when EGY_TIME=7999, accumulation mode= sample based */


/*Transfer function*/
/****************************************************************************************************************
									Current Transfer Function
*****************************************************************************************************************
It is the voltage at the ADC input pins per input current(A) (Volts/Amp)
E.g: For a current output current transformer with Turns Ratio of 2500:1. Burden resistor on board = 5.1*2=10.2 Ohm.
Current transfer function= 1/2500*Total Burden = 1/2500*10.2= 0.00408
****************************************************************************************************************/
/****************************************************************************************************************
									Voltage Transfer Function
*****************************************************************************************************************
It is the voltage at the ADC input pins per input Voltage(V)(Volts/Volts)
E.g. The defaul atteunation factor on board is 801.
Voltage transfer function = 1/801= 0.001248 ~=0.00125
****************************************************************************************************************/
#define CURRENT_TRANSFER_FUNCTION 1.0 / TURNS_RATIO_TRANSFORMER * BURDEN_RESISTOR * (double)ADE9000_CURRENT_PGA_GAIN //The RMS voltage at the ADC input pins per input RMS current  (V/A).(2500:1-->0.00408 with default burden resistors)
#define VOLTAGE_TRANSFER_FUNCTION 1.0 / ATTEUNATION_FACTOR                       //The RMS voltage at the ADC input pins per input RMS voltage (V/V)

/****************************************************************************************************************
									Constants: Do not change
*****************************************************************************************************************/
#define CAL_ANGLE_RADIANS(x) (x * 3.14159 / 180)
#define ONE_MILLION 1000000UL
#define ONE_THOUSAND 1000
#define SQRT_TO_2 1.41421

/*Full scale Codes referred from Datasheet.Respective digital codes are produced when ADC inputs are at full scale. Donot Change. */
#define ADE9000_RMS_FULL_SCALE_CODES 52702092
#define ADE9000_WATT_FULL_SCALE_CODES 20694066
#define ADE9000_RESAMPLED_FULL_SCALE_CODES 18196
#define ADE9000_PCF_FULL_SCALE_CODES 74532013
#define ADE9000_2to27	134217728
#define ADE9000_2to15	32768
/****************************************************************************************************************
									Conversion Constants
*****************************************************************************************************************
If calibration is done with the the calibration sketch, the following conversion constants apply:
If CURRENT_TRANSFER_FUNCTION or VOLTAGE_TRANSFER_FUNCTION are changed, update the conversion constants

CAL_IRMS_CC in uA/code 				= 10^6/(CURRENT_TRANSFER_FUNCTION*ADE9000_RMS_FULL_SCALE_CODES*SQRT(2))
CAL_VRMS_CC in uV/code 				= 10^6/(VOLTAGE_TRANSFER_FUNCTION*ADE9000_RMS_FULL_SCALE_CODES*SQRT(2))
CAL_POWER_CC in mW/code 			= 10^3/(CURRENT_TRANSFER_FUNCTION*VOLTAGE_TRANSFER_FUNCTION*ADE9000_WATT_FULL_SCALE_CODES*2)
CAL_ENERGY_CC in uWhr/xTHR_HI code 	= 10^6/(CURRENT_TRANSFER_FUNCTION*VOLTAGE_TRANSFER_FUNCTION*ADE9000_WATT_FULL_SCALE_CODES*2*8000*3600*2^-13)

If conversion constants are set correctly, the register codes are converted to physical parameters as:
e.g Channel A Vrms = (AVRMS(register)*CAL_VRMS_CC/10^6) Channel A Active Power = (AWATT(register)*CAL_POWER_CC/10^3)
*****************************************************************************************************************/
#define CAL_IRMS_CC (double)ONE_MILLION / (CURRENT_TRANSFER_FUNCTION * ADE9000_RMS_FULL_SCALE_CODES * SQRT_TO_2)                                                       // Conversion constants (uA/code)
#define CAL_VRMS_CC (double)ONE_MILLION / (VOLTAGE_TRANSFER_FUNCTION * ADE9000_RMS_FULL_SCALE_CODES * SQRT_TO_2)                                                       // Conversion constants (uV/code)
#define CAL_POWER_CC (double)ONE_THOUSAND / (VOLTAGE_TRANSFER_FUNCTION * CURRENT_TRANSFER_FUNCTION * ADE9000_WATT_FULL_SCALE_CODES * 2)                                // Conversion constants (mW/code) Applicable for Active, reactive and apparent power
#define CAL_ENERGY_CC (double)ONE_MILLION / (VOLTAGE_TRANSFER_FUNCTION * CURRENT_TRANSFER_FUNCTION * ADE9000_WATT_FULL_SCALE_CODES * 2 * 8000 * 3600 * (1.0 / 8192.0)) // Conversion constants (uWhr/xTHR_HI code)Applicable for Active, reactive and apparent energy
#define CAL_I_PCF (double)(CURRENT_TRANSFER_FUNCTION * ADE9000_PCF_FULL_SCALE_CODES)                       // Conversion constants (A/code) for sample buffer with current PCF data
#define CAL_V_PCF (double)(VOLTAGE_TRANSFER_FUNCTION * ADE9000_PCF_FULL_SCALE_CODES)                       // Conversion constants (V/code) for sample buffer with voltage PCF data


/****************************************************************************************************************
 Current PGA gain. Uncomment the needed one
****************************************************************************************************************/
// #define ADE9000_CURRENT_PGA_GAIN 0x1	//PGA gain of x1
#define ADE9000_CURRENT_PGA_GAIN 0x2	//PGA gain of x2
// #define ADE9000_CURRENT_PGA_GAIN 0x4	//PGA gain of x4

//PGA GAIN settings value. 
#define ADE9000_CURRENT_PGA ((ADE9000_CURRENT_PGA_GAIN<6)|(ADE9000_CURRENT_PGA_GAIN<4)|(ADE9000_CURRENT_PGA_GAIN<2) | ADE9000_CURRENT_PGA_GAIN)
/****************************************************************************************************************
 Definitions
****************************************************************************************************************/
/*Configuration registers*/
#define ADE9000_PGA_GAIN1 0x0000 /* PGA1@0x0000. Gain of all channels=1 */
#define ADE9000_PGA_GAIN2 0x1555 /* PGA2@0x1555. Gain of all channels=2 */
#define ADE9000_PGA_GAIN4 0x3FFF /* PGA4@0x3FFF. Gain of all channels=4 */


#define ADE9000_PGA_GAIN ADE9000_CURRENT_PGA /* PGA1@0x0000. Gain of all channels=1 */

#define ADE9000_CONFIG0 0x00000000    /* Integrator disabled */
#define ADE9000_CONFIG1 0x0002        /* CF3/ZX pin outputs Zero crossing */
#define ADE9000_CONFIG2 0x0C00        /* Default High pass corner frequency of 1.25Hz */
#define ADE9000_CONFIG3 0x0000        /* Peak and overcurrent detection disabled */
#define ADE9000_ACCMODE 0x0000        /* 50Hz operation, 3P4W Wye configuration, signed accumulation Clear bit 8 i.e. ACCMODE=0x00xx for 50Hz operation ACCMODE=0x0x9x for 3Wire delta when phase B is used as reference */
#define ADE9000_TEMP_CFG 0x000C       /* Temperature sensor enabled */
#define ADE9000_ZX_LP_SEL 0x001E      /* Line period and zero crossing obtained from combined signals VA,VB and VC */
#define ADE9000_MASK0 0x00020001      /* Enable EGYRDY interrupt, PAGE_FULL=1 Set this bit to enable an interrupt when a page enabled in the WFB_PG_IRQEN register is filled.*/
#define ADE9000_MASK1 0x00000000      /* MASK1 interrupts disabled*/
#define ADE9000_EVENT_MASK 0x00000000 /* Events disabled */
#define ADE9000_VLEVEL 0x0022EA28     /* Assuming Vnom=1/2 of full scale. Refer Technical reference manual for detailed calculations.*/
#define ADE9000_DICOEFF 0x00000000    /* Set DICOEFF= 0xFFFFE000 when integrator is enabled */
#define ADE9000_CFMODE	0x00000007	  /* Setup CF1 pin function. CF1: Total active power */
#define ADE9000_COMPMODE 0x00000007	  /* Phases to include in CF1 pulse output: ABC*/

/*Constant Definitions***/
#define ADE9000_FDSP 8000     /* Signal update Rate ADE9000 FDSP: 8000sps */
#define ADE9000_RUN_ON 0x0001 /* DSP ON */

/*Energy Accumulation Settings*/
#define ADE9000_EP_CFG 0x0011   /* Enable energy accumulation, accumulate samples at 8ksps */
								/* latch energy accumulation after EGYRDY */
								/* If accumulation is changed to half line cycle mode, change EGY_TIME */

//#define ADE9000_EP_CFG 0x0021   /* Enable energy accumulation, accumulate samples at 8ksps */
								/* add energy accumulation after EGYRDY */
								/* reset registers after reading */

#define ADE9000_EGY_TIME 0x1F3F /* Accumulate 8000 samples */

#define EGY_INTERRUPT_MASK0 0x00000001 //Enable EGYRDY interrupt


/*Waveform buffer Settings*/
#define ADE9000_WFB_CFG 0x1000     /* Neutral current samples enabled, Resampled data enabled */
								   /* Burst all channels */

#define WFB_ELEMENT_ARRAY_SIZE 512 /*size of buffer to read. 512 Max.Each element IA,VA...IN has max 512 points */
								   /*[Size of waveform buffer/number of sample sets = 2048/4 = 512] */
								   /*(Refer ADE9000 technical reference manual for more details) */
#define WFB_SAMPLES_PER_CHANNEL (2048/8) /*Number of samples to read from buffer for each channel */

/* State for calibration*/
typedef enum
{
	calNone = 0,
	calCurrentGain = 1,
	calPhaseGain,
	calVoltageGain,
	calCurrentOffset,
	calVoltageOffset,
	calPowerGain,
	calActivePowerOffset,
	calReactivePowerOffset,
	calFundActivePowerOffset,
	calFundReactivePowerOffset,
	calFundCurrentOffset,
	calFundVoltageOffset,
	calCurrentOneOffset,
	calVoltageOneOffset,
	calCurrentTenOffset,
	calVoltageTenOffset,
	calLastItem
}calibrationStep_t;

inline calibrationStep_t operator++ (calibrationStep_t& step, int32_t) {
	step = static_cast<calibrationStep_t>(static_cast<int>(step) + 1);
	if (step < calNone) step = calNone;
	else if (step >= calLastItem) step = static_cast<calibrationStep_t>(static_cast<int>(calLastItem) - 1);
	return step;
}

inline calibrationStep_t operator-- (calibrationStep_t& step, int32_t) {
	step = static_cast<calibrationStep_t>(static_cast<int>(step) - 1);
	if (step < calNone) step = calNone;
	else if (step >= calLastItem) step = static_cast<calibrationStep_t>(static_cast<int>(calLastItem) - 1);
	return step;
}

inline const char* calibrationStepString(calibrationStep_t& step)
{
	switch (step) {
	case calNone: return "none";
	case calCurrentGain: return "Ganancia Corriente";
	case calPhaseGain: return "Ajuste de fase";
	case calVoltageGain: return "Ganancia Voltaje";
	case calCurrentOffset: return "Offset Corriente";
	case calVoltageOffset: return "Offset Voltaje";
	case calPowerGain: return "Ganancia potencia";
	case calActivePowerOffset: return "Offset potencia activa";
	case calReactivePowerOffset: return "Offset potencia reactiva";
	case calFundActivePowerOffset: return "Offset potencia act fund";
	case calFundReactivePowerOffset: return "Offset potencia react fund";
	case calFundCurrentOffset: return "Offset Corriente fund";
	case calFundVoltageOffset: return "Offset Voltaje fund";
	case calCurrentOneOffset: return "Offset Corriente 1 ciclo";
	case calVoltageOneOffset: return "Offset Voltaje 1 ciclo";
	case calCurrentTenOffset: return "Offset Corriente 10 ciclo";
	case calVoltageTenOffset: return "Offset Voltaje 10 ciclo";
	}
	return "desconocido";
}

typedef enum {
	adeChannel_IA = 0,
	adeChannel_IB = 1,
	adeChannel_IC = 2,
	adeChannel_IN = 3,
	adeChannel_VA = 4,
	adeChannel_VB = 5,
	adeChannel_VC = 6,
	adeChannel_Default = 7,
} adeChannel;



#define formatNoPrefix		0x10000		//No cambia la escala del valor
#define formatAddZeros		0x20000		//Agrega 0 de relleno a la izquierda
#define formatRemoveSpaces	0x40000		//No agrega espacios para completar el ancho indicado

/****************************************************************************************************************
 Structures and Global Variables
****************************************************************************************************************/

/* Arrays of resampled waveform structure for saved data codes */
typedef union
{
	int32_t buffer[7];
	struct {
		int32_t IA;
		int32_t VA;
		int32_t IB;
		int32_t VB;
		int32_t IC;
		int32_t VC;
		int32_t IN;
	};
} WFBFixedDataRate_t;

/* Active Power structure for saved data codes */
struct ActivePowerRegs
{
	int32_t ActivePowerReg_A;
	int32_t ActivePowerReg_B;
	int32_t ActivePowerReg_C;
	float ActivePower_A;
	float ActivePower_B;
	float ActivePower_C;
};

/* Reactive Power structure for saved data codes */
struct ReactivePowerRegs
{
	int32_t ReactivePowerReg_A;
	int32_t ReactivePowerReg_B;
	int32_t ReactivePowerReg_C;
	float ReactivePower_A;
	float ReactivePower_B;
	float ReactivePower_C;
};

/* Apparent Power structure for saved data codes */
struct ApparentPowerRegs
{
	int32_t ApparentPowerReg_A;
	int32_t ApparentPowerReg_B;
	int32_t ApparentPowerReg_C;
	float ApparentPower_A;
	float ApparentPower_B;
	float ApparentPower_C;
};

/* Voltage structure for saved data codes */
struct VoltageRMSRegs
{
	int32_t VoltageRMSReg_A;
	int32_t VoltageRMSReg_B;
	int32_t VoltageRMSReg_C;
	float VoltageRMS_A;
	float VoltageRMS_B;
	float VoltageRMS_C;

	VoltageRMSRegs() {
		VoltageRMSReg_A = VoltageRMSReg_B = VoltageRMSReg_C = 0;
		VoltageRMS_A = VoltageRMS_B = VoltageRMS_C = 0.0;
	};
};

/* Current structure for saved data codes */
struct CurrentRMSRegs
{
	int32_t CurrentRMSReg_A;
	int32_t CurrentRMSReg_B;
	int32_t CurrentRMSReg_C;
	int32_t CurrentRMSReg_N;
	float CurrentRMS_A;
	float CurrentRMS_B;
	float CurrentRMS_C;
	float CurrentRMS_N;

	CurrentRMSRegs() {
		CurrentRMSReg_A = CurrentRMSReg_B = CurrentRMSReg_C = CurrentRMSReg_N = 0;
		CurrentRMS_A = CurrentRMS_B = CurrentRMS_C = CurrentRMS_N = 0.0;
	};
};

/* PGA Gain structure for saved voltage and current gain */
struct PGAGainRegs
{
	int8_t VoltagePGA_gain;
	int8_t CurrentPGA_gain;
};

struct VoltageTHDRegs
{
	int32_t VoltageTHDReg_A;
	int32_t VoltageTHDReg_B;
	int32_t VoltageTHDReg_C;
	float VoltageTHDValue_A;
	float VoltageTHDValue_B;
	float VoltageTHDValue_C;
};

struct CurrentTHDRegs
{
	int32_t CurrentTHDReg_A;
	int32_t CurrentTHDReg_B;
	int32_t CurrentTHDReg_C;
	float CurrentTHDValue_A;
	float CurrentTHDValue_B;
	float CurrentTHDValue_C;
};

/* Power factor structure for saved data codes */
struct PowerFactorRegs
{
	int32_t PowerFactorReg_A;
	int32_t PowerFactorReg_B;
	int32_t PowerFactorReg_C;
	float PowerFactorValue_A;
	float PowerFactorValue_B;
	float PowerFactorValue_C;
};

/* Period structure for saved data codes */
struct PeriodRegs
{
	int32_t PeriodReg_A;
	int32_t PeriodReg_B;
	int32_t PeriodReg_C;
	float FrequencyValue_A;
	float FrequencyValue_B;
	float FrequencyValue_C;
};

/* Angle structure for saved data codes and values */
struct AngleRegs
{
	int16_t AngleReg_VA_VB;
	int16_t AngleReg_VB_VC;
	int16_t AngleReg_VA_VC;
	int16_t AngleReg_VA_IA;
	int16_t AngleReg_VB_IB;
	int16_t AngleReg_VC_IC;
	int16_t AngleReg_IA_IB;
	int16_t AngleReg_IB_IC;
	int16_t AngleReg_IA_IC;
	float AngleValue_VA_VB;
	float AngleValue_VB_VC;
	float AngleValue_VA_VC;
	float AngleValue_VA_IA;
	float AngleValue_VB_IB;
	float AngleValue_VC_IC;
	float AngleValue_IA_IB;
	float AngleValue_IB_IC;
	float AngleValue_IA_IC;
};

/* Temperature structure for data codes */
struct TemperatureRegnValue
{
	int16_t Temperature_Reg;
	float Temperature;
};

struct EnergyPhaseVals
{
	double Watt_H;
	double VAR_H;
	double VA_H;

	EnergyPhaseVals() {
		Watt_H = VAR_H = VA_H = 0.0;
	};
};

struct TotalEnergyVals
{
	struct EnergyPhaseVals PhaseR;
	struct EnergyPhaseVals PhaseS;
	struct EnergyPhaseVals PhaseT;
};


class calibrationInfo {
	friend class ADE9000;

public:
	calibrationStep_t function;		//Funcion de calibración
	const char* unit;				//Unidad
	double conversionFactor;		//Factor de conversion entre valor RAW del ADC y la unidad de medición
	double multiplier;				//Factor de escala para las conversiones
	double realValue;				//Valor real que se está midiendo
	int32_t samples;				//Cantidad de muestras promediadas
	bool calA, calB, calC, calN;	//Fases que se van a calibrar
	struct {
		double A;					//Valor leido después de la conversion
		double B;
		double C;
		double N;
	} values;
	struct {
		int32_t A;					//Valores leidos de los registros 
		int32_t B;
		int32_t C;
		int32_t N;
	} regs;
	struct {						//Acumuladores para promedio
		int64_t A;
		int64_t B;
		int64_t C;
		int64_t N;
	} acc;

	bool inline isCalibratingVoltage() {
		return function == calVoltageGain || function == calVoltageOffset || function == calFundVoltageOffset || function == calVoltageOneOffset || function == calVoltageTenOffset;
	}

	bool inline isCalibratingCurrent() {
		return function == calCurrentGain || function == calCurrentOffset || function == calFundCurrentOffset || function == calCurrentOneOffset || function == calCurrentTenOffset;
	}

	bool inline isCalibratingPower() {
		return function == calPowerGain || function == calActivePowerOffset || function == calReactivePowerOffset || function == calFundActivePowerOffset || function == calFundReactivePowerOffset;
	}

	bool inline isCalibratingPhase() {
		return function == calPhaseGain;
	}

protected:
	double calcPhaseError(double watt, double var, double angle) {
		double sinAngle = sin(radians(angle));
		double cosAngle = cos(radians(angle));

		double errorAngle = (watt * sinAngle - var * cosAngle) / (watt * cosAngle + var * sinAngle);
		errorAngle = -degrees(atan(errorAngle));	//Calcular desfasaje

		Serial.printf("Valor real:%.3f°, medido: %.3f°\n", angle, errorAngle);
		return errorAngle;
	};

	int32_t calcPhaseErrorReg(double angle, double errorAngle) {
		const double omega = (float)2 * (float)3.14159 * (float)INPUT_FREQUENCY / (float)ADE9000_FDSP;
		int32_t factor = ((sin(radians(errorAngle) - omega) + sin(omega)) / (sin(2 * omega - radians(errorAngle)))) * 134217728; //2^27
		Serial.printf("Valor real : % .3f°, medido : % .3f°->Value : 0x % x\n", angle, errorAngle, factor);
		return factor;
	};

	void loadRegs(int32_t a, int32_t b, int32_t c, int32_t n) {
		regs.A = a;
		regs.B = b;
		regs.C = c;
		regs.N = n;
	};

	int32_t getExpectedRegisterValue() {
		if (isCalibratingPhase()) return 1.0;
		return realValue * multiplier / conversionFactor;
	};

	void calculateValues() {
		if (isCalibratingPhase()) return;
		values.A = regs.A * conversionFactor / multiplier;
		values.B = regs.B * conversionFactor / multiplier;
		values.C = regs.C * conversionFactor / multiplier;
		values.N = regs.N * conversionFactor / multiplier;
	};

	void calculatePromAcc() {
		if (samples==0) samples = 1;
		acc.A = acc.A / samples;
		acc.B = acc.B / samples;
		acc.C = acc.C / samples;
		acc.N = acc.N / samples;
	};

	void accumulateRegs() {
		if (calA) acc.A += (int64_t)regs.A;
		if (calB) acc.B += (int64_t)regs.B;
		if (calC) acc.C += (int64_t)regs.C;
		if (calN) acc.N += (int64_t)regs.N;
	};

	void clearAccumulators() {
		acc.A = acc.B = acc.C = acc.N = 0;
	}

};

typedef struct {
	float angle;
	int32_t factor;
} calibratePhaseResult;


class ADE9000
{
public:
	/*
	Constructor for ADE9000 object.
	*/
	ADE9000(uint32_t SPI_speed = 4000000UL, uint8_t chipSelect_Pin = 5);

	ADE9000(uint32_t SPI_speed, uint8_t chipSelect_Pin, uint8_t SPIport);

	/*
	Initial the pins controller and SPI of the ADE9000
	Input: Status of SPI of another device initializated.
	Output:-
	*/
	void initADE9000(bool initSPI = false);
	void initADE9000(uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin);

	String format(float value, uint32_t width, const char* unit, uint32_t minDec = 0);
	/*
	Initializes the ADE9000. The initial settings for registers are defined in ADE9000API.h header file
	Input: Register settings in header files
	Output:-
	*/
	void setupADE9000(void);

	/*
	Reset the ADE9000. The reset ADE9000 for init mode.
	Input: Select pin for reset the ADE9000 connect to ESP32.
	Output:-
	*/
	void resetADE9000(uint8_t);

	/*----- SPI Functions -----*/

	/*
	Initializes the arduino SPI port using SPI.h library
	Input: SPI speed, chip select pin
	Output:-
	*/
	void SPI_Init();
	void SPI_Init(uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin);

	/*
	Writes 16bit data to a 16 bit register.
	Input: Register address, data
	Output:-
	*/
	void SPI_Write_16(uint16_t Address, uint16_t Data);

	/*
	Writes 32bit data to a 32 bit register.
	Input: Register address, data
	Output:-
	*/
	void SPI_Write_32(uint16_t Address, uint32_t Data);

	/*
	Reads 16bit data from register.
	Input: Register address
	Output: 16 bit data
	*/
	uint16_t SPI_Read_16(uint16_t Address);

	/*
	Reads 32bit data from register.
	Input: Register address
	Output: 32 bit data
	*/
	uint32_t SPI_Read_32(uint16_t Address);


	/*
	Description: Burst reads the content of waveform buffer. This function only works with resampled data. Configure waveform buffer to have Resampled data, and burst enabled (BURST_CHAN=0000 in WFB_CFG Register).
	Input: The starting address. Use the starting address of a data set. e.g 0x800, 0x804 etc to avoid data going into incorrect arrays.
		   Read_Element_Length is the number of data sets to read. If the starting address is 0x800, the maximum sets to read are 512.
	Output: Resampled data returned in structure
	*/
	void SPI_Burst_Read_FixedDT_Buffer(uint16_t bufferPos, uint16_t samplesCount, WFBFixedDataRate_t* samplesBuffer);

	/*----- ADE9000 Calculated Parameter Read Functions -----*/

	/*
	Reads the Active power registers AWATT,BWATT and CWATT
	Input: Structure name
	Output: Active power codes stored in respective structure
	*/
	uint32_t readActivePowerRegs(ActivePowerRegs* Data);

	/*
	Reads the Reactive power registers AVAR,BVAR and CVAR
	Input: Structure name
	Output: Reactive power codes stored in respective structure
	*/
	uint32_t readReactivePowerRegs(ReactivePowerRegs* Data);

	/*
	Reads the Apparent power registers AVA,BVA and CVA
	Input: Structure name
	Output: Apparent power codes stored in respective structure
	*/
	uint32_t readApparentPowerRegs(ApparentPowerRegs* Data);

	/*
	Reads the voltage rms registers AVRMS,BVRMS and CVRMS
	Input: Structure name
	Output: Voltage rms codes stored in respective structure
	*/
	uint32_t readVoltageRMSRegs(VoltageRMSRegs* Data);

	/*
	Reads the current rms registers AIRMS,BIRMS and CIRMS
	Input: Structure name
	Output: Current rms codes stored in respective structure
	*/
	uint32_t readCurrentRMSRegs(CurrentRMSRegs* Data);

	uint32_t ReadFundActivePowerRegs(ActivePowerRegs* Data);
	uint32_t ReadFundReactivePowerRegs(ReactivePowerRegs* Data);
	uint32_t ReadFundApparentPowerRegs(ApparentPowerRegs* Data);
	uint32_t ReadFundVoltageRMSRegs(VoltageRMSRegs* Data);
	uint32_t ReadFundCurrentRMSRegs(CurrentRMSRegs* Data);
	uint32_t ReadHalfVoltageRMSRegs(VoltageRMSRegs* Data);
	uint32_t ReadHalfCurrentRMSRegs(CurrentRMSRegs* Data);
	uint32_t ReadTen12VoltageRMSRegs(VoltageRMSRegs* Data);
	uint32_t ReadTen12CurrentRMSRegs(CurrentRMSRegs* Data);
	uint32_t ReadVoltageTHDRegsnValues(VoltageTHDRegs* Data);
	uint32_t ReadCurrentTHDRegsnValues(CurrentTHDRegs* Data);

	/*
	Reads the power factor registers APF,BPF and CPF
	Input: Structure name
	Output: Power factor codes stored in respective structure
	*/
	uint32_t readPowerFactorRegsnValues(PowerFactorRegs* Data);

	/*
	Reads the period registers APERIOD,BPERIOD and CPERIOD
	Input: Structure name
	Output: Period codes stored in respective structure
	*/
	uint32_t readPeriodRegsnValues(PeriodRegs* Data);

	/*
	Reads angle registers
	Input: Structure name
	Output: Angle codes stored in respective structure
	*/
	uint32_t readAngleRegsnValues(AngleRegs* Data);

	/*
	Reads the temperature registers TEMP_RSLT
	Input: Structure name
	Output: Temperature codes stored in respective structure
	*/
	uint32_t readTempRegnValue(TemperatureRegnValue* Data);

	/*----- ADE9000 Calculated Physical parameters -----*/

	/*
	Convert the codes of registers AVRMS, BVRMS and CVRMS and converted to physical parameters
	Input: code value
	Output: Volts RMS physical parameter (Vrms)
	*/
	double convertCodeToVolts(int32_t value);

	/*
	Convert the codes of registers AIRMS, BIRMS and CIRMS and converted to physical parameters
	Input: code value
	Output: Volts RMS physical parameter (Arms)
	*/
	double convertCodeToAmperes(int32_t value);

	/*
	Convert the codes of registers to power
	Input: code value
	Output: Power physical parameter (Watt, VA, VAR)
	*/
	double convertCodeToPower(int32_t value);

	/*
	Convert the codes of registers to energy
	Input: code value
	Output: Power physical parameter (Watt/Hr, VA/Hr, VAR/Hr)
	*/
	double convertCodeToEnergy(int32_t value);

	/*----- ADE9000 Configuration functions -----*/

	/*
		Redirect ADC channel datapath
		Source: channel to redirect
		Destination: channel to change default adc origin
	*/
	void ADC_Redirect(adeChannel source, adeChannel destination);

	/*----- ADE9000 Calibration functions -----*/

	/*
	Get gain calibration of PGA of register PGA_GAIN
	Input:-
	Output:- PGA_GAIN (1, 2 or 4)
	*/
	void getPGA_gain(PGAGainRegs*);

	/*
	Phase gain calibration function
	Input: Stored in respective structure
	Output:-
	*/
	calibratePhaseResult phaseCalibrate(char phase);


	/*
	Update energy register by interrupt
	Input: Array size 3 to save each channels (A, B, C), the register for active, reactive and apparence energy
	Output:-
	*/
	bool updateEnergyRegister(TotalEnergyVals* energy, TotalEnergyVals* fundEnergy = nullptr);

	void readAccEnergyRegister(TotalEnergyVals* energy, TotalEnergyVals* fundEnergy = nullptr);



	bool startCalibration(calibrationStep_t step, bool phaseA, bool phaseB, bool phaseC, bool neutral);

	/*
		Devuelve true si se está calibrando el medidor
	*/
    bool isCalibrating();

    bool isCalibrating(calibrationStep_t function);



	int32_t updateCalibration(float realValue, calibrationInfo* info);


	bool endCalibration(bool save);


	const double getMaxInputVoltage() {
		//The max input voltage ios divided by 2 because is not fully differential, it's single ended.
		return ((double)((CAL_VRMS_CC) * ((double)ADE9000_RMS_FULL_SCALE_CODES)) / (double)ONE_MILLION) / 2.0;
	};

	const double getMaxInputCurrent() {
		return ((double)((CAL_IRMS_CC) * ((double)ADE9000_RMS_FULL_SCALE_CODES)) / (double)ONE_MILLION);
	};

	const double getMaxInputPower() {
		return ((double)((CAL_POWER_CC) * ((double)ADE9000_WATT_FULL_SCALE_CODES)) / (double)ONE_THOUSAND);
	};

	void setNoVoltageCutoff(float val) {
		if (val < 0) return;
		noVoltageCutoff = val;
	};

	float getNoVoltageCutoff() {
		return noVoltageCutoff;
	};

	void setNoCurrentCutoff(float val) {
		if (val < 0) return;
		noCurrentCutoff = val;
	};

	float getNoCurrentCutoff() {
		return noCurrentCutoff;
	};

	void setNoPowerCutoff(float val) {
		if (val < 0) return;
		noPowerCutoff = val;
	};

	float getNoPowerCutoff() {
		return noPowerCutoff;
	};

	void setupInterruption0(uint32_t flags) {
		SPI_Write_32(ADDR_MASK0, flags);
	};

	void setupInterruption0(ADE_MASK0_t flags) {
		SPI_Write_32(ADDR_MASK0, flags.raw);
	};

	ADE_MASK0_t readInterruption0Mask() {
		ADE_MASK0_t mask;
		mask.raw = SPI_Read_32(ADDR_MASK0);
		return mask;
	};

	void setupInterruption1(uint32_t flags) {
		SPI_Write_32(ADDR_MASK1, flags);
	};

	void setupInterruption1(ADE_MASK1_t flags) {
		SPI_Write_32(ADDR_MASK1, flags.raw);
	};
	

	ADE_MASK1_t readInterruption1Mask() {
		ADE_MASK1_t mask;
		mask.raw = SPI_Read_32(ADDR_MASK1);
		return mask;
	};


	ADE_STATUS0_t readStatus0() {
		ADE_STATUS0_t res;
		res.raw = SPI_Read_32(ADDR_STATUS0);
		return res;
	};

	ADE_STATUS1_t readStatus1() {
		ADE_STATUS1_t res;
		res.raw = SPI_Read_32(ADDR_STATUS1);
		return res;
	};

	ADE_EVENT_STATUS_t readEventStatus() {
		ADE_EVENT_STATUS_t res;
		res.raw = SPI_Read_32(ADDR_EVENT_STATUS);
		return res;
	};

	void clearStatusBit0(uint32_t mask = 0xFFFFFFFF) {
		SPI_Write_32(ADDR_STATUS0, mask);
	}

	void clearStatusBit1(uint32_t mask = 0xFFFFFFFF) {
		SPI_Write_32(ADDR_STATUS1, mask);
	}

	// Enable over current detection with a specific threshold level and optional channels mask.
	void enableOverCurrentDetection(float level, uint32_t channels = 0xF);

	// Check the status of over current detection and return the current status as ADE_OISTATUS_t structure.
	ADE_OISTATUS_t checkOverCurrentStatus();

	// Read the levels of over current events and store them in a CurrentRMSRegs data structure.
	void readOverCurrentLevels(struct CurrentRMSRegs* Data);

	// Enable dip detection with a specific threshold level and optional number of cycles.
	void setDipDetectionLevels(float level, uint32_t cycles = 10);

	// Check the status of dip detection and return the number of dip events registered.
	uint32_t checkDipStatus();

	// Read the levels of dip event and store them in a VoltageRMSRegs data structure.
	void readDipLevels(struct VoltageRMSRegs* Data);

	// Enable swell detection with a specific threshold level and optional number of cycles.
	void setSwellDetectionLevels(float level, uint32_t cycles = 10);

	// Check the status of swell detection and return the number of swell events registered.
	uint32_t checkSweelStatus();

	// Read the levels of swell event and store them in a VoltageRMSRegs data structure.
	void readSwellLevels(struct VoltageRMSRegs* Data);


private:
	Preferences preferences;		//Configuracion desde flash
	void loadCalibration();			//Carga la calibracion guardada en la memoria flash

    void printCalibration();

	float limitAngle(float angle, float min, float max, float cutOff = 0.0);

	float noVoltageCutoff;			//Cortes por bajo niveles. Si es menor a estos umbrales se toma como 0
	float noCurrentCutoff;			//Corte por baja corriente
	float noPowerCutoff;			//Corte por baja potencia

	uint32_t _SPI_speed;
	uint8_t _chipSelect_Pin;
	uint32_t ADC_REDIRECT = 0x001FFFFF;

	calibrationInfo calInfo;

	SPIClass port;
};

#endif