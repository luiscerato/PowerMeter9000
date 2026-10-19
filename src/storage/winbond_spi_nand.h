#pragma once

#include <Arduino.h>
#include <SPI.h>
#include "shared_spi_bus.h"

enum class WinbondSpiNandModel : uint8_t {
	unknown,
	w25n01gv,
	w25n01kv,
	w25n02kv
};

struct WinbondSpiNandGeometry {
	uint16_t blockCount = 0;
	uint16_t pagesPerBlock = 64;
	uint16_t pageDataSize = 2048;
	uint16_t spareSize = 0;
	uint8_t eccStrength = 0;
	uint8_t eccStatusMask = 0;
};

struct W25N02KvInfo {
	uint8_t manufacturerId = 0;
	uint16_t deviceId = 0;
	uint8_t protection = 0;
	uint8_t configuration = 0;
	uint8_t status = 0;
	WinbondSpiNandModel model = WinbondSpiNandModel::unknown;
	WinbondSpiNandGeometry geometry;
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
	uint8_t protectionBefore = 0;
	uint8_t protectionDuring = 0;
	uint8_t configurationBefore = 0;
	uint8_t configurationDuring = 0;
	uint8_t eraseStatus = 0;
	uint8_t programStatus = 0;
	uint8_t readStatus = 0;
	uint32_t expectedCrc32 = 0;
	uint32_t readCrc32 = 0;
	uint32_t eraseUs = 0;
	uint32_t programUs = 0;
	uint32_t readUs = 0;
};

struct W25N02KvTiming {
	uint64_t busWaitUs = 0;
	uint64_t spiActiveUs = 0;
	uint64_t readyWaitUs = 0;
};

class W25N02Kv {
public:
	static constexpr uint32_t pageDataSize = 2048;
	static constexpr uint32_t pagesPerBlock = 64;
	static constexpr uint32_t maxBlockCount = 2048;
	static constexpr uint32_t blockDataSize = pageDataSize * pagesPerBlock;

	// Prepara el controlador sin inicializar ni reasignar los pines del puerto.
	// spiPort: bus ya iniciado; chipSelectPin: CS activo en bajo; clockHz: velocidad NAND; busLock: mutex SPI opcional.
	W25N02Kv(SPIClass& spiPort, uint8_t chipSelectPin, uint32_t clockHz = 30000000,
		SharedSpiBus* busLock = nullptr);

	// Asigna el mutex que protege el bus SPI compartido; busLock puede ser nulo para un bus exclusivo.
	void setSpiBusLock(SharedSpiBus* busLock);

	// Reinicia e identifica la memoria sin modificar su contenido; sin parametros.
	bool begin();

	// Lee identificacion y registros de estado; info recibe los valores obtenidos.
	bool readInfo(W25N02KvInfo& info);

	// Devuelve el modelo detectado o "unknown" si el JEDEC ID no esta soportado.
	const char* modelName() const;

	// Devuelve la geometria correspondiente al dispositivo detectado.
	const WinbondSpiNandGeometry& geometry() const;

	// Devuelve la capacidad util, sin incluir el area spare.
	uint32_t capacityBytes() const;

	// Prueba borrado, escritura y lectura de una pagina dentro de block.
	// block: bloque reservado que sera destruido; result: diagnostico completo.
	bool runBlockTest(uint16_t block, W25N02KvTestResult& result);

	// Cambia la velocidad usada en las siguientes transacciones.
	// clockHz: frecuencia SPI en hertz.
	void setClockHz(uint32_t clockHz);

	// Prepara buffer-read y desprotege la NAND, conservando los valores anteriores.
	// protection y configuration reciben los registros que luego deben restaurarse.
	void beginWriteSession(uint8_t& protection, uint8_t& configuration);

	// Restaura los registros guardados al iniciar una sesion de escritura.
	// protection y configuration son los valores originales.
	void endWriteSession(uint8_t protection, uint8_t configuration);

	// Lee el marcador de fabrica del bloque sin modificarlo.
	// block: bloque consultado; marker y status reciben marcador y estado NAND.
	bool readBadBlockMarker(uint16_t block, uint8_t& marker, uint8_t& status);

	// Borra block completo y devuelve el registro de estado final.
	bool eraseBlock(uint16_t block, uint8_t& status);

	// Programa data en la pagina indicada por page.
	bool programPage(uint32_t page, const uint8_t* data, size_t dataSize, uint8_t& status,
		W25N02KvTiming* timing = nullptr);

	// Lee dataSize bytes de la pagina indicada por page.
	bool readPage(uint32_t page, uint8_t* data, size_t dataSize, uint8_t& status,
		W25N02KvTiming* timing = nullptr);

	// Lee dataSize bytes desde column dentro de page.
	// page: pagina absoluta; column: desplazamiento dentro de pagina; data: destino; dataSize: bytes a leer.
	bool readPage(uint32_t page, uint16_t column, uint8_t* data, size_t dataSize, uint8_t& status,
		W25N02KvTiming* timing = nullptr);

private:
	static constexpr uint8_t commandReset = 0xFF;
	static constexpr uint8_t commandJedecId = 0x9F;
	static constexpr uint8_t commandReadStatus = 0x0F;
	static constexpr uint8_t commandWriteStatus = 0x1F;
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
	static constexpr uint16_t w25n01gvDeviceId = 0xAA21;
	static constexpr uint16_t w25n01kvDeviceId = 0xAE21;
	static constexpr uint16_t w25n02kvDeviceId = 0xAA22;

	SPIClass& spi;
	SPISettings settings;
	uint8_t chipSelectPin;
	SharedSpiBus* spiBusLock = nullptr;
	WinbondSpiNandModel detectedModel = WinbondSpiNandModel::unknown;
	WinbondSpiNandGeometry detectedGeometry;

	// Selecciona modelo y geometria a partir del JEDEC ID.
	// manufacturerId y deviceId son los identificadores leidos de la memoria.
	bool detectDevice(uint8_t manufacturerId, uint16_t deviceId);

	// Envia un comando sin respuesta; command es el opcode NAND.
	void sendCommand(uint8_t command, W25N02KvTiming* timing = nullptr);

	// Lee un registro NAND; address selecciona el registro de estado.
	uint8_t readStatus(uint8_t address, W25N02KvTiming* timing = nullptr);

	// Escribe un registro NAND; address y value indican registro y valor.
	void writeStatus(uint8_t address, uint8_t value);

	// Espera que termine una operacion; initialDelayUs y pollDelayUs ajustan el sondeo sin bloquear de mas.
	bool waitReady(uint32_t timeoutMs, uint8_t& status, W25N02KvTiming* timing = nullptr,
		uint16_t initialDelayUs = 0, uint16_t pollDelayUs = 10);

	// Lee dataSize bytes del cache interno desde column.
	bool readCache(uint16_t column, uint8_t* data, size_t dataSize, W25N02KvTiming* timing = nullptr);

	// Transfiere page al cache interno y espera la lectura NAND.
	bool loadPage(uint32_t page, uint8_t& status, W25N02KvTiming* timing = nullptr);

	// Inicia una transaccion SPI protegida; settings selecciona frecuencia, orden y modo.
	bool beginSpiTransaction(const SPISettings& transactionSettings, W25N02KvTiming* timing = nullptr);

	// Finaliza una transaccion SPI y libera el mutex compartido; timing acumula tiempo activo.
	void endSpiTransaction(uint32_t activeStartedUs, W25N02KvTiming* timing = nullptr);

	// Calcula CRC32 IEEE de data con longitud dataSize.
	static uint32_t calculateCrc32(const uint8_t* data, size_t dataSize);

	// Lee los tres bytes JEDEC y los guarda en info.
	void readJedecId(W25N02KvInfo& info);
};

// Nombre generico para reutilizar el driver sin depender del modelo instalado.
using WinbondSpiNand = W25N02Kv;
using WinbondSpiNandInfo = W25N02KvInfo;
using WinbondSpiNandTestResult = W25N02KvTestResult;
using WinbondSpiNandTiming = W25N02KvTiming;
