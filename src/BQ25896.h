#pragma once
#ifndef bq25896_h_
#define bq25896_h_
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// #define bitRead(value, bit) (((value) >> (bit)) & 0x01)
//#define bitSet(value, bit) ((value) |= (1UL << (bit)))
//#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define ENABLED         HIGH
#ifndef DISABLED
#define DISABLED        LOW
#endif

class BQ25896
{
public:
    typedef union {
        uint8_t raw;
        struct {
            unsigned NTC_FAULT : 2;         //NTC Fault Status  [00 – Normal, 01 – TS Cold, 10 – TS Hot]
            unsigned NTC_FAULT_MODE : 1;     //NTC MODE Fault Status [0 - buck mode, 1 - boost mode]
            unsigned BAT_FAULT : 1;            //Battery Fault Status [0 – Normal, 1 – BATOVP (VBAT > VBATOVP)]
            unsigned CHRG_FAULT : 2;            //Charge Fault Status [00 – Normal, 01 – Input fault (VBUS > VACOV or VBAT < VBUS < VVBUSMIN(typical 3.8V)), 10 - Thermal shutdown, 11 – Charge Safety Timer Expiration]
            unsigned BOOST_FAULT : 1;           //Boost Mode Fault Status [0 – Normal, 1 – VBUS overloaded in OTG, or VBUS OVP, or battery is too low in boost mode]
            unsigned WATCHDOG_FAULT : 1;         //Watchdog Fault Status [0 – Normal, 1- Watchdog timer expiration]
        };
    } BQ_FAULT;

    enum class TS_RANK
    {
        NORMAL = 0,     //No hay error
        HOT = 2,        //Temperatura alta en modo buck
        COLD = 3,       //Temperatura baja en modo buck
        BOOST_COLD = 3, //Temperatura baja en modo boost
        BOOST_HOT = 4   //Temperatura alta en modo boost
    };

    enum class CHG_FAULT
    {
        NORMAL = 0, //00 – Normal
        INPUT_FAULT = 1,        //Input fault (VBUS > VACOV or VBAT < VBUS < VVBUSMIN(typical 3.8V))
        THERMAL_SHUTDOWN = 2,
        TIMER_EXPIRED = 3
    };

    enum class VBUS_STAT
    {
        NO_INPUT = 0,
        USB_HOST = 1,
        ADAPTER = 2,
        OTG = 7
    };

    enum class CHG_STAT
    {
        NOT_CHARGING = 0,
        PRE_CHARGE = 1,
        FAST_CHARGE = 2,
        CHARGE_DONE = 3
    };

    enum class VSYS_STAT
    {
        NOT_IN_VSYS = 0,
        IN_VSYSMIN = 1
    };
private:
    float RtoTemp(float R)
    {
        float temperature = R / 10000.0f;
        temperature = log(temperature);
        temperature /= 3950.0f;
        temperature += 1.f / 298.15f;
        temperature = 1.f / temperature;
        return temperature - 273.25f;
    }
    const uint8_t I2C_ADDR = 0x6A;
    enum class REG
    {
        ILIM = 0x00,
        VINDPM_OS = 0x01,
        ADC_CTRL = 0x02,
        SYS_CTRL = 0x03,
        ICHG = 0x04,
        IPRE_ITERM = 0x05,
        VREG = 0x06,
        TIMER = 0x07,
        BAT_COMP = 0x08,
        CTRL1 = 0x09,
        BOOST_CTRL = 0x0A,
        VBUS_STAT = 0x0B,
        FAULT_ = 0x0C,
        VINDPM = 0x0D,
        BATV = 0x0E,
        SYSV = 0x0F,
        TSPCT = 0x10,
        VBUSV = 0x11,
        ICHGR = 0x12,
        IDPM_LIM = 0x13,
        CTRL2 = 0x14
    };


private:

    void write(const REG reg, const bool stop = true)
    {
        wire->beginTransmission(I2C_ADDR);
        wire->write((uint8_t)reg);
        wire->endTransmission(stop);
    }

    void write_(const REG reg, const uint8_t data, const bool stop = true)
    {
        wire->beginTransmission(I2C_ADDR);
        wire->write((uint8_t)reg);
        wire->write(data);
        wire->endTransmission(stop);
    }

    byte read(const REG reg)
    {
        byte data = 0;
        write(reg, false);
        wire->requestFrom((uint8_t)I2C_ADDR, (uint8_t)1);
        if (wire->available() == 1)
            data = wire->read();
        return data;
    }

    TwoWire* wire;
    float   VBUS,
        VSYS,
        VBAT,
        ICHG,
        TSPCT,
        VINDPM,
        Temperature;

    VBUS_STAT   VBUS_STATUS;
    CHG_STAT    CHG_STATUS;
    TS_RANK     TS_RANK_;
    CHG_FAULT   CHG_FAULT_;
    VSYS_STAT   VSYS_STAT_;
    bool VBUS_attached;
    bool thermal_regulation;
    void setADC_enabled(void)
    {
        byte data = read(REG::ADC_CTRL);
        data |= (1UL << (7));       // start A/D convertion
        data |= (1UL << 6);         // set continuous convertion
        write_(REG::ADC_CTRL, data);
    }
    void takeVBUSData(void)
    {
        byte data = read(REG::VBUSV);
        VBUS_attached = (((data) >> (7)) & 0x01) ? true : false;
        data &= ~(1UL << (7));
        VBUS = 2.6f;
        VBUS += (float)data * 0.1f;
    }

    void takeVSYSData(void)
    {
        byte data = read(REG::SYSV);
        VSYS = (float)data * 0.02f;
        VSYS += 2.304f;
    }

    void takeVBATData()
    {
        byte data = read(REG::BATV);
        thermal_regulation = (((data) >> (7)) & 0x01) ? true : false;
        data &= ~(1UL << (7));
        VBAT = (float)data * 0.02f;
        VBAT += 2.304f;
    }

    void takeTSPCTData(void)
    {
        int data = (int)read(REG::TSPCT);
        TSPCT = (float)data * 0.465f + 21.f;

    }
    void takeICHGData(void)
    {
        uint8_t data = read(REG::ICHGR);
        ICHG = (float)data * 0.05f;
    }

    void takeVBUSSTAT(void)
    {
        uint8_t data = read(REG::VBUS_STAT);
        // parsing VBUS_STAT
        if (((data) >> (7)) & 0x01)
            VBUS_STATUS = VBUS_STAT::OTG;
        else
        {
            if (((data) >> (6)) & 0x01)
                VBUS_STATUS = VBUS_STAT::ADAPTER;
            else if ((((data) >> (5)) & 0x01))
                VBUS_STATUS = VBUS_STAT::USB_HOST;
            else
                VBUS_STATUS = VBUS_STAT::NO_INPUT;
        };
        // parsing CHG_STAT
        if (((data) >> (4)) & 0x01)
        {
            if (((data) >> (3)) & 0x01) { CHG_STATUS = CHG_STAT::CHARGE_DONE; }
            else { CHG_STATUS = CHG_STAT::FAST_CHARGE; };
        }
        else
        {
            if (((data) >> (3)) & 0x01)
                CHG_STATUS = CHG_STAT::PRE_CHARGE;
            else
                CHG_STATUS = CHG_STAT::NOT_CHARGING;

        };
        // parsing VSYS_status
        if (((data) >> (0)) & 0x01)
        {
            VSYS_STAT_ = VSYS_STAT::IN_VSYSMIN;
        }
        else
        {
            VSYS_STAT_ = VSYS_STAT::NOT_IN_VSYS;
        };
    }

    void takeFaultData(void)
    {
        uint8_t data = read(REG::FAULT_);
        if ((data & 0x7) == 0)
            TS_RANK_ = TS_RANK::NORMAL;
        else if ((data & 0x7) == 1)
            TS_RANK_ = TS_RANK::COLD;
        else if ((data & 0x7) == 2)
            TS_RANK_ = TS_RANK::HOT;
        else if ((data & 0x7) == 5)
            TS_RANK_ = TS_RANK::BOOST_COLD;
        else if ((data & 0x7) == 6)
            TS_RANK_ = TS_RANK::BOOST_HOT;
        else
            TS_RANK_ = TS_RANK::HOT;  //Error, no se debería dar esta condición

        uint8_t val = (data & 0x30) >> 4;
        if (val == 0)
            CHG_FAULT_ = CHG_FAULT::NORMAL;
        else if (val == 1)
            CHG_FAULT_ = CHG_FAULT::INPUT_FAULT;
        else if (val == 2)
            CHG_FAULT_ = CHG_FAULT::THERMAL_SHUTDOWN;
        else if (val == 3)
            CHG_FAULT_ = CHG_FAULT::TIMER_EXPIRED;
    }

    //DMA_HandleTypeDef s_DMAHandle;

public:
    BQ25896(TwoWire& w) : wire(&w) {};
    void begin(void) { setADC_enabled(); }
    void properties(void)
    {
        //  read register status
        takeVBUSSTAT();
        takeFaultData();
        // read register ADC
        takeVBUSData();
        takeVSYSData();
        takeVBATData();
        takeTSPCTData();
        takeICHGData();
        setADC_enabled();
    }

    BQ_FAULT getFaultState() {
        BQ_FAULT res;
        res.raw = read(REG::FAULT_);
        return res;
    };

    float getVBUS(void) { return VBUS; }
    float getVSYS(void) { return VSYS; }
    float getVBAT(void) { return VBAT; }
    float getICHG(void) { return ICHG; }
    float getTSPCT(void)
    {
        int data = (int)read(REG::TSPCT);
        float tmp = (float)data * 0.465f + 21.f;
        this->TSPCT = tmp;
        return tmp;
    }

    float getTemperature(void)
    {
        float VTS = 5.0f * TSPCT / 100.f;
        float RP = (VTS * 5230.f) / (5.f - VTS);
        float NTC = (RP * 30100.f) / (30100.f - RP);
        return RtoTemp(NTC);
    }

    CHG_STAT getCHG_STATUS(void) { return CHG_STATUS; }
    VBUS_STAT getVBUS_STATUS(void) { return VBUS_STATUS; }
    VSYS_STAT getVSYS_STATUS(void) { return VSYS_STAT_; }
    TS_RANK getTemp_Rank(void) { return TS_RANK_; }
    CHG_FAULT getCHG_Fault_STATUS(void) { return CHG_FAULT_; }

    float getFast_Charge_Current_Limit(void)
    {
        byte data = read(REG::ICHG);
        data &= ~(1UL << 7);
        return (float)data * 0.064f;
    }

    float getInput_Current_Limit(void)
    {
        byte data = read(REG::ILIM);
        data &= ~(1UL << 7);
        data &= ~(1UL << 6);
        return (float)data * 0.05f;
    }

    float getPreCharge_Current_Limit(void)
    {
        byte data = read(REG::IPRE_ITERM);
        data &= 0b11110000;
        return (float)(data >> 4) * 0.064f + 0.064f;
    }

    float getTermination_Current_Limit(void)
    {
        byte data = read(REG::IPRE_ITERM);
        data &= 0b00001111;
        return (float)(data) * 0.064f + 0.064f;
    }

    float getCharge_Voltage_Limit(void)
    {
        byte data = read(REG::VREG);
        data = data >> 2;
        return (float)(data) * 0.016f + 3.840f;
    }

    void setVINDPM(float trhreshold) {
        uint8_t data = read(REG::VINDPM);
        if (trhreshold < 3.9) trhreshold = 3.9;
        else if (trhreshold > 15.3) trhreshold = 15.3;
        trhreshold -= 2.6;

        data |= ((uint8_t)(trhreshold * 10)) & 0x7F;
        write_(REG::VINDPM, data);
    };

    void setDPMOptions(float trhreshold, bool force) {
        if (trhreshold < 3.9) trhreshold = 3.9;
        else if (trhreshold > 15.3) trhreshold = 15.3;
        trhreshold -= 2.6;

        uint8_t data = ((uint8_t)(trhreshold * 10)) & 0x7F;
        if (force) data |= 0x80;
        write_(REG::VINDPM, data);
    };

    void setFast_Charge_Current_Limit(float cur)
    {
        cur = (cur > 3.008f) ? 3.008f : ((cur < 0) ? 0 : cur);
        cur /= 8.128;
        cur *= 127;
        byte reg = read(REG::ICHG);
        byte tmp = 128 | (byte)cur;
        write_(REG::ICHG, tmp);
    }

    void setInput_Current_Limit(float cur)
    {
        cur = (cur > 3.25f) ? 3.25f : ((cur < 100) ? 100 : cur);
        cur -= 100;
        cur /= 3.15;
        cur *= 63;
        byte data = read(REG::ILIM);
        byte tmp = (((data) >> (7)) & 0x01) | (((data) >> (6)) & 0x01) | (byte)cur;
        write_(REG::ILIM, tmp);
    }

    void setPreCharge_Current_Limit(float cur)
    {
        cur = (cur > 1.024f) ? 1.024f : ((cur < 0.064f) ? 0.064f : cur);
        cur -= 0.064f;
        cur /= 0.96f;
        cur *= 0x0f;
        byte data = read(REG::IPRE_ITERM);
        data &= 0b00001111;
        data |= (byte)cur << 4;
        write_(REG::IPRE_ITERM, data);
    }
    void setTermination_Current_Limit(float cur)
    {
        cur = (cur > 1.024f) ? 1.024f : ((cur < 0.064f) ? 0.064f : cur);
        cur -= 0.064f;
        cur /= 0.96f;
        cur *= 0x0f;
        byte data = read(REG::IPRE_ITERM);
        data &= 0b11110000;
        data |= (byte)cur;
        write_(REG::IPRE_ITERM, data);
    }

    void setCharge_Voltage_Limit(float cur)
    {
        cur = (cur > 4.608f) ? 4.608f : ((cur < 3.840f) ? 3.840f : cur);
        cur -= 3.840f;
        cur /= 1.008f;
        cur *= 63;
        byte data = read(REG::VREG);
        data &= 0b00000011;
        data |= (byte)cur << 2;
        write_(REG::VREG, data);
    }


    void setBoost_Voltage(float cur)
    {
        cur = (cur > 5.51f) ? 5.51f : ((cur < 4.55f) ? 4.55f : cur);
        cur -= 4.55f;
        cur /= 0.96f;
        cur *= 16;
        byte data = (byte)cur << 4;
        write_(REG::BOOST_CTRL, data);
    }

    void setBatLoad(uint8_t mode)
    {
        byte data = read(REG::SYS_CTRL);
        if (mode == DISABLED)
        {
            data &= ~(1UL << 7UL);
        }
        else
        {
            data |= (1UL << 7UL);
        }
        data |= (1UL << 1UL);
        data |= (1UL << 2UL);
        data |= (1UL << 3UL);

        write_(REG::SYS_CTRL, data);
    }

    void clearTimer()
    {
        byte data = read(REG::SYS_CTRL);
        data |= 0x40;
        write_(REG::SYS_CTRL, data);
    }

    void setChargeEnable(uint8_t mode)
    {
        byte data = read(REG::SYS_CTRL);
        if (mode == DISABLED)
        {
            data &= ~(1U << 4U);
        }
        else
        {
            data |= (1U << 4U);
        }
        write_(REG::SYS_CTRL, data);
    }
    void setForceICO(uint8_t mode)
    {
        byte data = read(REG::CTRL1);
        if (mode == DISABLED)
        {
            data &= ~(1U << 7U);
            data &= ~(1U << 0U);
            data &= ~(1U << 1U);
        }
        else
        {
            data |= (1U << 7U);
            data |= (1U << 0U);
            data |= (1U << 1U);
        }
        write_(REG::CTRL1, data);
    }
};

#endif
