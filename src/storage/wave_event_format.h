#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace waveEventFormat {
constexpr uint32_t packetMagic = 0x31564157, commitMagic = 0x31435657;
constexpr uint16_t packetBytes = 4096, sliceBytes = 1344;
constexpr uint8_t dataPackets = 31, version = 1;
constexpr uint32_t sliceUs = 16000, packetUs = 48000;
constexpr float voltageScale = 0.25f, currentScale = 0.1f;
enum : uint16_t { clipped = 1, acquisitionGap = 2, timingEstimated = 4, prehistoryMissing = 8 };

struct __attribute__((packed)) packetHeader {
    uint32_t magic, waveId;
    uint64_t firstUs;
    int32_t sliceOffsetsUs[2];
    uint64_t triggerUs;
    uint32_t firstSequence;
    float voltageStep, currentStep;
    uint32_t dataCrc, headerCrc;
    uint16_t sampleRate, sampleCount;
    uint8_t formatVersion, packetIndex, sliceMask, channelMask;
    uint16_t flags, reserved;
};
struct __attribute__((packed)) eventLink { uint32_t eventId; uint64_t rootUs, unixUs; };
struct __attribute__((packed)) commitHeader {
    uint32_t magic, waveId;
    uint64_t triggerUs, endUs;
    uint8_t formatVersion, packetCount, linkCount, reserved;
    eventLink links[4];
    uint32_t crc;
};
static_assert(sizeof(packetHeader) == 64, "Wave header must occupy 64 bytes");
static_assert(sizeof(packetHeader) + 3 * sliceBytes == packetBytes, "Wave packet must occupy two NAND pages");
static_assert(32 * packetBytes == 128 * 1024, "One waveform must fit one erase block");

// Acumula CRC32 sin finalizar; crc es el estado anterior y data/length el tramo accesible por bytes.
inline uint32_t updateCrc32(uint32_t crc, const void* data, size_t length) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    while (length--) {
        crc ^= *bytes++;
        for (uint8_t bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

// Calcula CRC32 IEEE de data/length, sin tablas ni reservas de heap.
inline uint32_t crc32(const void* data, size_t length) {
    return ~updateCrc32(~0U, data, length);
}

// Valida metadatos de packet; la carga util se valida por separado antes de mostrarla.
inline bool validHeader(const packetHeader& packet) {
    packetHeader copy = packet;
    copy.headerCrc = 0;
    return packet.magic == packetMagic && packet.formatVersion == version && packet.waveId &&
        packet.packetIndex < dataPackets && packet.sampleRate == 8000 && packet.sampleCount == 384 &&
        packet.channelMask == 0x7f && packet.sliceMask == 7 && crc32(&copy, sizeof(copy)) == packet.headerCrc;
}

// Valida el cierre commit; exige identificador, version, enlaces y CRC correctos.
inline bool validCommit(const commitHeader& commit) {
    return commit.magic == commitMagic && commit.waveId && commit.formatVersion == version &&
        commit.packetCount == dataPackets && commit.linkCount > 0 && commit.linkCount <= 4 &&
        crc32(&commit, offsetof(commitHeader, crc)) == commit.crc;
}
}
