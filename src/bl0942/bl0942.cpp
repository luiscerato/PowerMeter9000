#include "bl0942.h"
#include "Arduino.h"
#include "pins.h"
#include "wifiUtils.h"

static const uint8_t BL0942_READ_COMMAND = 0x58;
static const uint8_t BL0942_FULL_PACKET = 0xAA;
static const uint8_t BL0942_PACKET_HEADER = 0x55;

static const uint8_t BL0942_WRITE_COMMAND = 0xA8;

static const uint8_t BL0942_REG_I_RMSOS = 0x12;
static const uint8_t BL0942_REG_WA_CREEP = 0x14;
static const uint8_t BL0942_REG_I_FAST_RMS_TH = 0x15;
static const uint8_t BL0942_REG_I_FAST_RMS_CYC = 0x16;
static const uint8_t BL0942_REG_FREQ_CYC = 0x17;
static const uint8_t BL0942_REG_OT_FUNX = 0x18;
static const uint8_t BL0942_REG_MODE = 0x19;
static const uint8_t BL0942_REG_SOFT_RESET = 0x1C;
static const uint8_t BL0942_REG_USR_WRPROT = 0x1D;
static const uint8_t BL0942_REG_TPS_CTRL = 0x1B;

static const uint32_t BL0942_REG_MODE_RESV = 0x03;
static const uint32_t BL0942_REG_MODE_CF_EN = 0x04;
static const uint32_t BL0942_REG_MODE_RMS_UPDATE_SEL = 0x08;
static const uint32_t BL0942_REG_MODE_FAST_RMS_SEL = 0x10;
static const uint32_t BL0942_REG_MODE_AC_FREQ_SEL = 0x20;
static const uint32_t BL0942_REG_MODE_CF_CNT_CLR_SEL = 0x40;
static const uint32_t BL0942_REG_MODE_CF_CNT_ADD_SEL = 0x80;
static const uint32_t BL0942_REG_MODE_UART_RATE_19200 = 0x200;
static const uint32_t BL0942_REG_MODE_UART_RATE_38400 = 0x300;
static const uint32_t BL0942_REG_MODE_DEFAULT =
BL0942_REG_MODE_RESV | BL0942_REG_MODE_CF_EN | BL0942_REG_MODE_CF_CNT_ADD_SEL | BL0942_REG_MODE_UART_RATE_38400;

static const uint32_t BL0942_REG_SOFT_RESET_MAGIC = 0x5a5a5a;
static const uint32_t BL0942_REG_USR_WRPROT_MAGIC = 0x55;

// 23-byte packet, 11 bits per byte, 2400 baud: about 105ms
static const uint32_t PKT_TIMEOUT_MS = 200;


void BL0942::begin()
{
    begin(-1, -1);
}

void BL0942::begin(int32_t rx, int32_t tx)
{
    debugI("Iniciando BL0942!");
    HardwareSerial::begin(4800, SERIAL_8N1, rx, tx);
    debugI("Setup 1");
    setup();
    debugI("Setup 2");
    setup();
    HardwareSerial::begin(38400, SERIAL_8N1, rx, tx);
}

void BL0942::loop() {
    DataPacket buffer;
    int avail = this->available();

    if (!avail) {
        if (millis() - updateTime > 99) {
            updateTime = millis();
            update();   //Enviar comando de actualizacion cada 1 segundos.
        }
        return;
    }
    if (avail < sizeof(buffer)) { 
        if (!this->rx_start_) {
            this->rx_start_ = millis();
        }
        else if (millis() > this->rx_start_ + PKT_TIMEOUT_MS) {
            debugE("Junk on wire. Throwing away partial message (%d bytes)", avail);
            this->readBytes((uint8_t*)&buffer, avail);
            this->rx_start_ = 0;
            update();
        }
        return;
    }

    if (this->readBytes((uint8_t*)&buffer, sizeof(buffer))) {
        if (this->validate_checksum_(&buffer)) {
            this->received_package_(&buffer);
        }
    }
    // update();
    updateTime = millis();
    this->rx_start_ = 0;
}


bool BL0942::validate_checksum_(DataPacket* data) {
    uint8_t checksum = BL0942_READ_COMMAND | this->address_;
    // Whole package but checksum
    uint8_t* raw = (uint8_t*)data;
    for (uint32_t i = 0; i < sizeof(*data) - 1; i++) {
        checksum += raw[i];
    }
    checksum ^= 0xFF;
    if (checksum != data->checksum) {
        debugV("BL0942 invalid checksum! 0x%02X != 0x%02X", checksum, data->checksum);
    }
    return checksum == data->checksum;
}

void BL0942::write_reg_(uint8_t reg, uint32_t val) {
    uint8_t pkt[6];

    this->flush();
    pkt[0] = BL0942_WRITE_COMMAND | this->address_;
    pkt[1] = reg;
    pkt[2] = (val & 0xff);
    pkt[3] = (val >> 8) & 0xff;
    pkt[4] = (val >> 16) & 0xff;
    pkt[5] = (pkt[0] + pkt[1] + pkt[2] + pkt[3] + pkt[4]) ^ 0xff;
    this->write(pkt, 6);
    delay(1);
}

int BL0942::read_reg_(uint8_t reg) {
    union {
        uint8_t b[4];
        uint32_t le32;
    } resp;

    this->write(BL0942_READ_COMMAND | this->address_);
    this->write(reg);
    this->flush();
    if (this->readBytes(resp.b, 4) &&
        resp.b[3] ==
        (uint8_t)((BL0942_READ_COMMAND + this->address_ + reg + resp.b[0] + resp.b[1] + resp.b[2]) ^ 0xff)) {
        resp.b[3] = 0;
        return resp.le32;
    }
    return -1;
}

void BL0942::update() {
    this->write(BL0942_READ_COMMAND | this->address_);
    this->write(BL0942_FULL_PACKET);
}

void BL0942::setup() {
    // If either current or voltage references are set explicitly by the user,
    // calculate the power reference from it unless that is also explicitly set.
    if ((this->current_reference_set_ || this->voltage_reference_set_) && !this->power_reference_set_) {
        this->power_reference_ = (this->voltage_reference_ * this->current_reference_ * 3537.0 / 305978.0) / 73989.0;
        this->power_reference_set_ = true;
    }

    // Similarly for energy reference, if the power reference was set by the user
    // either implicitly or explicitly.
    if (this->power_reference_set_ && !this->energy_reference_set_) {
        this->energy_reference_ = this->power_reference_ * 3600000 / 419430.4;
        this->energy_reference_set_ = true;
    }

    this->write_reg_(BL0942_REG_USR_WRPROT, BL0942_REG_USR_WRPROT_MAGIC);
    if (this->reset_)
        this->write_reg_(BL0942_REG_SOFT_RESET, BL0942_REG_SOFT_RESET_MAGIC);

    uint32_t mode = BL0942_REG_MODE_DEFAULT;
    //mode |= BL0942_REG_MODE_RMS_UPDATE_SEL; /* 800ms refresh time */
    if (this->line_freq_ == LINE_FREQUENCY_60HZ)
        mode |= BL0942_REG_MODE_AC_FREQ_SEL;
    this->write_reg_(BL0942_REG_MODE, mode);

    this->write_reg_(BL0942_REG_USR_WRPROT, 0);

    if (this->read_reg_(BL0942_REG_MODE) != mode)
        debugE("BL0942 setup failed!");
    else
        dump_config();

    this->flush();
}

void BL0942::received_package_(DataPacket* data) {
    // Bad header
    if (data->frame_header != BL0942_PACKET_HEADER) {
        debugE("Invalid data. Header mismatch: %d", data->frame_header);
        return;
    }

    // cf_cnt is only 24 bits, so track overflows
    uint32_t cf_cnt = get24bReg(data->cf_cnt);
    cf_cnt |= this->prev_cf_cnt_ & 0xff000000;
    if (cf_cnt < this->prev_cf_cnt_) {
        cf_cnt += 0x1000000;
    }
    this->prev_cf_cnt_ = cf_cnt;

    v_rms = get24bReg(data->v_rms) / voltage_reference_;
    i_rms = get24bReg(data->i_rms) / current_reference_;
    watt = get24bReg(data->watt) / power_reference_;
    total_energy_consumption = cf_cnt / energy_reference_;
    frequency = 1000000.0f / ((uint16_t)data->frequency[0] | ((uint16_t)data->frequency[1]<<8));

    if (v_rms < 0.1) v_rms = 0.0;

    debugV("BL0942: U %fV, I %fA, P %fW, Cnt %" PRId32 ", ∫P %fkWh, frequency %fHz, status 0x%08X", v_rms, i_rms,
        watt, cf_cnt, total_energy_consumption, frequency, data->status);
}

void BL0942::dump_config() {  // NOLINT(readability-function-cognitive-complexity)
    debugI("BL0942:");
    debugI("  Reset: %d", this->reset_);
    debugI("  Address: %d", this->address_);
    debugI("  Nominal line frequency: %d Hz", this->line_freq_);
    debugI("  Current reference: %f", this->current_reference_);
    debugI("  Energy reference: %f", this->energy_reference_);
    debugI("  Power reference: %f", this->power_reference_);
    debugI("  Voltage reference: %f", this->voltage_reference_);
}


uint32_t BL0942::get24bReg(uint8_t* reg)
{
    if (reg == nullptr) return 0;
    uint32_t val = (uint32_t)*reg++;
    val |= (uint32_t)(*reg++) << 8;
    val |= (uint32_t)(*reg++) << 16;
    return val;
}