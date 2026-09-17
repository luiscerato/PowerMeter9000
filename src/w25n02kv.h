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

struct W25N02KvTestResult {
	bool success = false;
	bool badBlock = false;
	bool eraseOk = false;
	bool programOk = false;
	bool readOk = false;
	bool dataMatch = false;
	uint16_t block = 0;
	uint8_t badBlockMarker = 0;
	uint8_t eraseStatus = 0;
	uint8_t programStatus = 0;
	uint8_t readStatus = 0;
	uint32_t expectedCrc32 = 0;
	uint32_t readCrc32 = 0;
	uint32_t eraseUs = 0;
	uint32_t programUs = 0;
	uint32_t readUs = 0;
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

	// Prueba borrado, escritura y lectura de una pagina dentro de block.
	// block: bloque reservado que sera destruido; result: diagnostico completo.
	bool runBlockTest(uint16_t block, W25N02KvTestResult& result);

private:
	static constexpr uint8_t commandReset = 0xFF;
	static constexpr uint8_t commandJedecId = 0x9F;
	static constexpr uint8_t commandReadStatus = 0x05;
	static constexpr uint8_t commandWriteStatus = 0x01;
	static constexpr uint8_t commandWriteEnable = 0x06;
	static constexpr uint8_t commandBlockErase = 0xD8;
	static constexpr uint8_t commandProgramLoad = 0x02;
	static constexpr uint8_t commandProgramExecute = 0x10;
	static constexpr uint8_t commandPageRead = 0x13;
	static constexpr uint8_t commandReadCache = 0x03;
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

	// Escribe un registro NAND; address y value indican registro y valor.
	void writeStatus(uint8_t address, uint8_t value);

	// Espera que termine una operacion; timeoutMs limita la espera y status recibe el estado final.
	bool waitReady(uint32_t timeoutMs, uint8_t& status);

	// Lee dataSize bytes del cache interno desde column.
	bool readCache(uint16_t column, uint8_t* data, size_t dataSize);

	// Transfiere page al cache interno y espera la lectura NAND.
	bool loadPage(uint32_t page, uint8_t& status);

	// Borra block completo y devuelve el registro de estado final.
	bool eraseBlock(uint16_t block, uint8_t& status);

	// Programa data en la primera pagina indicada por page.
	bool programPage(uint32_t page, const uint8_t* data, size_t dataSize, uint8_t& status);

	// Calcula CRC32 IEEE de data con longitud dataSize.
	static uint32_t calculateCrc32(const uint8_t* data, size_t dataSize);

	// Lee los tres bytes JEDEC y los guarda en info.
	void readJedecId(W25N02KvInfo& info);
};
