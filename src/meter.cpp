#include "meter.h"
#include "ade9000/ADE9000RegMap.h"
#include "pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "webserver.h"
#include "bl0942/bl0942.h"
#include "manager_api.h"
#include "event_store.h"
#include "grid_events.h"
#include "storage/grid_event_store.h"
#include "memory_diagnostics.h"
#include "meter_timing.h"
#include <memory>
#include <new>
#include <atomic>

/*
	Eventos de fases:
	Pico alto de voltaje
	Pico bajo de voltaje
	Pico de corriente
	Pérdida de fase

	Eventos generales:
	Corte de energía
*/


ADE9000 ade(15000000, pinAdeSS, VSPI);
BL0942 bl0942(2);					//Usar el puerto serie 2 para el BL0942

const uint8_t NOMINAL_INPUT_VOLTAGE = 220;
const float NOMINAL_INPUT_CURRENT = 1;/* Current nominal RMS for Input in Amperes */
const uint8_t INPUT_FREQUENCY = 50;/* Frequency in Hertz */
const uint8_t CALIBRATION_ANGLE_DEGREES = 60;/* Used for funtion transform current in Ohm */
const float BURDEN_RESISTOR = 11.24;/* Current nominal RMS for Input in Amperes */
const uint16_t TURNS_RATIO_TRANSFORMER = 4400;/* Used for funtion transform current */
const float ATTEUNATION_FACTOR = 1001;/* The defaul atteunation factor on board used funtion transform in Ohm/Ohm ((R1 + R2)/ R2) */
const uint8_t ACCUMULATION_TIME = 1;/* Accumulation time in seconds when EGY_TIME=7999, accumulation mode= sample based */


meterValues meterVals;		//Contiene los datos leidos por taskMeter, por lo que no se deben usar fuera de esa tarea
meterValues Meter;			//Contiene datos que se pueden leer de manera segura por otro task
static gridSettings gridConfig, requestedGridConfig;
static bool gridConfigPending = false;
static portMUX_TYPE gridConfigMutex = portMUX_INITIALIZER_UNLOCKED;

struct legacyGridSettingsV1 {
	phaseLimits phases[3];
	float noVoltage;
	float hysteresis;
	uint32_t lossConfirmMs;
	uint32_t voltageConfirmMs;
	uint32_t recoveryConfirmMs;
};
static_assert(sizeof(legacyGridSettingsV1) == 44, "Unexpected legacy Grid settings size");

// Copia en config los ajustes pedidos, sin exponer memoria compartida al servidor.
void meterGetGridSettings(gridSettings& config) {
	taskENTER_CRITICAL(&gridConfigMutex);
	config = requestedGridConfig;
	taskEXIT_CRITICAL(&gridConfigMutex);
}

// Guarda config validada en un unico registro NVS y solicita su aplicacion a MeterTask.
bool meterSaveGridSettings(const gridSettings& config) {
	if (!validGridSettings(config)) return false;
	Preferences prefs;
	if (!prefs.begin("grid", false)) return false;
	const bool saved = prefs.putBytes("settingsV1", &config, sizeof(config)) == sizeof(config);
	prefs.end();
	if (!saved) return false;
	taskENTER_CRITICAL(&gridConfigMutex);
	requestedGridConfig = config;
	gridConfigPending = true;
	taskEXIT_CRITICAL(&gridConfigMutex);
	return true;
}

// Restaura ajustes validos antes de iniciar MeterTask; sin parametros.
static void meterLoadGridSettings() {
	gridSettings config;
	Preferences prefs;
	if (prefs.begin("grid", true)) {
		const size_t storedSize = prefs.getBytesLength("settingsV1");
		if (storedSize == sizeof(config))
			prefs.getBytes("settingsV1", &config, sizeof(config));
		else if (storedSize == 48)
			prefs.getBytes("settingsV1", &config, 48); // Conserva la configuracion anterior y deja neutro desactivado.
		else if (storedSize == sizeof(legacyGridSettingsV1)) {
			legacyGridSettingsV1 legacy = {};
			prefs.getBytes("settingsV1", &legacy, sizeof(legacy));
			memcpy(config.phases, legacy.phases, sizeof(config.phases));
			config.noVoltage = legacy.noVoltage;
			config.hysteresis = legacy.hysteresis;
			config.lossConfirmMs = legacy.lossConfirmMs;
			config.voltageConfirmMs = legacy.voltageConfirmMs;
			config.recoveryConfirmMs = legacy.recoveryConfirmMs;
		}
		prefs.end();
	}
	if (!validGridSettings(config)) config = gridSettings{};
	taskENTER_CRITICAL(&gridConfigMutex);
	gridConfig = requestedGridConfig = config;
	taskEXIT_CRITICAL(&gridConfigMutex);
}
struct TotalEnergyVals MeterEnergy;

TaskHandle_t meterHandle = NULL;

WFBFixedDataRate_t readBuffer[WFB_SAMPLES_PER_CHANNEL / 2];		//Buffer temporal para leer los datos del wavebuffer (7ch*128 samples)
uint8_t outputSamples[8064];			//Buffer para 96ms de datos de 12 bits. 96ms ->768 samples por canal. 768*7*(12/8) => 8064 bytes
fastRMSData_t halfCycleSamples[fastDataSamplesCount];	//Buffer para 4 segundos de datos half
static uint32_t halfCycleSampleMs[fastDataSamplesCount] = {};
uint16_t halfCycleSamplesIndex = 0;		//Indice de buffer de muestras rápidas
uint16_t halfCycleSamplesCount = 0;		//Cantidad de muestras rápidas del buffer
uint16_t eventsDataPrevSamples = 100;	//Cantidad de muestras a guardar antes del evento
uint16_t eventsDataPostSamples = 100; 	//Cantodad de muestras a guardar después del úlitmo evento
uint16_t eventsDataMaxSamples = 300;	//Cantidad máximas de muestras a guardar durante un evento.

scopeInfo_t scopeInfo;
uint32_t meterUpdated = 0;
uint64_t meterLastSampleTimestampMs = 0;
portMUX_TYPE meterMutex = portMUX_INITIALIZER_UNLOCKED;

Preferences meterOptions;
extern capturedEvent EventsData;
static volatile bool meterTestEventRequested = false;
static volatile uint32_t meterIrq0Us = 0;
static volatile uint32_t meterIrq0Sequence = 0;
static uint32_t waveIrqSequence = 0;
static uint32_t wavePageIrqUs = 0;
static uint32_t waveReferenceUs = 0;
static uint32_t wavePart = 0;
static bool waveReferenceValid = false;
static uint32_t waveFrameSequence = 0;
static uint8_t waveReadPage = 0, waveAfterPage = 0, waveReadHalf = 0;
static uint32_t waveReadIntervalUs = 0;
static std::atomic<uint32_t> waveStoreLastUs{0}, waveStoreMaxUs{0};
static std::atomic<uint32_t> wavePageFullCount{0}, waveReadCount{0}, waveFrameCount{0};
static std::atomic<uint32_t> waveWsQueuedCount{0}, waveWsBlockedCount{0}, waveWsFailedCount{0};
static std::atomic<uint32_t> waveDiagStatus0{0}, waveDiagWfbConfig{0}, waveDiagWfbStatus{0};
static portMUX_TYPE waveTraceMutex = portMUX_INITIALIZER_UNLOCKED;
static uint8_t zeroCrossTimeoutMask = 0;
static constexpr uint32_t fastRmsPollIntervalUs = 10000U;
static constexpr uint32_t fastRmsReadyTimeoutUs = 15000U;

// Registra en la máscara las fases marcadas con timeout en STATUS1.
// status1 contiene la lectura actual del registro de estado.
static void meterUpdateZeroCrossTimeoutMask(const ADE_STATUS1_t& status1);

struct WaveReadTrace {
	char eventId[48];
	uint32_t block;
	uint32_t irqSequence;
	uint32_t irqUs;
	uint32_t readUs;
	uint32_t intervalUs;
	uint32_t partHash;
	uint8_t half;
	uint8_t part;
	uint8_t beforePage;
	uint8_t afterPage;
	const char* result;
};
static const uint8_t waveTraceCapacity = 48;
static WaveReadTrace waveTrace[waveTraceCapacity] = {};
static uint32_t waveTraceCount = 0;

// Registra un fragmento sin imprimir en la tarea de adquisicion.
// before/after: paginas antes/despues; readUs: duracion SPI; partData: 1344 bytes comprimidos.
static void meterTraceWave(uint8_t before, uint8_t after, uint32_t readUs, const uint8_t* partData)
{
	if (!EventsData.getEventId().length()) return;
	portENTER_CRITICAL(&waveTraceMutex);
	bool complete = waveTraceCount == waveTraceCapacity &&
		strcmp(waveTrace[0].eventId, EventsData.getEventId().c_str()) == 0;
	portEXIT_CRITICAL(&waveTraceMutex);
	if (complete) return;
	WaveReadTrace trace = {};
	snprintf(trace.eventId, sizeof(trace.eventId), "%s", EventsData.getEventId().c_str());
	trace.block = waveFrameSequence;
	trace.part = wavePart;
	trace.irqSequence = waveIrqSequence;
	trace.irqUs = wavePageIrqUs;
	trace.readUs = readUs;
	trace.intervalUs = waveReadIntervalUs;
	trace.half = waveReadHalf;
	trace.partHash = 2166136261u;
	for (size_t i = 0; i < 1344; i++) trace.partHash = (trace.partHash ^ partData[i]) * 16777619u;
	trace.beforePage = before;
	trace.afterPage = after;
	trace.result = "observed";
	portENTER_CRITICAL(&waveTraceMutex);
	if (waveTraceCount && strcmp(waveTrace[0].eventId, trace.eventId) != 0) waveTraceCount = 0;
	if (waveTraceCount < waveTraceCapacity) waveTrace[waveTraceCount++] = trace;
	portEXIT_CRITICAL(&waveTraceMutex);
}

// Copia el inicio de la ultima captura sin ocupar el stack de AsyncTCP; dest recibe el JSON.
void meterGetWaveDiagnostics(String& dest)
{
	std::unique_ptr<WaveReadTrace[]> snapshot(new (std::nothrow) WaveReadTrace[waveTraceCapacity]);
	if (!snapshot) {
		dest = "{\"schemaVersion\":1,\"error\":\"insufficient_heap\"}";
		return;
	}
	portENTER_CRITICAL(&waveTraceMutex);
	uint32_t count = waveTraceCount;
	memcpy(snapshot.get(), waveTrace, count * sizeof(WaveReadTrace));
	portEXIT_CRITICAL(&waveTraceMutex);
	dest = "{\"schemaVersion\":1,\"lastStoreUs\":" + String(waveStoreLastUs.load()) +
		",\"maxStoreUs\":" + String(waveStoreMaxUs.load()) + ",\"scope\":{\"clients\":" +
		String(webServerGetMeterWS()->count()) + ",\"pageFull\":" + String(wavePageFullCount.load()) +
		",\"reads\":" + String(waveReadCount.load()) + ",\"frames\":" + String(waveFrameCount.load()) +
		",\"queued\":" + String(waveWsQueuedCount.load()) + ",\"blocked\":" + String(waveWsBlockedCount.load()) +
		",\"sendFailed\":" + String(waveWsFailedCount.load()) + ",\"status0\":\"0x" + String(waveDiagStatus0.load(), HEX) +
		"\",\"wfbConfig\":\"0x" + String(waveDiagWfbConfig.load(), HEX) +
		"\",\"wfbStatus\":\"0x" + String(waveDiagWfbStatus.load(), HEX) + "\"},\"fragments\":[";
	for (uint32_t i = 0; i < count; i++) {
		const auto& trace = snapshot[i];
		char item[384];
		snprintf(item, sizeof(item), "%s{\"eventId\":\"%s\",\"block\":%u,\"part\":%u,\"half\":%u,\"beforePage\":%u,\"afterPage\":%u,\"irqSequence\":%u,\"irqUs\":%u,\"readUs\":%u,\"intervalUs\":%u,\"partHashFnv1a\":%u,\"result\":\"%s\"}",
			i ? "," : "", trace.eventId, trace.block, trace.part, trace.half,
			trace.beforePage, trace.afterPage, trace.irqSequence, trace.irqUs, trace.readUs, trace.intervalUs, trace.partHash, trace.result);
		dest += item;
	}
	dest += "]}";
}

// Estima el inicio en el frame actual; lookbackUs es la demora del detector.
// Sin IRQ precisa conservamos la frontera de 128 muestras, no descartamos el indice.
static int64_t meterWaveTriggerOffset(uint64_t lookbackUs = 0)
{
	uint32_t elapsedUs = micros() - waveReferenceUs;
	return estimateWaveTriggerOffset(wavePart, elapsedUs, waveReferenceValid, lookbackUs);
}

// Busca el primer cambio en el mismo orden que meterPushEvent; oi habilita las fases OC cambiadas.
// Descuenta la confirmacion nominal y, para un cierre aislado, la duracion ya conocida.
static uint64_t meterEventLookbackUs(const ADE_OISTATUS_t& oi)
{
	phaseEvent_t* origins[] = {
		&meterVals.phaseR.voltageDip, &meterVals.phaseS.voltageDip, &meterVals.phaseT.voltageDip,
		&meterVals.phaseR.voltageSwell, &meterVals.phaseS.voltageSwell, &meterVals.phaseT.voltageSwell,
		oi.OIPHASEA ? &meterVals.phaseR.overCurrent : nullptr,
		oi.OIPHASEB ? &meterVals.phaseS.overCurrent : nullptr,
		oi.OIPHASEC ? &meterVals.phaseT.overCurrent : nullptr,
		oi.OIPHASEN ? &meterVals.neutral.overCurrent : nullptr
	};
	for (auto* origin : origins) {
		if (!origin || !origin->hasChanged()) continue;
		uint64_t lookbackUs = origin->active ? 0 : origin->getDurationMs() * 1000ULL;
		if (origin->type != eventType::overCurrent)
			lookbackUs += uint64_t(EventsData.getConfirmationHalfCycles(origin->type == eventType::voltageSwell)) * 10000ULL;
		return lookbackUs;
	}
	return 0;
}

// Devuelve el tipo de evento usado por Power Meter Manager.
// event: tipo interno del medidor.
static const char* meterManagerEventType(eventType event)
{
	if (event == eventType::voltageDip) return "voltage_dip";
	if (event == eventType::voltageSwell) return "voltage_swell";
	return "over_current";
}

// Devuelve el nombre corto de fase usado por Power Meter Manager.
// phase: fase interna del medidor.
static const char* meterManagerPhaseName(phaseName phase)
{
	if (phase == phaseName::phaseR) return "R";
	if (phase == phaseName::phaseS) return "S";
	if (phase == phaseName::phaseT) return "T";
	return "N";
}

// Devuelve la unidad principal del evento.
// event: tipo interno del medidor.
static const char* meterManagerEventUnit(eventType event)
{
	if (event == eventType::overCurrent) return "A";
	return "V";
}

// Registra una transicion del evento que se esta capturando.
// event: evento de fase detectado.
static void meterPushEvent(phaseEvent_t& event)
{
	if (!event.hasChanged()) return;
	uint64_t durationMs = event.getDurationMs();
	String eventId = EventsData.getEventId();
	if (!eventId.length()) eventId = eventStoreNextEventId();
	EventsData.pushEvent(event.getJson().c_str(), durationMs);
	uint64_t eventTimestamp = managerClockSynchronized() ? event.getTimestampMs() : 0;
	eventStoreAddMeta(eventId, meterManagerEventType(event.type), meterManagerPhaseName(event.name), event.value, meterManagerEventUnit(event.type), eventTimestamp, durationMs, !event.active);
	if (!event.active) eventStoreEndWave(eventId, meterManagerEventType(event.type), meterManagerPhaseName(event.name));
}

// Registra una transicion virtual usando el mismo flujo que un evento real.
// eventId: id comun. active: true para inicio. timestampMs: Unix ms o 0. durationMs: duracion real.
static void meterPushVirtualEvent(const String& eventId, bool active, uint64_t timestampMs, uint64_t durationMs)
{
	const char* action = active ? "start" : "end";
	String eventJson = "{\"phase\":\"Fase R\",\"type\":\"Dip\",\"action\":\"";
	eventJson += action;
	eventJson += "\",\"time\":";
	if (timestampMs) managerAppendUint64(eventJson, timestampMs);
	else eventJson += "null";
	eventJson += ",\"durationMs\":";
	managerAppendUint64(eventJson, durationMs);
	eventJson += ",\"val\":";
	eventJson += String(active ? meterVals.phaseR.FastVrms : meterVals.phaseR.FastVrms * 0.75, 2);
	eventJson += "}";
	EventsData.pushEvent(eventJson.c_str(), durationMs);
	eventStoreAddMeta(eventId, "manual_capture", "R", meterVals.phaseR.FastVrms * 0.75, "V", timestampMs, durationMs, !active);
	if (!active) eventStoreEndWave(eventId, "manual_capture", "R");
}

uint64_t MeterGetLastSampleTimestampMs()
{
	return meterLastSampleTimestampMs;
}

bool MeterTriggerTestEvent()
{
	meterTestEventRequested = true;
	if (meterHandle != NULL) xTaskNotifyGive(meterHandle);
	return true;
}


void MeterInit()
{
	Serial.println("Iniciando ADE9000!");
	pinMode(pinAdeInt0, INPUT_PULLUP);
	pinMode(pinAdeInt1, INPUT_PULLUP);

	ade.initADE9000(pinAdeClk, pinAdeSdi, pinAdeSdo);
	ade.ADC_Redirect(adeChannel_IA, adeChannel_IC);	//Cruzar los canales A con C
	ade.ADC_Redirect(adeChannel_IC, adeChannel_IA);
	ade.ADC_Redirect(adeChannel_VA, adeChannel_VC);
	ade.ADC_Redirect(adeChannel_VC, adeChannel_VA);
	ade.setupADE9000();              // Initialize ADE9000 registers according to values in ADE9000API.h
	//5 vueltas de cable por cada trafo
	ade.setNoPowerCutoff(0.5);		//0.5mA es 0A
	// ade.setNoCurrentCutoff(0.01);


	scopeInfo.currentScale = 0.1;
	scopeInfo.voltageScale = 0.25;
	scopeInfo.sampleFreq = 8000;
	webServerSetMeterEvents(scopeWSevents);		//Callback para eventos del websocket del scope
	webServerSetAcEvents(acEventsWSevents);		//Callback para eventos del websocket de los eventos
	eventStoreInit();
	gridEventsInit();


	MeterLoadOptions();				//Cargar configuración de los eventos del medidor
	MeterReadEnergyRegisters();		//Leer los registros de energía
	meterLoadGridSettings();

	memset(halfCycleSamples, 0, sizeof(halfCycleSamples));
	bl0942.begin(pinBLRx, pinBLTx);
	xTaskCreatePinnedToCore(MeterTask, "meterTask", 8192, NULL, 10, &meterHandle, APP_CPU_NUM);

	/*
		Asignar los pines de interrupciones a una función que se encargue de desbloquear la tarea que lee el ADE9000
	*/

	//Interrupción 0 se va a encargar de desbloquear por eventos de medición
	ade.setupInterruption0(ADE_MASK0_BITS_EGYRDY | ADE_MASK0_BITS_PAGE_FULL | ADE_MASK0_BITS_RMSONERDY
		| ADE_MASK0_BITS_RMS1012RDY | ADE_MASK0_BITS_THD_PF_RDY);

	attachInterrupt(pinAdeInt0, []() {
		meterIrq0Us = micros();
		meterIrq0Sequence++;
		BaseType_t switchToMeterTask = pdFALSE; //
		vTaskNotifyGiveFromISR(meterHandle, &switchToMeterTask);
		portYIELD_FROM_ISR(switchToMeterTask);
		}, FALLING);
	ade.clearStatusBit0();




	//Interrupcion 1 va a desbloquear por eventos de picos de voltaje, sobrecorriente y pérdida de cruces por cero de los voltajes
	ade.setupInterruption1(ADE_MASK1_BITS_DIPA | ADE_MASK1_BITS_DIPB | ADE_MASK1_BITS_DIPC |
		ADE_MASK1_BITS_SWELLA | ADE_MASK1_BITS_SWELLB | ADE_MASK1_BITS_SWELLC |
		ADE_MASK1_BITS_ZXTOVA | ADE_MASK1_BITS_ZXTOVB | ADE_MASK1_BITS_ZXTOVC);

	attachInterrupt(pinAdeInt1, []() {
		BaseType_t switchToMeterTask = pdFALSE; //
		vTaskNotifyGiveFromISR(meterHandle, &switchToMeterTask);
		portYIELD_FROM_ISR(switchToMeterTask);
		}, FALLING);

	ade.clearStatusBit1();	//Limpiar flags de interrupciones




	Serial.println("Meter inicializado!");

}

static constexpr uint16_t capturedEventMaxSamples = 300;
capturedEvent EventsData(capturedEventMaxSamples);

struct meterPendingFastSample {
	float voltages[3], currents[4];
	uint32_t sampleMs = 0, lastEventMs = 0;
	uint16_t timestamp = 0;
	bool active = false;
};
static meterPendingFastSample pendingFastSample;

// Actualiza estados con el instante original; voltages/currents son las siete lecturas del ADE.
static void meterApplyFastRms(const float voltages[3], const float currents[4], uint32_t sampleMs);
// Completa la muestra pendiente; force descarta el BL si llego otro turno antes de terminar.
static void meterFinishFastRmsSample(bool force);

// Atiende la UART sin esperar y publica las lecturas de neutro; sin parametros, solo MeterTask.
static void meterPollNeutralSamples()
{
	const uint32_t startedUs = micros();
	const bool updated = bl0942.loop();
	meterVals.neutral.Vrms = bl0942.getVoltage();
	meterVals.neutral.FastVrms = bl0942.getFastVoltage();
	meterVals.neutralFastValid = bl0942.hasFastSample(30000U);
	if (updated) meterUpdated++;
	meterTimingRecord(meterTimingPart::bl, micros() - startedUs);
	meterFinishFastRmsSample(false);
}

// Inicia la consulta BL del turno ADE; lastEventMs conserva la ultima transicion del evento.
static void meterRecordFastRmsSample(uint32_t lastEventMs)
{
	pendingFastSample.lastEventMs = lastEventMs;
	pendingFastSample.active = true;
	bl0942.startFastSample();
	meterFinishFastRmsSample(false);
}

// Publica un conjunto ADE/BL sin cambiar su fecha; force cierra un turno anterior incompleto.
static void meterFinishFastRmsSample(bool force)
{
	if (!pendingFastSample.active || (!force && bl0942.isFastSamplePending())) return;
	meterTimingScope timing(meterTimingPart::commit);
	pendingFastSample.active = false;
	meterVals.neutral.FastVrms = force ? NAN : bl0942.getCycleFastVoltage();
	meterVals.neutralFastValid = isfinite(meterVals.neutral.FastVrms);
	meterApplyFastRms(pendingFastSample.voltages, pendingFastSample.currents, pendingFastSample.sampleMs);
	meterFastDataSamplePush(pendingFastSample.sampleMs, pendingFastSample.timestamp);
	if (EventsData.isCapturing()) {
		if (EventsData.isSampleBufferFull())
			EventsData.stopCapture();
		else if (pendingFastSample.sampleMs - pendingFastSample.lastEventMs >= 1000)
			EventsData.stopCapture();
		else
			EventsData.pushSamples(&halfCycleSamples[halfCycleSamplesIndex]);
	}
	meterUpdated++;
}

void MeterTask(void* arg)
{
	uint32_t waitingTime = 0, runningTime = 0, startTime = 0, stopTime = 0, realTime = 0;
	uint32_t EventsCount;

	uint32_t firstEventMs = 0, lastEventMs = 0;
	uint32_t lastEvent, newEvent;
	bool recording = false;
	bool testEventActive = false;
	uint32_t testEventStartMs = 0;
	String testEventId = "";
	ADE_OISTATUS_t lastOI = { .raw = 0 }, oi, overCurrentEvent;


	while (millis() < 250)
		vTaskDelay(25 / portTICK_RATE_MS);	//Esperar que todo esté listo

	uint32_t lastRmsReadyUs = micros();
	uint32_t nextForcedFastRmsUs = 0;
	bool forcedFastRms = false;

	while (1) {
		stopTime = micros();
		runningTime += stopTime - startTime;

		TickType_t waitTicks = pdMS_TO_TICKS(10);
		if (!waitTicks) waitTicks = 1;
		TickType_t uartWaitTicks = pdMS_TO_TICKS(bl0942.timeUntilPollUs() / 1000U);
		if (!uartWaitTicks) uartWaitTicks = 1;
		if (uartWaitTicks < waitTicks) waitTicks = uartWaitTicks;
		if (forcedFastRms) {
			const int32_t remainingUs = int32_t(nextForcedFastRmsUs - micros());
			TickType_t forcedWait = remainingUs > 0 ? pdMS_TO_TICKS(uint32_t(remainingUs) / 1000U) : 1;
			if (!forcedWait) forcedWait = 1;
			if (forcedWait < waitTicks) waitTicks = forcedWait;
		}
		const uint32_t waitStartedUs = micros();
		EventsCount = ulTaskNotifyTake(pdTRUE, waitTicks);
		startTime = micros();
		const uint32_t actualWaitUs = startTime - waitStartedUs;
		meterTimingRecord(meterTimingPart::wait, actualWaitUs);
		const uint32_t requestedWaitUs = uint32_t(waitTicks) * portTICK_PERIOD_MS * 1000U;
		if (actualWaitUs > requestedWaitUs)
			meterTimingRecord(meterTimingPart::wakeLate, actualWaitUs - requestedWaitUs);
		meterTimingScope cycleTiming(meterTimingPart::cycle);
		waitingTime += startTime - stopTime;

		if (micros() - realTime > 999999) {
			realTime = micros() - realTime;
			// Serial.printf("CPU -> realTime=%dus, runningTime=%dus, waitingTime=%dus  ==>> CPU=%.2f%%\n", realTime, runningTime, waitingTime,
			// 	(float)runningTime / (float)realTime * 100.0);
			realTime = micros();
			waitingTime = runningTime = 0;
		}

		// Sin salida Serial/RemoteDebug en adquisicion: puede demorar la siguiente lectura Wave.
		// if (EventsCount == 0)
		// 	debugI("Algo ha ocurrido y no se recibieron interrupciones dentro los 50ms de espera!");

		/*
			Usar un bloque while para leer los flags de las interrupciones porque se da el caso de que se produce una nueva interrupcion
			entre que se leyó el estado actual y se limpió los bits, entonces el pin de interrupción sigue activo y no se genera la interrupcion
			correspondiente. De esta manera se asegura que se limpien todos los bits y se sigen generando interrupciones.
		*/
		{
		meterTimingScope irqTiming(meterTimingPart::irq0);
		while (digitalRead(pinAdeInt0) == 0) {		//Leer que causó la interrupcion
			uint32_t interruptUs = meterIrq0Us;
			uint32_t interruptSequence = meterIrq0Sequence;
			static uint32_t lastTimedIrq = 0;
			if (interruptUs && interruptSequence != lastTimedIrq) {
				meterTimingRecord(meterTimingPart::irqDelay, micros() - interruptUs);
				lastTimedIrq = interruptSequence;
			}
			ADE_STATUS0_t status0 = ade.readStatus0();

			if (ade.isCalibrating(calPhaseGain)) status0.EGYRDY = 0;	//Si está calibrando no se puede leer la energía.

			ade.clearStatusBit0(status0.raw);
			// if (EventsCount == 0) Serial.printf("Status0=0x%x\n", status0.raw);
			// Serial.printf("Status 0= 0x%X. time: %uus\n", status0, t);

			// Leer la mitad Wave pendiente antes de RMS, potencia, THD y energia.
			if (status0.PAGE_FULL) {
				wavePageFullCount++;
				wavePageIrqUs = interruptUs;
				waveIrqSequence = interruptSequence;
				meterReadWaveBuffer();
				meterUpdated++;
			}

			if (status0.RMSONERDY) {
				meterTimingScope timing(meterTimingPart::fast);
				lastRmsReadyUs = micros();
				forcedFastRms = false;
				nextForcedFastRmsUs = 0;
				meterReadHalfRMS();
				meterRecordFastRmsSample(lastEventMs);
			}
			if (status0.RMS1012RDY) {
				meterTimingScope timing(meterTimingPart::slowRms);
				meterRead1012RMS();
				meterReadRMS();
				meterReadPower();
				meterUpdated++;
			}
			if (status0.THD_PF_RDY) {
				meterTimingScope timing(meterTimingPart::thd);
				meterReadTHD();
				meterReadAngles();
				meterReadPeriods();
				meterUpdated++;
			}
			if (status0.EGYRDY) {
				meterTimingScope timing(meterTimingPart::energy);
				meterReadEnergy();
				meterUpdated++;
			}
		}

		}
		{
			meterTimingScope timing(meterTimingPart::calibration);
			bl0942.pollCalibration(meterVals.phaseR.Vrms, meterVals.phaseS.Vrms, meterVals.phaseT.Vrms);
		}
		meterPollNeutralSamples();

		// Reservas, finalizacion y comandos manuales solo despues de atender IRQ0 pendiente.
		{
			meterTimingScope timing(meterTimingPart::events);
			eventStoreServiceCapture();
		}
		{
		meterTimingScope timing(meterTimingPart::commands);
		if (meterTestEventRequested && !EventsData.isCapturing() && !ade.isCalibrating()) {
			meterTestEventRequested = false;
			lastEventMs = firstEventMs = millis();
			testEventStartMs = millis();
			testEventId = eventStoreNextEventId();
			uint64_t eventTimestamp = managerNowMs();
			int64_t triggerOffset = meterWaveTriggerOffset();
			if (EventsData.startCapture(eventsDataPrevSamples, testEventId)) {
				eventStoreBeginEvent(testEventId, eventTimestamp);
				if (webServerGetMeterWS()->count() == 0) eventStoreStartWave(testEventId, eventTimestamp, triggerOffset);
				else eventStoreSetWaveUnavailable(testEventId, "scope_active");
				for (int32_t i = (eventsDataPrevSamples - 1); i >= 0; i--) {
					EventsData.pushSamples(meterGetFastDataSample(i));
				}
				meterPushVirtualEvent(testEventId, true, eventTimestamp, 0);
				EventsData.markTriggerSample();
				testEventActive = true;
				meterUpdated++;
			}
		}

		if (testEventActive && millis() - testEventStartMs >= 300) {
			lastEventMs = millis();
			uint64_t eventTimestamp = managerNowMs();
			meterPushVirtualEvent(testEventId, false, eventTimestamp, 300);
			testEventActive = false;
			meterUpdated++;
		}

		}
		{
		meterTimingScope timing(meterTimingPart::irq1);
		oi .raw = ade.SPI_Read_16(ADDR_OISTATUS);
		overCurrentEvent.raw = 0;

		if (oi.OIPHASE != lastOI.OIPHASE) {
			// debugI("overCurrent -> R:%u, S:%u, T:%u, N:%u", oi.OIPHASEA, oi.OIPHASEB, oi.OIPHASEC, oi.OIPHASEN);
			CurrentRMSRegs current;
			ade.readOverCurrentLevels(&current);
			if (oi.OIPHASEA != lastOI.OIPHASEA) {
				meterVals.phaseR.overCurrent.setStatus(oi.OIPHASEA, current.CurrentRMS_A);
				meterVals.phaseR.overCurrent.printEvent();
				overCurrentEvent.OIPHASEA = true;
			}
			if (oi.OIPHASEB != lastOI.OIPHASEB) {
				meterVals.phaseS.overCurrent.setStatus(oi.OIPHASEB, current.CurrentRMS_B);
				meterVals.phaseS.overCurrent.printEvent();
				overCurrentEvent.OIPHASEB = true;
			}
			if (oi.OIPHASEC != lastOI.OIPHASEC) {
				meterVals.phaseT.overCurrent.setStatus(oi.OIPHASEC, current.CurrentRMS_C);
				meterVals.phaseT.overCurrent.printEvent();
				overCurrentEvent.OIPHASEC = true;
			}
			if (oi.OIPHASEN != lastOI.OIPHASEN) {
				meterVals.neutral.overCurrent.setStatus(oi.OIPHASEN, current.CurrentRMS_N);
				meterVals.neutral.overCurrent.printEvent();
				overCurrentEvent.OIPHASEN = true;
			}
			lastOI = oi;
		}


		while ((digitalRead(pinAdeInt1) == 0 || overCurrentEvent.OIPHASE)) {		//Leer que causó la interrupcion
			ADE_STATUS1_t status1 = ade.readStatus1();
			if (status1.raw) ade.clearStatusBit1(status1.raw);	//Limpiar los estados que causaron la interrupción
			meterUpdateZeroCrossTimeoutMask(status1);
			// debugI("status1:0x%x. OI:%u", status1.raw, status1.OI);

			/*
				Por algún motivo cuando se genera un evento de sobrecorriente y se limpia ese bit de estado, si el evento sigue se vuelve
				a disparar. Esto hace que no se salga casi nunca del loop, ya que al limpiarse, en el próximo ciclo se vuevle a generar y
				vuelve a disparar la interrupción.
				Cuando se dispare un evento de oc se va a deshabilitar la interrupción hasta que la condición termine.
			*/


			const uint32_t dipSwellMask = ADE_STATUS1_BITS_DIPA | ADE_STATUS1_BITS_DIPB | ADE_STATUS1_BITS_DIPC |
				ADE_STATUS1_BITS_SWELLA | ADE_STATUS1_BITS_SWELLB | ADE_STATUS1_BITS_SWELLC;
			if (!(status1.raw & dipSwellMask) && !overCurrentEvent.OIPHASE) {
				meterUpdated++;
				break;
			}
			ADE_EVENT_STATUS_t ds = meterReadDipSwell();
			// debugI("DipR:%c, DipS:%c, DipT:%c, SwellR:%c, SwellR:%c, SwellT:%c",
			// 	ds.DIPA ? '1' : ' ', ds.DIPB ? '1' : ' ', ds.DIPC ? '1' : ' ', ds.SWELLA ? '1' : ' ', ds.SWELLB ? '1' : ' ', ds.SWELLC ? '1' : ' ');

			bool saveEvent = false;

			if (!ade.isCalibrating()) {
				if (!EventsData.isCapturing()) {
					lastEventMs = firstEventMs = millis();
					String eventId = eventStoreNextEventId();
					uint64_t eventTimestamp = managerNowMs();
					uint64_t lookbackUs = meterEventLookbackUs(overCurrentEvent);
					int64_t triggerOffset = meterWaveTriggerOffset(lookbackUs);
					if (EventsData.startCapture(eventsDataPrevSamples, eventId)) {
						eventStoreBeginEvent(eventId, eventTimestamp);
						if (webServerGetMeterWS()->count() == 0) eventStoreStartWave(eventId, eventTimestamp, triggerOffset);
						else eventStoreSetWaveUnavailable(eventId, "scope_active");
						//Guardar datos actuales en la primer parte del buffer e iniciar la grabación
						for (int32_t i = (eventsDataPrevSamples - 1); i >= 0; i--) {
							EventsData.pushSamples(meterGetFastDataSample(i));
						}
						saveEvent = true;
						EventsData.markTriggerSample(lookbackUs);
					}
				}
				else {
					lastEventMs = millis();
					saveEvent = true;
				}
			}

			//Guardar el evento en el registro
			if (saveEvent) {
				meterPushEvent(meterVals.phaseR.voltageDip);
				meterPushEvent(meterVals.phaseS.voltageDip);
				meterPushEvent(meterVals.phaseT.voltageDip);

				meterPushEvent(meterVals.phaseR.voltageSwell);
				meterPushEvent(meterVals.phaseS.voltageSwell);
				meterPushEvent(meterVals.phaseT.voltageSwell);

				if (overCurrentEvent.OIPHASEA) meterPushEvent(meterVals.phaseR.overCurrent);
				if (overCurrentEvent.OIPHASEB) meterPushEvent(meterVals.phaseS.overCurrent);
				if (overCurrentEvent.OIPHASEC) meterPushEvent(meterVals.phaseT.overCurrent);
				if (overCurrentEvent.OIPHASEN) meterPushEvent(meterVals.neutral.overCurrent);
			}
			overCurrentEvent.OIPHASE = 0;	//Poner a 0 para salir del loop
			meterUpdated++;
		}
		}
		const uint32_t nowUs = micros();
		static uint32_t nextWaveDiagUs = 0;
		if (!nextWaveDiagUs || static_cast<int32_t>(nowUs - nextWaveDiagUs) >= 0) {
			waveDiagStatus0 = ade.readStatus0().raw;
			waveDiagWfbConfig = ade.SPI_Read_16(ADDR_WFB_CFG);
			waveDiagWfbStatus = ade.SPI_Read_16(ADDR_WFB_TRG_STAT);
			nextWaveDiagUs = nowUs + 1000000U;
		}
		if (!forcedFastRms && uint32_t(nowUs - lastRmsReadyUs) >= fastRmsReadyTimeoutUs) {
			forcedFastRms = true;
			nextForcedFastRmsUs = lastRmsReadyUs + fastRmsReadyTimeoutUs;
		}
		if (forcedFastRms && digitalRead(pinAdeInt0) != LOW &&
			static_cast<int32_t>(nowUs - nextForcedFastRmsUs) >= 0) {
			meterReadHalfRMS();
			meterRecordFastRmsSample(lastEventMs);
			do {
				nextForcedFastRmsUs += fastRmsPollIntervalUs;
			} while (static_cast<int32_t>(micros() - nextForcedFastRmsUs) >= 0);
		}
	}
}


bool MeterWriteEnergyRegisters(bool reset)
{
	if (reset){
		debugI("Reseteando registros de energía del medidor");}
	else{
		debugI("Actualizando registros de energía del medidor");}
	
	if (!meterOptions.begin("meter", false)) {
		debugE("No se pudo abrir la partición de opciones del medidor para actualizar los registros de energía");
		return false;
	}
	meterOptions.putFloat("rWattH", reset ? 0.0 : Meter.phaseR.Watt_H);
	meterOptions.putFloat("rVarH", reset ? 0.0 : Meter.phaseR.VAR_H);
	meterOptions.putFloat("rVaH", reset ? 0.0 : Meter.phaseR.VA_H);
	
	meterOptions.putFloat("sWattH", reset ? 0.0 : Meter.phaseS.Watt_H);
	meterOptions.putFloat("sVarH", reset ? 0.0 : Meter.phaseS.VAR_H);
	meterOptions.putFloat("sVaH", reset ? 0.0 : Meter.phaseS.VA_H);

	meterOptions.putFloat("tWattH", reset ? 0.0 : Meter.phaseT.Watt_H);
	meterOptions.putFloat("tVarH", reset ? 0.0 : Meter.phaseT.VAR_H);
	meterOptions.putFloat("tVaH", reset ? 0.0 : Meter.phaseT.VA_H);
	
	meterOptions.putFloat("WattH", reset ? 0.0 : Meter.energy.Watt_H);
	meterOptions.putFloat("VarH", reset ? 0.0 : Meter.energy.VAR_H);
	meterOptions.putFloat("VaH", reset ? 0.0 : Meter.energy.VA_H);

	meterOptions.end();
	return true;
}

void MeterReadEnergyRegisters()
{
	debugI("Leyendo registros de energía del medidor");
	
	if (!meterOptions.begin("meter", false)) {
		debugE("No se pudo abrir la partición de opciones del medidor para leer los registros de energía");
		return;
	}
	MeterEnergy.PhaseR.Watt_H = meterOptions.getFloat("rWattH", 0.0);
	MeterEnergy.PhaseR.VAR_H = meterOptions.getFloat("rVarH", 0.0);
	MeterEnergy.PhaseR.VA_H = meterOptions.getFloat("rVaH", 0.0);
	
	MeterEnergy.PhaseS.Watt_H = meterOptions.getFloat("sWattH", 0.0);
	MeterEnergy.PhaseS.VAR_H = meterOptions.getFloat("sVarH", 0.0);
	MeterEnergy.PhaseS.VA_H = meterOptions.getFloat("sVaH", 0.0);

	MeterEnergy.PhaseT.Watt_H = meterOptions.getFloat("tWattH", 0.0);
	MeterEnergy.PhaseT.VAR_H = meterOptions.getFloat("tVarH", 0.0);
	MeterEnergy.PhaseT.VA_H = meterOptions.getFloat("tVaH", 0.0);
	
	meterVals.phaseR.Watt_H = Meter.phaseR.Watt_H = MeterEnergy.PhaseR.Watt_H;
	meterVals.phaseR.VAR_H = Meter.phaseR.VAR_H = MeterEnergy.PhaseR.VAR_H;
	meterVals.phaseR.VA_H = Meter.phaseR.VA_H = MeterEnergy.PhaseR.VA_H;
	
	meterVals.phaseS.Watt_H = Meter.phaseS.Watt_H = MeterEnergy.PhaseS.Watt_H;
	meterVals.phaseS.VAR_H = Meter.phaseS.VAR_H = MeterEnergy.PhaseS.VAR_H;
	meterVals.phaseS.VA_H = Meter.phaseS.VA_H = MeterEnergy.PhaseS.VA_H;

	meterVals.phaseT.Watt_H = Meter.phaseT.Watt_H = MeterEnergy.PhaseT.Watt_H;
	meterVals.phaseT.VAR_H = Meter.phaseT.VAR_H = MeterEnergy.PhaseT.VAR_H;
	meterVals.phaseT.VA_H = Meter.phaseT.VA_H = MeterEnergy.PhaseT.VA_H;
	
	meterVals.energy.Watt_H = Meter.energy.Watt_H = meterOptions.getFloat("WattH", 0.0);
	meterVals.energy.VAR_H = Meter.energy.VAR_H = meterOptions.getFloat("VarH", 0.0);
	meterVals.energy.VA_H = Meter.energy.VA_H = meterOptions.getFloat("VaH", 0.0);

	meterOptions.end();
}

void MeterLoadOptions()
{
	debugI("Cargando configuracion de meter");
	bool dipEnabled, swellEnabled, ocEnabled;
	float dipVoltage, swellVoltage, ocCurrent;
	int dipCycles, swellCycles;

	//Cargar valores de deteccion de eventos. TODO: reemplazar por opciones en otro lado
	meterOptions.begin("meter", false);

	if (!meterOptions.isKey("ready")) {
		meterOptions.putBool("ready", true);
		meterOptions.putBool("dipEnabled", true);
		meterOptions.putFloat("dipVoltage", 200.0);
		meterOptions.putInt("dipCicles", 5);
		meterOptions.putBool("swellEnabled", true);
		meterOptions.putFloat("swellVoltage", 255.0);
		meterOptions.putInt("swellCicles", 5);
		meterOptions.putBool("overcurrent", true);
		meterOptions.putFloat("ocCurrent", 85.0);
		meterOptions.putUInt("eventPrevSamples", eventsDataPrevSamples);
		meterOptions.putUInt("eventPostSamples", eventsDataPostSamples);
		meterOptions.putUInt("eventMaxSamples", eventsDataMaxSamples);
	}
	if (!meterOptions.isKey("eventPrevSamples")) meterOptions.putUInt("eventPrevSamples", eventsDataPrevSamples);
	if (!meterOptions.isKey("eventPostSamples")) meterOptions.putUInt("eventPostSamples", eventsDataPostSamples);
	if (!meterOptions.isKey("eventMaxSamples")) meterOptions.putUInt("eventMaxSamples", eventsDataMaxSamples);

	dipEnabled = meterOptions.getBool("dipEnabled", true);
	dipVoltage = meterOptions.getFloat("dipVoltage", 0.0);
	dipCycles = meterOptions.getInt("dipCicles", 5);

	swellEnabled = meterOptions.getBool("swellEnabled", true);
	swellVoltage = meterOptions.getFloat("swellVoltage", ade.getMaxInputVoltage());
	swellCycles = meterOptions.getInt("swellCicles", 5);

	ocEnabled = meterOptions.getBool("overcurrent", true);
	ocCurrent = meterOptions.getFloat("ocCurrent", ade.getMaxInputCurrent());
	eventsDataPrevSamples = meterOptions.getUInt("eventPrevSamples", eventsDataPrevSamples);
	eventsDataPostSamples = meterOptions.getUInt("eventPostSamples", eventsDataPostSamples);
	eventsDataMaxSamples = meterOptions.getUInt("eventMaxSamples", eventsDataMaxSamples);

	meterOptions.end();

	if (!dipEnabled) dipVoltage = 0.0;
	if (!swellEnabled) swellVoltage = ade.getMaxInputVoltage();
	if (!ocEnabled) ocCurrent = ade.getMaxInputCurrent();

	EventsData.setDipEventParam(dipVoltage, dipCycles);
	EventsData.seSwellEventParam(swellVoltage, swellCycles);
	EventsData.setOverCurrentParam(ocCurrent);

	debugI("dipEnabled: %s, dipVoltage: %.2f, dipCycles: %d", dipEnabled ? "True" : "False", dipVoltage, dipCycles);
	debugI("swellEnabled: %s, swellVoltage: %.2f, swellCycles: %d", swellEnabled ? "True" : "False", swellVoltage, swellCycles);
	debugI("ocEnabled: %s, ocCurrent: %.2f", ocEnabled ? "True" : "False", ocCurrent);

	//Configurar los niveles de disparo de los eventos en las señales
	// taskENTER_CRITICAL(&meterMutex);
	// 	taskEXIT_CRITICAL(&meterMutex);
	ade.setDipDetectionLevels(dipVoltage, dipCycles);
	ade.setSwellDetectionLevels(swellVoltage, swellCycles);
	ade.enableOverCurrentDetection(ocCurrent);		//No poner un valor bajo porque se genera un evento cad avez que se produce el overcurrent y se traba el loop del meter task

}

// Guarda el conjunto ya completado; sampleMs y timestamp pertenecen a la lectura ADE original.
void meterFastDataSamplePush(uint32_t sampleMs, uint16_t timestamp)
{
	halfCycleSamplesIndex++;
	if (halfCycleSamplesIndex > (fastDataSamplesCount - 1))halfCycleSamplesIndex = 0;
	halfCycleSamples[halfCycleSamplesIndex].timeStamp = timestamp;
	halfCycleSampleMs[halfCycleSamplesIndex] = sampleMs;
	fastRMSData_t& sample = halfCycleSamples[halfCycleSamplesIndex];
	sample.setValue(0, meterVals.phaseR.FastVrms);
	sample.setValue(1, meterVals.phaseS.FastVrms);
	sample.setValue(2, meterVals.phaseT.FastVrms);
	sample.setValue(3, meterVals.phaseR.FastIrms);
	sample.setValue(4, meterVals.phaseS.FastIrms);
	sample.setValue(5, meterVals.phaseT.FastIrms);
	sample.setValue(6, meterVals.neutral.FastIrms);
	sample.setValue(7, meterVals.neutralFastValid ? meterVals.neutral.FastVrms : NAN);

	halfCycleSamplesCount++;
	if (halfCycleSamplesCount > (fastDataSamplesCount))halfCycleSamplesCount = fastDataSamplesCount;
	gridEventStorePushFastSample(halfCycleSamples[halfCycleSamplesIndex], sampleMs);
}

/*
	Devolver la dirección de la muestra indicada en sample
	Tener en cuenta que 0 es la muestra mas reciente y cuando más alto el numero más antigua es la muestra.
*/
fastRMSData_t* meterGetFastDataSample(uint32_t sample)
{
	if (sample >= halfCycleSamplesCount) return nullptr;
	int32_t pos = (int32_t)halfCycleSamplesIndex - (int32_t)sample;
	if (pos < 0) pos += fastDataSamplesCount;
	return &halfCycleSamples[pos];
}

// Copia las muestras Fast cuyo instante monotónico pertenece al intervalo solicitado.
// fromMs/toMs delimitan el rango y dest/maxSamples describen el buffer de salida.
uint16_t meterCopyFastSamples(uint32_t fromMs, uint32_t toMs, fastRmsRingSample* dest, uint16_t maxSamples)
{
	if (!dest || !maxSamples || !halfCycleSamplesCount) return 0;
	uint16_t copied = 0;
	const uint16_t oldest = uint16_t((halfCycleSamplesIndex + fastDataSamplesCount + 1 - halfCycleSamplesCount) % fastDataSamplesCount);
	for (uint16_t index = 0; index < halfCycleSamplesCount && copied < maxSamples; ++index) {
		const uint16_t position = uint16_t((oldest + index) % fastDataSamplesCount);
		const uint32_t sampleMs = halfCycleSampleMs[position];
		if (int32_t(sampleMs - fromMs) < 0 || int32_t(toMs - sampleMs) < 0) continue;
		dest[copied++] = fastRmsRingSample{halfCycleSamples[position], sampleMs};
	}
	return copied;
}

uint16_t meterFastDataSampleGetIndex()
{
	return halfCycleSamplesIndex;
}



void acEventsWSevents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
	if (type == WS_EVT_CONNECT) {
		Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
		client->ping();
		server->textAll("");

	}
	else if (type == WS_EVT_DISCONNECT) {
		Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
	}
	else if (type == WS_EVT_ERROR) {
		Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
	}
	else if (type == WS_EVT_PONG) {
		Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char*)data : "");
	}
	else if (type == WS_EVT_DATA) {
		AwsFrameInfo* info = (AwsFrameInfo*)arg;
		String msg = "";
		if (info->final && info->index == 0 && info->len == len) {
			Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

			if (info->opcode == WS_TEXT) {
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)data[i];
				}
			}
			Serial.printf("%s\n", msg.c_str());
			//Buscar informacion en el mensaje
			float value;

			if (sscanf((const char*)data, "scaleVoltage=%f", &value) == 1) {
				if (value > 0.0 && value < 100.0) scopeInfo.voltageScale = value;
				Serial.printf("Cambiando escala de voltaje a %.3f V/bit\n", value);
			}
			else if (sscanf((const char*)data, "scaleCurrent=%f", &value) == 1) {
				if (value > 0.0 && value < 100.0) scopeInfo.currentScale = value;
				Serial.printf("Cambiando escala de corriente a %.3f A/bit\n", value);
			}
		}
	}
}





void scopeWSevents(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len)
{
	if (type == WS_EVT_CONNECT) {
		Serial.printf("ws[%s][%u] connect\n", server->url(), client->id());
		client->ping();
		server->textAll("");
	}
	else if (type == WS_EVT_DISCONNECT) {
		Serial.printf("ws[%s][%u] disconnect\n", server->url(), client->id());
	}
	else if (type == WS_EVT_ERROR) {
		Serial.printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
	}
	else if (type == WS_EVT_PONG) {
		Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char*)data : "");
	}
	else if (type == WS_EVT_DATA) {
		AwsFrameInfo* info = (AwsFrameInfo*)arg;
		String msg = "";
		if (info->final && info->index == 0 && info->len == len) {
			Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

			if (info->opcode == WS_TEXT) {
				for (size_t i = 0; i < info->len; i++) {
					msg += (char)data[i];
				}
			}
			Serial.printf("%s\n", msg.c_str());
			//Buscar informacion en el mensaje
			float value;

			if (sscanf((const char*)data, "scaleVoltage=%f", &value) == 1) {
				if (value > 0.0 && value < 100.0) scopeInfo.voltageScale = value;
				Serial.printf("Cambiando escala de voltaje a %.3f V/bit\n", value);
			}
			else if (sscanf((const char*)data, "scaleCurrent=%f", &value) == 1) {
				if (value > 0.0 && value < 100.0) scopeInfo.currentScale = value;
				Serial.printf("Cambiando escala de corriente a %.3f A/bit\n", value);
			}
		}
	}
}



// Envia metadata y muestras completas; events es el JSON, data las muestras y size sus bytes.
static void meterSendCapturedEvent(const String& events, const uint8_t* data, size_t size)
{
	if (acEventsWS.count() == 0) return;
	bool queueReady = true;
	for (const auto& client : acEventsWS.getClients()) {
		if (client.status() == WS_CONNECTED && client.queueLen() > WS_MAX_QUEUED_MESSAGES - 2)
			queueReady = false;
	}
	if (!data || !size || size > capturedEventMaxSamples * sizeof(fastRMSData_t) ||
		!queueReady || !memoryHasRoom(size + events.length() + 1024, 32 * 1024)) {
		memoryReportState("event WS skipped", size);
		return;
	}
	try {
		// Preparar ambos buffers evita enviar metadata si falla la reserva de las muestras.
		auto metadata = std::make_shared<std::vector<uint8_t>>(events.length());
		auto samples = std::make_shared<std::vector<uint8_t>>(size);
		memcpy(metadata->data(), events.c_str(), events.length());
		memcpy(samples->data(), data, size);
		const auto textStatus = acEventsWS.textAll(metadata);
		const auto dataStatus = acEventsWS.binaryAll(samples);
		if (textStatus != AsyncWebSocket::ENQUEUED || dataStatus != AsyncWebSocket::ENQUEUED)
			memoryReportState("event WS incomplete delivery", size);
	} catch (const std::bad_alloc&) {
		memoryReportState("event WS allocation rejected", size);
	}
}

void MeterLoop()
{
	static uint32_t index = 0, energyUpdateTime = millis();
	bool dataReady = false;

	if (meterUpdated) {
		const uint64_t sampleTimestampMs = managerNowMs();
		taskENTER_CRITICAL(&meterMutex);
		meterUpdated = 0;
		Meter = meterVals;
		meterLastSampleTimestampMs = sampleTimestampMs;
		if (EventsData.isDataReady()) dataReady = true;
		taskEXIT_CRITICAL(&meterMutex);
	}
	bl0942.pollDiagnostics();

	//Una vez capturados los eventos, enviarlos por el web socket
	if (dataReady) {
		const size_t sampleCount = EventsData.getSampleCount();
		const auto* samples = reinterpret_cast<const uint8_t*>(EventsData.getBuffer());
		if (!samples || !sampleCount || sampleCount > capturedEventMaxSamples) {
			memoryReportState("invalid event sample count", sampleCount);
			EventsData.reset();
			return;
		}
		const size_t sampleBytes = sampleCount * sizeof(fastRMSData_t);
		String events;
		EventsData.getEvents(events);
		if (!eventStoreFinishFastRms(EventsData.getEventId(), samples, sampleBytes, EventsData.getTriggerSample())) return;
		if (mqtt.connected()) {
			mqtt.publish("PowerMeter9000/events", 0, false, events.c_str());
		}
		meterSendCapturedEvent(events, samples, sampleBytes);

		EventsData.reset();
		debugI("Captura de evento procesada");

		// EventsData.printSamples(index, index+10);
		// index+=10;
		// if (index >= EventsData.getSampleCount() ) {
		// 	EventsData.reset();
		// }
	}
	else index = 0;
	eventStoreLoop();
	gridEventsPersistLoop();
	gridEventNotificationLoop();

	if (millis() - energyUpdateTime >= 3600 * 8 * 1000) {	//Cada 8 horas actualizar los registros de energía
		if (MeterWriteEnergyRegisters(false)) {
			energyUpdateTime = millis();	//Solo actualizar el tiempo si se pudo guardar
			debugI("Registros de energía actualizados correctamente");
		}
	}
}


// Envia una onda solo con margen de heap; data/size describen el frame de 96 ms.
static void meterSendScopeFrame(const uint8_t* data, size_t size)
{
	static bool gapPending = false;
	try {
		auto* socket = webServerGetMeterWS();
		if (socket->count() == 0) {
			gapPending = false;
			return;
		}
		if (ESP.getFreeHeap() < 50 * 1024 || !socket->availableForWriteAll()) {
			waveWsBlockedCount++;
			gapPending = true;
			return;
		}
		// El aviso se encola al recuperarse, antes de las muestras nuevas, nunca con heap bajo.
		if (gapPending) {
			if (socket->textAll("scope_gap") != AsyncWebSocket::ENQUEUED) {
				waveWsFailedCount++;
				return;
			}
			gapPending = false;
			if (ESP.getFreeHeap() < 50 * 1024 || !socket->availableForWriteAll()) {
				waveWsBlockedCount++;
				gapPending = true;
				return;
			}
		}
		if (!memoryHasRoom(size + 1024, 32 * 1024)) {
			waveWsBlockedCount++;
			gapPending = true;
			return;
		}
		if (socket->binaryAll(data, size) == AsyncWebSocket::ENQUEUED) waveWsQueuedCount++;
		else {
			waveWsFailedCount++;
			gapPending = true;
		}
	} catch (const std::bad_alloc&) {
		waveWsFailedCount++;
		gapPending = true;
		memoryReportState("scope WS allocation rejected", size);
	}
}

void meterReadWaveBuffer()
{
	meterTimingScope timing(meterTimingPart::wave);
	static uint32_t lastTime = 0, timeRead, timeScale, timeCompress, timeSend;
	waveReadCount++;
	timeRead = micros();
	readWaveBuffer();
	timeRead = micros() - timeRead;
	meterTimingRecord(meterTimingPart::waveSpi, timeRead);

	timeScale = micros();
	scaleBuffer(readBuffer, WFB_SAMPLES_PER_CHANNEL / 2, scopeInfo.voltageScale, scopeInfo.currentScale);
	timeScale = micros() - timeScale;
	meterTimingRecord(meterTimingPart::waveScale, timeScale);

	timeCompress = micros();
	compressWaveBuffer12(readBuffer[0].buffer, outputSamples, wavePart);
	meterTraceWave(waveReadPage, waveAfterPage, timeRead, outputSamples + wavePart * 1344);
	wavePart++;
	timeCompress = micros() - timeCompress;
	meterTimingRecord(meterTimingPart::wavePack, timeCompress);
	
	if (wavePart == 6) {	//Buffer de 96ms comprimido listo para enviar
		wavePart = 0;
		waveFrameSequence++;
		waveFrameCount++;

		timeSend = micros();
		size_t size = sizeof(outputSamples);
		bool scopeActive = webServerGetMeterWS()->count() > 0;
		uint32_t storeStartedUs = micros();
		eventStoreProcessWaveFrame(outputSamples, size, scopeActive);
		uint32_t storeElapsedUs = micros() - storeStartedUs;
		meterTimingRecord(meterTimingPart::waveStore, storeElapsedUs);
		waveStoreLastUs = storeElapsedUs;
		if (storeElapsedUs > waveStoreMaxUs.load()) waveStoreMaxUs = storeElapsedUs;
		{
			meterTimingScope webTiming(meterTimingPart::waveWeb);
			meterSendScopeFrame(outputSamples, size);
		}

		timeSend = micros() - timeSend;

		// debugI("Tiempo de lectura: %uus, tiempo escalado: %uus, tiempo de compresion: %uus, tiempo de envio: %uus, free heap: %u bytes\n",
		// 	timeRead, timeScale, timeCompress, timeSend, ESP.getFreeHeap());
	}
}


void meterReadRMS()
{
	VoltageRMSRegs volts; CurrentRMSRegs curr;
	ade.readVoltageRMSRegs(&volts);
	ade.readCurrentRMSRegs(&curr);

	meterVals.phaseR.Vrms = volts.VoltageRMS_A;
	meterVals.phaseS.Vrms = volts.VoltageRMS_B;
	meterVals.phaseT.Vrms = volts.VoltageRMS_C;

	meterVals.phaseR.Irms = curr.CurrentRMS_A;
	meterVals.phaseS.Irms = curr.CurrentRMS_B;
	meterVals.phaseT.Irms = curr.CurrentRMS_C;
	meterVals.neutral.Irms = curr.CurrentRMS_N;
	//Calcular voltajes entre fase y fase
	meterVals.phaseR.VVrms = sumVoltages(meterVals.phaseR.Vrms, 0, meterVals.phaseS.Vrms, meterVals.phaseR.AngleV);
	meterVals.phaseS.VVrms = sumVoltages(meterVals.phaseS.Vrms, 0, meterVals.phaseT.Vrms, meterVals.phaseS.AngleV);
	meterVals.phaseT.VVrms = sumVoltages(meterVals.phaseT.Vrms, 0, meterVals.phaseR.Vrms, meterVals.phaseT.AngleV);

	meterVals.average.Vrms = (meterVals.phaseR.Vrms + meterVals.phaseS.Vrms + meterVals.phaseT.Vrms) / 3.0;
	meterVals.average.VVrms = (meterVals.phaseR.VVrms + meterVals.phaseS.VVrms + meterVals.phaseT.VVrms) / 3.0;
	meterVals.average.Irms = (meterVals.phaseR.Irms + meterVals.phaseS.Irms + meterVals.phaseT.Irms) / 3.0;
}

// Actualiza estados Grid; voltages/currents contienen R, S, T y N, sampleMs es el instante ADE.
static void meterApplyFastRms(const float voltages[3], const float currents[4], uint32_t sampleMs)
{
	meterTimingScope timing(meterTimingPart::grid);
	meterVals.phaseR.FastVrms = voltages[0];
	meterVals.phaseS.FastVrms = voltages[1];
	meterVals.phaseT.FastVrms = voltages[2];
	meterVals.phaseR.FastIrms = currents[0];
	meterVals.phaseS.FastIrms = currents[1];
	meterVals.phaseT.FastIrms = currents[2];
	meterVals.neutral.FastIrms = currents[3];
	taskENTER_CRITICAL(&gridConfigMutex);
	const bool settingsChanged = gridConfigPending;
	const bool resetPhases = settingsChanged &&
		(memcmp(&gridConfig, &requestedGridConfig, offsetof(gridSettings, neutral)) != 0);
	if (settingsChanged) {
		gridConfig = requestedGridConfig;
		gridConfigPending = false;
	}
	taskEXIT_CRITICAL(&gridConfigMutex);
	if (resetPhases) {
		meterVals.phaseR.gridState = phaseState{};
		meterVals.phaseS.gridState = phaseState{};
		meterVals.phaseT.gridState = phaseState{};
	}
	updatePhaseState(meterVals.phaseR.gridState, voltages[0], sampleMs, gridConfig.phases[0], gridConfig);
	updatePhaseState(meterVals.phaseS.gridState, voltages[1], sampleMs, gridConfig.phases[1], gridConfig);
	updatePhaseState(meterVals.phaseT.gridState, voltages[2], sampleMs, gridConfig.phases[2], gridConfig);
	const gridCondition previousGridState = meterVals.gridState;
	meterVals.gridState = deriveGridState(meterVals.phaseR.gridState.state,
		meterVals.phaseS.gridState.state, meterVals.phaseT.gridState.state);
	if (meterVals.gridState != previousGridState) meterVals.gridStateChangedAtMs = sampleMs;
	gridEventsUpdate(meterVals.phaseR.gridState, meterVals.phaseS.gridState,
		meterVals.phaseT.gridState, meterVals.gridState, voltages[0],
		voltages[1], voltages[2], sampleMs);
	if (gridEventsReadyForStorage()) {
		gridEventStoreObserve(meterVals.phaseR.gridState, meterVals.phaseS.gridState, meterVals.phaseT.gridState,
			meterVals.gridState, voltages[0], voltages[1], voltages[2],
			meterVals, gridConfig, sampleMs);
	}

	meterVals.average.FastVrms = (meterVals.phaseR.FastVrms + meterVals.phaseS.FastVrms + meterVals.phaseT.FastVrms) / 3.0;
	meterVals.average.FastIrms = (meterVals.phaseR.FastIrms + meterVals.phaseS.FastIrms + meterVals.phaseT.FastIrms) / 3.0;
}

// Lee RMS rápidos y conserva los ZXTOV para diagnóstico, sin reemplazar muestras por cero.
void meterReadHalfRMS()
{
	// Cierra el turno anterior antes de reutilizar su unica instantanea de RAM.
	meterPollNeutralSamples();
	meterFinishFastRmsSample(true);
	meterUpdateZeroCrossTimeoutMask(ade.readStatus1());
	VoltageRMSRegs volts; CurrentRMSRegs curr;
	ade.ReadHalfVoltageRMSRegs(&volts);
	ade.ReadHalfCurrentRMSRegs(&curr);
	float voltages[] = { volts.VoltageRMS_A, volts.VoltageRMS_B, volts.VoltageRMS_C };
	float currents[] = { curr.CurrentRMS_A, curr.CurrentRMS_B, curr.CurrentRMS_C, curr.CurrentRMS_N };
	for (uint8_t phase = 0; phase < 3; ++phase) {
		if (!(zeroCrossTimeoutMask & (1U << phase))) continue;
		if (voltages[phase] > gridConfig.noVoltage) {
			zeroCrossTimeoutMask &= ~(1U << phase);
			continue;
		}
		// Tras validar ZXTOV en la placa, se puede probar forzar a cero esta fase:
		// voltages[phase] = 0;
		// currents[phase] = 0;
	}
	// Si se confirma que el RMS neutro queda retenido cuando todas las fases pierden cruce:
	// if ((zeroCrossTimeoutMask & 0x07U) == 0x07U) currents[3] = 0;
	memcpy(pendingFastSample.voltages, voltages, sizeof(voltages));
	memcpy(pendingFastSample.currents, currents, sizeof(currents));
	pendingFastSample.sampleMs = millis();
	timeval stamp;
	gettimeofday(&stamp, nullptr);
	pendingFastSample.timestamp = uint16_t(stamp.tv_sec * 1000ULL + stamp.tv_usec / 1000);
}

// Registra las fases cuyo STATUS1 informa timeout de cruce por cero.
// status1 contiene la lectura actual del registro de estado.
static void meterUpdateZeroCrossTimeoutMask(const ADE_STATUS1_t& status1)
{
	zeroCrossTimeoutMask |= (status1.ZXTOVA ? 0x01U : 0U) |
		(status1.ZXTOVB ? 0x02U : 0U) | (status1.ZXTOVC ? 0x04U : 0U);
}

void meterRead1012RMS()
{

}

void meterReadPower()
{
	ActivePowerRegs watt; ReactivePowerRegs var; ApparentPowerRegs va;

	ade.readActivePowerRegs(&watt);
	ade.readReactivePowerRegs(&var);
	ade.readApparentPowerRegs(&va);

	meterVals.phaseR.Watt = watt.ActivePower_A;
	meterVals.phaseS.Watt = watt.ActivePower_B;
	meterVals.phaseT.Watt = watt.ActivePower_C;
	meterVals.neutral.Watt = 0;

	meterVals.phaseR.VAR = var.ReactivePower_A;
	meterVals.phaseS.VAR = var.ReactivePower_B;
	meterVals.phaseT.VAR = var.ReactivePower_C;
	meterVals.neutral.VAR = 0;

	meterVals.phaseR.VA = va.ApparentPower_A;
	meterVals.phaseS.VA = va.ApparentPower_B;
	meterVals.phaseT.VA = va.ApparentPower_C;
	meterVals.neutral.VA = 0;

	meterVals.power.Watt = meterVals.phaseR.Watt + meterVals.phaseS.Watt + meterVals.phaseT.Watt;
	meterVals.power.VAR = meterVals.phaseR.VAR + meterVals.phaseS.VAR + meterVals.phaseT.VAR;
	meterVals.power.VA = meterVals.phaseR.VA + meterVals.phaseS.VA + meterVals.phaseT.VA;

	// El APF crudo del ADE9000 se conserva en el driver para diagnóstico. Por ahora
	// la interfaz publica W/VA porque el APF anterior quedaba fijo cerca de 1.0;
	// comparar ambos valores antes de decidir si el APF vuelve a ser el valor principal.
	phaseValues* phases[] = { &meterVals.phaseR, &meterVals.phaseS, &meterVals.phaseT };
	for (phaseValues* phase : phases) {
		phase->PowerFactor = phase->VA >= 5.0f ? constrain(phase->Watt / phase->VA, -1.0f, 1.0f) : 0.0f;
	}
	meterVals.average.PowerFactor = meterVals.power.VA >= 5.0f ?
		constrain(meterVals.power.Watt / meterVals.power.VA, -1.0f, 1.0f) : 0.0f;

	meterVals.average.Watt = (meterVals.phaseR.Watt + meterVals.phaseS.Watt + meterVals.phaseT.Watt) / 3.0;
	meterVals.average.VAR = (meterVals.phaseR.VAR + meterVals.phaseS.VAR + meterVals.phaseT.VAR) / 3.0;
	meterVals.average.VA = (meterVals.phaseR.VA + meterVals.phaseS.VA + meterVals.phaseT.VA) / 3.0;
}

void meterReadTHD()
{
	CurrentTHDRegs curr; VoltageTHDRegs volt;

	ade.ReadVoltageTHDRegsnValues(&volt);
	ade.ReadCurrentTHDRegsnValues(&curr);

	meterVals.phaseR.Ithd = curr.CurrentTHDValue_A;
	meterVals.phaseS.Ithd = curr.CurrentTHDValue_B;
	meterVals.phaseT.Ithd = curr.CurrentTHDValue_C;
	meterVals.neutral.Ithd = 0;

	meterVals.phaseR.Vthd = volt.VoltageTHDValue_A;
	meterVals.phaseS.Vthd = volt.VoltageTHDValue_B;
	meterVals.phaseT.Vthd = volt.VoltageTHDValue_C;
	meterVals.neutral.Vthd = 0;

	meterVals.average.Ithd = (meterVals.phaseR.Ithd + meterVals.phaseS.Ithd + meterVals.phaseT.Ithd) / 3.0;
	meterVals.average.Vthd = (meterVals.phaseR.Vthd + meterVals.phaseS.Vthd + meterVals.phaseT.Vthd) / 3.0;
}

void meterReadPeriods()
{
	PeriodRegs period;
	ade.readPeriodRegsnValues(&period);

	meterVals.phaseR.Freq = period.FrequencyValue_A;
	meterVals.phaseS.Freq = period.FrequencyValue_B;
	meterVals.phaseT.Freq = period.FrequencyValue_C;
	meterVals.average.Freq = (meterVals.phaseR.Freq + meterVals.phaseS.Freq + meterVals.phaseT.Freq) / 3.0;
}

void meterReadAngles()
{
	AngleRegs ang;
	ade.readAngleRegsnValues(&ang);

	meterVals.phaseR.AngleVI = ang.AngleValue_VA_IA;
	meterVals.phaseS.AngleVI = ang.AngleValue_VB_IB;
	meterVals.phaseT.AngleVI = ang.AngleValue_VC_IC;

	meterVals.phaseR.AngleV = ang.AngleValue_VA_VB;
	meterVals.phaseS.AngleV = ang.AngleValue_VB_VC;
	// meterVals.phaseT.AngleV = 360 - ang.AngleValue_VA_VC;
	meterVals.phaseT.AngleV = ang.AngleValue_VA_VC;

	meterVals.phaseR.AngleI = ang.AngleValue_IA_IB;
	meterVals.phaseS.AngleI = ang.AngleValue_IB_IC;
	// meterVals.phaseT.AngleI = 360 - ang.AngleValue_IA_IC;
	meterVals.phaseT.AngleI = ang.AngleValue_IA_IC;

	if (meterVals.phaseR.Irms < 0.2) meterVals.phaseR.AngleVI = meterVals.phaseR.AngleI = 0.0;
	if (meterVals.phaseS.Irms < 0.2) meterVals.phaseS.AngleVI = meterVals.phaseS.AngleI = 0.0;
	if (meterVals.phaseT.Irms < 0.2) meterVals.phaseT.AngleVI = meterVals.phaseT.AngleI = 0.0;
}

void meterReadEnergy()
{
	ade.readAccEnergyRegister(&MeterEnergy);

	meterVals.phaseR.Watt_H = MeterEnergy.PhaseR.Watt_H;
	meterVals.phaseS.Watt_H = MeterEnergy.PhaseS.Watt_H;
	meterVals.phaseT.Watt_H = MeterEnergy.PhaseT.Watt_H;

	meterVals.phaseR.VAR_H = MeterEnergy.PhaseR.VAR_H;
	meterVals.phaseS.VAR_H = MeterEnergy.PhaseS.VAR_H;
	meterVals.phaseT.VAR_H = MeterEnergy.PhaseT.VAR_H;

	meterVals.phaseR.VA_H = MeterEnergy.PhaseR.VA_H;
	meterVals.phaseS.VA_H = MeterEnergy.PhaseS.VA_H;
	meterVals.phaseT.VA_H = MeterEnergy.PhaseT.VA_H;

	meterVals.energy.Watt_H = MeterEnergy.PhaseR.Watt_H + MeterEnergy.PhaseS.Watt_H + MeterEnergy.PhaseT.Watt_H;
	meterVals.energy.VAR_H = MeterEnergy.PhaseR.VAR_H + MeterEnergy.PhaseS.VAR_H + MeterEnergy.PhaseT.VAR_H;
	meterVals.energy.VA_H = MeterEnergy.PhaseR.VA_H + MeterEnergy.PhaseS.VA_H + MeterEnergy.PhaseT.VA_H;
}

ADE_EVENT_STATUS_t meterReadDipSwell()
{
	ADE_EVENT_STATUS_t event;
	struct VoltageRMSRegs volts;

	event = ade.readEventStatus();
	event.raw &= 0x3f;		//Enmascarar solo los eventos de voltajes dip y swell
	meterVals.voltageEvents = event;

	if (event.DIPA || event.DIPB || event.DIPC) ade.readDipLevels(&volts);	//Leer solo si hay un evento activo

	meterVals.phaseR.voltageDip.setStatus(event.DIPA, volts.VoltageRMS_A);
	meterVals.phaseS.voltageDip.setStatus(event.DIPB, volts.VoltageRMS_B);
	meterVals.phaseT.voltageDip.setStatus(event.DIPC, volts.VoltageRMS_C);

	if (meterVals.phaseR.voltageDip.hasChanged()) meterVals.phaseR.voltageDip.printEvent();
	if (meterVals.phaseS.voltageDip.hasChanged()) meterVals.phaseS.voltageDip.printEvent();
	if (meterVals.phaseT.voltageDip.hasChanged()) meterVals.phaseT.voltageDip.printEvent();

	if (event.SWELLA || event.SWELLB || event.SWELLC) ade.readSwellLevels(&volts);	//Leer solo si hay un evento activo
	meterVals.phaseR.voltageSwell.setStatus(event.SWELLA, volts.VoltageRMS_A);
	meterVals.phaseS.voltageSwell.setStatus(event.SWELLB, volts.VoltageRMS_B);
	meterVals.phaseT.voltageSwell.setStatus(event.SWELLC, volts.VoltageRMS_C);

	if (meterVals.phaseR.voltageSwell.hasChanged()) meterVals.phaseR.voltageSwell.printEvent();
	if (meterVals.phaseS.voltageSwell.hasChanged()) meterVals.phaseS.voltageSwell.printEvent();
	if (meterVals.phaseT.voltageSwell.hasChanged()) meterVals.phaseT.voltageSwell.printEvent();

	return event;
}


ADE_OISTATUS_t meterReadOverCurrent()
{
	ADE_OISTATUS_t status;
	struct CurrentRMSRegs current;

	status = ade.checkOverCurrentStatus();
	status.raw &= 0xf;		//Enmascarar solo los eventos de corriente
	meterVals.currentEvents = status;
	if (status.OIPHASE) ade.readOverCurrentLevels(&current);	//Leer solo si hay un evento activo

	meterVals.phaseR.overCurrent.setStatus(status.OIPHASEA, current.CurrentRMS_A);
	meterVals.phaseS.overCurrent.setStatus(status.OIPHASEB, current.CurrentRMS_B);
	meterVals.phaseT.overCurrent.setStatus(status.OIPHASEC, current.CurrentRMS_C);
	meterVals.neutral.overCurrent.setStatus(status.OIPHASEN, current.CurrentRMS_N);

	if (meterVals.phaseR.overCurrent.hasChanged()) meterVals.phaseR.overCurrent.printEvent();
	if (meterVals.phaseS.overCurrent.hasChanged()) meterVals.phaseS.overCurrent.printEvent();
	if (meterVals.phaseT.overCurrent.hasChanged()) meterVals.phaseT.overCurrent.printEvent();
	if (meterVals.neutral.overCurrent.hasChanged()) meterVals.neutral.overCurrent.printEvent();

	return status;
}


// Lee la mitad con el criterio original del osciloscopio; sin parametros.
void readWaveBuffer()
{
	static uint32_t previousReadUs = 0;
	uint32_t nowUs = micros();
	waveReadIntervalUs = previousReadUs ? nowUs - previousReadUs : 0;
	previousReadUs = nowUs;
	uint8_t lastPage = (ade.SPI_Read_16(ADDR_WFB_TRG_STAT) >> 12) & 15;
	waveReadPage = lastPage;
	waveReadHalf = lastPage >= 14 || lastPage < 3 ? 1 : 0;
	ade.SPI_Burst_Read_FixedDT_Buffer(waveReadHalf * 1024, WFB_SAMPLES_PER_CHANNEL / 2, readBuffer);
	waveAfterPage = (ade.SPI_Read_16(ADDR_WFB_TRG_STAT) >> 12) & 15;
	waveReferenceUs = wavePageIrqUs;
	waveReferenceValid = (lastPage == 7 || lastPage == 15) &&
		(uint32_t)(micros() - waveReferenceUs) < 2000;
}

/*
	Escalar el buffer de entrada para ajustar un bit al valor indicado en voltage o current
*/
void scaleBuffer(WFBFixedDataRate_t* samplesBuffer, int32_t samplesCount, float voltage, float current)
{
	if (samplesCount < 0 || samplesCount >= WFB_SAMPLES_PER_CHANNEL) return;
	if (voltage == 0.0 || current == 0.0) return;

	current = 1.0 / (CAL_I_PCF * current) * 65536;
	voltage = 1.0 / (CAL_V_PCF * voltage) * 65536;

	for (uint32_t i = 0; i < samplesCount; i++) {
		samplesBuffer[i].IA *= current;
		samplesBuffer[i].IB *= current;
		samplesBuffer[i].IC *= current;
		samplesBuffer[i].IN *= current;
		samplesBuffer[i].VA *= voltage;
		samplesBuffer[i].VB *= voltage;
		samplesBuffer[i].VC *= voltage;
	}
}

void compressWaveBuffer12(int32_t* waveBufferRaw, uint8_t* waveBuffer, uint16_t part)
{
	uint32_t time = micros();
	int32_t* raw = waveBufferRaw;
	uint8_t* buf = &waveBuffer[part * 1344];  //2688 es la cantidad de bytes resultantes de comprimir el buffer de 1024 words 
	// uint8_t* buf = &waveBuffer[part * 2688];  //2688 es la cantidad de bytes resultantes de comprimir el buffer de 2048 words 


	int32_t val1, val2, k = 0, bytes = 0;
	for (int32_t i = 0; i < 896; i++) {	//896 muestras en el buffer 128*7=896
		/*
			Como se comprime y se guardan 12 bits, se deben juntar de a 2 muestras y guardar 3 bytes
			Siempre se guarda después de leer la segunda muestra
		*/
		if (!(k++ & 1)) {
			val1 = (*raw++) >> 16;  //Comprimir a 12 bits
			if (val1 > 2047) val1 = 2047;
			else if (val1 < -2048) val1 = -2048;
		}
		else {  //Solo guardar cada 2 words leidas
			val2 = (*raw++) >> 16;  //Comprimir a 12 bits
			if (val2 > 2047) val2 = 2047;
			else if (val2 < -2048) val2 = -2048;

			uint8_t a, b, c;

			*buf++ = a = val1 & 0xFF;
			*buf++ = b = uint8_t((val1 & 0x0F00) >> 8) | uint8_t((val2 & 0x000F) << 4);
			*buf++ = c = (val2 & 0xFF0) >> 4;
			bytes += 3;
		}
	}

	time = micros() - time;
	// Serial.printf("Tiempo de compresion: %u us, k: %d, bytes: %d\n", time, k, bytes);
}

float sumVoltages(float v1, float deg1, float v2, float deg2)
{
	deg2 -= deg1;		//Diferencia en grados entre cada fase
	//c2 = a^2 + b^2 − 2 a b cos(θ)
	float sum = v1 * v1 + v2 * v2 - 2 * v1 * v2 * cosf(radians(deg2));
	sum = sqrtf(sum);
	if (isnanf(sum)) sum = 0.0;
	return sum;
}

