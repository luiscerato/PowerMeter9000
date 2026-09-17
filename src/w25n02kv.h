#pragma once

#include <Arduino.h>
#include <SPI.h>

struct W25N02KvInfo {
	uint8_t manufacturerId = 0;
	uint16_t deviceId = 0;
	uint8_t protection = 0;
	uint8_t configuration = 0;
	uint8_t status = 0;
};

class W25N02Kv {
public:
	static constexpr uint32_t pageDataSize = 2048;
	static constexpr uint32_t pagesPerBlock = 64;
	static constexpr uint32_t blockCount = 2048;
	static constexpr uint32_t blockDataSize = pageDataSize * pagesPerBlock;

	// Prepara el controlador sin inicializar ni reasignar los pines del puerto.
	// spiPort: bus ya iniciado; chipSelectPin: CS activo en bajo; clockHz: velocidad NAND.
	W25N02Kv(SPIClass& spiPort, uint8_t chipSelectPin, uint32_t clockHz = 20000000);

	// Reinicia e identifica la memoria sin modificar su contenido; sin parametros.
	bool begin();

	// Lee identificacion y registros de estado; info recibe los valores obtenidos.
	bool readInfo(W25N02KvInfo& info);

private:
	static constexpr uint8_t commandReset = 0xFF;
	static constexpr uint8_t commandJedecId = 0x9F;
	static constexpr uint8_t commandReadStatus = 0x05;
	static constexpr uint8_t protectionRegister = 0xA0;
	static constexpr uint8_t configurationRegister = 0xB0;
	static constexpr uint8_t statusRegister = 0xC0;
	static constexpr uint8_t winbondManufacturerId = 0xEF;
	static constexpr uint16_t expectedDeviceId = 0xAA22;

	SPIClass& spi;
	SPISettings settings;
	uint8_t chipSelectPin;

	// Envia un comando sin respuesta; command es el opcode NAND.
	void sendCommand(uint8_t command);

	// Lee un registro NAND; address selecciona el registro de estado.
	uint8_t readStatus(uint8_t address);

	// Lee los tres bytes JEDEC y los guarda en info.
	void readJedecId(W25N02KvInfo& info);
};
