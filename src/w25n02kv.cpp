#include "w25n02kv.h"

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
