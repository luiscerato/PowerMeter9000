#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace waveEventFormat {
constexpr uint32_t pageMagic = 0x32564157, packetMagic = 0x31564157, commitMagic = 0x31435657;
constexpr uint16_t pageBytes = 2048, pagePayloadBytes = 2016, packetBytes = 4096, sliceBytes = 1344;
constexpr uint16_t samplesPerPage = 192, halfSliceBytes = 672;
constexpr uint8_t dataPages = 31, version = 2, legacyVersion = 1, legacyDataPackets = 31;
constexpr uint32_t sliceUs = 16000, pageUs = 24000, packetUs = 48000;
constexpr float voltageScale = 0.25f, currentScale = 0.1f;
enum : uint16_t { clipped = 1, acquisitionGap = 2, timingEstimated = 4, prehistoryMissing = 8 };

struct __attribute__((packed)) pageHeader {
    uint32_t magic, waveId;
    uint64_t firstUs;
    uint16_t firstSequence, flags;
    uint32_t dataCrc, headerCrc;
    uint8_t formatVersion, pageIndex;
    uint16_t sampleCount;
};
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
static_assert(sizeof(pageHeader) == 32, "Wave page header must occupy 32 bytes");
static_assert(sizeof(pageHeader) + pagePayloadBytes == pageBytes, "Wave page must occupy 2 KiB");
static_assert(samplesPerPage * 7 * 12 / 8 == pagePayloadBytes, "Wave page must fit 24 ms of packed samples");
static_assert(64 * 7 * 12 / 8 == halfSliceBytes && halfSliceBytes % 4 == 0, "16 ms slices must split on packed-sample boundaries");
static_assert(sizeof(packetHeader) == 64, "Legacy Wave header must occupy 64 bytes");
static_assert(sizeof(packetHeader) + 3 * sliceBytes == packetBytes, "Legacy Wave packet must occupy 4 KiB");
static_assert(sizeof(commitHeader) == 112, "Wave commit must occupy 112 bytes");
static_assert(dataPages * pageBytes + pageBytes == 64 * 1024, "Wave capture must occupy 64 KiB");
static_assert(2 * (dataPages * pageBytes + pageBytes) == 128 * 1024, "Two Wave captures must fit one erase block");

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
inline uint32_t crc32(const void* data, size_t length) { return ~updateCrc32(~0U, data, length); }

// Valida page, cabecera v2 leida de NAND; las muestras y su CRC se verifican por separado.
inline bool validPageHeader(const pageHeader& page) {
    pageHeader copy = page;
    copy.headerCrc = 0;
    return page.magic == pageMagic && page.waveId && page.formatVersion == version &&
        page.pageIndex < dataPages && page.sampleCount == samplesPerPage &&
        crc32(&copy, sizeof(copy)) == page.headerCrc;
}

// Valida packet, cabecera v1 anterior; permite conservar la lectura de bloques existentes.
inline bool validLegacyHeader(const packetHeader& packet) {
    packetHeader copy = packet;
    copy.headerCrc = 0;
    return packet.magic == packetMagic && packet.formatVersion == legacyVersion && packet.waveId &&
        packet.packetIndex < legacyDataPackets && packet.sampleRate == 8000 && packet.sampleCount == 384 &&
        packet.channelMask == 0x7f && packet.sliceMask == 7 && crc32(&copy, sizeof(copy)) == packet.headerCrc;
}

// Valida commit, cierre de Wave; v2 usa una copia y v1 conserva el cierre duplicado.
inline bool validCommit(const commitHeader& commit) {
    return commit.magic == commitMagic && commit.waveId &&
        ((commit.formatVersion == version && commit.packetCount == dataPages) ||
         (commit.formatVersion == legacyVersion && commit.packetCount == legacyDataPackets)) &&
        commit.linkCount > 0 && commit.linkCount <= 4 && crc32(&commit, offsetof(commitHeader, crc)) == commit.crc;
}
}
