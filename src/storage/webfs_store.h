#pragma once

#include <Arduino.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "nand_partition.h"
#include "webfs_format.h"

enum class WebFsBank : uint8_t {
	none,
	a,
	b
};

struct WebFsBankInfo {
	bool valid = false;
	uint32_t sequence = 0;
	uint32_t imageSize = 0;
	uint16_t fileCount = 0;
	uint32_t capacityBytes = 0;
};

struct WebFsInfo {
	bool mounted = false;
	WebFsBank activeBank = WebFsBank::none;
	WebFsBankInfo bankA;
	WebFsBankInfo bankB;
};

struct WebFsFile {
	WebFsBank bank = WebFsBank::none;
	WebFsFormat::FileEntry entry{};
};

enum class WebFsUploadResult : uint8_t {
	none,
	inProgress,
	completed,
	busy,
	invalidImage,
	capacityExceeded,
	nandError,
	verificationFailed
};

struct WebFsUploadInfo {
	WebFsUploadResult result = WebFsUploadResult::none;
	uint32_t totalBytes = 0;
	uint32_t receivedBytes = 0;
	WebFsBank targetBank = WebFsBank::none;
};

// Monta y consulta las dos imagenes WebFS almacenadas en NAND.
class WebFsStore {
public:
	// Asocia dos bancos NAND ya configurados; bankA y bankB identifican las regiones A/B.
	void configure(NandPartition& bankA, NandPartition& bankB);

	// Lee los commits y monta la imagen valida mas reciente; sin parametros.
	bool begin();

	// Relee ambos commits sin modificar la NAND; sin parametros.
	bool refresh();

	// Indica si existe una imagen WebFS valida; sin parametros.
	bool isMounted() const;

	// Copia el estado de ambos bancos y de la imagen activa; info recibe la instantanea.
	void getInfo(WebFsInfo& info) const;

	// Busca un path completo en la imagen activa; path identifica el archivo y file recibe su descriptor.
	bool findFile(const char* path, WebFsFile& file);

	// Lee una pagina logica de la imagen seleccionada; bank, pageIndex, data y completionQueue definen la operacion.
	bool readImagePage(WebFsBank bank, uint32_t pageIndex, uint8_t* data, QueueHandle_t completionQueue,
		uint32_t requestId = 0) const;

	// Devuelve el banco activo para fijar una descarga; sin parametros.
	WebFsBank activeBank() const;

	// Devuelve la capacidad util de una imagen en el banco solicitado; bank identifica A o B.
	uint32_t imageCapacity(WebFsBank bank) const;

	// Devuelve el descriptor de un banco aun si no esta activo; bank identifica A o B.
	bool getBankInfo(WebFsBank bank, WebFsBankInfo& info) const;

	// Reserva el banco A/B indicado y rechaza el formato si hay carga o consulta en curso.
	bool preparePartitionFormat(WebFsBank bank);

	// Remonta o invalida bank segun success y luego libera su reserva.
	void finishPartitionFormat(WebFsBank bank, bool success);

	// Inicia una carga binaria completa hacia el banco inactivo; totalBytes define el cuerpo HTTP esperado.
	bool beginUpload(uint32_t totalBytes);

	// Agrega un fragmento recibido por HTTP; data y length definen los bytes consecutivos de la imagen.
	bool appendUpload(const uint8_t* data, size_t length);

	// Verifica y confirma la imagen escrita al terminar el cuerpo HTTP; sin parametros.
	WebFsUploadResult finishUpload();

	// Copia el avance y resultado de la ultima carga; info recibe la instantanea.
	void getUploadInfo(WebFsUploadInfo& info) const;

	// Invalida el banco activo para volver al otro banco valido; sin parametros.
	bool rollback();

private:
	struct BankState {
		NandPartition* partition = nullptr;
		WebFsFormat::BankCommit commit{};
		WebFsFormat::ImageHeader header{};
		uint16_t usableBlocks = 0;
		uint16_t commitBlock = NandPartition::invalidBlock;
		bool valid = false;
	};

	BankState banks[2];
	WebFsBank mountedBank = WebFsBank::none;
	SemaphoreHandle_t lookupMutex = nullptr;
	StaticSemaphore_t lookupMutexStorage;
	uint8_t* uploadPage = nullptr;
	uint8_t* verifyPage = nullptr;
	StaticQueue_t readResultQueueStorage;
	uint8_t readResultQueueData[sizeof(NandIoResult)] = {};
	QueueHandle_t readResultQueue = nullptr;
	uint32_t nextRequestId = 0;
	WebFsUploadInfo upload;
	WebFsFormat::ImageHeader uploadHeader{};
	uint32_t uploadCrc = 0xFFFFFFFFU;
	uint16_t uploadPageBytes = 0;
	bool uploadPrepared = false;
	bool uploadLockHeld = false;
	uint8_t formattingBanks = 0;
	portMUX_TYPE formatMutex = portMUX_INITIALIZER_UNLOCKED;

	// Devuelve el indice interno de un banco; bank identifica A o B y index recibe 0 o 1.
	bool bankIndex(WebFsBank bank, uint8_t& index) const;

	// Indica si index esta reservado para formato.
	bool isBankFormatting(uint8_t index) const;

	// Monta un banco leyendo su commit y cabecera; bank identifica el estado a actualizar.
	bool mountBank(BankState& bank);

	// Lee bytes consecutivos de una imagen durante una consulta protegida; bank, offset, data y length definen el rango.
	bool readImageBytes(BankState& bank, uint32_t offset, uint8_t* data, size_t length);

	// Convierte una pagina logica de imagen a bloque y pagina relativos; bank, pageIndex, block y page reciben el resultado.
	bool translateImagePage(const BankState& bank, uint32_t pageIndex, uint16_t& block, uint8_t& page) const;

	// Devuelve el ultimo bloque utilizable reservado para commit; bank identifica la region a revisar.
	uint16_t findCommitBlock(const BankState& bank) const;

	// Verifica estructura y CRC de una cabecera WebFS; header identifica los datos recibidos y capacity limita la imagen.
	bool validateHeader(const WebFsFormat::ImageHeader& header, uint32_t capacity) const;

	// Verifica el CRC de la tabla de archivos; bank contiene la cabecera y la ubicacion de la imagen.
	bool validateTable(BankState& bank);

	// Espera una lectura NAND completada; requestId identifica la solicitud y result recibe la respuesta.
	bool waitReadResult(uint32_t requestId, NandIoResult& result) const;

	// Prepara el banco de carga despues de recibir una cabecera completa; sin parametros.
	bool prepareUpload();

	// Borra todos los bloques utilizables del banco elegido; bank contiene la region destino.
	bool eraseBank(BankState& bank);

	// Programa una pagina completa de imagen durante la carga; imagePage identifica el indice logico.
	bool programUploadPage(uint32_t imagePage);

	// Relee la imagen escrita y calcula su CRC32; bank contiene el destino y crc recibe el resultado.
	bool verifyUploadImage(BankState& bank, uint32_t& crc);

	// Escribe la confirmacion final de un banco validado; bank contiene el destino y imageCrc protege la imagen.
	bool writeCommit(BankState& bank, uint32_t imageCrc);

	// Actualiza un CRC32 IEEE incremental; crc es el estado y data/length delimitan los bytes nuevos.
	static uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t length);

	// Compara secuencias uint32 con desborde modular; candidate y current son los valores a ordenar.
	static bool isNewerSequence(uint32_t candidate, uint32_t current);
};
