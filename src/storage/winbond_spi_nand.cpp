#include "winbond_spi_nand.h"
#include <new>

W25N02Kv::W25N02Kv(SPIClass& spiPort, uint8_t chipSelectPin, uint32_t clockHz, SharedSpiBus* busLock)
	: spi(spiPort), settings(clockHz, MSBFIRST, SPI_MODE0), chipSelectPin(chipSelectPin), spiBusLock(busLock) {}

// Asigna el mutex que protege las transacciones del bus SPI compartido.
void W25N02Kv::setSpiBusLock(SharedSpiBus* busLock)
{
	spiBusLock = busLock;
}

bool W25N02Kv::begin()
{
	pinMode(chipSelectPin, OUTPUT);
	digitalWrite(chipSelectPin, HIGH);
	sendCommand(commandReset);
	delayMicroseconds(600);

	W25N02KvInfo info;
	return readInfo(info);
}

bool W25N02Kv::readInfo(W25N02KvInfo& info)
{
	readJedecId(info);
	info.protection = readStatus(protectionRegister);
	info.configuration = readStatus(configurationRegister);
	info.status = readStatus(statusRegister);
	const bool detected = detectDevice(info.manufacturerId, info.deviceId);
	info.model = detectedModel;
	info.geometry = detectedGeometry;
	return detected;
}

const char* W25N02Kv::modelName() const
{
	switch (detectedModel) {
	case WinbondSpiNandModel::w25n01gv: return "W25N01GV";
	case WinbondSpiNandModel::w25n01kv: return "W25N01KV";
	case WinbondSpiNandModel::w25n02kv: return "W25N02KV";
	default: return "unknown";
	}
}

const WinbondSpiNandGeometry& W25N02Kv::geometry() const
{
	return detectedGeometry;
}

uint32_t W25N02Kv::capacityBytes() const
{
	return uint32_t(detectedGeometry.blockCount) * detectedGeometry.pagesPerBlock * detectedGeometry.pageDataSize;
}

bool W25N02Kv::detectDevice(uint8_t manufacturerId, uint16_t deviceId)
{
	detectedModel = WinbondSpiNandModel::unknown;
	detectedGeometry = WinbondSpiNandGeometry{};
	if (manufacturerId != winbondManufacturerId) return false;

	if (deviceId == w25n01gvDeviceId) {
		detectedModel = WinbondSpiNandModel::w25n01gv;
		detectedGeometry.blockCount = 1024;
		detectedGeometry.spareSize = 64;
		detectedGeometry.eccStrength = 1;
		detectedGeometry.eccStatusMask = 0x30;
		return true;
	}
	if (deviceId == w25n01kvDeviceId) {
		detectedModel = WinbondSpiNandModel::w25n01kv;
		detectedGeometry.blockCount = 1024;
		detectedGeometry.spareSize = 64;
		detectedGeometry.eccStrength = 4;
		detectedGeometry.eccStatusMask = 0x30;
		return true;
	}
	if (deviceId == w25n02kvDeviceId) {
		detectedModel = WinbondSpiNandModel::w25n02kv;
		detectedGeometry.blockCount = 2048;
		detectedGeometry.spareSize = 128;
		detectedGeometry.eccStrength = 8;
		detectedGeometry.eccStatusMask = 0x70;
		return true;
	}
	return false;
}

bool W25N02Kv::runBlockTest(uint16_t block, W25N02KvTestResult& result)
{
	result = W25N02KvTestResult{};
	result.block = block;
	if (block >= detectedGeometry.blockCount) return false;

	const uint8_t originalProtection = readStatus(protectionRegister);
	const uint8_t originalConfiguration = readStatus(configurationRegister);
	result.protectionBefore = originalProtection;
	result.configurationBefore = originalConfiguration;
	writeStatus(configurationRegister, originalConfiguration | 0x08); // Fuerza lectura desde cache, no modo continuo.
	result.configurationDuring = readStatus(configurationRegister);

	const uint32_t page = uint32_t(block) * pagesPerBlock;
	uint8_t status = 0;
	if (!loadPage(page, status) || !readCache(pageDataSize, &result.badBlockMarker, 1)) {
		writeStatus(configurationRegister, originalConfiguration);
		return false;
	}
	result.badBlock = result.badBlockMarker != 0xFF;
	if (result.badBlock) {
		writeStatus(configurationRegister, originalConfiguration);
		return false;
	}

	uint8_t* data = new (std::nothrow) uint8_t[pageDataSize];
	if (!data) {
		writeStatus(configurationRegister, originalConfiguration);
		return false;
	}
	for (uint32_t i = 0; i < pageDataSize; ++i)
		data[i] = uint8_t((i * 37U + (i >> 3) + 0x5AU) & 0xFFU);
	result.expectedCrc32 = calculateCrc32(data, pageDataSize);

	writeStatus(protectionRegister, 0x00);
	result.protectionDuring = readStatus(protectionRegister);
	uint32_t startedUs = micros();
	result.eraseOk = eraseBlock(block, result.eraseStatus);
	result.eraseUs = micros() - startedUs;
	if (result.eraseOk) {
		startedUs = micros();
		result.programOk = programPage(page, data, pageDataSize, result.programStatus);
		result.programUs = micros() - startedUs;
	}
	if (result.programOk) {
		memset(data, 0, pageDataSize);
		startedUs = micros();
		result.readOk = loadPage(page, result.readStatus) && readCache(0, data, pageDataSize);
		result.readUs = micros() - startedUs;
		if (result.readOk) {
			result.readCrc32 = calculateCrc32(data, pageDataSize);
			result.dataMatch = result.readCrc32 == result.expectedCrc32;
			for (uint32_t i = 0; result.dataMatch && i < pageDataSize; ++i)
				result.dataMatch = data[i] == uint8_t((i * 37U + (i >> 3) + 0x5AU) & 0xFFU);
		}
	}

	delete[] data;
	writeStatus(protectionRegister, originalProtection);
	writeStatus(configurationRegister, originalConfiguration);
	result.success = result.eraseOk && result.programOk && result.readOk && result.dataMatch;
	return result.success;
}

void W25N02Kv::setClockHz(uint32_t clockHz)
{
	settings = SPISettings(clockHz, MSBFIRST, SPI_MODE0);
}

void W25N02Kv::beginWriteSession(uint8_t& protection, uint8_t& configuration)
{
	protection = readStatus(protectionRegister);
	configuration = readStatus(configurationRegister);
	writeStatus(configurationRegister, configuration | 0x08);
	writeStatus(protectionRegister, 0x00);
}

void W25N02Kv::endWriteSession(uint8_t protection, uint8_t configuration)
{
	writeStatus(protectionRegister, protection);
	writeStatus(configurationRegister, configuration);
}

bool W25N02Kv::readBadBlockMarker(uint16_t block, uint8_t& marker, uint8_t& status)
{
	if (block >= detectedGeometry.blockCount) return false;
	return loadPage(uint32_t(block) * pagesPerBlock, status) && readCache(pageDataSize, &marker, 1);
}

void W25N02Kv::sendCommand(uint8_t command, W25N02KvTiming* timing)
{
	if (!beginSpiTransaction(settings, timing)) return;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(command);
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted, timing);
}

uint8_t W25N02Kv::readStatus(uint8_t address, W25N02KvTiming* timing)
{
	if (!beginSpiTransaction(settings, timing)) return 0;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandReadStatus);
	spi.transfer(address);
	const uint8_t value = spi.transfer(0x00);
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted, timing);
	return value;
}

void W25N02Kv::writeStatus(uint8_t address, uint8_t value)
{
	uint8_t status = 0;
	if (!waitReady(100, status)) return;
	sendCommand(commandWriteEnable);
	if (!beginSpiTransaction(settings)) return;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandWriteStatus);
	spi.transfer(address);
	spi.transfer(value);
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted);
	waitReady(100, status);
}

bool W25N02Kv::waitReady(uint32_t timeoutMs, uint8_t& status, W25N02KvTiming* timing,
	uint16_t initialDelayUs, uint16_t pollDelayUs)
{
	const uint32_t startedUs = micros();
	if (initialDelayUs >= 1000) {
		delay(initialDelayUs / 1000);
		initialDelayUs %= 1000;
	}
	if (initialDelayUs) delayMicroseconds(initialDelayUs);
	uint32_t lastYieldUs = micros();
	do {
		status = readStatus(statusRegister, timing);
		if (!(status & 0x01)) {
			if (timing) timing->readyWaitUs += micros() - startedUs;
			return true;
		}
		if (micros() - lastYieldUs >= 1000) {
			delay(1);
			lastYieldUs = micros();
		}
		else if (pollDelayUs) delayMicroseconds(pollDelayUs);
	} while (micros() - startedUs < timeoutMs * 1000UL);
	if (timing) timing->readyWaitUs += micros() - startedUs;
	return false;
}

bool W25N02Kv::readCache(uint16_t column, uint8_t* data, size_t dataSize, W25N02KvTiming* timing)
{
	if (!data || uint32_t(column) + dataSize > pageDataSize + detectedGeometry.spareSize) return false;
	if (!beginSpiTransaction(settings, timing)) return false;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandReadCache);
	spi.transfer(uint8_t(column >> 8));
	spi.transfer(uint8_t(column));
	spi.transfer(0x00);
	spi.transferBytes(nullptr, data, dataSize);
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted, timing);
	return true;
}

bool W25N02Kv::loadPage(uint32_t page, uint8_t& status, W25N02KvTiming* timing)
{
	if (page >= uint32_t(detectedGeometry.blockCount) * pagesPerBlock) return false;
	if (!beginSpiTransaction(settings, timing)) return false;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandPageRead);
	spi.transfer(uint8_t(page >> 16));
	spi.transfer(uint8_t(page >> 8));
	spi.transfer(uint8_t(page));
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted, timing);
	return waitReady(100, status, timing, 40, 10);
}

bool W25N02Kv::eraseBlock(uint16_t block, uint8_t& status)
{
	if (block >= detectedGeometry.blockCount) return false;
	const uint32_t page = uint32_t(block) * pagesPerBlock;
	uint8_t readyStatus = 0;
	if (!waitReady(100, readyStatus)) return false;
	sendCommand(commandWriteEnable);
	if (!beginSpiTransaction(settings)) return false;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandBlockErase);
	spi.transfer(uint8_t(page >> 16));
	spi.transfer(uint8_t(page >> 8));
	spi.transfer(uint8_t(page));
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted);
	return waitReady(1000, status, nullptr, 1000, 50) && !(status & 0x04);
}

bool W25N02Kv::programPage(uint32_t page, const uint8_t* data, size_t dataSize, uint8_t& status,
	W25N02KvTiming* timing)
{
	if (!data || !dataSize || dataSize > pageDataSize ||
		page >= uint32_t(detectedGeometry.blockCount) * pagesPerBlock) return false;
	uint8_t readyStatus = 0;
	if (!waitReady(100, readyStatus, timing)) return false;
	sendCommand(commandWriteEnable, timing);
	if (!beginSpiTransaction(settings, timing)) return false;
	uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandProgramLoad);
	spi.transfer(0x00);
	spi.transfer(0x00);
	spi.writeBytes(data, dataSize);
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted, timing);

	if (!beginSpiTransaction(settings, timing)) return false;
	activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandProgramExecute);
	spi.transfer(uint8_t(page >> 16));
	spi.transfer(uint8_t(page >> 8));
	spi.transfer(uint8_t(page));
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted, timing);
	return waitReady(1000, status, timing, 200, 20) && !(status & 0x08);
}

bool W25N02Kv::readPage(uint32_t page, uint8_t* data, size_t dataSize, uint8_t& status,
	W25N02KvTiming* timing)
{
	return readPage(page, 0, data, dataSize, status, timing);
}

// Lee una porcion de la pagina indicada empezando en column.
bool W25N02Kv::readPage(uint32_t page, uint16_t column, uint8_t* data, size_t dataSize, uint8_t& status,
	W25N02KvTiming* timing)
{
	if (!data || !dataSize || uint32_t(column) + dataSize > pageDataSize) return false;
	return loadPage(page, status, timing) && readCache(column, data, dataSize, timing);
}

uint32_t W25N02Kv::calculateCrc32(const uint8_t* data, size_t dataSize)
{
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t i = 0; i < dataSize; ++i) {
		crc ^= data[i];
		for (uint8_t bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320U & uint32_t(-int32_t(crc & 1U)));
	}
	return ~crc;
}

void W25N02Kv::readJedecId(W25N02KvInfo& info)
{
	if (!beginSpiTransaction(settings)) return;
	const uint32_t activeStarted = micros();
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandJedecId);
	spi.transfer(0x00);
	info.manufacturerId = spi.transfer(0x00);
	info.deviceId = uint16_t(spi.transfer(0x00)) << 8;
	info.deviceId |= spi.transfer(0x00);
	digitalWrite(chipSelectPin, HIGH);
	endSpiTransaction(activeStarted);
}

// Inicia una transaccion SPI y toma el mutex compartido cuando existe.
bool W25N02Kv::beginSpiTransaction(const SPISettings& transactionSettings, W25N02KvTiming* timing)
{
	const uint32_t waitingStarted = micros();
	if (spiBusLock && !spiBusLock->lock()) return false;
	spi.beginTransaction(transactionSettings);
	if (timing) timing->busWaitUs += micros() - waitingStarted;
	return true;
}

// Finaliza una transaccion SPI y libera el mutex compartido.
void W25N02Kv::endSpiTransaction(uint32_t activeStartedUs, W25N02KvTiming* timing)
{
	spi.endTransaction();
	if (timing) timing->spiActiveUs += micros() - activeStartedUs;
	if (spiBusLock) spiBusLock->unlock();
}
