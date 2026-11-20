#include "network_diagnostics.h"
#include "diagnostic_logger.h"
#include "memory_diagnostics.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <atomic>
#include <stdio.h>

namespace {
constexpr uint32_t firstProbeDelayMs = 20000;
constexpr uint32_t probeIntervalMs = 3600000;
constexpr uint32_t tcpSummaryIntervalMs = 60000;
constexpr uint32_t tcpUrgentIntervalMs = 10000;
constexpr uint32_t probeRetryDelayMs = 10000;
constexpr size_t probeStackBytes = 12 * 1024;
constexpr uint32_t probeMemoryMargin = 48 * 1024;
constexpr uint8_t urgentCoalesceCount = 8;
constexpr uint8_t urgentDiscardCount = 3;

std::atomic<uint32_t> coalescedPolls{0};
std::atomic<uint32_t> discardedPolls{0};
std::atomic<uint32_t> tcpErrors{0};
std::atomic<int8_t> lastTcpError{0};
std::atomic<uint8_t> peakQueueDepth{0};
std::atomic<bool> probeRunning{false};
std::atomic<uint32_t> queueWaiters{0};
uint32_t queueBlockedSinceMs = 0;
bool queueBlockReported = false;
bool serverAvailable = false;
bool probeHasRun = false;
uint32_t serverStartedMs = 0;
uint32_t lastProbeMs = 0;
uint32_t nextProbeAttemptMs = 0;
uint32_t lastTcpSummaryMs = 0;

// Conserva la mayor profundidad de cola hasta el siguiente resumen; queueDepth es la lectura actual.
void recordQueueDepth(uint8_t queueDepth)
{
	uint8_t peak = peakQueueDepth.load(std::memory_order_relaxed);
	while (queueDepth > peak && !peakQueueDepth.compare_exchange_weak(peak, queueDepth,
		std::memory_order_relaxed, std::memory_order_relaxed)) {}
}

// Lee la primera linea HTTP; client es la conexion, line/capacity definen el buffer y timeoutMs limita la espera.
bool readHttpStatusLine(WiFiClient& client, char* line, size_t capacity, uint32_t timeoutMs)
{
	if (!line || capacity < 2) return false;
	const uint32_t startedMs = millis();
	size_t length = 0;
	while (uint32_t(millis() - startedMs) < timeoutMs) {
		while (client.available()) {
			const int value = client.read();
			if (value < 0) break;
			if (value == '\n') { line[length] = '\0'; return length != 0; }
			if (value != '\r') {
				if (length + 1 >= capacity) { line[length] = '\0'; return false; }
				line[length++] = char(value);
			}
		}
		if (!client.connected()) break;
		vTaskDelay(pdMS_TO_TICKS(5));
	}
	line[length] = '\0';
	return false;
}

// Consulta /ping en la IP local con timeouts acotados; no recibe parametros.
void runLocalHttpProbe()
{
	const uint32_t startedMs = millis();
	IPAddress address = WiFi.localIP();
	if (address == IPAddress(0, 0, 0, 0)) address = WiFi.softAPIP();
	const bool connected = address != IPAddress(0, 0, 0, 0);
	const char* failure = connected ? "connect_timeout" : "no_local_ip";
	uint16_t statusCode = 0;
	char statusLine[64] = {};
	WiFiClient client;

	if (connected && client.connect(address, 80, 1000)) {
		client.setNoDelay(true);
		char request[96];
		const int requestLength = snprintf(request, sizeof(request),
			"GET /ping HTTP/1.0\r\nHost: %u.%u.%u.%u\r\nConnection: close\r\n\r\n",
			address[0], address[1], address[2], address[3]);
		if (requestLength > 0 && size_t(requestLength) < sizeof(request) &&
			client.write(reinterpret_cast<const uint8_t*>(request), size_t(requestLength)) == size_t(requestLength)) {
			failure = "response_timeout";
			if (readHttpStatusLine(client, statusLine, sizeof(statusLine), 1000)) {
				if (sscanf(statusLine, "HTTP/%*s %hu", &statusCode) == 1)
					failure = statusCode == 200 ? nullptr : "http_status";
				else failure = "bad_status_line";
			}
		}
		else failure = "send_failed";
	}
	client.stop();
	const uint32_t elapsedMs = millis() - startedMs;
	if (failure) {
		DIAG_WARN(DiagnosticModule::health, "Autoprueba HTTP local fallo=%s ms=%lu ip=%u.%u.%u.%u",
			failure, static_cast<unsigned long>(elapsedMs), address[0], address[1], address[2], address[3]);
	}
	else {
		DIAG_INFO(DiagnosticModule::health, "Autoprueba HTTP local OK status=%u ms=%lu ip=%u.%u.%u.%u",
			statusCode, static_cast<unsigned long>(elapsedMs), address[0], address[1], address[2], address[3]);
	}
}

// Ejecuta la prueba fuera del loop principal y libera su pila al terminar; parameter no se usa.
void localProbeTask(void* parameter)
{
	(void)parameter;
	runLocalHttpProbe();
	probeRunning.store(false, std::memory_order_release);
	vTaskDelete(nullptr);
}
}

// Cuenta productores esperando espacio TCP; entering indica entrada o salida de la espera.
extern "C" void meterAsyncTcpQueueWait(bool entering)
{
	if (entering) queueWaiters.fetch_add(1, std::memory_order_relaxed);
	else queueWaiters.fetch_sub(1, std::memory_order_relaxed);
}

// Marca el inicio del intervalo para la primera prueba; no recibe parametros.
void networkDiagnosticsStart()
{
	if (serverAvailable) return;
	serverAvailable = true;
	serverStartedMs = millis();
	lastTcpSummaryMs = serverStartedMs;
}

// Cuenta una coalescencia sin formatear ni escribir desde la tarea AsyncTCP.
extern "C" void meterAsyncTcpPollCoalesced(uint8_t queueDepth)
{
	coalescedPolls.fetch_add(1, std::memory_order_relaxed);
	recordQueueDepth(queueDepth);
}

// Cuenta un descarte de poll sin formatear ni escribir desde la tarea AsyncTCP.
extern "C" void meterAsyncTcpPollDiscarded(uint8_t queueDepth)
{
	discardedPolls.fetch_add(1, std::memory_order_relaxed);
	recordQueueDepth(queueDepth);
}

// Cuenta errores lwIP para resumirlos desde el loop; errorCode conserva el ultimo codigo recibido.
extern "C" void meterAsyncTcpError(int8_t errorCode)
{
	tcpErrors.fetch_add(1, std::memory_order_relaxed);
	lastTcpError.store(errorCode, std::memory_order_relaxed);
}

// Emite resumen TCP filtrado y lanza la prueba inicial a los 20 s, luego cada hora.
void networkDiagnosticsPoll()
{
	if (!serverAvailable) return;
	const uint32_t now = millis();
	if (queueWaiters.load(std::memory_order_relaxed)) {
		if (!queueBlockedSinceMs) queueBlockedSinceMs = now;
		const uint32_t blockedMs = now - queueBlockedSinceMs;
		if (blockedMs >= 60000U && !queueBlockReported) {
			queueBlockReported = true;
			DIAG_WARN(DiagnosticModule::tcp, "Cola AsyncTCP bloqueada mas de 60s");
		}
		if (blockedMs >= 15U * 60000U) {
			DIAG_WARN(DiagnosticModule::health, "Reinicio por espera AsyncTCP persistente 15min");
			ESP.restart();
		}
	} else {
		queueBlockedSinceMs = 0;
		queueBlockReported = false;
	}
	const uint32_t coalesced = coalescedPolls.load(std::memory_order_relaxed);
	const uint32_t discarded = discardedPolls.load(std::memory_order_relaxed);
	const uint32_t errors = tcpErrors.load(std::memory_order_relaxed);
	const uint32_t summaryAge = now - lastTcpSummaryMs;
	const bool urgentTcpIssue = discarded >= urgentDiscardCount || coalesced >= urgentCoalesceCount || errors >= 3;
	if (summaryAge >= tcpSummaryIntervalMs || (urgentTcpIssue && summaryAge >= tcpUrgentIntervalMs)) {
		lastTcpSummaryMs = now;
		const uint32_t coalescedSummary = coalescedPolls.exchange(0, std::memory_order_relaxed);
		const uint32_t discardedSummary = discardedPolls.exchange(0, std::memory_order_relaxed);
		const uint32_t errorSummary = tcpErrors.exchange(0, std::memory_order_relaxed);
		const int8_t lastError = lastTcpError.exchange(0, std::memory_order_relaxed);
		const uint8_t peak = peakQueueDepth.exchange(0, std::memory_order_relaxed);
		if (coalescedSummary || discardedSummary || errorSummary)
			DIAG_WARN(DiagnosticModule::tcp, "AsyncTCP errores=%lu ultimo_err=%d coalescidos=%lu descartados=%lu pico=%u",
				static_cast<unsigned long>(errorSummary), int(lastError), static_cast<unsigned long>(coalescedSummary),
				static_cast<unsigned long>(discardedSummary), unsigned(peak));
	}

	const bool firstProbeDue = !probeHasRun && uint32_t(now - serverStartedMs) >= firstProbeDelayMs;
	const bool retryProbeDue = !probeHasRun && nextProbeAttemptMs && int32_t(now - nextProbeAttemptMs) >= 0;
	const bool hourlyProbeDue = probeHasRun && uint32_t(now - lastProbeMs) >= probeIntervalMs;
	if (!firstProbeDue && !retryProbeDue && !hourlyProbeDue) return;
	// La prueba crea una pila de 16 KB; si el heap ya esta comprometido, espera otro ciclo.
	if (!memoryHasRoom(probeStackBytes, probeMemoryMargin)) {
		nextProbeAttemptMs = now + probeRetryDelayMs;
		return;
	}
	if (probeRunning.exchange(true, std::memory_order_acq_rel)) return;
	probeHasRun = true;
	lastProbeMs = now;
	nextProbeAttemptMs = 0;
	if (xTaskCreate(localProbeTask, "localHttpProbe", 3072, nullptr, 1, nullptr) != pdPASS) {
		probeRunning.store(false, std::memory_order_release);
		DIAG_WARN(DiagnosticModule::health, "No se pudo crear la tarea de autoprueba HTTP");
	}
}
