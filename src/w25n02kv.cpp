#include "w25n02kv.h"
#include <new>

W25N02Kv::W25N02Kv(SPIClass& spiPort, uint8_t chipSelectPin, uint32_t clockHz)
	: spi(spiPort), settings(clockHz, MSBFIRST, SPI_MODE0), chipSelectPin(chipSelectPin) {}

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
	return info.manufacturerId == winbondManufacturerId && info.deviceId == expectedDeviceId;
}

bool W25N02Kv::runBlockTest(uint16_t block, W25N02KvTestResult& result)
{
	result = W25N02KvTestResult{};
	result.block = block;
	if (block >= blockCount) return false;

	const uint8_t originalProtection = readStatus(protectionRegister);
	const uint8_t originalConfiguration = readStatus(configurationRegister);
	writeStatus(configurationRegister, originalConfiguration | 0x08); // Fuerza lectura desde cache, no modo continuo.

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
	if (block >= blockCount) return false;
	return loadPage(uint32_t(block) * pagesPerBlock, status) && readCache(pageDataSize, &marker, 1);
}

void W25N02Kv::sendCommand(uint8_t command)
{
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(command);
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
}

uint8_t W25N02Kv::readStatus(uint8_t address)
{
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandReadStatus);
	spi.transfer(address);
	const uint8_t value = spi.transfer(0x00);
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
	return value;
}

void W25N02Kv::writeStatus(uint8_t address, uint8_t value)
{
	uint8_t status = 0;
	if (!waitReady(100, status)) return;
	sendCommand(commandWriteEnable);
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandWriteStatus);
	spi.transfer(address);
	spi.transfer(value);
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
	waitReady(100, status);
}

bool W25N02Kv::waitReady(uint32_t timeoutMs, uint8_t& status)
{
	const uint32_t started = millis();
	do {
		status = readStatus(statusRegister);
		if (!(status & 0x01)) return true;
		delay(1);
	} while (millis() - started < timeoutMs);
	return false;
}

bool W25N02Kv::readCache(uint16_t column, uint8_t* data, size_t dataSize)
{
	if (!data || uint32_t(column) + dataSize > pageDataSize + 128) return false;
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandReadCache);
	spi.transfer(uint8_t(column >> 8));
	spi.transfer(uint8_t(column));
	spi.transfer(0x00);
	for (size_t i = 0; i < dataSize; ++i) data[i] = spi.transfer(0x00);
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
	return true;
}

bool W25N02Kv::loadPage(uint32_t page, uint8_t& status)
{
	if (page >= blockCount * pagesPerBlock) return false;
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandPageRead);
	spi.transfer(uint8_t(page >> 16));
	spi.transfer(uint8_t(page >> 8));
	spi.transfer(uint8_t(page));
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
	return waitReady(100, status);
}

bool W25N02Kv::eraseBlock(uint16_t block, uint8_t& status)
{
	if (block >= blockCount) return false;
	const uint32_t page = uint32_t(block) * pagesPerBlock;
	uint8_t readyStatus = 0;
	if (!waitReady(100, readyStatus)) return false;
	sendCommand(commandWriteEnable);
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandBlockErase);
	spi.transfer(uint8_t(page >> 16));
	spi.transfer(uint8_t(page >> 8));
	spi.transfer(uint8_t(page));
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
	return waitReady(1000, status) && !(status & 0x04);
}

bool W25N02Kv::programPage(uint32_t page, const uint8_t* data, size_t dataSize, uint8_t& status)
{
	if (!data || !dataSize || dataSize > pageDataSize || page >= blockCount * pagesPerBlock) return false;
	uint8_t readyStatus = 0;
	if (!waitReady(100, readyStatus)) return false;
	sendCommand(commandWriteEnable);
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandProgramLoad);
	spi.transfer(0x00);
	spi.transfer(0x00);
	for (size_t i = 0; i < dataSize; ++i) spi.transfer(data[i]);
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();

	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandProgramExecute);
	spi.transfer(uint8_t(page >> 16));
	spi.transfer(uint8_t(page >> 8));
	spi.transfer(uint8_t(page));
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
	return waitReady(1000, status) && !(status & 0x08);
}

bool W25N02Kv::readPage(uint32_t page, uint8_t* data, size_t dataSize, uint8_t& status)
{
	if (!data || !dataSize || dataSize > pageDataSize) return false;
	return loadPage(page, status) && readCache(0, data, dataSize);
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
	spi.beginTransaction(settings);
	digitalWrite(chipSelectPin, LOW);
	spi.transfer(commandJedecId);
	spi.transfer(0x00);
	info.manufacturerId = spi.transfer(0x00);
	info.deviceId = uint16_t(spi.transfer(0x00)) << 8;
	info.deviceId |= spi.transfer(0x00);
	digitalWrite(chipSelectPin, HIGH);
	spi.endTransaction();
}
