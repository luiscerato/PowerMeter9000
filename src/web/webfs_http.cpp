#include "webfs_http.h"

#include <new>

#include "storage/storage_manager.h"
#include "storage/webfs_read_session.h"

namespace {

struct WebFsUploadRequest {
	bool accepted = false;
	WebFsUploadResult result = WebFsUploadResult::none;
};

static StorageManager* storageManager = nullptr;
static constexpr uint8_t webFsReadSessionCount = 4;
static WebFsReadSession webFsReadSessions[webFsReadSessionCount];

static const char recoveryPage[] PROGMEM = R"HTML(
<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Power Meter Recovery</title>
<style>
body{font:15px Arial;margin:24px;max-width:540px;color:#20252b}h1{font-size:22px}section{border:1px solid #ccd3dc;padding:12px;margin:12px 0}button,input{font:inherit;padding:6px;margin:4px 0}small{color:#56616c}#result{white-space:pre-wrap}
</style>
<h1>Power Meter - Recovery</h1>
<section id="state">Loading status...</section>
<section><label>WebFS image <input id="file" type="file" accept=".fs32,application/octet-stream"></label><br><button onclick="upload()">Upload to NAND</button><div id="result"></div></section>
<script>
const state=document.querySelector('#state'),result=document.querySelector('#result');
// load: muestra el estado de WebFS y sus dos bancos NAND; no recibe parametros.
async function load(){try{let r=await fetch('/webfs',{cache:'no-store'}),d=await r.json();state.textContent=`NAND WebFS: ${d.nand.mounted?'ready':'missing'}\nNAND A: ${d.nand.a.valid?'valid':'invalid'} (${d.nand.a.imageSize} bytes)\nNAND B: ${d.nand.b.valid?'valid':'invalid'} (${d.nand.b.imageSize} bytes)\nActive: ${d.nand.active||'none'} sequence ${d.nand.sequence}`;}catch(e){state.textContent='Unable to read recovery status';}}
// upload: carga la imagen seleccionada a la NAND; no recibe parámetros y usa el input file.
async function upload(){let f=document.querySelector('#file').files[0];if(!f)return;result.textContent='Uploading '+f.size+' bytes...';let r=await fetch('/webfs/image',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});result.textContent=await r.text();load();}
load();
</script>
)HTML";

// Devuelve una sesion WebFS libre para una respuesta HTTP; sin parametros.
WebFsReadSession* findWebFsReadSession()
{
	for (WebFsReadSession& session : webFsReadSessions) {
		if (!session.isActive()) return &session;
	}
	return nullptr;
}

// Envia un archivo desde la imagen WebFS activa.
// request: solicitud HTTP. path: ruta absoluta del recurso.
bool sendNandAsset(AsyncWebServerRequest* request, const String& path)
{
	WebFsFile file;
	if (!storageManager->webFs().findFile(path.c_str(), file)) return false;
	WebFsReadSession* session = findWebFsReadSession();
	if (!session || !session->begin(storageManager->webFs(), file)) return false;
	AsyncWebServerResponse* response = request->beginResponse(WebFsFormat::contentType(WebFsFormat::MimeType(file.entry.mimeType)),
		file.entry.storedSize, [session](uint8_t* data, size_t maxLength, size_t offset) -> size_t {
			return session->copyChunk(data, maxLength, offset);
		});
	if (file.entry.encoding == uint8_t(WebFsFormat::Encoding::gzip)) response->addHeader("Content-Encoding", "gzip");
	response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
	response->addHeader("ETag", String(file.entry.dataCrc32, HEX));
	request->send(response);
	return true;
}

// Convierte un resultado de carga WebFS a una etiqueta JSON estable.
// result: estado final informado por WebFsStore.
const char* webFsUploadResultName(WebFsUploadResult result)
{
	switch (result) {
	case WebFsUploadResult::completed: return "completed";
	case WebFsUploadResult::busy: return "busy";
	case WebFsUploadResult::invalidImage: return "invalid_image";
	case WebFsUploadResult::capacityExceeded: return "capacity_exceeded";
	case WebFsUploadResult::nandError: return "nand_error";
	case WebFsUploadResult::verificationFailed: return "verification_failed";
	case WebFsUploadResult::inProgress: return "in_progress";
	default: return "none";
	}
}

// Registra los endpoints para consultar, actualizar y recuperar WebFS.
// server: servidor HTTP principal.
void registerWebFsEndpoints(AsyncWebServer& server)
{
	server.on("/recovery", HTTP_GET, [](AsyncWebServerRequest* request) { webFsHttpSendRecovery(request); });

	// Registra el estado despues de /webfs/image porque el servidor coincide por prefijo.
	auto registerWebFsStatus = [&server]() {
	server.on("/webfs", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (request->url() != "/webfs") {
			request->send(404, "application/json", "{\"error\":\"not_found\"}");
			return;
		}
		WebFsInfo info;
		storageManager->webFs().getInfo(info);
		String json = "{\"nand\":{\"mounted\":";
		json += info.mounted ? "true" : "false";
		json += ",\"active\":\"";
		json += info.activeBank == WebFsBank::a ? "a" : info.activeBank == WebFsBank::b ? "b" : "";
		json += "\",\"sequence\":";
		const WebFsBankInfo& activeInfo = info.activeBank == WebFsBank::b ? info.bankB : info.bankA;
		json += activeInfo.sequence;
		json += ",\"a\":{\"valid\":";
		json += info.bankA.valid ? "true" : "false";
		json += ",\"imageSize\":";
		json += info.bankA.imageSize;
		json += ",\"capacityBytes\":";
		json += info.bankA.capacityBytes;
		json += "},\"b\":{\"valid\":";
		json += info.bankB.valid ? "true" : "false";
		json += ",\"imageSize\":";
		json += info.bankB.imageSize;
		json += ",\"capacityBytes\":";
		json += info.bankB.capacityBytes;
		json += "}}}";
		AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});
	};

	server.on("/webfs/image", HTTP_GET, [](AsyncWebServerRequest* request) {
		if (request->url() != "/webfs/image") {
			request->send(404, "application/json", "{\"error\":\"not_found\"}");
			return;
		}
		WebFsInfo info;
		storageManager->webFs().getInfo(info);
		if (!info.mounted) {
			request->send(404, "application/json", "{\"ok\":false,\"reason\":\"webfs_unavailable\"}");
			return;
		}
		WebFsReadSession* session = findWebFsReadSession();
		if (!session) {
			request->send(429, "application/json", "{\"ok\":false,\"reason\":\"webfs_download_busy\"}");
			return;
		}
		WebFsFile image;
		image.bank = info.activeBank;
		image.entry.dataOffset = 0;
		image.entry.storedSize = info.activeBank == WebFsBank::a ? info.bankA.imageSize : info.bankB.imageSize;
		if (!session->begin(storageManager->webFs(), image)) {
			request->send(503, "application/json", "{\"ok\":false,\"reason\":\"webfs_session_unavailable\"}");
			return;
		}
		AsyncWebServerResponse* response = request->beginResponse("application/octet-stream", image.entry.storedSize,
			[session](uint8_t* data, size_t maxLength, size_t offset) -> size_t { return session->copyChunk(data, maxLength, offset); });
		response->addHeader("Content-Disposition", "attachment; filename=power-meter-webfs.fs32");
		response->addHeader("Cache-Control", "no-store");
		request->send(response);
	});

	server.on("/webfs/image", HTTP_POST,
		[](AsyncWebServerRequest* request) {
			if (request->url() != "/webfs/image") {
				request->send(404, "application/json", "{\"error\":\"not_found\"}");
				return;
			}
			WebFsUploadRequest* uploadRequest = static_cast<WebFsUploadRequest*>(request->_tempObject);
			WebFsUploadResult result = uploadRequest ? uploadRequest->result : WebFsUploadResult::invalidImage;
			if (uploadRequest && uploadRequest->accepted) result = storageManager->webFs().finishUpload();
			delete uploadRequest;
			request->_tempObject = nullptr;
			const uint16_t code = result == WebFsUploadResult::completed ? 200 : result == WebFsUploadResult::busy ? 409 : result == WebFsUploadResult::capacityExceeded ? 413 : 400;
			WebFsUploadInfo progress;
			storageManager->webFs().getUploadInfo(progress);
			String json = "{\"ok\":";
			json += result == WebFsUploadResult::completed ? "true" : "false";
			json += ",\"result\":\"";
			json += webFsUploadResultName(result);
			json += "\",\"receivedBytes\":";
			json += progress.receivedBytes;
			json += ",\"totalBytes\":";
			json += progress.totalBytes;
			json += "}";
			request->send(code, "application/json", json);
		}, nullptr,
		[](AsyncWebServerRequest* request, uint8_t* data, size_t length, size_t index, size_t total) {
			if (!index) {
				WebFsUploadRequest* uploadRequest = new (std::nothrow) WebFsUploadRequest;
				request->_tempObject = uploadRequest;
				if (!uploadRequest || storageManager->state() != StorageState::ready || !storageManager->webFs().beginUpload(total)) {
					if (uploadRequest) uploadRequest->result = WebFsUploadResult::busy;
					return;
				}
				uploadRequest->accepted = true;
			}
			WebFsUploadRequest* uploadRequest = static_cast<WebFsUploadRequest*>(request->_tempObject);
			if (!uploadRequest || !uploadRequest->accepted) return;
			if (!storageManager->webFs().appendUpload(data, length)) {
				WebFsUploadInfo progress;
				storageManager->webFs().getUploadInfo(progress);
				uploadRequest->result = progress.result;
			}
		});

	server.on("/webfs/rollback", HTTP_POST, [](AsyncWebServerRequest* request) {
		if (request->url() != "/webfs/rollback") {
			request->send(404, "application/json", "{\"error\":\"not_found\"}");
			return;
		}
		if (!request->hasParam("confirm", true) || request->getParam("confirm", true)->value() != "ROLLBACK") {
			request->send(400, "application/json", "{\"ok\":false,\"reason\":\"confirmation_required\"}");
			return;
		}
		for (WebFsReadSession& session : webFsReadSessions) {
			if (session.isActive()) {
				request->send(409, "application/json", "{\"ok\":false,\"reason\":\"webfs_read_active\"}");
				return;
			}
		}
		if (!storageManager->webFs().rollback()) {
			request->send(409, "application/json", "{\"ok\":false,\"reason\":\"previous_bank_unavailable\"}");
			return;
		}
		request->send(200, "application/json", "{\"ok\":true}");
	});
	registerWebFsStatus();
}

}

// Registra los endpoints WebFS y asocia el almacenamiento NAND.
// server: servidor HTTP principal. storage: administrador de almacenamiento NAND.
void webFsHttpBegin(AsyncWebServer& server, StorageManager& storage)
{
	storageManager = &storage;
	registerWebFsEndpoints(server);
}

// Sirve un recurso estatico desde la imagen WebFS activa en NAND.
// request: solicitud HTTP para el recurso o la pagina principal.
void webFsHttpHandleAsset(AsyncWebServerRequest* request)
{
	const String path = request->url() == "/" ? "/index.html" : request->url();
	const bool root = path == "/index.html";
	if (sendNandAsset(request, path)) return;
	if (root) webFsHttpSendRecovery(request);
	else request->send(404, "text/plain", "Page Not Found");
}

// Envia la pagina de recuperacion compilada en el firmware.
// request: solicitud HTTP que debe recibir la pagina minima.
void webFsHttpSendRecovery(AsyncWebServerRequest* request)
{
	AsyncWebServerResponse* response = request->beginResponse(200, "text/html; charset=utf-8", recoveryPage);
	response->addHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
	request->send(response);
}
