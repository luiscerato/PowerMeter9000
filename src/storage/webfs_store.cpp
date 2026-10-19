#include "webfs_store.h"
#include <string.h>
#include <new>
#include <time.h>

namespace {
constexpr uint8_t commitPage = WinbondSpiNand::pagesPerBlock - 1;
}

// Asocia los bancos A/B creados por StorageManager; bankA y bankB son vistas NAND independientes.
void WebFsStore::configure(NandPartition& bankA, NandPartition& bankB)
{
	banks[0].partition = &bankA;
	banks[1].partition = &bankB;
	mountedBank = WebFsBank::none;
	if (!lookupMutex) lookupMutex = xSemaphoreCreateMutexStatic(&lookupMutexStorage);
	if (!readResultQueue)
		readResultQueue = xQueueCreateStatic(1, sizeof(NandIoResult), readResultQueueData, &readResultQueueStorage);
}

// Lee el estado de ambos bancos y selecciona la imagen valida mas reciente; sin parametros.
bool WebFsStore::begin()
{
	return refresh();
}

// Relee los commits A/B sin modificar NAND; sin parametros.
bool WebFsStore::refresh()
{
	mountedBank = WebFsBank::none;
	for (BankState& bank : banks) mountBank(bank);
	if (banks[0].valid) mountedBank = WebFsBank::a;
	if (banks[1].valid && (!banks[0].valid || isNewerSequence(banks[1].commit.sequence, banks[0].commit.sequence)))
		mountedBank = WebFsBank::b;
	return mountedBank != WebFsBank::none;
}

// Indica si al menos una imagen WebFS se monto correctamente; sin parametros.
bool WebFsStore::isMounted() const
{
	return mountedBank != WebFsBank::none;
}

// Copia el estado compacto de bancos y montaje; info recibe la instantanea.
void WebFsStore::getInfo(WebFsInfo& info) const
{
	info = WebFsInfo{};
	if (lookupMutex && xSemaphoreTake(lookupMutex, 0) != pdTRUE) return;
	info.mounted = isMounted();
	info.activeBank = mountedBank;
	const BankState* states[] = { &banks[0], &banks[1] };
	WebFsBankInfo* output[] = { &info.bankA, &info.bankB };
	for (uint8_t index = 0; index < 2; ++index) {
		const BankState& bank = *states[index];
		WebFsBankInfo& value = *output[index];
		value.valid = bank.valid;
		value.sequence = bank.commit.sequence;
		value.imageSize = bank.header.imageSize;
		value.fileCount = bank.header.fileCount;
		value.capacityBytes = imageCapacity(index ? WebFsBank::b : WebFsBank::a);
	}
	if (lookupMutex) xSemaphoreGive(lookupMutex);
}

// Busca una entrada ordenada por path en la imagen activa; path identifica el recurso y file recibe su metadata.
bool WebFsStore::findFile(const char* path, WebFsFile& file)
{
	file = WebFsFile{};
	uint8_t bankIndexValue = 0;
	if (!path || !bankIndex(mountedBank, bankIndexValue) || !lookupMutex || !readResultQueue) return false;
	if (isBankFormatting(bankIndexValue)) return false;
	if (xSemaphoreTake(lookupMutex, pdMS_TO_TICKS(750)) != pdTRUE) return false;
	BankState& bank = banks[bankIndexValue];
	bool found = false;
	if (isBankFormatting(bankIndexValue)) {
		xSemaphoreGive(lookupMutex);
		return false;
	}
	uint16_t low = 0, high = bank.header.fileCount;
	while (low < high) {
		const uint16_t middle = uint16_t(low + (high - low) / 2);
		WebFsFormat::FileEntry entry{};
		if (!readImageBytes(bank, bank.header.tableOffset + uint32_t(middle) * sizeof(entry),
			reinterpret_cast<uint8_t*>(&entry), sizeof(entry))) break;
		entry.path[WebFsFormat::pathSize - 1] = 0;
		const int comparison = strcmp(path, entry.path);
		if (!comparison) {
			if (entry.dataOffset >= bank.header.dataOffset && entry.storedSize &&
				uint64_t(entry.dataOffset) + entry.storedSize <= bank.header.imageSize) {
				file.bank = mountedBank;
				file.entry = entry;
				found = true;
			}
			break;
		}
		if (comparison < 0) high = middle;
		else low = middle + 1;
	}
	xSemaphoreGive(lookupMutex);
	return found;
}

// Lee una pagina logica de imagen usando la cola indicada; bank, pageIndex, data y completionQueue describen la lectura.
bool WebFsStore::readImagePage(WebFsBank bank, uint32_t pageIndex, uint8_t* data, QueueHandle_t completionQueue,
	uint32_t requestId) const
{
	uint8_t index = 0;
	if (!data || !completionQueue || !bankIndex(bank, index) || isBankFormatting(index) || !banks[index].valid) return false;
	uint16_t block = 0;
	uint8_t page = 0;
	if (!translateImagePage(banks[index], pageIndex, block, page)) return false;
	return banks[index].partition->submitReadPage(block, page, 0, data, WebFsFormat::pageSize,
		StoragePriority::low, completionQueue, requestId);
}

// Devuelve el banco elegido al montar una imagen valida; sin parametros.
WebFsBank WebFsStore::activeBank() const
{
	return mountedBank;
}

// Calcula bytes utilizables excluyendo la pagina de commit; bank identifica A o B.
uint32_t WebFsStore::imageCapacity(WebFsBank bank) const
{
	uint8_t index = 0;
	if (!bankIndex(bank, index) || !banks[index].partition) return 0;
	uint16_t usableBlocks = 0;
	for (uint16_t block = 0; block < UINT16_MAX; ++block) {
		if (block >= 24) break;
		if (banks[index].partition->isUsableBlock(block)) ++usableBlocks;
	}
	const uint32_t pages = uint32_t(usableBlocks) * WinbondSpiNand::pagesPerBlock;
	return pages > 1 ? (pages - 1) * WebFsFormat::pageSize : 0;
}

// Copia informacion de un banco A/B; bank identifica el banco e info recibe el resultado.
bool WebFsStore::getBankInfo(WebFsBank bank, WebFsBankInfo& info) const
{
	uint8_t index = 0;
	if (!bankIndex(bank, index)) return false;
	const BankState& state = banks[index];
	info.valid = state.valid;
	info.sequence = state.commit.sequence;
	info.imageSize = state.header.imageSize;
	info.fileCount = state.header.fileCount;
	info.capacityBytes = imageCapacity(bank);
	return true;
}

// Reserva un banco y espera que terminen las consultas WebFS antes de borrarlo.
bool WebFsStore::preparePartitionFormat(WebFsBank bank)
{
	uint8_t index = 0;
	if (!bankIndex(bank, index) || !lookupMutex || xSemaphoreTake(lookupMutex, 0) != pdTRUE) return false;
	portENTER_CRITICAL(&formatMutex);
	const uint8_t bit = uint8_t(1U << index);
	const bool available = !(formattingBanks & bit) && upload.result != WebFsUploadResult::inProgress;
	if (available) formattingBanks |= bit;
	portEXIT_CRITICAL(&formatMutex);
	xSemaphoreGive(lookupMutex);
	return available;
}

// Remonta los bancos despues del borrado o invalida el destino si la operacion fallo.
void WebFsStore::finishPartitionFormat(WebFsBank bank, bool success)
{
	uint8_t index = 0;
	if (!bankIndex(bank, index)) return;
	if (lookupMutex) xSemaphoreTake(lookupMutex, portMAX_DELAY);
	if (success) refresh();
	else {
		banks[index].valid = false;
		banks[index].header = WebFsFormat::ImageHeader{};
		banks[index].commit = WebFsFormat::BankCommit{};
		if (mountedBank == bank) {
			const uint8_t other = index ? 0 : 1;
			mountedBank = banks[other].valid ? (other ? WebFsBank::b : WebFsBank::a) : WebFsBank::none;
		}
	}
	portENTER_CRITICAL(&formatMutex);
	formattingBanks &= uint8_t(~(1U << index));
	portEXIT_CRITICAL(&formatMutex);
	if (lookupMutex) xSemaphoreGive(lookupMutex);
}

// Inicia una carga hacia el banco no activo; totalBytes es el tamano exacto del cuerpo HTTP.
bool WebFsStore::beginUpload(uint32_t totalBytes)
{
	if (upload.result == WebFsUploadResult::inProgress) {
		return false;
	}
	if (!lookupMutex || xSemaphoreTake(lookupMutex, 0) != pdTRUE) return false;
	uploadLockHeld = true;
	upload = WebFsUploadInfo{};
	upload.totalBytes = totalBytes;
	upload.targetBank = mountedBank == WebFsBank::a ? WebFsBank::b : WebFsBank::a;
	uint8_t targetIndex = 0;
	if (!bankIndex(upload.targetBank, targetIndex) || isBankFormatting(targetIndex)) {
		upload.result = WebFsUploadResult::busy;
		xSemaphoreGive(lookupMutex);
		uploadLockHeld = false;
		return false;
	}
	upload.result = totalBytes ? WebFsUploadResult::inProgress : WebFsUploadResult::invalidImage;
	uploadHeader = WebFsFormat::ImageHeader{};
	delete[] uploadPage;
	uploadPage = new (std::nothrow) uint8_t[WebFsFormat::pageSize];
	if (!uploadPage) {
		upload.result = WebFsUploadResult::nandError;
		xSemaphoreGive(lookupMutex);
		uploadLockHeld = false;
		return false;
	}
	uploadCrc = 0xFFFFFFFFU;
	uploadPageBytes = 0;
	uploadPrepared = false;
	return upload.result == WebFsUploadResult::inProgress;
}

// Copia y programa fragmentos HTTP por paginas NAND; data y length describen el siguiente tramo consecutivo.
bool WebFsStore::appendUpload(const uint8_t* data, size_t length)
{
	if (!data || upload.result != WebFsUploadResult::inProgress || uint64_t(upload.receivedBytes) + length > upload.totalBytes) {
		upload.result = WebFsUploadResult::invalidImage;
		return false;
	}
	while (length) {
		const size_t count = min(length, size_t(WebFsFormat::pageSize - uploadPageBytes));
		memcpy(uploadPage + uploadPageBytes, data, count);
		uploadCrc = updateCrc32(uploadCrc, data, count);
		uploadPageBytes += count;
		upload.receivedBytes += count;
		data += count;
		length -= count;
		if (uploadPageBytes != WebFsFormat::pageSize) continue;
		if (!uploadPrepared && !prepareUpload()) return false;
		const uint32_t imagePage = (upload.receivedBytes / WebFsFormat::pageSize) - 1;
		if (!programUploadPage(imagePage)) {
			upload.result = WebFsUploadResult::nandError;
			return false;
		}
		uploadPageBytes = 0;
	}
	return true;
}

// Verifica los datos programados y confirma el banco cuando la carga termino; sin parametros.
WebFsUploadResult WebFsStore::finishUpload()
{
	if (upload.result != WebFsUploadResult::inProgress) {
		delete[] uploadPage;
		uploadPage = nullptr;
		if (uploadLockHeld) {
			xSemaphoreGive(lookupMutex);
			uploadLockHeld = false;
		}
		return upload.result;
	}
	if (!uploadPrepared || upload.receivedBytes != upload.totalBytes || uploadPageBytes) {
		upload.result = WebFsUploadResult::invalidImage;
		delete[] uploadPage;
		uploadPage = nullptr;
		if (uploadLockHeld) {
			xSemaphoreGive(lookupMutex);
			uploadLockHeld = false;
		}
		return upload.result;
	}
	uint8_t index = 0;
	if (!bankIndex(upload.targetBank, index)) {
		upload.result = WebFsUploadResult::nandError;
		delete[] uploadPage;
		uploadPage = nullptr;
		if (uploadLockHeld) {
			xSemaphoreGive(lookupMutex);
			uploadLockHeld = false;
		}
		return upload.result;
	}
	BankState& bank = banks[index];
	uint32_t verifiedCrc = 0;
	verifyPage = new (std::nothrow) uint8_t[WebFsFormat::pageSize];
	if (!verifyPage || !verifyUploadImage(bank, verifiedCrc) || verifiedCrc != ~uploadCrc || !validateTable(bank)) {
		delete[] verifyPage;
		verifyPage = nullptr;
		delete[] uploadPage;
		uploadPage = nullptr;
		upload.result = WebFsUploadResult::verificationFailed;
		if (uploadLockHeld) {
			xSemaphoreGive(lookupMutex);
			uploadLockHeld = false;
		}
		return upload.result;
	}
	delete[] verifyPage;
	verifyPage = nullptr;
	if (!writeCommit(bank, verifiedCrc)) {
		delete[] uploadPage;
		uploadPage = nullptr;
		upload.result = WebFsUploadResult::nandError;
		if (uploadLockHeld) {
			xSemaphoreGive(lookupMutex);
			uploadLockHeld = false;
		}
		return upload.result;
	}
	refresh();
	delete[] uploadPage;
	uploadPage = nullptr;
	upload.result = WebFsUploadResult::completed;
	if (uploadLockHeld) {
		xSemaphoreGive(lookupMutex);
		uploadLockHeld = false;
	}
	return upload.result;
}

// Copia el estado de la carga en curso o terminada; info recibe la instantanea.
void WebFsStore::getUploadInfo(WebFsUploadInfo& info) const
{
	info = upload;
}

// Invalida el commit actual para montar la imagen anterior; sin parametros.
bool WebFsStore::rollback()
{
	if (!lookupMutex || xSemaphoreTake(lookupMutex, pdMS_TO_TICKS(750)) != pdTRUE) return false;
	portENTER_CRITICAL(&formatMutex);
	const bool bankFormatting = formattingBanks != 0;
	portEXIT_CRITICAL(&formatMutex);
	if (bankFormatting) {
		xSemaphoreGive(lookupMutex);
		return false;
	}
	uint8_t activeIndex = 0;
	if (!bankIndex(mountedBank, activeIndex)) {
		xSemaphoreGive(lookupMutex);
		return false;
	}
	const uint8_t otherIndex = activeIndex ? 0 : 1;
	BankState& active = banks[activeIndex];
	if (!banks[otherIndex].valid || !active.partition || active.commitBlock == NandPartition::invalidBlock) {
		xSemaphoreGive(lookupMutex);
		return false;
	}
	NandIoResult stale;
	while (xQueueReceive(readResultQueue, &stale, 0) == pdPASS) {}
	const uint32_t requestId = ++nextRequestId;
	if (!active.partition->submitEraseBlock(active.commitBlock, StoragePriority::low, readResultQueue, requestId)) {
		xSemaphoreGive(lookupMutex);
		return false;
	}
	NandIoResult result;
	if (!waitReadResult(requestId, result) || !result.success) {
		xSemaphoreGive(lookupMutex);
		return false;
	}
	refresh();
	xSemaphoreGive(lookupMutex);
	return true;
}

// Convierte el enum del banco al indice del arreglo interno; bank identifica el banco e index recibe 0 o 1.
bool WebFsStore::bankIndex(WebFsBank bank, uint8_t& index) const
{
	if (bank == WebFsBank::a) { index = 0; return true; }
	if (bank == WebFsBank::b) { index = 1; return true; }
	return false;
}

// Indica si index esta reservado para formateo.
bool WebFsStore::isBankFormatting(uint8_t index) const
{
	if (index > 1) return false;
	portENTER_CRITICAL(const_cast<portMUX_TYPE*>(&formatMutex));
	const bool formatting = formattingBanks & uint8_t(1U << index);
	portEXIT_CRITICAL(const_cast<portMUX_TYPE*>(&formatMutex));
	return formatting;
}

// Lee commit, cabecera y tabla de un banco; bank contiene el estado que se actualiza.
bool WebFsStore::mountBank(BankState& bank)
{
	bank.valid = false;
	bank.commit = WebFsFormat::BankCommit{};
	bank.header = WebFsFormat::ImageHeader{};
	bank.usableBlocks = 0;
	bank.commitBlock = NandPartition::invalidBlock;
	if (!bank.partition || !bank.partition->isEnabled() || !readResultQueue) return false;
	for (uint16_t block = 0; block < 24; ++block)
		if (bank.partition->isUsableBlock(block)) ++bank.usableBlocks;
	bank.commitBlock = findCommitBlock(bank);
	if (bank.commitBlock == NandPartition::invalidBlock || bank.usableBlocks < 2) return false;
	NandIoResult stale;
	while (xQueueReceive(readResultQueue, &stale, 0) == pdPASS) {}
	uint8_t page[WebFsFormat::pageSize] = {};
	const uint32_t requestId = ++nextRequestId;
	if (!bank.partition->submitReadPage(bank.commitBlock, commitPage, 0, page, sizeof(page), StoragePriority::low,
		readResultQueue, requestId)) return false;
	NandIoResult result;
	if (!waitReadResult(requestId, result) || !result.success) return false;
	memcpy(&bank.commit, page, sizeof(bank.commit));
	const uint32_t storedCommitCrc = bank.commit.crc32;
	bank.commit.crc32 = 0;
	if (bank.commit.magic != WebFsFormat::commitMagic || bank.commit.formatVersion != WebFsFormat::version ||
		WebFsFormat::calculateCrc32(reinterpret_cast<const uint8_t*>(&bank.commit), sizeof(bank.commit)) != storedCommitCrc) return false;
	bank.commit.crc32 = storedCommitCrc;
	if (!readImageBytes(bank, 0, reinterpret_cast<uint8_t*>(&bank.header), sizeof(bank.header))) return false;
	const uint32_t capacity = imageCapacity(bank.partition == banks[0].partition ? WebFsBank::a : WebFsBank::b);
	if (!validateHeader(bank.header, capacity) || bank.commit.imageSize != bank.header.imageSize) return false;
	if (!validateTable(bank)) return false;
	bank.valid = true;
	return true;
}

// Lee bytes consecutivos desde una imagen en un buffer del Store; bank, offset, data y length delimitan el rango.
bool WebFsStore::readImageBytes(BankState& bank, uint32_t offset, uint8_t* data, size_t length)
{
	if (!data || !length || uint64_t(offset) + length > imageCapacity(bank.partition == banks[0].partition ? WebFsBank::a : WebFsBank::b)) return false;
	while (length) {
		const uint32_t pageIndex = offset / WebFsFormat::pageSize;
		const uint16_t column = offset % WebFsFormat::pageSize;
		const size_t count = min(length, size_t(WebFsFormat::pageSize - column));
		uint16_t block = 0;
		uint8_t page = 0;
		if (!translateImagePage(bank, pageIndex, block, page)) return false;
		NandIoResult stale;
		while (xQueueReceive(readResultQueue, &stale, 0) == pdPASS) {}
		const uint32_t requestId = ++nextRequestId;
		if (!bank.partition->submitReadPage(block, page, column, data, count, StoragePriority::low, readResultQueue, requestId)) return false;
		NandIoResult result;
		if (!waitReadResult(requestId, result) || !result.success || result.transferredBytes != count) return false;
		offset += count;
		data += count;
		length -= count;
	}
	return true;
}

// Traduce una pagina lineal de la imagen a bloque y pagina NAND, saltando bloques defectuosos.
bool WebFsStore::translateImagePage(const BankState& bank, uint32_t pageIndex, uint16_t& block, uint8_t& page) const
{
	const uint32_t pageCapacity = uint32_t(bank.usableBlocks) * WinbondSpiNand::pagesPerBlock;
	if (!bank.partition || pageIndex >= pageCapacity - 1) return false;
	const uint16_t usableOrdinal = uint16_t(pageIndex / WinbondSpiNand::pagesPerBlock);
	uint16_t found = 0;
	for (uint16_t relative = 0; relative < 24; ++relative) {
		if (!bank.partition->isUsableBlock(relative)) continue;
		if (found++ == usableOrdinal) {
			block = relative;
			page = pageIndex % WinbondSpiNand::pagesPerBlock;
			return true;
		}
	}
	return false;
}

// Busca el ultimo bloque utilizable de un banco para alojar el commit; bank identifica la region.
uint16_t WebFsStore::findCommitBlock(const BankState& bank) const
{
	if (!bank.partition) return NandPartition::invalidBlock;
	for (int16_t block = 23; block >= 0; --block)
		if (bank.partition->isUsableBlock(uint16_t(block))) return uint16_t(block);
	return NandPartition::invalidBlock;
}

// Verifica rango, version y CRC de cabecera; header describe la imagen y capacity limita sus bytes.
bool WebFsStore::validateHeader(const WebFsFormat::ImageHeader& source, uint32_t capacity) const
{
	WebFsFormat::ImageHeader header = source;
	const uint32_t storedCrc = header.headerCrc32;
	header.headerCrc32 = 0;
	if (header.magic != WebFsFormat::imageMagic || header.formatVersion != WebFsFormat::version ||
		header.fileEntrySize != WebFsFormat::entrySize || !header.imageSize || header.imageSize > capacity ||
		header.tableOffset < WebFsFormat::headerSize || header.dataOffset < header.tableOffset ||
		header.dataOffset % WebFsFormat::pageSize || header.imageSize % WebFsFormat::pageSize ||
		uint64_t(header.tableOffset) + uint64_t(header.fileCount) * WebFsFormat::entrySize > header.dataOffset ||
		header.dataOffset > header.imageSize) return false;
	return WebFsFormat::calculateCrc32(reinterpret_cast<const uint8_t*>(&header), sizeof(header)) == storedCrc;
}

// Relee la tabla y verifica su CRC antes de publicar el banco; bank contiene la cabecera validada.
bool WebFsStore::validateTable(BankState& bank)
{
	uint8_t* tablePage = new (std::nothrow) uint8_t[WebFsFormat::pageSize];
	if (!tablePage) return false;
	uint32_t crc = 0xFFFFFFFFU;
	uint32_t remaining = bank.header.dataOffset - bank.header.tableOffset;
	uint32_t offset = bank.header.tableOffset;
	while (remaining) {
		const size_t count = min<size_t>(remaining, WebFsFormat::pageSize);
		if (!readImageBytes(bank, offset, tablePage, count)) {
			delete[] tablePage;
			return false;
		}
		for (size_t index = 0; index < count; ++index) {
			crc ^= tablePage[index];
			for (uint8_t bit = 0; bit < 8; ++bit)
				crc = (crc >> 1) ^ (0xEDB88320U & uint32_t(-int32_t(crc & 1U)));
		}
		offset += count;
		remaining -= count;
	}
	delete[] tablePage;
	return ~crc == bank.header.tableCrc32;
}

// Espera un resultado NAND concreto durante el montaje o busqueda; requestId identifica la operacion.
bool WebFsStore::waitReadResult(uint32_t requestId, NandIoResult& result) const
{
	const TickType_t started = xTaskGetTickCount();
	TickType_t remaining = pdMS_TO_TICKS(750);
	while (xQueueReceive(readResultQueue, &result, remaining) == pdPASS) {
		if (result.requestId == requestId) return true;
		const TickType_t elapsed = xTaskGetTickCount() - started;
		if (elapsed >= pdMS_TO_TICKS(750)) break;
		remaining = pdMS_TO_TICKS(750) - elapsed;
	}
	return false;
}

// Valida la cabecera inicial y borra el banco inactivo antes de programar datos; sin parametros.
bool WebFsStore::prepareUpload()
{
	memcpy(&uploadHeader, uploadPage, sizeof(uploadHeader));
	uint8_t index = 0;
	if (!bankIndex(upload.targetBank, index)) {
		upload.result = WebFsUploadResult::nandError;
		return false;
	}
	BankState& bank = banks[index];
	const uint32_t capacity = imageCapacity(upload.targetBank);
	if (!upload.totalBytes || upload.totalBytes % WebFsFormat::pageSize || !validateHeader(uploadHeader, capacity) ||
		uploadHeader.imageSize != upload.totalBytes) {
		upload.result = uploadHeader.imageSize > capacity ? WebFsUploadResult::capacityExceeded : WebFsUploadResult::invalidImage;
		return false;
	}
	bank.valid = false;
	bank.header = uploadHeader;
	if (!eraseBank(bank)) {
		upload.result = WebFsUploadResult::nandError;
		return false;
	}
	uploadPrepared = true;
	return true;
}

// Borra los bloques utilizables de un banco destino; bank identifica la region que se recicla.
bool WebFsStore::eraseBank(BankState& bank)
{
	if (!bank.partition) return false;
	for (uint16_t block = 0; block < 24; ++block) {
		if (!bank.partition->isUsableBlock(block)) continue;
		NandIoResult stale;
		while (xQueueReceive(readResultQueue, &stale, 0) == pdPASS) {}
		const uint32_t requestId = ++nextRequestId;
		if (!bank.partition->submitEraseBlock(block, StoragePriority::low, readResultQueue, requestId)) return false;
		NandIoResult result;
		if (!waitReadResult(requestId, result) || !result.success) return false;
	}
	return true;
}

// Programa la pagina actual de carga usando el mapa logico del banco; imagePage identifica la pagina WebFS.
bool WebFsStore::programUploadPage(uint32_t imagePage)
{
	uint8_t index = 0;
	if (!bankIndex(upload.targetBank, index)) return false;
	BankState& bank = banks[index];
	uint16_t block = 0;
	uint8_t page = 0;
	if (!translateImagePage(bank, imagePage, block, page)) return false;
	NandIoResult stale;
	while (xQueueReceive(readResultQueue, &stale, 0) == pdPASS) {}
	const uint32_t requestId = ++nextRequestId;
	if (!bank.partition->submitProgramPage(block, page, uploadPage, WebFsFormat::pageSize, StoragePriority::low,
		readResultQueue, requestId)) return false;
	NandIoResult result;
	return waitReadResult(requestId, result) && result.success;
}

// Relee cada pagina escrita y calcula el CRC completo; bank identifica la imagen y crc recibe el valor final.
bool WebFsStore::verifyUploadImage(BankState& bank, uint32_t& crc)
{
	crc = 0xFFFFFFFFU;
	for (uint32_t offset = 0; offset < upload.totalBytes; offset += WebFsFormat::pageSize) {
		if (!readImageBytes(bank, offset, verifyPage, WebFsFormat::pageSize)) return false;
		crc = updateCrc32(crc, verifyPage, WebFsFormat::pageSize);
	}
	crc = ~crc;
	return true;
}

// Programa el commit de una imagen validada como ultima operacion; bank identifica el banco e imageCrc protege sus bytes.
bool WebFsStore::writeCommit(BankState& bank, uint32_t imageCrc)
{
	if (bank.commitBlock == NandPartition::invalidBlock) return false;
	WebFsFormat::BankCommit commit{};
	commit.magic = WebFsFormat::commitMagic;
	commit.formatVersion = WebFsFormat::version;
	const WebFsBank active = mountedBank;
	uint8_t activeIndex = 0;
	commit.sequence = bankIndex(active, activeIndex) && banks[activeIndex].valid ? banks[activeIndex].commit.sequence + 1U : 1U;
	commit.imageSize = upload.totalBytes;
	commit.imageCrc32 = imageCrc;
	commit.activatedAt = uint32_t(time(nullptr));
	commit.crc32 = 0;
	commit.crc32 = WebFsFormat::calculateCrc32(reinterpret_cast<const uint8_t*>(&commit), sizeof(commit));
	memset(uploadPage, 0xFF, WebFsFormat::pageSize);
	memcpy(uploadPage, &commit, sizeof(commit));
	NandIoResult stale;
	while (xQueueReceive(readResultQueue, &stale, 0) == pdPASS) {}
	const uint32_t requestId = ++nextRequestId;
	if (!bank.partition->submitProgramPage(bank.commitBlock, commitPage, uploadPage, WebFsFormat::pageSize,
		StoragePriority::low, readResultQueue, requestId)) return false;
	NandIoResult result;
	return waitReadResult(requestId, result) && result.success;
}

// Actualiza CRC32 IEEE sin finalizarlo; crc conserva el estado y data/length delimitan los bytes nuevos.
uint32_t WebFsStore::updateCrc32(uint32_t crc, const uint8_t* data, size_t length)
{
	for (size_t index = 0; index < length; ++index) {
		crc ^= data[index];
		for (uint8_t bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320U & uint32_t(-int32_t(crc & 1U)));
	}
	return crc;
}

// Compara secuencias uint32 contemplando el desborde; candidate y current son las secuencias a ordenar.
bool WebFsStore::isNewerSequence(uint32_t candidate, uint32_t current)
{
	return int32_t(candidate - current) > 0;
}
