#pragma once

#include <Arduino.h>

namespace WebFsFormat {

constexpr uint32_t imageMagic = 0x32335346U; // "FS32" little-endian.
constexpr uint32_t commitMagic = 0x4D434657U; // "WFCM" little-endian.
constexpr uint16_t version = 1;
constexpr uint16_t headerSize = 64;
constexpr uint16_t entrySize = 128;
constexpr uint16_t pathSize = 96;
constexpr uint16_t pageSize = 2048;

enum class MimeType : uint16_t {
	binary,
	html,
	css,
	javascript,
	json,
	svg,
	png,
	jpeg,
	ico,
	text
};

enum class Encoding : uint8_t {
	none,
	gzip
};

// Describe la cabecera fija de una imagen WebFS; todos los campos usan little-endian.
struct __attribute__((packed)) ImageHeader {
	uint32_t magic;
	uint16_t formatVersion;
	uint16_t fileEntrySize;
	uint32_t imageSize;
	uint16_t fileCount;
	uint16_t reserved0;
	uint32_t tableOffset;
	uint32_t dataOffset;
	uint32_t createdAt;
	uint32_t tableCrc32;
	uint32_t dataCrc32;
	uint32_t headerCrc32;
	uint8_t reserved[24];
};
static_assert(sizeof(ImageHeader) == headerSize, "Unexpected WebFS header size");

// Describe un archivo contenido en WebFS; path es la ruta completa y no hay carpetas separadas.
struct __attribute__((packed)) FileEntry {
	char path[pathSize];
	uint32_t dataOffset;
	uint32_t storedSize;
	uint32_t originalSize;
	uint32_t modifiedTime;
	uint32_t dataCrc32;
	uint16_t mimeType;
	uint8_t encoding;
	uint8_t flags;
	uint8_t reserved[8];
};
static_assert(sizeof(FileEntry) == entrySize, "Unexpected WebFS entry size");

// Confirma una imagen ya escrita; se programa como ultima pagina del banco NAND.
struct __attribute__((packed)) BankCommit {
	uint32_t magic;
	uint16_t formatVersion;
	uint16_t reserved0;
	uint32_t sequence;
	uint32_t imageSize;
	uint32_t imageCrc32;
	uint32_t activatedAt;
	uint32_t crc32;
	uint8_t reserved[4];
};
static_assert(sizeof(BankCommit) == 32, "Unexpected WebFS commit size");

// Calcula CRC32 IEEE para cabeceras, tablas y contenido; data y length definen el rango.
uint32_t calculateCrc32(const uint8_t* data, size_t length);

// Devuelve el MIME HTTP asociado a un codigo WebFS; mimeType identifica el tipo almacenado.
const char* contentType(MimeType mimeType);

} // namespace WebFsFormat
