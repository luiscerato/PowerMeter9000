/*
  ADE9000.cpp - Library for ADE9000 with ESP32 - Energy and PQ monitoring AFE
  Date: 12-07-2020
*/
/****************************************************************************************************************
 Includes
***************************************************************************************************************/

#include "ADE9000.h"
#include "Preferences.h"



// SPIClass port(VSPI);

/*
    Formatea un numero para que según el ancho pueda contener la mayor cantidad de decimales posibles
    A su vez, verifica si puede cambiar la unidad de medición
    Width: mínimo 3: x.x

*/
String ADE9000::format(float value, uint32_t width, const char* unit, uint32_t format)
{
    float val = abs(value);
    if (width < 3) width = 3;
    uint32_t dec = width - 2;
    float div = 1.0;
    char prefix = 0;
    char str[32] = {};

    if (!(format & formatNoPrefix)) {
        if (val > 1000000) { div = 1000000;  prefix = 'M'; }  //Mega
        else if (val > 1000) { div = 1000;   prefix = 'k'; }   //Kilo
        else if (val < 1) { div = 0.001; prefix = 'm'; }     //Mili
    }
    val = abs(value / div);

    while (val >= 10) {
        val /= 10;
        dec--;
        if (dec == 0) break;
    }

    if (dec < (format & 0x7)) dec = (format & 0x7);
    if (format & formatRemoveSpaces) width = 1;

    if (prefix) {
        width -= 1;
        if (dec) dec--;
        snprintf(str, sizeof(str), "%*.*f%c%s", width, dec, value / div, prefix, unit);
    }
    else
        snprintf(str, sizeof(str), "%*.*f%s", width, dec, value / div, unit);

    return String(str);
}

ADE9000::ADE9000(uint32_t SPI_speed, uint8_t chipSelect_Pin)
{
    this->_SPI_speed = SPI_speed;
    this->_chipSelect_Pin = chipSelect_Pin;
    this->ADC_REDIRECT = 0x001FFFFF;
    calInfo.function = calNone;
    calInfo.calA = calInfo.calB = calInfo.calC = calInfo.calN = false;
    noVoltageCutoff = noCurrentCutoff = noPowerCutoff = 0.0;
}

ADE9000::ADE9000(uint32_t SPI_speed, uint8_t chipSelect_Pin, uint8_t SPIport)
{
    port = SPIClass(SPIport);
    this->_SPI_speed = SPI_speed;
    this->_chipSelect_Pin = chipSelect_Pin;
    this->ADC_REDIRECT = 0x001FFFFF;
    calInfo.function = calNone;
    calInfo.calA = calInfo.calB = calInfo.calC = calInfo.calN = false;
    noVoltageCutoff = noCurrentCutoff = noPowerCutoff = 0.0;
}

void ADE9000::initADE9000(bool initSPI)
{
    if (!initSPI)
        this->SPI_Init();

    pinMode(_chipSelect_Pin, OUTPUT);                                   // Set Chip select pin as output
    digitalWrite(_chipSelect_Pin, HIGH);                                // Set Chip select pin high
}

void ADE9000::initADE9000(uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin)
{
    SPI_Init(clkPin, misoPin, mosiPin);
    pinMode(_chipSelect_Pin, OUTPUT);                                   // Set Chip select pin as output
    digitalWrite(_chipSelect_Pin, HIGH);                                // Set Chip select pin high
}


void ADE9000::setupADE9000(void)
{
    // SPI_Write_16(ADDR_PGA_GAIN, ADE9000_PGA_GAIN1);
    SPI_Write_16(ADDR_PGA_GAIN, 0x0055);
    SPI_Write_32(ADDR_ADC_REDIRECT, ADC_REDIRECT);      //Load adc redirect configuration. Call ADC_redirect before thi function to change channels.
    SPI_Write_32(ADDR_CONFIG0, ADE9000_CONFIG0);
    SPI_Write_16(ADDR_CONFIG1, ADE9000_CONFIG1 | ADE_CONFIG1_BITS_DIP_SWELL_IRQ_MODE);
    SPI_Write_16(ADDR_CONFIG2, ADE9000_CONFIG2);
    SPI_Write_16(ADDR_CONFIG3, ADE9000_CONFIG3);
    SPI_Write_16(ADDR_ACCMODE, ADE9000_ACCMODE);
    SPI_Write_16(ADDR_TEMP_CFG, ADE9000_TEMP_CFG);
    SPI_Write_16(ADDR_ZX_LP_SEL, ADE9000_ZX_LP_SEL);
    SPI_Write_32(ADDR_MASK0, ADE9000_MASK0);
    SPI_Write_32(ADDR_MASK1, ADE9000_MASK1);
    SPI_Write_32(ADDR_EVENT_MASK, ADE9000_EVENT_MASK);
    SPI_Write_32(ADDR_VLEVEL, ADE9000_VLEVEL);
    SPI_Write_32(ADDR_DICOEFF, ADE9000_DICOEFF);
    SPI_Write_32(ADDR_CFMODE, ADE9000_CFMODE);
    SPI_Write_32(ADDR_COMPMODE, ADE9000_COMPMODE);

    loadCalibration();                          //Load calibration data from flash memory

    SPI_Write_16(ADDR_WFB_PG_IRQEN, 0x8080);    //Interruption on full page 7 y 15

    //Cargar ajustes del waveform
    SPI_Write_16(ADDR_WFB_CFG, 0x13F0);         //WF_IN_EN=1: IN waveform samples are read out of waveform buffer through the SPI.
    //WF_SRC=11: Current and voltage channel waveform samples, processed by the DSP(xI_PCF, xV_PCF) at 8 kSPS.
    //WF_MODE=11: Continuous fill—save event address of enabled trigger events.
    //WF_CAP_SEL=1: Fixed data rate data.
    //WF_CAP_EN=1: The waveform capture is started, according to the type of capture in WF_CAP_SEL and the WF_SRC bits when this bit goes from a 0 to a 1.
    //BURST_CHAN=0000: Selects which data to read out of the waveform buffer through SPI. All channels.

    SPI_Write_16(ADDR_RUN, ADE9000_RUN_ON);    //DSP ON


    Serial.printf("ADE9000 rango de entradas:\nVoltaje maximo: % .3fV\nCorriente maxima : % .3fA\nPotencia maxima: %.2fW\n",
        getMaxInputVoltage(), getMaxInputCurrent(), getMaxInputPower());
}


void ADE9000::loadCalibration()
{
    preferences.begin("ADE9000", false);

    // SPI_Write_32(ADDR_APHCAL0, 0xFBFC9813);
    // SPI_Write_32(ADDR_BPHCAL0, 0xFC1BC118);
    // SPI_Write_32(ADDR_CPHCAL0, 0xFBDE7F6F);
    // SPI_Write_32(ADDR_NPHCAL, 0xFBFCF2DE);

    SPI_Write_32(ADDR_APHCAL0, preferences.getInt("APHCAL0", 0));
    SPI_Write_32(ADDR_BPHCAL0, preferences.getInt("BPHCAL0", 0));
    SPI_Write_32(ADDR_CPHCAL0, preferences.getInt("CPHCAL0", 0));
    SPI_Write_32(ADDR_NPHCAL, preferences.getInt("NPHCAL", 0));

    //Cargar current gain
    SPI_Write_32(ADDR_AIGAIN, preferences.getInt("AIGAIN", 0));
    SPI_Write_32(ADDR_BIGAIN, preferences.getInt("BIGAIN", 0));
    SPI_Write_32(ADDR_CIGAIN, preferences.getInt("CIGAIN", 0));
    SPI_Write_32(ADDR_NIGAIN, preferences.getInt("NIGAIN", 0));
    //Cargar voltage gain
    SPI_Write_32(ADDR_AVGAIN, preferences.getInt("AVGAIN", 0));
    SPI_Write_32(ADDR_BVGAIN, preferences.getInt("BVGAIN", 0));
    SPI_Write_32(ADDR_CVGAIN, preferences.getInt("CVGAIN", 0));

    //Current offset
    SPI_Write_32(ADDR_AIRMSOS, preferences.getInt("AIRMSOS", 0));
    SPI_Write_32(ADDR_BIRMSOS, preferences.getInt("BIRMSOS", 0));
    SPI_Write_32(ADDR_CIRMSOS, preferences.getInt("CIRMSOS", 0));
    SPI_Write_32(ADDR_NIRMSOS, preferences.getInt("NIRMSOS", 0));
    //Voltage offset
    SPI_Write_32(ADDR_AVRMSOS, preferences.getInt("AVRMSOS", 0));
    SPI_Write_32(ADDR_BVRMSOS, preferences.getInt("BVRMSOS", 0));
    SPI_Write_32(ADDR_CVRMSOS, preferences.getInt("CVRMSOS", 0));

    //Offset Fundamental
    SPI_Write_32(ADDR_AIFRMSOS, preferences.getInt("AIFRMSOS", 0));
    SPI_Write_32(ADDR_BIFRMSOS, preferences.getInt("BIFRMSOS", 0));
    SPI_Write_32(ADDR_CIFRMSOS, preferences.getInt("CIFRMSOS", 0));

    SPI_Write_32(ADDR_AVFRMSOS, preferences.getInt("AVFRMSOS", 0));
    SPI_Write_32(ADDR_BVFRMSOS, preferences.getInt("BVFRMSOS", 0));
    SPI_Write_32(ADDR_CVFRMSOS, preferences.getInt("CVFRMSOS", 0));

    //Offsets one cicle
    SPI_Write_32(ADDR_AIRMSONEOS, preferences.getInt("AIRMSONEOS", 0));
    SPI_Write_32(ADDR_BIRMSONEOS, preferences.getInt("BIRMSONEOS", 0));
    SPI_Write_32(ADDR_CIRMSONEOS, preferences.getInt("CIRMSONEOS", 0));
    SPI_Write_32(ADDR_NIRMSONEOS, preferences.getInt("NIRMSONEOS", 0));

    SPI_Write_32(ADDR_AVRMSONEOS, preferences.getInt("AVRMSONEOS", 0));
    SPI_Write_32(ADDR_BVRMSONEOS, preferences.getInt("BVRMSONEOS", 0));
    SPI_Write_32(ADDR_CVRMSONEOS, preferences.getInt("CVRMSONEOS", 0));

    //Offsets 1012 cicles
    SPI_Write_32(ADDR_AIRMS1012OS, preferences.getInt("AIRMS1012OS", 0));
    SPI_Write_32(ADDR_BIRMS1012OS, preferences.getInt("BIRMS1012OS", 0));
    SPI_Write_32(ADDR_CIRMS1012OS, preferences.getInt("CIRMS1012OS", 0));
    SPI_Write_32(ADDR_NIRMS1012OS, preferences.getInt("NIRMS1012OS", 0));

    SPI_Write_32(ADDR_AVRMS1012OS, preferences.getInt("AVRMS1012OS", 0));
    SPI_Write_32(ADDR_BVRMS1012OS, preferences.getInt("BVRMS1012OS", 0));
    SPI_Write_32(ADDR_CVRMS1012OS, preferences.getInt("CVRMS1012OS", 0));

    preferences.end();

    SPI_Write_16(ADDR_EGY_TIME, ADE9000_EGY_TIME);
    SPI_Write_16(ADDR_EP_CFG, ADE9000_EP_CFG); //Energy accumulation ON
}

void ADE9000::resetADE9000(uint8_t ADE9000_RESET_PIN)
{
    digitalWrite(ADE9000_RESET_PIN, LOW);
    delay(50);
    digitalWrite(ADE9000_RESET_PIN, HIGH);
    delay(1000);
    Serial.println("Reset Done");
}

void ADE9000::SPI_Init()
{
    port.begin();                                                       //Initiate SPI port
}

void ADE9000::SPI_Init(uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin)
{
    port.begin(clkPin, misoPin, mosiPin);                                            //Initiate SPI port
}

void ADE9000::SPI_Write_16(uint16_t Address, uint16_t Data)
{
    uint16_t temp_address;
    //Initiate SPI port
    port.beginTransaction(SPISettings(_SPI_speed, MSBFIRST, SPI_MODE0)); //Setup SPI parameters
    digitalWrite(_chipSelect_Pin, LOW);
    temp_address = ((Address << 4) & 0xFFF0); // Shift address to align with cmd packet
    port.transfer16(temp_address);
    port.transfer16(Data);

    digitalWrite(_chipSelect_Pin, HIGH);
    port.endTransaction();
}

void ADE9000::SPI_Write_32(uint16_t Address, uint32_t Data)
{
    uint16_t temp_address;

    port.beginTransaction(SPISettings(_SPI_speed, MSBFIRST, SPI_MODE0)); //Setup SPI parameters
    digitalWrite(_chipSelect_Pin, LOW);
    temp_address = ((Address << 4) & 0xFFF0); // Shift address  to align with cmd packet
    port.transfer16(temp_address);
    port.transfer32(Data);

    digitalWrite(_chipSelect_Pin, HIGH);
    port.endTransaction();
}

uint16_t ADE9000::SPI_Read_16(uint16_t Address)
{
    uint16_t temp_address;
    uint16_t returnData;

    port.beginTransaction(SPISettings(_SPI_speed, MSBFIRST, SPI_MODE0)); //Setup SPI parameters
    digitalWrite(_chipSelect_Pin, LOW);

    temp_address = (((Address << 4) & 0xFFF0) + 8);
    port.transfer16(temp_address);
    returnData = port.transfer16(0);

    digitalWrite(_chipSelect_Pin, HIGH);
    port.endTransaction();
    return returnData;
}

uint32_t ADE9000::SPI_Read_32(uint16_t Address)
{
    uint16_t temp_address;
    uint32_t returnData;

    port.beginTransaction(SPISettings(_SPI_speed, MSBFIRST, SPI_MODE0)); //Setup SPI parameters
    digitalWrite(_chipSelect_Pin, LOW);

    temp_address = (((Address << 4) & 0xFFF0) + 8);
    port.transfer16(temp_address);
    returnData = port.transfer32(0);

    digitalWrite(_chipSelect_Pin, HIGH);
    port.endTransaction();
    return returnData;
}


void ADE9000::SPI_Burst_Read_FixedDT_Buffer(uint16_t bufferPos, uint16_t samplesCount, WFBFixedDataRate_t* samplesBuffer)
{
    uint16_t temp;
    uint16_t i;

    if ((bufferPos + samplesCount) > 0xFFF) return;

    port.beginTransaction(SPISettings(_SPI_speed, MSBFIRST, SPI_MODE0)); //Setup SPI parameters
    digitalWrite(_chipSelect_Pin, LOW);

    // port.transfer16(((Address << 4) & 0xFFF0) + 8);  //Send the starting address
    port.transfer16(((0x800 + bufferPos) << 4) + 8);  //Send the starting address

    //burst read the data upto Read_Length 
    for (i = 0;i < samplesCount;i++) {
        samplesBuffer->IA = port.transfer32(0);
        samplesBuffer->VA = port.transfer32(0);
        samplesBuffer->IB = port.transfer32(0);
        samplesBuffer->VB = port.transfer32(0);
        samplesBuffer->IC = port.transfer32(0);
        samplesBuffer->VC = port.transfer32(0);
        samplesBuffer->IN = port.transfer32(0);
        samplesBuffer++;
    }
    digitalWrite(_chipSelect_Pin, HIGH);
    port.endTransaction();
}

uint32_t ADE9000::readActivePowerRegs(ActivePowerRegs* Data)
{
    uint32_t time = micros();
    Data->ActivePowerReg_A = int32_t(SPI_Read_32(ADDR_AWATT));
    Data->ActivePowerReg_B = int32_t(SPI_Read_32(ADDR_BWATT));
    Data->ActivePowerReg_C = int32_t(SPI_Read_32(ADDR_CWATT));
    Data->ActivePower_A = (float)(CAL_POWER_CC * Data->ActivePowerReg_A) / ONE_THOUSAND;
    Data->ActivePower_B = (float)(CAL_POWER_CC * Data->ActivePowerReg_B) / ONE_THOUSAND;
    Data->ActivePower_C = (float)(CAL_POWER_CC * Data->ActivePowerReg_C) / ONE_THOUSAND;
    if (abs(Data->ActivePower_A) < noPowerCutoff) Data->ActivePower_A = 0.0;
    if (abs(Data->ActivePower_B) < noPowerCutoff) Data->ActivePower_B = 0.0;
    if (abs(Data->ActivePower_C) < noPowerCutoff) Data->ActivePower_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::readReactivePowerRegs(ReactivePowerRegs* Data)
{
    uint32_t time = micros();
    Data->ReactivePowerReg_A = int32_t(SPI_Read_32(ADDR_AVAR));
    Data->ReactivePowerReg_B = int32_t(SPI_Read_32(ADDR_BVAR));
    Data->ReactivePowerReg_C = int32_t(SPI_Read_32(ADDR_CVAR));
    Data->ReactivePower_A = (float)(CAL_POWER_CC * Data->ReactivePowerReg_A) / ONE_THOUSAND;
    Data->ReactivePower_B = (float)(CAL_POWER_CC * Data->ReactivePowerReg_B) / ONE_THOUSAND;
    Data->ReactivePower_C = (float)(CAL_POWER_CC * Data->ReactivePowerReg_C) / ONE_THOUSAND;
    if (abs(Data->ReactivePower_A) < noPowerCutoff) Data->ReactivePower_A = 0.0;
    if (abs(Data->ReactivePower_B) < noPowerCutoff) Data->ReactivePower_B = 0.0;
    if (abs(Data->ReactivePower_C) < noPowerCutoff) Data->ReactivePower_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::readApparentPowerRegs(ApparentPowerRegs* Data)
{
    uint32_t time = micros();
    Data->ApparentPowerReg_A = int32_t(SPI_Read_32(ADDR_AVA));
    Data->ApparentPowerReg_B = int32_t(SPI_Read_32(ADDR_BVA));
    Data->ApparentPowerReg_C = int32_t(SPI_Read_32(ADDR_CVA));
    Data->ApparentPower_A = (float)(CAL_POWER_CC * Data->ApparentPowerReg_A) / ONE_THOUSAND;
    Data->ApparentPower_B = (float)(CAL_POWER_CC * Data->ApparentPowerReg_B) / ONE_THOUSAND;
    Data->ApparentPower_C = (float)(CAL_POWER_CC * Data->ApparentPowerReg_C) / ONE_THOUSAND;
    if (abs(Data->ApparentPower_A) < noPowerCutoff) Data->ApparentPower_A = 0.0;
    if (abs(Data->ApparentPower_B) < noPowerCutoff) Data->ApparentPower_B = 0.0;
    if (abs(Data->ApparentPower_C) < noPowerCutoff) Data->ApparentPower_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::readVoltageRMSRegs(VoltageRMSRegs* Data)
{
    uint32_t time = micros();
    Data->VoltageRMSReg_A = int32_t(SPI_Read_32(ADDR_AVRMS));
    Data->VoltageRMSReg_B = int32_t(SPI_Read_32(ADDR_BVRMS));
    Data->VoltageRMSReg_C = int32_t(SPI_Read_32(ADDR_CVRMS));
    Data->VoltageRMS_A = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_A) / ONE_MILLION;
    Data->VoltageRMS_B = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_B) / ONE_MILLION;
    Data->VoltageRMS_C = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_C) / ONE_MILLION;
    if (Data->VoltageRMS_A < noVoltageCutoff) Data->VoltageRMS_A = 0.0;
    if (Data->VoltageRMS_B < noVoltageCutoff) Data->VoltageRMS_B = 0.0;
    if (Data->VoltageRMS_C < noVoltageCutoff) Data->VoltageRMS_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::readCurrentRMSRegs(CurrentRMSRegs* Data)
{
    uint32_t time = micros();
    Data->CurrentRMSReg_A = int32_t(SPI_Read_32(ADDR_AIRMS));
    Data->CurrentRMSReg_B = int32_t(SPI_Read_32(ADDR_BIRMS));
    Data->CurrentRMSReg_C = int32_t(SPI_Read_32(ADDR_CIRMS));
    Data->CurrentRMSReg_N = int32_t(SPI_Read_32(ADDR_NIRMS));
    Data->CurrentRMS_A = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_A) / (float)ONE_MILLION;
    Data->CurrentRMS_B = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_B) / (float)ONE_MILLION;
    Data->CurrentRMS_C = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_C) / (float)ONE_MILLION;
    Data->CurrentRMS_N = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_N) / (float)ONE_MILLION;
    if (Data->CurrentRMS_A < noCurrentCutoff) Data->CurrentRMS_A = 0.0;
    if (Data->CurrentRMS_B < noCurrentCutoff) Data->CurrentRMS_B = 0.0;
    if (Data->CurrentRMS_C < noCurrentCutoff) Data->CurrentRMS_C = 0.0;
    if (Data->CurrentRMS_N < noCurrentCutoff) Data->CurrentRMS_N = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadFundActivePowerRegs(ActivePowerRegs* Data)
{
    uint32_t time = micros();
    Data->ActivePowerReg_A = int32_t(SPI_Read_32(ADDR_AFWATT));
    Data->ActivePowerReg_B = int32_t(SPI_Read_32(ADDR_BFWATT));
    Data->ActivePowerReg_C = int32_t(SPI_Read_32(ADDR_CFWATT));
    Data->ActivePower_A = (float)(CAL_POWER_CC * Data->ActivePowerReg_A) / ONE_THOUSAND;
    Data->ActivePower_B = (float)(CAL_POWER_CC * Data->ActivePowerReg_B) / ONE_THOUSAND;
    Data->ActivePower_C = (float)(CAL_POWER_CC * Data->ActivePowerReg_C) / ONE_THOUSAND;
    if (abs(Data->ActivePower_A) < noPowerCutoff) Data->ActivePower_A = 0.0;
    if (abs(Data->ActivePower_B) < noPowerCutoff) Data->ActivePower_B = 0.0;
    if (abs(Data->ActivePower_C) < noPowerCutoff) Data->ActivePower_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadFundReactivePowerRegs(ReactivePowerRegs* Data)
{
    uint32_t time = micros();
    Data->ReactivePowerReg_A = int32_t(SPI_Read_32(ADDR_AFVAR));
    Data->ReactivePowerReg_B = int32_t(SPI_Read_32(ADDR_BFVAR));
    Data->ReactivePowerReg_C = int32_t(SPI_Read_32(ADDR_CFVAR));
    Data->ReactivePower_A = (float)(CAL_POWER_CC * Data->ReactivePowerReg_A) / ONE_THOUSAND;
    Data->ReactivePower_B = (float)(CAL_POWER_CC * Data->ReactivePowerReg_B) / ONE_THOUSAND;
    Data->ReactivePower_C = (float)(CAL_POWER_CC * Data->ReactivePowerReg_C) / ONE_THOUSAND;
    if (abs(Data->ReactivePower_A) < noPowerCutoff) Data->ReactivePower_A = 0.0;
    if (abs(Data->ReactivePower_B) < noPowerCutoff) Data->ReactivePower_B = 0.0;
    if (abs(Data->ReactivePower_C) < noPowerCutoff) Data->ReactivePower_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadFundApparentPowerRegs(ApparentPowerRegs* Data)
{
    uint32_t time = micros();
    Data->ApparentPowerReg_A = int32_t(SPI_Read_32(ADDR_AFVA));
    Data->ApparentPowerReg_B = int32_t(SPI_Read_32(ADDR_BFVA));
    Data->ApparentPowerReg_C = int32_t(SPI_Read_32(ADDR_CFVA));
    Data->ApparentPower_A = (float)(CAL_POWER_CC * Data->ApparentPowerReg_A) / ONE_THOUSAND;
    Data->ApparentPower_B = (float)(CAL_POWER_CC * Data->ApparentPowerReg_B) / ONE_THOUSAND;
    Data->ApparentPower_C = (float)(CAL_POWER_CC * Data->ApparentPowerReg_C) / ONE_THOUSAND;
    if (abs(Data->ApparentPower_A) < noPowerCutoff) Data->ApparentPower_A = 0.0;
    if (abs(Data->ApparentPower_B) < noPowerCutoff) Data->ApparentPower_B = 0.0;
    if (abs(Data->ApparentPower_C) < noPowerCutoff) Data->ApparentPower_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadFundVoltageRMSRegs(VoltageRMSRegs* Data)
{
    uint32_t time = micros();
    Data->VoltageRMSReg_A = int32_t(SPI_Read_32(ADDR_AVFRMS));
    Data->VoltageRMSReg_B = int32_t(SPI_Read_32(ADDR_BVFRMS));
    Data->VoltageRMSReg_C = int32_t(SPI_Read_32(ADDR_CVFRMS));
    Data->VoltageRMS_A = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_A) / ONE_MILLION;
    Data->VoltageRMS_B = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_B) / ONE_MILLION;
    Data->VoltageRMS_C = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_C) / ONE_MILLION;
    if (Data->VoltageRMS_A < noVoltageCutoff) Data->VoltageRMS_A = 0.0;
    if (Data->VoltageRMS_B < noVoltageCutoff) Data->VoltageRMS_B = 0.0;
    if (Data->VoltageRMS_C < noVoltageCutoff) Data->VoltageRMS_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadFundCurrentRMSRegs(CurrentRMSRegs* Data)
{
    uint32_t time = micros();
    Data->CurrentRMSReg_A = int32_t(SPI_Read_32(ADDR_AIFRMS));
    Data->CurrentRMSReg_B = int32_t(SPI_Read_32(ADDR_BIFRMS));
    Data->CurrentRMSReg_C = int32_t(SPI_Read_32(ADDR_CIFRMS));
    Data->CurrentRMS_A = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_A) / (float)ONE_MILLION;
    Data->CurrentRMS_B = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_B) / (float)ONE_MILLION;
    Data->CurrentRMS_C = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_C) / (float)ONE_MILLION;
    Data->CurrentRMS_N = 0;
    if (Data->CurrentRMS_A < noCurrentCutoff) Data->CurrentRMS_A = 0.0;
    if (Data->CurrentRMS_B < noCurrentCutoff) Data->CurrentRMS_B = 0.0;
    if (Data->CurrentRMS_C < noCurrentCutoff) Data->CurrentRMS_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadHalfVoltageRMSRegs(VoltageRMSRegs* Data)
{
    uint32_t time = micros();
    Data->VoltageRMSReg_A = int32_t(SPI_Read_32(ADDR_AVRMSONE));
    Data->VoltageRMSReg_B = int32_t(SPI_Read_32(ADDR_BVRMSONE));
    Data->VoltageRMSReg_C = int32_t(SPI_Read_32(ADDR_CVRMSONE));
    Data->VoltageRMS_A = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_A) / ONE_MILLION;
    Data->VoltageRMS_B = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_B) / ONE_MILLION;
    Data->VoltageRMS_C = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_C) / ONE_MILLION;
    if (Data->VoltageRMS_A < noVoltageCutoff) Data->VoltageRMS_A = 0.0;
    if (Data->VoltageRMS_B < noVoltageCutoff) Data->VoltageRMS_B = 0.0;
    if (Data->VoltageRMS_C < noVoltageCutoff) Data->VoltageRMS_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadHalfCurrentRMSRegs(CurrentRMSRegs* Data)
{
    uint32_t time = micros();
    Data->CurrentRMSReg_A = int32_t(SPI_Read_32(ADDR_AIRMSONE));
    Data->CurrentRMSReg_B = int32_t(SPI_Read_32(ADDR_BIRMSONE));
    Data->CurrentRMSReg_C = int32_t(SPI_Read_32(ADDR_CIRMSONE));
    Data->CurrentRMSReg_N = int32_t(SPI_Read_32(ADDR_NIRMSONE));
    Data->CurrentRMS_A = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_A) / (float)ONE_MILLION;
    Data->CurrentRMS_B = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_B) / (float)ONE_MILLION;
    Data->CurrentRMS_C = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_C) / (float)ONE_MILLION;
    Data->CurrentRMS_N = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_N) / (float)ONE_MILLION;
    if (Data->CurrentRMS_A < noCurrentCutoff) Data->CurrentRMS_A = 0.0;
    if (Data->CurrentRMS_B < noCurrentCutoff) Data->CurrentRMS_B = 0.0;
    if (Data->CurrentRMS_C < noCurrentCutoff) Data->CurrentRMS_C = 0.0;
    if (Data->CurrentRMS_N < noCurrentCutoff) Data->CurrentRMS_N = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadTen12VoltageRMSRegs(VoltageRMSRegs* Data)
{
    uint32_t time = micros();
    Data->VoltageRMSReg_A = int32_t(SPI_Read_32(ADDR_AVRMS1012));
    Data->VoltageRMSReg_B = int32_t(SPI_Read_32(ADDR_BVRMS1012));
    Data->VoltageRMSReg_C = int32_t(SPI_Read_32(ADDR_CVRMS1012));
    Data->VoltageRMS_A = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_A) / ONE_MILLION;
    Data->VoltageRMS_B = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_B) / ONE_MILLION;
    Data->VoltageRMS_C = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_C) / ONE_MILLION;
    if (Data->VoltageRMS_A < noVoltageCutoff) Data->VoltageRMS_A = 0.0;
    if (Data->VoltageRMS_B < noVoltageCutoff) Data->VoltageRMS_B = 0.0;
    if (Data->VoltageRMS_C < noVoltageCutoff) Data->VoltageRMS_C = 0.0;
    return micros() - time;
}

uint32_t ADE9000::ReadTen12CurrentRMSRegs(CurrentRMSRegs* Data)
{
    uint32_t time = micros();
    Data->CurrentRMSReg_A = int32_t(SPI_Read_32(ADDR_AIRMS1012));
    Data->CurrentRMSReg_B = int32_t(SPI_Read_32(ADDR_BIRMS1012));
    Data->CurrentRMSReg_C = int32_t(SPI_Read_32(ADDR_CIRMS1012));
    Data->CurrentRMSReg_N = int32_t(SPI_Read_32(ADDR_NIRMS1012));
    Data->CurrentRMS_A = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_A) / (float)ONE_MILLION;
    Data->CurrentRMS_B = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_B) / (float)ONE_MILLION;
    Data->CurrentRMS_C = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_C) / (float)ONE_MILLION;
    Data->CurrentRMS_N = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_N) / (float)ONE_MILLION;
    if (Data->CurrentRMS_A < noCurrentCutoff) Data->CurrentRMS_A = 0.0;
    if (Data->CurrentRMS_B < noCurrentCutoff) Data->CurrentRMS_B = 0.0;
    if (Data->CurrentRMS_C < noCurrentCutoff) Data->CurrentRMS_C = 0.0;
    if (Data->CurrentRMS_N < noCurrentCutoff) Data->CurrentRMS_N = 0.0;
    return micros() - time;
}



uint32_t ADE9000::readPowerFactorRegsnValues(PowerFactorRegs* Data)
{
    uint32_t tempReg;
    float tempValue;

    uint32_t time = micros();
    tempReg = int32_t(SPI_Read_32(ADDR_APF)); // Read PF register
    Data->PowerFactorReg_A = tempReg;
    tempValue = (float)tempReg / (float)134217728; // Calculate PF
    Data->PowerFactorValue_A = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_BPF)); // Read PF register
    Data->PowerFactorReg_B = tempReg;
    tempValue = (float)tempReg / (float)134217728; // Calculate PF
    Data->PowerFactorValue_B = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_CPF)); // Read PF register
    Data->PowerFactorReg_C = tempReg;
    tempValue = (float)tempReg / (float)134217728; // Calculate PF
    Data->PowerFactorValue_C = tempValue;
    return micros() - time;
}


uint32_t ADE9000::readPeriodRegsnValues(PeriodRegs* Data)
{
    uint32_t tempReg;
    float tempValue;
    uint32_t time = micros();
    tempReg = int32_t(SPI_Read_32(ADDR_APERIOD)); // Read PERIOD register
    Data->PeriodReg_A = tempReg;
    tempValue = (float)(8000 * 65536) / (float)(tempReg + 1); // Calculate Frequency
    Data->FrequencyValue_A = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_BPERIOD)); // Read PERIOD register
    Data->PeriodReg_B = tempReg;
    tempValue = (float)(8000 * 65536) / (float)(tempReg + 1); // Calculate Frequency
    Data->FrequencyValue_B = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_CPERIOD)); // Read PERIOD register
    Data->PeriodReg_C = tempReg;
    tempValue = (float)(8000 * 65536) / (float)(tempReg + 1); // Calculate Frequency
    Data->FrequencyValue_C = tempValue;
    return micros() - time;
}


uint32_t ADE9000::readAngleRegsnValues(AngleRegs* Data)
{
    uint32_t tempReg;
    uint16_t temp;
    float mulConstant;
    float tempValue;

    uint32_t time = micros();
    temp = SPI_Read_16(ADDR_ACCMODE); // Read frequency setting register
    if ((temp & 0x0100) >= 0)
    {
        mulConstant = 0.02109375; // Multiplier constant for 60Hz system
    }
    else
    {
    }
    mulConstant = 0.017578125; // Multiplier constant for 50Hz system

    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_VA_VB)); // Read ANGLE register
    Data->AngleReg_VA_VB = tempReg;
    tempValue = tempReg * mulConstant; // Calculate Angle in degrees
    Data->AngleValue_VA_VB = constrain(tempValue, -3600, 3600);

    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_VB_VC));
    Data->AngleReg_VB_VC = tempReg;
    tempValue = tempReg * mulConstant;
    Data->AngleValue_VB_VC = constrain(tempValue, -3600, 3600);
    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_VA_VC));
    Data->AngleReg_VA_VC = tempReg;
    tempValue = tempReg * mulConstant;
    Data->AngleValue_VA_VC = constrain(tempValue, -3600, 3600);
    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_VA_IA));
    Data->AngleReg_VA_IA = tempReg;
    tempValue = tempReg * mulConstant;
    if (tempValue > 180) tempValue = tempValue - 360;
    Data->AngleValue_VA_IA = constrain(tempValue, -3600, 3600);

    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_VB_IB));
    Data->AngleReg_VB_IB = tempReg;
    tempValue = tempReg * mulConstant;
    if (tempValue > 180) tempValue = tempValue - 360;
    Data->AngleValue_VB_IB = constrain(tempValue, -3600, 3600);
    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_VC_IC));
    Data->AngleReg_VC_IC = tempReg;
    tempValue = tempReg * mulConstant;
    if (tempValue > 180) tempValue = tempValue - 360;
    Data->AngleValue_VC_IC = constrain(tempValue, -3600, 3600);
    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_IA_IB));
    Data->AngleReg_IA_IB = tempReg;
    tempValue = tempReg * mulConstant;
    Data->AngleValue_IA_IB = constrain(tempValue, -3600, 3600);
    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_IB_IC));
    Data->AngleReg_IB_IC = tempReg;
    tempValue = tempReg * mulConstant;
    Data->AngleValue_IB_IC = constrain(tempValue, -3600, 3600);
    tempReg = int16_t(SPI_Read_16(ADDR_ANGL_IA_IC));
    Data->AngleReg_IA_IC = tempReg;
    tempValue = tempReg * mulConstant;
    Data->AngleValue_IA_IC = constrain(tempValue, -3600, 3600);
    return micros() - time;
}
uint32_t ADE9000::ReadVoltageTHDRegsnValues(VoltageTHDRegs* Data)
{
    uint32_t tempReg;
    float tempValue;
    uint32_t time = micros();

    tempReg = int32_t(SPI_Read_32(ADDR_AVTHD)); //Read THD register
    Data->VoltageTHDReg_A = tempReg;
    tempValue = (float)tempReg * 100 / (float)134217728; //Calculate THD in %
    Data->VoltageTHDValue_A = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_BVTHD)); //Read THD register
    Data->VoltageTHDReg_B = tempReg;
    tempValue = (float)tempReg * 100 / (float)134217728; //Calculate THD in %
    Data->VoltageTHDValue_B = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_CVTHD)); //Read THD register
    Data->VoltageTHDReg_C = tempReg;
    tempValue = (float)tempReg * 100 / (float)134217728; //Calculate THD in %
    Data->VoltageTHDValue_C = tempValue;
    return micros() - time;
}

uint32_t ADE9000::ReadCurrentTHDRegsnValues(CurrentTHDRegs* Data)
{
    uint32_t tempReg;
    float tempValue;
    uint32_t time = micros();

    tempReg = int32_t(SPI_Read_32(ADDR_AITHD)); //Read THD register
    Data->CurrentTHDReg_A = tempReg;
    tempValue = (float)tempReg * 100 / (float)134217728; //Calculate THD in %	
    Data->CurrentTHDValue_A = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_BITHD)); //Read THD register
    Data->CurrentTHDReg_B = tempReg;
    tempValue = (float)tempReg * 100 / (float)134217728; //Calculate THD in %	
    Data->CurrentTHDValue_B = tempValue;
    tempReg = int32_t(SPI_Read_32(ADDR_CITHD)); //Read THD register
    Data->CurrentTHDReg_C = tempReg;
    tempValue = (float)tempReg * 100 / (float)134217728; //Calculate THD in %		
    Data->CurrentTHDValue_C = tempValue;
    return micros() - time;
}


uint32_t ADE9000::readTempRegnValue(TemperatureRegnValue* Data)
{
    uint32_t trim;
    uint16_t gain;
    uint16_t offset;
    uint16_t tempReg;
    float tempValue;

    uint32_t time = micros();
    SPI_Write_16(ADDR_TEMP_CFG, ADE9000_TEMP_CFG); // Start temperature acquisition cycle with settings in defined in ADE9000_TEMP_CFG
    delay(2);                                      // delay of 2ms. Increase delay if TEMP_TIME is changed

    trim = SPI_Read_32(ADDR_TEMP_TRIM);
    gain = (trim & 0xFFFF);                // Extract 16 LSB
    offset = ((trim >> 16) & 0xFFFF);      // Extract 16 MSB
    tempReg = SPI_Read_16(ADDR_TEMP_RSLT); // Read Temperature result register
    tempValue = (float)(offset >> 5) - ((float)tempReg * (float)gain / (float)65536);

    Data->Temperature_Reg = tempReg;
    Data->Temperature = tempValue;
    return micros() - time;
}


double ADE9000::convertCodeToVolts(int32_t value)
{
    double result = 0;
    result = (double)(CAL_VRMS_CC * value) / ONE_MILLION;
    return result;
}

double ADE9000::convertCodeToAmperes(int32_t value)
{
    double result = 0;
    result = (double)(CAL_IRMS_CC * value) / ONE_MILLION;
    return result;
}

double ADE9000::convertCodeToPower(int32_t value)
{
    double result = 0;
    result = (double)(CAL_POWER_CC * value) / ONE_THOUSAND;
    return result;
}

double ADE9000::convertCodeToEnergy(int32_t value)
{
    double result = 0;
    result = (double)(CAL_ENERGY_CC * value) / ONE_MILLION;
    return result;
}


void ADE9000::ADC_Redirect(adeChannel source, adeChannel destination)
{
    if (destination >= adeChannel_Default) return;

    uint32_t mask = ~(adeChannel_Default << (destination * 3));
    ADC_REDIRECT &= mask;
    ADC_REDIRECT |= source << (destination * 3);

    Serial.printf("ADC_Redirect: origin: 0x%x, destination: 0x%x. Result: 0x%x\n", source, destination, ADC_REDIRECT);
}


void ADE9000::getPGA_gain(PGAGainRegs* Data)
{
    int16_t pgaGainRegister;
    int16_t temp;
    pgaGainRegister = SPI_Read_16(ADDR_PGA_GAIN); // Ensure PGA_GAIN is set correctly in SetupADE9000 function.
    temp = pgaGainRegister & (0x0003);            // Extract gain of current channel
#ifdef DEBUG_MODE
    Serial.print("PGA Gain Register is: ");
    Serial.println(temp, HEX);
#endif
    if (temp == 0) // 00-->Gain 1: 01-->Gain 2: 10/11-->Gain 4
    {
        Data->CurrentPGA_gain = 1;
    }
    else
    {
        if (temp == 1)
        {
            Data->CurrentPGA_gain = 2;
        }
        else
        {
            Data->CurrentPGA_gain = 4;
        }
    }
    temp = (pgaGainRegister >> 8) & (0x0003); //extract gain of voltage channel
    if (temp == 0)
    {
        Data->VoltagePGA_gain = 1;
    }
    else
    {
        if (temp == 1)
        {
            Data->VoltagePGA_gain = 2;
        }
        else
        {
            Data->VoltagePGA_gain = 4;
        }
    }
}


calibratePhaseResult ADE9000::phaseCalibrate(char phase)
{
    const double omega = (float)2 * (float)3.14159 * (float)INPUT_FREQUENCY / (float)ADE9000_FDSP;
    calibratePhaseResult res = { 0.0, 0 };
    int32_t watt, var;

    if (phase == 'R' || phase == 'A') {
        watt = SPI_Read_32(ADDR_AWATT);
        var = SPI_Read_32(ADDR_AVAR);
    }
    else if (phase == 'S' || phase == 'B') {
        watt = SPI_Read_32(ADDR_BWATT);
        var = SPI_Read_32(ADDR_BVAR);
    }
    else if (phase == 'T' || phase == 'C') {
        watt = SPI_Read_32(ADDR_CWATT);
        var = SPI_Read_32(ADDR_CVAR);
    }
    else
        return res;

    double angle = degrees(atan((double)var / (double)watt));   //Calcular angulo actual
    int32_t factor = ((sin(radians(angle) - omega) + sin(omega)) / (sin(2 * omega - radians(angle)))) * 134217728; //2^27

    // Serial.printf("Calibracion de fase R: angulo actual: %.5f [w:%d, var:%d] => APHCAL0=0x%X\n", angle, watt, var, factor);
    res.angle = angle;
    res.factor = factor;
    return res;
}

void ADE9000::pGain_calibrate(int32_t* pgainReg, int32_t* accActiveEgyReg, int arraySize, uint8_t currentPGA_gain, uint8_t voltagePGA_gain, float pGaincalPF)
{
    Serial.println("Computing power gain calibration registers...");
    delay((ACCUMULATION_TIME + 1) * 1000); // Delay to ensure the energy registers are accumulated for defined interval
    int32_t expectedActiveEnergyCode;
    int32_t actualActiveEnergyCode;
    int i;
    float temp;
    temp = ((float)ADE9000_FDSP * (float)NOMINAL_INPUT_VOLTAGE * (float)NOMINAL_INPUT_CURRENT * (float)CURRENT_TRANSFER_FUNCTION * (float)currentPGA_gain * (float)VOLTAGE_TRANSFER_FUNCTION * (float)voltagePGA_gain * (float)ADE9000_WATT_FULL_SCALE_CODES * 2 * (float)(pGaincalPF)) / (float)(8192);
    expectedActiveEnergyCode = (int32_t)temp;
#ifdef DEBUG_MODE
    Serial.print("Expected Active Energy Code: ");
    Serial.println(expectedActiveEnergyCode, HEX);
#endif

    for (i = 0; i < arraySize; i++)
    {
        actualActiveEnergyCode = accActiveEgyReg[i];

        temp = (((float)expectedActiveEnergyCode / (float)actualActiveEnergyCode) - 1) * 134217728; // Calculate the gain.
        pgainReg[i] = (int32_t)temp;                                                                // Round off
#ifdef DEBUG_MODE
        Serial.print("Channel ");
        Serial.print(i + 1);
        Serial.print("Actual Active Energy Code: ");
        Serial.println(actualActiveEnergyCode, HEX);
        Serial.print("Power Gain Register: ");
        Serial.println(pgainReg[i], HEX);
#endif
    }
}

bool ADE9000::updateEnergyRegister(TotalEnergyVals* energy, TotalEnergyVals* fundEnergy)
{
    if (calInfo.function != calNone) return false;      //No se pueden leer los registros de energía si se está calibrando

    uint32_t temp;
    if (energy == nullptr) return false;

    temp = SPI_Read_32(ADDR_STATUS0);
    temp &= EGY_INTERRUPT_MASK0;
    if (temp == EGY_INTERRUPT_MASK0) {
        SPI_Write_32(ADDR_STATUS0, 0xFFFFFFFF);
        energy->PhaseR.Watt_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_AWATTHR_HI)) / ONE_MILLION;
        energy->PhaseS.Watt_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_BWATTHR_HI)) / ONE_MILLION;
        energy->PhaseT.Watt_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_CWATTHR_HI)) / ONE_MILLION;

        energy->PhaseR.VAR_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_AVARHR_HI)) / ONE_MILLION;
        energy->PhaseS.VAR_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_BVARHR_HI)) / ONE_MILLION;
        energy->PhaseT.VAR_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_CVARHR_HI)) / ONE_MILLION;

        energy->PhaseR.VA_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_AVAHR_HI)) / ONE_MILLION;
        energy->PhaseS.VA_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_BVAHR_HI)) / ONE_MILLION;
        energy->PhaseT.VA_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_CVAHR_HI)) / ONE_MILLION;

        if (fundEnergy != nullptr) {    //Actualizar contadores de energía fundamental si se puede
            fundEnergy->PhaseR.Watt_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_AFWATTHR_HI)) / ONE_MILLION;
            fundEnergy->PhaseS.Watt_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_BFWATTHR_HI)) / ONE_MILLION;
            fundEnergy->PhaseT.Watt_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_CFWATTHR_HI)) / ONE_MILLION;

            fundEnergy->PhaseR.VAR_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_AFVARHR_HI)) / ONE_MILLION;
            fundEnergy->PhaseS.VAR_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_BFVARHR_HI)) / ONE_MILLION;
            fundEnergy->PhaseT.VAR_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_CFVARHR_HI)) / ONE_MILLION;

            fundEnergy->PhaseR.VA_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_AFVAHR_HI)) / ONE_MILLION;
            fundEnergy->PhaseS.VA_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_BFVAHR_HI)) / ONE_MILLION;
            fundEnergy->PhaseT.VA_H += (double)(CAL_ENERGY_CC * (int32_t)SPI_Read_32(ADDR_CFVAHR_HI)) / ONE_MILLION;
        }
        return true;    //exit function
    }
    return false;
}

bool ADE9000::startCalibration(calibrationStep_t function, bool phaseA, bool phaseB, bool phaseC, bool neutral)
{
    Serial.printf("Iniciando calibracion: %s!\n", calibrationStepString(function));
    if (calInfo.function != calNone) return false;
    if (function <= calNone || function >= calLastItem) return false;

    calInfo.function = function;
    if (!calInfo.isCalibratingCurrent()) neutral = false;
    calInfo.calA = phaseA; calInfo.calB = phaseB; calInfo.calC = phaseC; calInfo.calN = neutral;
    calInfo.clearAccumulators();
    calInfo.samples = 0;
    Serial.printf("Preparando para calibrar %s%s%s%s\n", phaseA ? "Fase A " : "", phaseB ? "Fase B " : "", phaseA ? "Fase C " : "", phaseB ? "Neutro" : "");

    if (calInfo.isCalibratingCurrent()) {
        calInfo.multiplier = ONE_MILLION;
        calInfo.conversionFactor = CAL_IRMS_CC;
        calInfo.unit = "A";
    }
    else if (calInfo.isCalibratingVoltage()) {
        calInfo.multiplier = ONE_MILLION;
        calInfo.conversionFactor = CAL_VRMS_CC;
        calInfo.unit = "V";
    }
    else if (calInfo.isCalibratingPower()) {
        calInfo.multiplier = ONE_THOUSAND;
        calInfo.conversionFactor = CAL_POWER_CC;
        calInfo.unit = "W";
    }
    else if (calInfo.isCalibratingPhase()) {
        calInfo.multiplier = ONE_THOUSAND;
        calInfo.conversionFactor = CAL_POWER_CC;
        calInfo.unit = "W";
    }

    Serial.printf("Factores de calibracion: %.6fu%s/LSB. Multiplicador: %.0f\n", calInfo.conversionFactor, calInfo.unit, calInfo.multiplier);

    if (calInfo.function == calCurrentGain) {
        if (calInfo.calA) SPI_Write_32(ADDR_AIGAIN, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BIGAIN, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CIGAIN, 0);
        if (calInfo.calN) SPI_Write_32(ADDR_NIGAIN, 0);
    }
    else if (calInfo.function == calPhaseGain) {
        // SPI_Write_16(ADDR_RUN, 0);    //DSP ON

        if (calInfo.calA) { SPI_Write_32(ADDR_APHCAL0, 0); };
        if (calInfo.calB) { SPI_Write_32(ADDR_BPHCAL0, 0); };
        if (calInfo.calC) { SPI_Write_32(ADDR_CPHCAL0, 0); };
        if (calInfo.calN) { SPI_Write_32(ADDR_NPHCAL, 0); };

        // SPI_Write_32(ADDR_EP_CFG, 0x0011); /* Enable energy accumulation, accumulate samples at 8ksps, latch energy accumulation after EGYRDY and don't reset registers*/
        // SPI_Write_32(ADDR_EGY_TIME, 7999); /* Accumulate 8000 samples */
        // SPI_Write_16(ADDR_RUN, ADE9000_RUN_ON);    //DSP ON
    }
    else if (calInfo.function == calVoltageGain) {
        if (calInfo.calA) SPI_Write_32(ADDR_AVGAIN, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BVGAIN, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CVGAIN, 0);
    }
    else if (calInfo.function == calCurrentOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AIRMSOS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BIRMSOS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CIRMSOS, 0);
        if (calInfo.calN) SPI_Write_32(ADDR_NIRMSOS, 0);
    }
    else if (calInfo.function == calVoltageOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AVRMSOS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BVRMSOS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CVRMSOS, 0);
    }
    else if (calInfo.function == calPowerGain) {
    }
    else if (calInfo.function == calActivePowerOffset) {
    }
    else if (calInfo.function == calReactivePowerOffset) {
    }
    else if (calInfo.function == calFundActivePowerOffset) {
    }
    else if (calInfo.function == calFundReactivePowerOffset) {
    }
    else if (calInfo.function == calFundCurrentOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AIFRMSOS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BIFRMSOS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CIFRMSOS, 0);
    }
    else if (calInfo.function == calFundVoltageOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AVFRMSOS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BVFRMSOS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CVFRMSOS, 0);
    }
    else if (calInfo.function == calCurrentOneOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AIRMSONEOS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BIRMSONEOS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CIRMSONEOS, 0);
        if (calInfo.calN) SPI_Write_32(ADDR_NIRMSONEOS, 0);
    }
    else if (calInfo.function == calVoltageOneOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AVRMSONEOS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BVRMSONEOS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CVRMSONEOS, 0);
    }
    else if (calInfo.function == calCurrentTenOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AIRMS1012OS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BIRMS1012OS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CIRMS1012OS, 0);
        if (calInfo.calN) SPI_Write_32(ADDR_NIRMS1012OS, 0);
    }
    else if (calInfo.function == calVoltageTenOffset) {
        if (calInfo.calA) SPI_Write_32(ADDR_AVRMS1012OS, 0);
        if (calInfo.calB) SPI_Write_32(ADDR_BVRMS1012OS, 0);
        if (calInfo.calC) SPI_Write_32(ADDR_CVRMS1012OS, 0);
    }
    return true;
}

int32_t ADE9000::updateCalibration(float realValue, calibrationInfo* info)
{
    //Borrar promedio si cambia el valor de referencia
    if (realValue != calInfo.realValue) {
        calInfo.clearAccumulators();
        calInfo.samples = 0;
    }
    calInfo.realValue = realValue;

    //Offsets y ganancia de corrientes
    if (calInfo.function == calCurrentGain || calInfo.function == calCurrentOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AIRMS), SPI_Read_32(ADDR_BIRMS), SPI_Read_32(ADDR_CIRMS), SPI_Read_32(ADDR_NIRMS));
    }
    else if (calInfo.function == calFundCurrentOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AIFRMS), SPI_Read_32(ADDR_BIFRMS), SPI_Read_32(ADDR_CIFRMS), 0);
    }
    else if (calInfo.function == calCurrentOneOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AIRMSONE), SPI_Read_32(ADDR_BIRMSONE), SPI_Read_32(ADDR_CIRMSONE), SPI_Read_32(ADDR_NIRMSONE));
    }
    else if (calInfo.function == calCurrentTenOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AIRMS1012), SPI_Read_32(ADDR_BIRMS1012), SPI_Read_32(ADDR_CIRMS1012), SPI_Read_32(ADDR_NIRMS1012));
    }

    //Offsets y ganancia de voltajes
    else if (calInfo.function == calVoltageGain || calInfo.function == calVoltageOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AVRMS), SPI_Read_32(ADDR_BVRMS), SPI_Read_32(ADDR_CVRMS), 0);
    }
    else if (calInfo.function == calFundVoltageOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AVFRMS), SPI_Read_32(ADDR_BVFRMS), SPI_Read_32(ADDR_CVFRMS), 0);
    }
    else if (calInfo.function == calVoltageOneOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AVRMSONE), SPI_Read_32(ADDR_BVRMSONE), SPI_Read_32(ADDR_CVRMSONE), 0);
    }
    else if (calInfo.function == calVoltageTenOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AVRMS1012), SPI_Read_32(ADDR_BVRMS1012), SPI_Read_32(ADDR_CVRMS1012), 0);
    }

    //Calibracion de fases
    else if (calInfo.function == calPhaseGain) {
        uint32_t temp = SPI_Read_32(ADDR_STATUS0);  //Esperar a que se termine de acumular energía
        if (temp & EGY_INTERRUPT_MASK0) {
            int32_t wa, wb, wc, ra, rb, rc;
            SPI_Write_32(ADDR_STATUS0, EGY_INTERRUPT_MASK0);
            wa = (int32_t)SPI_Read_32(ADDR_AWATTHR_HI);
            wb = (int32_t)SPI_Read_32(ADDR_BWATTHR_HI);
            wc = (int32_t)SPI_Read_32(ADDR_CWATTHR_HI);
            ra = (int32_t)SPI_Read_32(ADDR_AVARHR_HI);
            rb = (int32_t)SPI_Read_32(ADDR_BVARHR_HI);
            rc = (int32_t)SPI_Read_32(ADDR_CVARHR_HI);

            calInfo.values.A = calInfo.calcPhaseError(wa, ra, realValue);
            calInfo.values.B = calInfo.calcPhaseError(wb, rb, realValue);
            calInfo.values.C = calInfo.calcPhaseError(wc, rc, realValue);
            calInfo.values.N = 0.0;

            wa = calInfo.calcPhaseErrorReg(realValue, calInfo.values.A);
            wb = calInfo.calcPhaseErrorReg(realValue, calInfo.values.B);
            wc = calInfo.calcPhaseErrorReg(realValue, calInfo.values.C);

            calInfo.loadRegs(wa, wb, wc, 0);
            calInfo.samples++;
        }
    }
    //Offsets y ganancia de potencias
    else if (calInfo.function == calPowerGain || calInfo.function == calActivePowerOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AWATT), SPI_Read_32(ADDR_BWATT), SPI_Read_32(ADDR_CWATT), 0);
    }
    else if (calInfo.function == calReactivePowerOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AVAR), SPI_Read_32(ADDR_BVAR), SPI_Read_32(ADDR_CVAR), 0);
    }
    else if (calInfo.function == calFundActivePowerOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AFWATT), SPI_Read_32(ADDR_BFWATT), SPI_Read_32(ADDR_CFWATT), 0);
    }
    else if (calInfo.function == calFundReactivePowerOffset) {
        calInfo.loadRegs(SPI_Read_32(ADDR_AFVAR), SPI_Read_32(ADDR_BFVAR), SPI_Read_32(ADDR_CFVAR), 0);
    }
    else
        calInfo.samples = -1;

    if (calInfo.function != calPhaseGain) {
        calInfo.calculateValues();
        calInfo.accumulateRegs();
        calInfo.samples++;
    }
    if (info) *info = calInfo;
    return calInfo.samples;
}

bool ADE9000::endCalibration(bool save)
{
    bool result = false;

    preferences.begin("ADE9000", false);
    int64_t expectedValue = calInfo.getExpectedRegisterValue();    //Valor de conversion esperado
    calInfo.calculatePromAcc(); //Calcular promedio de valores acumulados

    if (calInfo.function == calCurrentGain || calInfo.function == calVoltageGain) {
        //gain=((AIRMSexpected / AIRMSmeasured) - 1) * 2^27
        Serial.printf("Parametros de calculo:\n Valor real: %.4f%s, factor de conversion: %.6fu%s/LSB => Valor ADC: %lld\n", calInfo.realValue, calInfo.unit,
            calInfo.conversionFactor, calInfo.unit, expectedValue);

        Serial.printf("Promedio acumuladores:\nA= %.4f -> 0x%x\nB= %.4f -> 0x%x\nC= %.4f -> 0x%x\nN= %.4f -> 0x%x\n",
            calInfo.values.A, calInfo.acc.A, calInfo.values.B, calInfo.acc.B, calInfo.values.C, calInfo.acc.C, calInfo.values.N, calInfo.acc.N);
        double gainA, gainB, gainC, gainN;
        gainA = expectedValue / ((double)calInfo.acc.A);
        gainB = expectedValue / ((double)calInfo.acc.B);
        gainC = expectedValue / ((double)calInfo.acc.C);
        gainN = expectedValue / ((double)calInfo.acc.N);
        calInfo.regs.A = (gainA - 1.0) * ADE9000_2to27;
        calInfo.regs.B = (gainB - 1.0) * ADE9000_2to27;
        calInfo.regs.C = (gainC - 1.0) * ADE9000_2to27;
        calInfo.regs.N = (gainN - 1.0) * ADE9000_2to27;

        char unit = calInfo.unit[0];
        if (unit == 'A') unit = 'I';
        Serial.printf("Ganancias calculadas en base a %d muestras\n", calInfo.samples);
        if (calInfo.calA) Serial.printf("Fase A(R): gain = %0.5f A%cGAIN = 0x%x\n", gainA, unit, calInfo.regs.A);
        if (calInfo.calB) Serial.printf("Fase B(S): gain = %0.5f B%cGAIN = 0x%x\n", gainB, unit, calInfo.regs.B);
        if (calInfo.calC) Serial.printf("Fase C(T): gain = %0.5f C%cGAIN = 0x%x\n", gainC, unit, calInfo.regs.C);
        if (calInfo.calN) Serial.printf("Neutro(N): gain = %0.5f N%cGAIN = 0x%x\n", gainN, unit, calInfo.regs.N);

        if (save) {
            Serial.println("Guardando ajustes en memoria...");
            if (calInfo.function == calCurrentGain) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AIGAIN, (int32_t)calInfo.regs.A);  preferences.putInt("AIGAIN", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BIGAIN, (int32_t)calInfo.regs.B);  preferences.putInt("BIGAIN", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CIGAIN, (int32_t)calInfo.regs.C);  preferences.putInt("CIGAIN", (int32_t)calInfo.regs.C); };
                if (calInfo.calN) { SPI_Write_32(ADDR_NIGAIN, (int32_t)calInfo.regs.N);  preferences.putInt("NIGAIN", (int32_t)calInfo.regs.N); };
            }
            else {
                if (calInfo.calA) { SPI_Write_32(ADDR_AVGAIN, (int32_t)calInfo.regs.A);  preferences.putInt("AVGAIN", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BVGAIN, (int32_t)calInfo.regs.B);  preferences.putInt("BVGAIN", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CVGAIN, (int32_t)calInfo.regs.C);  preferences.putInt("CVGAIN", (int32_t)calInfo.regs.C); };
            }
        }
    }
    else if (calInfo.isCalibratingVoltage() || calInfo.isCalibratingCurrent()) {
        char regName[16] = { 0,0,0,0,0,0,0,0,0,0,0 };
        //offset=(AxRMSexpected^2 - AxRMSmeasured^2) / 2^15
        Serial.printf("Parametros de calculo:\n Valor real: %.4f%s, factor de conversion: %.6fu%s/LSB => Valor ADC: %lld\n", calInfo.realValue, calInfo.unit,
            calInfo.conversionFactor, calInfo.unit, expectedValue);
        Serial.printf("Promedio acumuladores:\nA= %.4f -> 0x%x\nB= %.4f -> 0x%x\nC= %.4f -> 0x%x\nN= %.4f -> 0x%x\n",
            calInfo.values.A, calInfo.acc.A, calInfo.values.B, calInfo.acc.B, calInfo.values.C, calInfo.acc.C, calInfo.values.N, calInfo.acc.N);
        calInfo.regs.A = (double)(expectedValue * expectedValue - calInfo.acc.A * calInfo.acc.A) / (double)ADE9000_2to15;
        calInfo.regs.B = (double)(expectedValue * expectedValue - calInfo.acc.B * calInfo.acc.B) / (double)ADE9000_2to15;
        calInfo.regs.C = (double)(expectedValue * expectedValue - calInfo.acc.C * calInfo.acc.C) / (double)ADE9000_2to15;
        calInfo.regs.N = (double)(expectedValue * expectedValue - calInfo.acc.N * calInfo.acc.N) / (double)ADE9000_2to15;

        //Crear el nombre del registro correspondiente
        regName[0] = calInfo.unit[0] == 'A' ? 'I' : 'V';
        if (calInfo.function == calFundVoltageOffset || calInfo.function == calFundCurrentOffset) strcat(regName, "F");   //Fundamental
        strcat(regName, "RMS");
        if (calInfo.function == calVoltageOneOffset || calInfo.function == calVoltageOneOffset) strcat(regName, "ONE"); //One
        if (calInfo.function == calVoltageTenOffset || calInfo.function == calVoltageTenOffset) strcat(regName, "1012");; //1012
        strcat(regName, "OS");

        Serial.printf("Offsets calculados en base a %d muestras\n", calInfo.samples);
        if (calInfo.calA) Serial.printf("Fase A(R): A%s = 0x%x\n", regName, calInfo.regs.A);
        if (calInfo.calB) Serial.printf("Fase B(S): B%s = 0x%x\n", regName, calInfo.regs.B);
        if (calInfo.calC) Serial.printf("Fase C(T): C%s = 0x%x\n", regName, calInfo.regs.C);
        if (calInfo.calN) Serial.printf("Neutro(N): N%s = 0x%x\n", regName, calInfo.regs.N);

        if (save) {
            Serial.println("Guardando ajustes en memoria...");
            if (calInfo.function == calCurrentOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AIRMSOS, (int32_t)calInfo.regs.A);  preferences.putInt("AIRMSOS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BIRMSOS, (int32_t)calInfo.regs.B);  preferences.putInt("BIRMSOS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CIRMSOS, (int32_t)calInfo.regs.C);  preferences.putInt("CIRMSOS", (int32_t)calInfo.regs.C); };
                if (calInfo.calN) { SPI_Write_32(ADDR_NIRMSOS, (int32_t)calInfo.regs.N);  preferences.putInt("NIRMSOS", (int32_t)calInfo.regs.N); };
            }
            else if (calInfo.function == calVoltageOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AVRMSOS, (int32_t)calInfo.regs.A);  preferences.putInt("AVRMSOS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BVRMSOS, (int32_t)calInfo.regs.B);  preferences.putInt("BVRMSOS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CVRMSOS, (int32_t)calInfo.regs.C);  preferences.putInt("CVRMSOS", (int32_t)calInfo.regs.C); };
            }
            else if (calInfo.function == calFundCurrentOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AIFRMSOS, (int32_t)calInfo.regs.A);  preferences.putInt("AIFRMSOS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BIFRMSOS, (int32_t)calInfo.regs.B);  preferences.putInt("BIFRMSOS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CIFRMSOS, (int32_t)calInfo.regs.C);  preferences.putInt("CIFRMSOS", (int32_t)calInfo.regs.C); };
            }
            else if (calInfo.function == calFundVoltageOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AVFRMSOS, (int32_t)calInfo.regs.A);  preferences.putInt("AVFRMSOS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BVFRMSOS, (int32_t)calInfo.regs.B);  preferences.putInt("BVFRMSOS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CVFRMSOS, (int32_t)calInfo.regs.C);  preferences.putInt("CVFRMSOS", (int32_t)calInfo.regs.C); };
            }
            else if (calInfo.function == calCurrentOneOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AIRMSONEOS, (int32_t)calInfo.regs.A);  preferences.putInt("AIRMSONEOS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BIRMSONEOS, (int32_t)calInfo.regs.B);  preferences.putInt("BIRMSONEOS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CIRMSONEOS, (int32_t)calInfo.regs.C);  preferences.putInt("CIRMSONEOS", (int32_t)calInfo.regs.C); };
                if (calInfo.calN) { SPI_Write_32(ADDR_NIRMSONEOS, (int32_t)calInfo.regs.N);  preferences.putInt("NIRMSONEOS", (int32_t)calInfo.regs.N); };
            }
            else if (calInfo.function == calVoltageOneOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AVRMSONEOS, (int32_t)calInfo.regs.A);  preferences.putInt("AVRMSONEOS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BVRMSONEOS, (int32_t)calInfo.regs.B);  preferences.putInt("BVRMSONEOS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CVRMSONEOS, (int32_t)calInfo.regs.C);  preferences.putInt("CVRMSONEOS", (int32_t)calInfo.regs.C); };
            }
            else if (calInfo.function == calCurrentTenOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AIRMS1012OS, (int32_t)calInfo.regs.A);  preferences.putInt("AIRMS1012OS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BIRMS1012OS, (int32_t)calInfo.regs.B);  preferences.putInt("BIRMS1012OS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CIRMS1012OS, (int32_t)calInfo.regs.C);  preferences.putInt("CIRMS1012OS", (int32_t)calInfo.regs.C); };
                if (calInfo.calN) { SPI_Write_32(ADDR_NIRMS1012OS, (int32_t)calInfo.regs.N);  preferences.putInt("NIRMS1012OS", (int32_t)calInfo.regs.N); };
            }
            else if (calInfo.function == calVoltageTenOffset) {
                if (calInfo.calA) { SPI_Write_32(ADDR_AVRMS1012OS, (int32_t)calInfo.regs.A);  preferences.putInt("AVRMS1012OS", (int32_t)calInfo.regs.A); };
                if (calInfo.calB) { SPI_Write_32(ADDR_BVRMS1012OS, (int32_t)calInfo.regs.B);  preferences.putInt("BVRMS1012OS", (int32_t)calInfo.regs.B); };
                if (calInfo.calC) { SPI_Write_32(ADDR_CVRMS1012OS, (int32_t)calInfo.regs.C);  preferences.putInt("CVRMS1012OS", (int32_t)calInfo.regs.C); };
            }
        }
    }
    else if (calInfo.isCalibratingPhase()) {
        Serial.printf("Valores calculados en base a %d muestras\n", calInfo.samples);
        if (calInfo.calA) Serial.printf("Fase A(R): %.3f° APHCAL0 = 0x%x\n", calInfo.values.A, calInfo.regs.A);
        if (calInfo.calB) Serial.printf("Fase B(S): %.3f° BPHCAL0 = 0x%x\n", calInfo.values.B, calInfo.regs.B);
        if (calInfo.calC) Serial.printf("Fase C(T): %.3f° CPHCAL0 = 0x%x\n", calInfo.values.C, calInfo.regs.C);
        if (calInfo.calN) Serial.printf("Neutro(N): %.3f° NPHCAL0 = 0x%x\n", calInfo.values.N, calInfo.regs.N);

        if (calInfo.calA) { SPI_Write_32(ADDR_APHCAL0, (int32_t)calInfo.regs.A);  preferences.putInt("APHCAL0", (int32_t)calInfo.regs.A); };
        if (calInfo.calB) { SPI_Write_32(ADDR_BPHCAL0, (int32_t)calInfo.regs.B);  preferences.putInt("BPHCAL0", (int32_t)calInfo.regs.B); };
        if (calInfo.calC) { SPI_Write_32(ADDR_CPHCAL0, (int32_t)calInfo.regs.C);  preferences.putInt("CPHCAL0", (int32_t)calInfo.regs.C); };
        if (calInfo.calN) { SPI_Write_32(ADDR_NPHCAL, (int32_t)calInfo.regs.N);  preferences.putInt("NPHCAL", (int32_t)calInfo.regs.N); };
    }
    calInfo.function = calNone;
    preferences.end();

    if (!save) loadCalibration();
    return result;
}


void ADE9000::enableOverCurrentDetection(float level, uint32_t channels)
{
    if (level < 0 || level > getMaxInputCurrent()) return;
    channels &= 0xF;

    int32_t value = level * (float)ONE_MILLION / (CAL_IRMS_CC) * 0.03125;  //OI_LVL = xIRMSONE*2^−5
    Serial.printf("enableOverCurrentDetection: %.2fV -> OILVL=%d, channels=0x%X\n", level, value, channels);
    SPI_Write_32(ADDR_OILVL, (uint32_t)value);

    ADE_CONFIG3_t cfg;
    cfg.raw = SPI_Read_16(ADDR_CONFIG3);
    cfg.OC_EN = channels;
    SPI_Write_16(ADDR_CONFIG3, cfg.raw);
}

ADE_OISTATUS_t ADE9000::checkOverCurrentStatus()
{
    ADE_OISTATUS_t status;
    status.raw = SPI_Read_16(ADDR_OISTATUS);
    return status;
}

void ADE9000::readOverCurrentLevels(struct CurrentRMSRegs* Data)
{
    Data->CurrentRMSReg_A = int32_t(SPI_Read_32(ADDR_OIA) & 0x00FFFFFF);
    Data->CurrentRMSReg_B = int32_t(SPI_Read_32(ADDR_OIB) & 0x00FFFFFF);
    Data->CurrentRMSReg_C = int32_t(SPI_Read_32(ADDR_OIC) & 0x00FFFFFF);
    Data->CurrentRMSReg_N = int32_t(SPI_Read_32(ADDR_OIN) & 0x00FFFFFF);
    Data->CurrentRMS_A = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_A) / ONE_MILLION * 32.0;  //OI_LVL = xIRMSONE*2^−5
    Data->CurrentRMS_B = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_B) / ONE_MILLION * 32.0;
    Data->CurrentRMS_C = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_C) / ONE_MILLION * 32.0;
    Data->CurrentRMS_N = (float)(CAL_IRMS_CC * Data->CurrentRMSReg_N) / ONE_MILLION * 32.0;
}


void ADE9000::setDipDetectionLevels(float level, uint32_t cycles)
{
    if (level < 0 || level > getMaxInputVoltage()) return;
    if (cycles < 2 || cycles > 100) return;

    int32_t value = level * (float)ONE_MILLION / (CAL_VRMS_CC) * 0.03125;  //DIP_LVL = xVRMSONE*2^−5
    Serial.printf("setDipDetectionLevels: %.2fV -> DIPLVL=%d, DIPCYC=%d\n", level, value, cycles);
    SPI_Write_32(ADDR_DIP_LVL, (uint32_t)value);
    SPI_Write_16(ADDR_DIP_CYC, cycles);
}

uint32_t ADE9000::checkDipStatus()
{
    uint32_t res = 0x0;
    ADE_EVENT_STATUS_t status = readEventStatus();
    if (status.DIPA) res |= 0x1;
    if (status.DIPB) res |= 0x2;
    if (status.DIPC) res |= 0x4;
    return res;
}

void ADE9000::readDipLevels(struct VoltageRMSRegs* Data)
{
    Data->VoltageRMSReg_A = int32_t(SPI_Read_32(ADDR_DIPA) & 0x00FFFFFF);
    Data->VoltageRMSReg_B = int32_t(SPI_Read_32(ADDR_DIPB) & 0x00FFFFFF);
    Data->VoltageRMSReg_C = int32_t(SPI_Read_32(ADDR_DIPC) & 0x00FFFFFF);
    Data->VoltageRMS_A = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_A) / ONE_MILLION * 32.0; //DIP_LVL = xVRMSONE*2^−5
    Data->VoltageRMS_B = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_B) / ONE_MILLION * 32.0;
    Data->VoltageRMS_C = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_C) / ONE_MILLION * 32.0;
}

void ADE9000::setSwellDetectionLevels(float level, uint32_t cycles)
{
    if (level < 0 || level > getMaxInputVoltage()) return;
    if (cycles < 2 || cycles > 100) return;

    int32_t value = level * (float)ONE_MILLION / (CAL_VRMS_CC) * 0.03125;  //SWELL_LVL = xVRMSONE*2^−5
    Serial.printf("setSwellDetectionLevels: %.2fV -> SWELLLVL=%d, SWELLCYC=%d\n", level, value, cycles);
    SPI_Write_32(ADDR_SWELL_LVL, (uint32_t)level);
    SPI_Write_16(ADDR_SWELL_CYC, cycles);
}


uint32_t ADE9000::checkSweelStatus()
{
    uint32_t res = 0x0;
    ADE_EVENT_STATUS_t status = readEventStatus();
    if (status.SWELLA) res |= 0x1;
    if (status.SWELLB) res |= 0x2;
    if (status.SWELLC) res |= 0x4;
    return res;
}

void ADE9000::readSwellLevels(struct VoltageRMSRegs* Data)
{
    Data->VoltageRMSReg_A = int32_t(SPI_Read_32(ADDR_SWELLA) & 0x00FFFFFF);
    Data->VoltageRMSReg_B = int32_t(SPI_Read_32(ADDR_SWELLB) & 0x00FFFFFF);
    Data->VoltageRMSReg_C = int32_t(SPI_Read_32(ADDR_SWELLC) & 0x00FFFFFF);
    Data->VoltageRMS_A = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_A) / ONE_MILLION * 32.0; //SWELL_LVL = xVRMSONE*2^−5
    Data->VoltageRMS_B = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_B) / ONE_MILLION * 32.0;
    Data->VoltageRMS_C = (float)(CAL_VRMS_CC * Data->VoltageRMSReg_C) / ONE_MILLION * 32.0;
}