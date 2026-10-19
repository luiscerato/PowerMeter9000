#include "webfs_format.h"

namespace WebFsFormat {

// Calcula el CRC32 IEEE usado por el formato binario; data y length definen los bytes protegidos.
uint32_t calculateCrc32(const uint8_t* data, size_t length)
{
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t index = 0; index < length; ++index) {
		crc ^= data[index];
		for (uint8_t bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320U & uint32_t(-int32_t(crc & 1U)));
	}
	return ~crc;
}

// Devuelve un Content-Type estable para cada codigo guardado; mimeType identifica la entrada WebFS.
const char* contentType(MimeType mimeType)
{
	switch (mimeType) {
	case MimeType::html: return "text/html; charset=utf-8";
	case MimeType::css: return "text/css; charset=utf-8";
	case MimeType::javascript: return "application/javascript; charset=utf-8";
	case MimeType::json: return "application/json; charset=utf-8";
	case MimeType::svg: return "image/svg+xml";
	case MimeType::png: return "image/png";
	case MimeType::jpeg: return "image/jpeg";
	case MimeType::ico: return "image/x-icon";
	case MimeType::text: return "text/plain; charset=utf-8";
	default: return "application/octet-stream";
	}
}

} // namespace WebFsFormat
