#include "grid_event_store.h"
#include "store_format_guard.h"
#include "../manager_api.h"
#include "../grid_events.h"
#include <algorithm>
#include <cstring>
#include <sys/time.h>
#include <esp_system.h>

static GridEventStore* mountedGridEventStore = nullptr;
static constexpr uintptr_t eraseCompletionContext = GridEventFormat::maxPagesPerEvent;
static volatile bool gridTestRequested = false;
constexpr uint32_t GridEventStore::captureArmMs;

GridEventStore::GridEventStore(NandPartition& gridPartition) : partition(gridPartition) {}

// Pausa lectores y escritor y espera que terminen antes de reconstruir el indice en RAM.
void GridEventStore::prepareFormat()
{
	formatPaused = true;
	while (activeCalls.load()) vTaskDelay(1);
}

// Reanuda observacion y consultas si la reconstruccion del Store termino correctamente.
void GridEventStore::finishFormat(bool success)
{
	if (success) formatPaused = false;
}

// Prepara colas, reconstruye el cursor y publica esta instancia para MeterTask.
bool GridEventStore::begin()
{
	if (!partition.isEnabled()) return false;
	if (capture.pages) free(capture.pages);
	if (!completionQueue)
		completionQueue = xQueueCreateStatic(GridEventFormat::maxPagesPerEvent + 1,
			sizeof(NandIoResult), completionQueueData, &completionQueueStorage);
	else xQueueReset(completionQueue);
	if (!recoveryQueue)
		recoveryQueue = xQueueCreateStatic(1, sizeof(NandIoResult), recoveryQueueData, &recoveryQueueStorage);
	else xQueueReset(recoveryQueue);
	if (!scanMutex) scanMutex = xSemaphoreCreateMutex();
	if (!completionQueue || !recoveryQueue || !scanMutex || !recoverCursor()) return false;
	for (auto& candidate : candidates) candidate = Candidate{};
	neutralState = phaseState{};
	neutralPreviousVoltage = NAN;
	capture = Capture{};
	statesReady = false;
	storeMetrics.captureActive = false;
	storeMetrics.rootPending = false;
	storeMetrics.nextEventId = nextEventId;
	gridTestRequested = false;
	mountedGridEventStore = this;
	return true;
}

// Recibe estados de fase y red, abre candidatos y confirma cambios ya estabilizados.
void GridEventStore::observe(const phaseState& r, const phaseState& s, const phaseState& t, gridCondition overall,
	float vr, float vs, float vt, const meterValues& meter, const gridSettings& settings, uint32_t sampleMs)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return;
	poll();
	if (gridTestRequested) {
		gridTestRequested = false;
		Candidate test;
		test.active = true;
		test.subject = GridEventFormat::Subject(esp_random() % 3U);
		test.previousState = uint8_t(phaseCondition::normal);
	const uint8_t states[] = { uint8_t(phaseCondition::undervoltage), uint8_t(phaseCondition::overvoltage), uint8_t(phaseCondition::phaseLoss) };
		test.currentState = states[esp_random() % 3U];
		test.startedMs = sampleMs - captureArmMs;
		test.previousVoltage = 230.0f;
		test.currentVoltage = test.currentState == uint8_t(phaseCondition::phaseLoss) ? 0.0f :
		test.currentState == uint8_t(phaseCondition::undervoltage) ? 170.0f : 260.0f;
		snapshotSettings(test.settings, settings);
		snapshotMeter(test.meter, meter);
		armCapture(test, sampleMs);
		confirmCandidate(test, sampleMs - 1000U, sampleMs);
	}
	const phaseState* phases[] = { &r, &s, &t };
	const float voltages[] = { vr, vs, vt };
	if (!statesReady) {
		for (uint8_t index = 0; index < 3; ++index) {
			observedStates[index] = uint8_t(phases[index]->state);
			observedStartedMs[index] = phases[index]->changedAtMs;
			observedVoltages[index] = voltages[index];
		}
		observedStates[3] = uint8_t(overall);
		observedStartedMs[3] = sampleMs;
		statesReady = true;
		startStartupCapture(phases, overall, voltages, meter, settings, sampleMs);
		return;
	}
	for (uint8_t index = 0; index < 3; ++index)
		observePhase(index, *phases[index], voltages[index], meter, settings, sampleMs);
	observeGrid(phases, overall, meter, settings, sampleMs);
	observeNeutral(meter, settings, sampleMs);
}

// Abre una captura inicial cuando el arranque electrico o un cambio frente a NVS lo justifica.
// phases/overall son estados nuevos; voltages/meter/settings contextualizan el evento.
void GridEventStore::startStartupCapture(const phaseState* phases[3], gridCondition overall, const float voltages[3],
	const meterValues& meter, const gridSettings& settings, uint32_t sampleMs)
{
	const esp_reset_reason_t reason = esp_reset_reason();
	const bool electrical = reason == ESP_RST_POWERON || reason == ESP_RST_BROWNOUT;
	const uint8_t previousGrid = gridEventsStartupState(3);
	const bool gridChanged = previousGrid && previousGrid != uint8_t(overall);
	if (!electrical && !gridChanged) {
		bool phaseChanged = false;
		for (uint8_t index = 0; index < 3; ++index)
			phaseChanged |= gridEventsStartupState(index) && gridEventsStartupState(index) != uint8_t(phases[index]->state);
		if (!phaseChanged) return;
	}
	Candidate candidate;
	candidate.active = true;
	candidate.subject = GridEventFormat::Subject::grid;
	candidate.previousState = previousGrid;
	candidate.currentState = uint8_t(overall);
	if (!electrical && !gridChanged) {
		for (uint8_t index = 0; index < 3; ++index) {
			const uint8_t previous = gridEventsStartupState(index);
			if (!previous || previous == uint8_t(phases[index]->state)) continue;
			candidate.subject = GridEventFormat::Subject(index);
			candidate.previousState = previous;
			candidate.currentState = uint8_t(phases[index]->state);
			candidate.currentVoltage = voltages[index];
			break;
		}
	}
	candidate.startedMs = sampleMs;
	candidate.previousVoltage = NAN;
	if (candidate.subject == GridEventFormat::Subject::grid) candidate.currentVoltage = NAN;
	candidate.sourceFlags = reason == ESP_RST_BROWNOUT ? GridEventFormat::startupBrownout :
		reason == ESP_RST_POWERON ? GridEventFormat::startupPowerOn : GridEventFormat::startupRecovered;
	snapshotSettings(candidate.settings, settings);
	snapshotMeter(candidate.meter, meter);
	armCapture(candidate, sampleMs);
	confirmCandidate(candidate, sampleMs, sampleMs);
	for (uint8_t index = 0; index < 3; ++index) {
		const uint8_t previous = gridEventsStartupState(index);
		if (!previous || previous == uint8_t(phases[index]->state) || candidate.subject == GridEventFormat::Subject(index)) continue;
		Candidate phaseChange = candidate;
		phaseChange.subject = GridEventFormat::Subject(index);
		phaseChange.previousState = previous;
		phaseChange.currentState = uint8_t(phases[index]->state);
		phaseChange.currentVoltage = voltages[index];
		addTransition(phaseChange, sampleMs);
	}
	if (candidate.subject != GridEventFormat::Subject::grid && gridChanged) {
		Candidate gridChange = candidate;
		gridChange.subject = GridEventFormat::Subject::grid;
		gridChange.previousState = previousGrid;
		gridChange.currentState = uint8_t(overall);
		gridChange.currentVoltage = NAN;
		addTransition(gridChange, sampleMs);
	}
}

// Guarda muestras posteriores mientras la ventana temporal de la captura sigue abierta.
void GridEventStore::pushFastSample(const fastRMSData_t& sample, uint32_t sampleMs)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return;
	if (!capture.active) return;
	if (int32_t(sampleMs - capture.startedMs) >= 0 && int32_t(capture.endMs - sampleMs) >= 0)
		appendPostSample(sample, sampleMs);
	poll();
}

// Atiende resultados NAND y despacha las paginas que ya quedaron inmutables.
void GridEventStore::poll()
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return;
	NandIoResult result;
	while (completionQueue && xQueueReceive(completionQueue, &result, 0) == pdPASS) {
		const uintptr_t context = reinterpret_cast<uintptr_t>(result.context);
		if (context == eraseCompletionContext) {
			capture.erasePending = false;
			if (result.success) blockHasData[currentBlock] = false;
			else {
				if (!capture.failed) ++storeMetrics.failedEvents;
				capture.failed = true;
			}
			continue;
		}
		if (context >= GridEventFormat::maxPagesPerEvent) continue;
		const uint8_t pageIndex = uint8_t(context);
		capture.pagePending[pageIndex] = false;
		if (!result.success) {
			++storeMetrics.failedPages;
			if (!capture.failed) ++storeMetrics.failedEvents;
			capture.failed = true;
			continue;
		}
		capture.pageWritten[pageIndex] = true;
		++storeMetrics.writtenPages;
		if (pageIndex == 0) {
			capture.rootWritten = true;
			blockHasData[currentBlock] = true;
		}
	}
	if (!capture.active) return;
	if (capture.failed) {
		bool requestPending = capture.erasePending;
		for (uint8_t page = 0; page < capture.pageCount; ++page) requestPending |= capture.pagePending[page];
		if (!requestPending) finishCapture(false);
		return;
	}
	storeMetrics.captureActive = true;
	storeMetrics.rootPending = capture.rootAssigned && !capture.rootWritten;
	if (capture.rootAssigned && !capture.rootSubmitted && ensureWritableBlock()) {
		prepareRootPage();
		capture.rootSubmitted = submitPage(0);
	}
	if (!capture.rootWritten) return;
	const bool windowEnded = int32_t(millis() - capture.endMs) >= 0;
	bool hasCompletedPage = false;
	for (uint8_t page = 1; page < capture.pageCount; ++page)
		hasCompletedPage |= (windowEnded || capture.continuationSampleCount[page - 1] >= GridEventFormat::continuationSamples) &&
			!capture.pagePending[page] && !capture.pageWritten[page] && !hasPendingCandidatesInPage(page - 1U);
	if (hasCompletedPage) {
		prepareContinuationPages();
		for (uint8_t page = 1; page < capture.pageCount; ++page)
			if ((windowEnded || capture.continuationSampleCount[page - 1] >= GridEventFormat::continuationSamples) &&
				!hasPendingCandidatesInPage(page - 1U))
				submitPage(page);
	}
	bool samplesComplete = true;
	for (uint8_t page = 0; page + 1 < capture.pageCount; ++page)
		samplesComplete &= capture.continuationSampleCount[page] >= GridEventFormat::continuationSamples;
	if ((!windowEnded && !samplesComplete) || hasPendingCandidatesInWindow()) return;
	bool pagesWritten = true;
	for (uint8_t page = 1; page < capture.pageCount; ++page) pagesWritten &= capture.pageWritten[page];
	if (pagesWritten) finishCapture(true);
}

// Devuelve una copia de contadores y estado operativo del Store.
void GridEventStore::getMetrics(GridEventStoreMetrics& metrics) const
{
	metrics = storeMetrics;
	metrics.nextEventId = nextEventId;
	metrics.captureActive = capture.active;
	metrics.rootPending = capture.rootAssigned && !capture.rootWritten;
}

// Escanea la NAND para resumir los identificadores disponibles, sin conservar un indice RAM.
bool GridEventStore::getCatalog(GridEventCatalog& catalog)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) { catalog = GridEventCatalog{}; return false; }
	GridEventIndexEntry ignored;
	getEntries(0, 0, true, &ignored, 0, catalog);
	return catalog.eventCount || !catalog.oldestId;
}

// Obtiene un fragmento temporal de cabeceras, ordenado desde el ID mas nuevo.
uint8_t GridEventStore::getEntries(uint32_t fromId, uint32_t toId, bool last, GridEventIndexEntry* entries,
	uint8_t maxEntries, GridEventCatalog& catalog)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) { catalog = GridEventCatalog{}; return 0; }
	if (!scanMutex || !xSemaphoreTake(scanMutex, pdMS_TO_TICKS(5000))) return 0;
	const uint8_t count = scanEntries(fromId, toId, last, entries, maxEntries, catalog);
	xSemaphoreGive(scanMutex);
	return count;
}

// Busca una captura por ID recorriendo sus cabeceras desde NAND con prioridad baja.
bool GridEventStore::findEntry(uint32_t eventId, GridEventIndexEntry& entry)
{
	StoreFormatGuard guard(formatPaused, activeCalls);
	if (!guard) return false;
	if (!eventId || !scanMutex || !xSemaphoreTake(scanMutex, pdMS_TO_TICKS(5000))) return false;
	GridEventCatalog catalog;
	const bool found = scanEntries(eventId, eventId, false, &entry, 1, catalog) == 1;
	xSemaphoreGive(scanMutex);
	return found;
}

// Evalua cambios de una fase y conserva un candidato liviano antes de reservar paginas.
void GridEventStore::observePhase(uint8_t index, const phaseState& phase, float voltage, const meterValues& meter,
	const gridSettings& settings, uint32_t sampleMs)
{
	const uint8_t current = uint8_t(phase.state);
	Candidate& candidate = candidates[index];
	if (current != observedStates[index]) {
		if (!candidate.active || candidate.currentState != current) {
			candidate = Candidate{};
			candidate.active = true;
			candidate.subject = GridEventFormat::Subject(index);
			candidate.previousState = observedStates[index];
			candidate.currentState = current;
			candidate.startedMs = phase.changedAtMs;
			candidate.previousVoltage = observedVoltages[index];
			candidate.currentVoltage = voltage;
			snapshotSettings(candidate.settings, settings);
			snapshotMeter(candidate.meter, meter);
		}
		confirmCandidate(candidate, observedStartedMs[index], sampleMs);
		candidate = Candidate{};
		observedStates[index] = current;
		observedStartedMs[index] = phase.changedAtMs;
	}
	else if (phase.pending != phase.state) {
		if (!candidate.active || candidate.currentState != uint8_t(phase.pending) || candidate.startedMs != phase.pendingSinceMs)
			startCandidate(index, phase, voltage, meter, settings);
		Candidate& pending = candidates[index];
		const uint32_t confirmationMs = phase.pending == phaseCondition::phaseLoss ? settings.lossConfirmMs :
			phase.pending == phaseCondition::normal ? settings.recoveryConfirmMs : settings.voltageConfirmMs;
		if (!capture.active && uint32_t(sampleMs - pending.startedMs) >= std::min(captureArmMs, confirmationMs))
			armCapture(pending, sampleMs);
	}
	else if (candidate.active) {
		discardCandidate(index);
	}
	// Mantener la ultima muestra del estado confirmado durante una transicion pendiente.
	if (phase.pending == phase.state && phase.state == phaseCondition(current) &&
		classifyPhase(voltage, phase.state, settings.phases[index], settings) == phase.state)
		observedVoltages[index] = voltage;
}

// Descarta index sin afectar capturas ya confirmadas ni candidatos de otros sujetos.
void GridEventStore::discardCandidate(uint8_t index)
{
	candidates[index] = Candidate{};
	if (!capture.active || capture.rootAssigned) return;
	for (const auto& candidate : candidates) if (candidate.active) return;
	++storeMetrics.discardedCandidates;
	finishCapture(false);
}

// Reutiliza el almacen de capturas para neutro; no modifica el estado agregado de las tres fases.
void GridEventStore::observeNeutral(const meterValues& meter, const gridSettings& settings, uint32_t sampleMs)
{
	const neutralSettings& config = settings.neutral;
	Candidate& candidate = candidates[3];
	if (memcmp(&lastNeutralSettings, &config, sizeof(config))) {
		lastNeutralSettings = config;
		neutralState = phaseState{};
		neutralPreviousVoltage = NAN;
		discardCandidate(3);
	}
	const phaseCondition previous = neutralState.state;
	const uint32_t previousStartedMs = neutralState.changedAtMs;
	const float volts = meter.neutralFastValid ? meter.neutral.FastVrms : NAN;
	const bool changed = updateNeutralState(neutralState, volts, sampleMs, config);
	const bool record = neutralState.pending == phaseCondition::overvoltage || previous == phaseCondition::overvoltage;
	if (neutralState.hasSample && record && neutralState.pending != previous &&
		(!candidate.active || candidate.currentState != uint8_t(neutralState.pending) || candidate.startedMs != neutralState.pendingSinceMs)) {
		if (candidate.active) discardCandidate(3);
		candidate = Candidate{};
		candidate.active = true;
		candidate.subject = GridEventFormat::Subject::neutral;
		candidate.previousState = uint8_t(previous);
		candidate.currentState = uint8_t(neutralState.pending);
		candidate.startedMs = neutralState.pendingSinceMs;
		candidate.previousVoltage = neutralPreviousVoltage;
		candidate.currentVoltage = volts;
		snapshotSettings(candidate.settings, settings);
		// En raices de neutro estos campos describen su umbral y tiempos, sin agrandar el formato.
		candidate.settings.noVoltage = config.highVoltage;
		candidate.settings.hysteresis = config.hysteresis;
		candidate.settings.voltageConfirmMs = config.confirmMs;
		candidate.settings.recoveryConfirmMs = config.recoveryMs;
		snapshotMeter(candidate.meter, meter);
	}
	if (changed && candidate.active) {
		confirmCandidate(candidate, previous == phaseCondition::unknown ? candidate.startedMs : previousStartedMs, sampleMs);
		candidate = Candidate{};
	} else if (candidate.active && (!neutralState.hasSample || neutralState.pending == neutralState.state)) {
		discardCandidate(3);
	} else if (candidate.active && !capture.active && uint32_t(sampleMs - candidate.startedMs) >=
		std::min(captureArmMs, neutralState.pending == phaseCondition::normal ? config.recoveryMs : config.confirmMs))
		armCapture(candidate, sampleMs);
	if (neutralState.hasSample && neutralState.pending == neutralState.state) neutralPreviousVoltage = volts;
}

// Detecta cambios del estado agregado y los incorpora a la misma linea de tiempo.
void GridEventStore::observeGrid(const phaseState* phases[3], gridCondition overall, const meterValues& meter,
	const gridSettings& settings, uint32_t sampleMs)
{
	const uint8_t current = uint8_t(overall);
	if (current == observedStates[3]) return;
	uint32_t startedMs = phases[0]->changedAtMs;
	for (uint8_t index = 1; index < 3; ++index)
		if (int32_t(phases[index]->changedAtMs - startedMs) > 0) startedMs = phases[index]->changedAtMs;
	confirmGrid(overall, observedStartedMs[3], startedMs, meter, settings, sampleMs);
	observedStates[3] = current;
	observedStartedMs[3] = startedMs;
}

// Copia el contexto del primer cruce pendiente de una fase para poder reconstruirlo despues.
void GridEventStore::startCandidate(uint8_t index, const phaseState& phase, float voltage, const meterValues& meter,
	const gridSettings& settings)
{
	Candidate& candidate = candidates[index];
	candidate = Candidate{};
	candidate.active = true;
	candidate.subject = GridEventFormat::Subject(index);
	candidate.previousState = observedStates[index];
	candidate.currentState = uint8_t(phase.pending);
	candidate.startedMs = phase.pendingSinceMs;
	candidate.previousVoltage = observedVoltages[index];
	candidate.currentVoltage = voltage;
	snapshotSettings(candidate.settings, settings);
	snapshotMeter(candidate.meter, meter);
}

// Reserva la captura configurada y reconstruye muestras previas y posteriores ya disponibles.
void GridEventStore::armCapture(const Candidate& candidate, uint32_t sampleMs)
{
	if (capture.active) return;
	capture = Capture{};
	capture.pageCount = std::max<uint8_t>(GridEventFormat::minPagesPerEvent,
		std::min<uint8_t>(GridEventFormat::maxPagesPerEvent, candidate.settings.capturePages));
	capture.pages = static_cast<uint8_t*>(malloc(size_t(capture.pageCount) * GridEventFormat::pageSize));
	if (!capture.pages) {
		++storeMetrics.failedEvents;
		return;
	}
	memset(capture.pages, 0xFF, size_t(capture.pageCount) * GridEventFormat::pageSize);
	capture.active = true;
	capture.startedMs = candidate.startedMs;
	capture.endMs = candidate.startedMs + uint32_t(GridEventFormat::continuationSamples) *
		uint32_t(capture.pageCount - 1U) * 10U;
	copyFastRange(candidate.startedMs - uint32_t(GridEventFormat::preTriggerSamples) * 10U, candidate.startedMs - 1U, true);
	copyFastRange(candidate.startedMs, sampleMs, false);
	storeMetrics.captureActive = true;
}

// Usa una transicion confirmada como raiz del incidente o como entrada secundaria de su linea de tiempo.
void GridEventStore::confirmCandidate(const Candidate& candidate, uint32_t previousStartedMs, uint32_t sampleMs)
{
	if (!capture.active) armCapture(candidate, sampleMs);
	if (!capture.active) return;
	if (!capture.rootAssigned) {
		capture.rootAssigned = true;
		capture.eventId = nextEventId;
		capture.rootStartedMs = candidate.startedMs;
		capture.settings = candidate.settings;
		capture.meter = candidate.meter;
		capture.root = GridEventFormat::ListHeader{};
		capture.root.formatVersion = GridEventFormat::version;
		capture.root.timeStatus = candidate.sourceFlags | GridEventFormat::packedFastRms;
		capture.root.eventId = capture.eventId;
		capture.root.subjectAndStates = GridEventFormat::packSubjectAndStates(candidate.subject, candidate.previousState, candidate.currentState);
		capture.root.previousStateDuration10Ms = encodeDuration10Ms(previousStartedMs, candidate.startedMs);
		capture.root.previousVoltage = encodeVoltage(candidate.previousVoltage);
		capture.root.currentVoltage = encodeVoltage(candidate.currentVoltage);
		struct timeval now = {};
		gettimeofday(&now, nullptr);
		uint64_t timestamp = managerNowMs();
		if (timestamp) timestamp -= uint32_t(sampleMs - candidate.startedMs);
		else {
			const uint32_t elapsed = uint32_t(sampleMs - candidate.startedMs);
			const uint64_t localNow = uint64_t(now.tv_sec) * 1000ULL + now.tv_usec / 1000U;
			timestamp = localNow > elapsed ? localNow - elapsed : uint64_t(estimatedEpoch) * 1000ULL;
			capture.root.timeStatus |= GridEventFormat::timestampEstimated;
		}
		if (timestamp / 1000ULL < estimatedEpoch) {
			timestamp = uint64_t(estimatedEpoch) * 1000ULL + candidate.startedMs;
			capture.root.timeStatus |= GridEventFormat::timestampEstimated;
		}
		capture.root.unixSeconds = uint32_t(timestamp / 1000ULL);
		capture.root.milliseconds = uint16_t(timestamp % 1000ULL);
		++storeMetrics.acceptedEvents;
	}
	else addTransition(candidate, previousStartedMs);
}

// Incorpora el cambio general derivado de R/S/T; no tiene un unico voltaje asociado.
void GridEventStore::confirmGrid(gridCondition current, uint32_t previousStartedMs, uint32_t startedMs,
	const meterValues& meter, const gridSettings& settings, uint32_t sampleMs)
{
	Candidate candidate;
	candidate.active = true;
	candidate.subject = GridEventFormat::Subject::grid;
	candidate.previousState = observedStates[3];
	candidate.currentState = uint8_t(current);
	candidate.startedMs = startedMs;
	candidate.previousVoltage = NAN;
	candidate.currentVoltage = NAN;
	snapshotSettings(candidate.settings, settings);
	snapshotMeter(candidate.meter, meter);
	confirmCandidate(candidate, previousStartedMs, sampleMs);
}

// Copia un intervalo del anillo Fast sin reservar buffers grandes sobre la pila.
void GridEventStore::copyFastRange(uint32_t fromMs, uint32_t toMs, bool preTrigger)
{
	fastRmsRingSample sample;
	uint32_t cursor = fromMs;
	while (meterCopyFastSamples(cursor, toMs, &sample, 1)) {
		if (preTrigger) {
			if (capture.rootSampleCount >= GridEventFormat::preTriggerSamples) break;
			const size_t offset = GridEventFormat::rootSampleOffset + size_t(capture.rootSampleCount) * sizeof(fastRMSData_t);
			memcpy(capture.pages + offset, &sample.data, sizeof(sample.data));
			if (!capture.firstSampleKnown) {
				capture.firstSampleKnown = true;
				capture.firstSampleMs = sample.sampleMs;
			}
			++capture.rootSampleCount;
		}
		else appendPostSample(sample.data, sample.sampleMs);
		if (sample.sampleMs == UINT32_MAX) break;
		cursor = sample.sampleMs + 1U;
	}
}

// Escribe una muestra posterior en la primera pagina de continuacion con espacio.
void GridEventStore::appendPostSample(const fastRMSData_t& sample, uint32_t sampleMs)
{
	if (int32_t(sampleMs - capture.startedMs) < 0 || int32_t(capture.endMs - sampleMs) < 0) return;
	for (uint8_t page = 0; page + 1 < capture.pageCount; ++page) {
		if (capture.continuationSampleCount[page] >= GridEventFormat::continuationSamples) continue;
		const size_t offset = GridEventFormat::continuationSampleOffset +
			size_t(capture.continuationSampleCount[page]) * sizeof(fastRMSData_t);
		memcpy(capture.pages + size_t(page + 1) * GridEventFormat::pageSize + offset, &sample, sizeof(sample));
		++capture.continuationSampleCount[page];
		return;
	}
}

// Agrega una transicion distinta de la raiz dentro de la ventana temporal retenida.
void GridEventStore::addTransition(const Candidate& candidate, uint32_t previousStartedMs)
{
	if (capture.transitionCount >= GridEventFormat::maxTransitions ||
		int32_t(candidate.startedMs - capture.startedMs) < 0 || int32_t(capture.endMs - candidate.startedMs) < 0) return;
	const uint8_t page = std::min<uint32_t>(uint32_t(candidate.startedMs - capture.startedMs) /
		(uint32_t(GridEventFormat::continuationSamples) * 10U), capture.pageCount - 2U);
	if (capture.transitionCountPerPage[page] >= GridEventFormat::transitionsPerPage ||
		capture.pagePending[page + 1] || capture.pageWritten[page + 1]) return;
	int32_t offset = int32_t(candidate.startedMs - capture.rootStartedMs);
	offset = std::max<int32_t>(INT16_MIN, std::min<int32_t>(INT16_MAX, offset));
	GridEventFormat::Transition& transition = capture.transitions[page * GridEventFormat::transitionsPerPage +
		capture.transitionCountPerPage[page]++];
	++capture.transitionCount;
	transition.offsetMs = int16_t(offset);
	transition.previousStateDuration10Ms = encodeDuration10Ms(previousStartedMs, candidate.startedMs);
	transition.subjectAndStates = GridEventFormat::packSubjectAndStates(candidate.subject, candidate.previousState, candidate.currentState);
	transition.previousVoltage = encodeVoltage(candidate.previousVoltage);
	transition.currentVoltage = encodeVoltage(candidate.currentVoltage);
}

// Copia la configuracion Grid actual a una representacion fija y autocontenida.
void GridEventStore::snapshotSettings(GridEventFormat::SettingsSnapshot& dest, const gridSettings& settings)
{
	for (uint8_t index = 0; index < 3; ++index) {
		dest.lowVoltage[index] = settings.phases[index].lowVoltage;
		dest.highVoltage[index] = settings.phases[index].highVoltage;
	}
	dest.noVoltage = settings.noVoltage;
	dest.hysteresis = settings.hysteresis;
	dest.lossConfirmMs = settings.lossConfirmMs;
	dest.voltageConfirmMs = settings.voltageConfirmMs;
	dest.recoveryConfirmMs = settings.recoveryConfirmMs;
	dest.capturePages = settings.capturePages;
}

// Copia las mediciones y contadores que contextualizan el inicio del evento.
void GridEventStore::snapshotMeter(GridEventFormat::MeterSnapshot& dest, const meterValues& meter)
{
	const phaseValues* phases[] = { &meter.phaseR, &meter.phaseS, &meter.phaseT, &meter.neutral };
	for (uint8_t index = 0; index < 4; ++index) {
		dest.voltage[index] = phases[index]->Vrms;
		dest.current[index] = phases[index]->Irms;
	}
	for (uint8_t index = 0; index < 3; ++index) {
		dest.activePower[index] = phases[index]->Watt;
		dest.reactivePower[index] = phases[index]->VAR;
		dest.apparentPower[index] = phases[index]->VA;
		dest.powerFactor[index] = phases[index]->PowerFactor;
		dest.activeEnergy[index] = phases[index]->Watt_H;
		dest.reactiveEnergy[index] = phases[index]->VAR_H;
		dest.apparentEnergy[index] = phases[index]->VA_H;
	}
	dest.totalPower[0] = meter.power.Watt;
	dest.totalPower[1] = meter.power.VAR;
	dest.totalPower[2] = meter.power.VA;
	dest.frequency = meter.average.Freq;
	dest.totalEnergy[0] = meter.energy.Watt_H;
	dest.totalEnergy[1] = meter.energy.VAR_H;
	dest.totalEnergy[2] = meter.energy.VA_H;
}

// Inserta cabecera, configuracion, snapshot y CRC en la pagina raiz ya poblada con muestras previas.
void GridEventStore::prepareRootPage()
{
	capture.root.sampleCount = capture.rootSampleCount;
	capture.root.pageCount = capture.pageCount;
	capture.root.firstSampleOffsetMs = capture.firstSampleKnown ? int32_t(capture.firstSampleMs - capture.rootStartedMs) : 0;
	capture.root.headerCrc16 = GridEventFormat::crc16(reinterpret_cast<const uint8_t*>(&capture.root),
		offsetof(GridEventFormat::ListHeader, headerCrc16));
	memcpy(capture.pages, &capture.root, sizeof(capture.root));
	memcpy(capture.pages + sizeof(capture.root), &capture.settings, sizeof(capture.settings));
	memcpy(capture.pages + sizeof(capture.root) + sizeof(capture.settings), &capture.meter, sizeof(capture.meter));
	const size_t bodySize = sizeof(capture.settings) + sizeof(capture.meter) +
		size_t(capture.rootSampleCount) * sizeof(fastRMSData_t);
	const uint16_t bodyCrc = GridEventFormat::crc16(capture.pages + sizeof(capture.root), bodySize);
	const size_t crcOffset = GridEventFormat::rootSampleOffset + size_t(capture.rootSampleCount) * sizeof(fastRMSData_t);
	memcpy(capture.pages + crcOffset, &bodyCrc, sizeof(bodyCrc));
}

// Ordena transiciones y prepara todas las paginas posteriores con CRC independiente.
void GridEventStore::prepareContinuationPages()
{
	for (uint8_t page = 0; page + 1 < capture.pageCount; ++page) {
		if (capture.pagePending[page + 1] || capture.pageWritten[page + 1]) continue;
		uint8_t* pageData = capture.pages + size_t(page + 1) * GridEventFormat::pageSize;
		const uint8_t first = page * GridEventFormat::transitionsPerPage;
		const uint8_t count = capture.transitionCountPerPage[page];
		std::sort(capture.transitions + first, capture.transitions + first + count,
			[](const GridEventFormat::Transition& left, const GridEventFormat::Transition& right) { return left.offsetMs < right.offsetMs; });
		GridEventFormat::ContinuationHeader header = {};
		header.eventId = capture.eventId;
		header.pageIndex = page + 1;
		header.pageCount = capture.pageCount;
		header.sampleCount = capture.continuationSampleCount[page];
		header.transitionCount = count;
		memcpy(pageData, &header, sizeof(header));
		if (count) memcpy(pageData + sizeof(header), capture.transitions + first,
			size_t(count) * sizeof(GridEventFormat::Transition));
		const size_t usedBytes = GridEventFormat::continuationSampleOffset +
			size_t(capture.continuationSampleCount[page]) * sizeof(fastRMSData_t);
		header.pageCrc16 = GridEventFormat::crc16(pageData, usedBytes);
		memcpy(pageData, &header, sizeof(header));
	}
}

// Encola una pagina del evento actual conservando el buffer hasta su finalizacion.
bool GridEventStore::submitPage(uint8_t pageIndex)
{
	if (pageIndex >= capture.pageCount || capture.pagePending[pageIndex] || capture.pageWritten[pageIndex]) return true;
	const uint8_t page = currentPage + pageIndex;
	uint8_t* pageData = capture.pages + size_t(pageIndex) * GridEventFormat::pageSize;
	if (!partition.submitProgramPage(currentBlock, page, pageData, GridEventFormat::pageSize,
		StoragePriority::medium, completionQueue, capture.eventId, reinterpret_cast<void*>(uintptr_t(pageIndex)))) return false;
	capture.pagePending[pageIndex] = true;
	return true;
}

// Selecciona un bloque donde entre el evento completo y lo borra si pertenece a una vuelta anterior.
bool GridEventStore::ensureWritableBlock()
{
	if (uint16_t(currentPage) + capture.pageCount > WinbondSpiNand::pagesPerBlock) {
		currentPage = 0;
		currentBlock = partition.nextUsableBlock(currentBlock);
		if (currentBlock == NandPartition::invalidBlock) return false;
	}
	if (currentPage || !blockHasData[currentBlock]) return !capture.erasePending;
	if (capture.erasePending) return false;
	if (!partition.submitEraseBlock(currentBlock, StoragePriority::medium, completionQueue, capture.eventId,
		reinterpret_cast<void*>(eraseCompletionContext))) return false;
	capture.erasePending = true;
	return false;
}

// Indica si alguna fase sigue pendiente y comenzo antes del final de la ventana conservada.
bool GridEventStore::hasPendingCandidatesInWindow() const
{
	for (const Candidate& candidate : candidates)
		if (candidate.active && int32_t(candidate.startedMs - capture.startedMs) >= 0 &&
			int32_t(capture.endMs - candidate.startedMs) >= 0) return true;
	return false;
}

// Indica si una transicion pendiente pertenece al tramo temporal de la pagina solicitada.
bool GridEventStore::hasPendingCandidatesInPage(uint8_t continuationPage) const
{
	if (continuationPage >= capture.pageCount - 1U) return false;
	for (const Candidate& candidate : candidates) {
		if (!candidate.active || int32_t(candidate.startedMs - capture.startedMs) < 0 ||
			int32_t(capture.endMs - candidate.startedMs) < 0) continue;
		const uint8_t page = std::min<uint32_t>(uint32_t(candidate.startedMs - capture.startedMs) /
			(uint32_t(GridEventFormat::continuationSamples) * 10U), capture.pageCount - 2U);
		if (page == continuationPage) return true;
	}
	return false;
}

// Libera la captura y avanza el cursor; un fallo abandona el resto del bloque dudoso.
void GridEventStore::finishCapture(bool success)
{
	if (success && capture.rootAssigned) {
		nextEventId = capture.eventId + 1U;
		currentPage += capture.pageCount;
		if (currentPage >= WinbondSpiNand::pagesPerBlock) {
			currentPage = 0;
			currentBlock = partition.nextUsableBlock(currentBlock);
			if (currentBlock == NandPartition::invalidBlock) {
				++storeMetrics.failedEvents;
				currentBlock = 0;
			}
		}
	}
	else if (capture.rootAssigned) {
		nextEventId = capture.eventId + 1U;
		currentPage = 0;
		currentBlock = partition.nextUsableBlock(currentBlock);
		if (currentBlock == NandPartition::invalidBlock) currentBlock = 0;
	}
	free(capture.pages);
	capture = Capture{};
	storeMetrics.captureActive = false;
	storeMetrics.rootPending = false;
	storeMetrics.nextEventId = nextEventId;
}

// Recorre cabeceras variables y ubica el evento valido mas nuevo para continuar la secuencia.
bool GridEventStore::recoverCursor()
{
	NandPartitionInfo info;
	partition.getInfo(info);
	if (!info.enabled || info.blockCount > 64) return false;
	memset(blockHasData, 0, sizeof(blockHasData));
	bool found = false;
	uint32_t latestId = 0;
	uint16_t latestBlock = 0;
	uint8_t latestPage = 0;
	uint8_t latestPageCount = 0;
	storeMetrics.recoveredEvents = 0;
	for (uint16_t block = 0; block < info.blockCount; ++block) {
		if (!partition.isUsableBlock(block)) continue;
		for (uint8_t page = 0; page < WinbondSpiNand::pagesPerBlock;) {
			GridEventFormat::ListHeader header;
			bool valid = false, erased = false;
			if (!readHeader(block, page, header, valid, erased)) return false;
			if (erased) break;
			if (!erased) blockHasData[block] = true;
			if (valid) {
				++storeMetrics.recoveredEvents;
				if (!found || isNewerEventId(header.eventId, latestId)) {
					found = true;
					latestId = header.eventId;
					latestBlock = block;
					latestPage = page;
					latestPageCount = header.pageCount;
				}
				page += header.pageCount;
			}
			else ++page;
		}
	}
	if (!found) {
		currentBlock = 0;
		while (!partition.isUsableBlock(currentBlock)) {
			currentBlock = partition.nextUsableBlock(currentBlock);
			if (currentBlock == NandPartition::invalidBlock) return false;
		}
		currentPage = 0;
		nextEventId = 1;
	}
	else {
		currentBlock = latestBlock;
		currentPage = latestPage + latestPageCount;
		if (currentPage >= WinbondSpiNand::pagesPerBlock) {
			currentPage = 0;
			currentBlock = partition.nextUsableBlock(currentBlock);
			if (currentBlock == NandPartition::invalidBlock) return false;
		}
		// Una raiz parcial impide conocer su longitud; se abandona el resto de ese bloque.
		if (currentPage) {
			GridEventFormat::ListHeader header;
			bool valid = false, erased = false;
			if (!readHeader(currentBlock, currentPage, header, valid, erased)) return false;
			if (!erased) {
				currentPage = 0;
				currentBlock = partition.nextUsableBlock(currentBlock);
				if (currentBlock == NandPartition::invalidBlock) return false;
			}
		}
		nextEventId = latestId + 1U;
	}
	storeMetrics.nextEventId = nextEventId;
	return true;
}

// Recorre cabeceras NAND y conserva solo el tramo temporal solicitado para una consulta web.
uint8_t GridEventStore::scanEntries(uint32_t fromId, uint32_t toId, bool last, GridEventIndexEntry* entries,
	uint8_t maxEntries, GridEventCatalog& catalog)
{
	catalog = GridEventCatalog{};
	NandPartitionInfo info;
	partition.getInfo(info);
	if (!info.enabled) return 0;
	uint8_t count = 0;
	for (uint16_t block = 0; block < info.blockCount; ++block) {
		if (!partition.isUsableBlock(block)) continue;
		for (uint8_t page = 0; page < WinbondSpiNand::pagesPerBlock;) {
			GridEventFormat::ListHeader header;
			bool valid = false, erased = false;
			if (!readHeader(block, page, header, valid, erased)) return count;
			if (erased) break;
			if (!valid) { ++page; continue; }
			++catalog.eventCount;
			if (!catalog.oldestId || isNewerEventId(catalog.oldestId, header.eventId)) catalog.oldestId = header.eventId;
			if (!catalog.newestId || isNewerEventId(header.eventId, catalog.newestId)) catalog.newestId = header.eventId;
			const bool selected = last || (header.eventId >= fromId && header.eventId <= toId);
			if (selected && entries && maxEntries) {
				uint8_t position = 0;
				while (position < count && isNewerEventId(entries[position].header.eventId, header.eventId)) ++position;
				if (position < maxEntries) {
					const uint8_t end = min<uint8_t>(count, maxEntries - 1);
					for (uint8_t index = end; index > position; --index) entries[index] = entries[index - 1];
					entries[position].block = block;
					entries[position].page = page;
					entries[position].header = header;
					if (count < maxEntries) ++count;
				}
			}
			page += header.pageCount;
		}
	}
	return count;
}

// Lee y valida la cabecera que permite saltar el evento sin traer su payload.
bool GridEventStore::readHeader(uint16_t block, uint8_t page, GridEventFormat::ListHeader& header, bool& valid, bool& erased)
{
	NandIoResult stale;
	while (xQueueReceive(recoveryQueue, &stale, 0) == pdPASS) {}
	if (!partition.submitReadPage(block, page, 0, headerBuffer, sizeof(headerBuffer),
		StoragePriority::low, recoveryQueue)) return false;
	NandIoResult result;
	if (xQueueReceive(recoveryQueue, &result, pdMS_TO_TICKS(250)) != pdPASS || !result.success) return false;
	erased = true;
	for (uint8_t byte : headerBuffer) if (byte != 0xFF) { erased = false; break; }
	memcpy(&header, headerBuffer, sizeof(header));
	valid = !erased && header.formatVersion == GridEventFormat::version && header.eventId &&
		header.sampleCount <= ((header.timeStatus & GridEventFormat::retiredNeutralFormat) ? 100 : GridEventFormat::preTriggerSamples) &&
		header.pageCount >= GridEventFormat::minPagesPerEvent &&
		header.pageCount <= GridEventFormat::maxPagesPerEvent &&
		uint16_t(page) + header.pageCount <= WinbondSpiNand::pagesPerBlock &&
		header.headerCrc16 == GridEventFormat::crc16(headerBuffer, offsetof(GridEventFormat::ListHeader, headerCrc16));
	return true;
}

// Convierte voltaje a centesimas y reserva UINT16_MAX para un sujeto sin voltaje propio.
uint16_t GridEventStore::encodeVoltage(float voltage)
{
	if (!isfinite(voltage)) return GridEventFormat::unavailableVoltage;
	if (!(voltage > 0)) return 0;
	const float scaled = voltage * 100.0f;
	return scaled >= float(UINT16_MAX - 1U) ? uint16_t(UINT16_MAX - 1U) : uint16_t(lroundf(scaled));
}

// Convierte una diferencia monotónica a unidades de 10 ms sin desbordar el campo persistido.
uint32_t GridEventStore::encodeDuration10Ms(uint32_t startedMs, uint32_t endedMs)
{
	const uint64_t duration = uint32_t(endedMs - startedMs);
	const uint64_t units = (duration + 5U) / 10U;
	return units > UINT32_MAX ? UINT32_MAX : uint32_t(units);
}

// Compara identificadores de evento contemplando el wrap natural de uint32_t.
bool GridEventStore::isNewerEventId(uint32_t candidate, uint32_t current)
{
	return int32_t(candidate - current) > 0;
}

// Reenvia estados al Store montado sin obligar a MeterTask a conocer StorageManager.
void gridEventStoreObserve(const phaseState& r, const phaseState& s, const phaseState& t, gridCondition overall,
	float vr, float vs, float vt, const meterValues& meter, const gridSettings& settings, uint32_t sampleMs)
{
	if (mountedGridEventStore) mountedGridEventStore->observe(r, s, t, overall, vr, vs, vt, meter, settings, sampleMs);
}

// Reenvia una muestra Fast al Store montado; sampleMs conserva el instante monotónico exacto.
void gridEventStorePushFastSample(const fastRMSData_t& sample, uint32_t sampleMs)
{
	if (mountedGridEventStore) mountedGridEventStore->pushFastSample(sample, sampleMs);
}

// Marca una captura artificial que sera creada por MeterTask en su siguiente ciclo.
void gridEventStoreRequestTest()
{
	gridTestRequested = true;
}
