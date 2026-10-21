#pragma once

#include <stdint.h>
#include <limits>
#include <cmath>

// Canales LSB-first: VR, VS, VT, IR, IS, IT, IN, VN; ocho valores unsigned de 14 bits.
struct fastRMSData_t {
    uint16_t timeStamp = 0; // Reloj monotono en unidades de 100 us; desborda cada 6.5536 s.
    uint8_t channels[14] = {};
    static constexpr uint16_t unavailable = 0x3FFF;

    // Escribe raw en channel (0..7), conservando los bits de los canales vecinos.
    void setRaw(uint8_t channel, uint16_t raw) {
        if (channel >= 8) return;
        const uint8_t byte = channel * 14 / 8, shift = channel * 14 % 8;
        const uint32_t mask = uint32_t(unavailable) << shift;
        const uint32_t value = uint32_t(raw & unavailable) << shift;
        for (uint8_t i = 0; i < (shift + 14 + 7) / 8; ++i)
            channels[byte + i] = (channels[byte + i] & ~uint8_t(mask >> (8 * i))) |
                uint8_t(value >> (8 * i));
    }

    // Lee el codigo unsigned de channel (0..7); un indice incorrecto devuelve sin dato.
    uint16_t getRaw(uint8_t channel) const {
        if (channel >= 8) return unavailable;
        const uint8_t byte = channel * 14 / 8, shift = channel * 14 % 8;
        uint32_t value = 0;
        for (uint8_t i = 0; i < (shift + 14 + 7) / 8; ++i)
            value |= uint32_t(channels[byte + i]) << (8 * i);
        return (value >> shift) & unavailable;
    }

    // Cuantiza value en V o A segun channel; NaN, negativos y fuera de rango quedan sin dato.
    void setValue(uint8_t channel, float value) {
        const bool voltage = channel < 3 || channel == 7;
        if (!std::isfinite(value) || value < 0 || value > (voltage ? 409.55f : 163.82f)) {
            setRaw(channel, unavailable);
            return;
        }
        const uint16_t raw = uint16_t(value * (voltage ? 40.0f : 100.0f) + 0.5f);
        setRaw(channel, raw < unavailable ? raw : unavailable - 1);
    }

    // Devuelve channel en V o A; el codigo reservado se transforma en NaN.
    float getValue(uint8_t channel) const {
        const uint16_t raw = getRaw(channel);
        return raw == unavailable ? std::numeric_limits<float>::quiet_NaN() :
            raw * (channel < 3 || channel == 7 ? 0.025f : 0.01f);
    }
};

static_assert(sizeof(fastRMSData_t) == 16, "Fast RMS must occupy exactly 128 bits");
