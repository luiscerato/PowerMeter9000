#pragma once

#include "grid_event_format.h"
#include "nand_partition.h"
#include "../meter.h"
#include <atomic>

static_assert(sizeof(fastRMSData_t) == 16, "Fast RMS includes eight packed 14-bit channels");
static_assert(GridEventFormat::rootSampleOffset + GridEventFormat::preTriggerSamples * sizeof(fastRMSData_t) + 2 <= GridEventFormat::pageSize, "Root samples overflow");
static_assert(GridEventFormat::continuationSampleOffset + GridEventFormat::continuationSamples * sizeof(fastRMSData_t) <= GridEventFormat::pageSize, "Continuation samples overflow");

struct GridEventStoreMetrics {
	uint32_t recoveredEvents = 0;
	uint32_t acceptedEvents = 0;
	uint32_t discardedCandidates = 0;
	uint32_t failedEvents = 0;
	uint32_t writtenPages = 0;
	uint32_t failedPages = 0;
	uint32_t nextEventId = 1;
	bool captureActive = false;
	bool rootPending = false;
};

// Identifica una captura Grid mientras una consulta temporal recorre la NAND.
struct GridEventIndexEntry {
	uint16_t block = 0;
	uint8_t page = 0;
	GridEventFormat::ListHeader header = {};
};

// Resume el rango de identificadores actualmente recuperables de la particion Grid.
struct GridEventCatalog {
	uint32_t eventCount = 0;
	uint32_t oldestId = 0;
	uint32_t newestId = 0;
};

// Guarda incidentes Grid variables de tres a seis paginas en un anillo NAND.
class GridEventStore {
public:
	// Asocia el Store con la particion Grid; partition permanece bajo control externo.
	explicit GridEventStore(NandPartition& partition);

	// Reconstruye el cursor de eventos y prepara las colas de finalizacion.
	bool begin();

	// Pausa observacion y consultas y espera sus llamadas activas antes del borrado.
	void prepareFormat();

	// Reanuda el Store solo despues de recuperar correctamente su cursor.
	void finishFormat(bool success);

	// Observa estados Grid a 100 Hz; meter/settings capturan el contexto del candidato.
	void observe(const phaseState& r, const phaseState& s, const phaseState& t, gridCondition overall,
		float vr, float vs, float vt, const meterValues& meter, const gridSettings& settings, uint32_t sampleMs);

	// Agrega una muestra Fast nueva a la captura candidata abierta.
	void pushFastSample(const fastRMSData_t& sample, uint32_t sampleMs);

	// Recoge resultados NAND y sella paginas cuando el incidente queda completo.
	void poll();

	// Copia metricas del Store; metrics recibe la instantanea actual.
	void getMetrics(GridEventStoreMetrics& metrics) const;

	// Escanea las cabeceras y devuelve el catalogo actual; catalog recibe el resumen.
	bool getCatalog(GridEventCatalog& catalog);

	// Busca hasta maxEntries en el rango inclusivo; last selecciona los IDs mas nuevos.
	uint8_t getEntries(uint32_t fromId, uint32_t toId, bool last, GridEventIndexEntry* entries, uint8_t maxEntries,
		GridEventCatalog& catalog);

	// Busca una captura por ID recorriendo temporalmente las cabeceras NAND.
	bool findEntry(uint32_t eventId, GridEventIndexEntry& entry);

private:
	static constexpr uint32_t captureArmMs = 150;
	static constexpr uint32_t estimatedEpoch = 1767225600UL;

	struct Candidate {
		bool active = false;
		GridEventFormat::Subject subject = GridEventFormat::Subject::phaseR;
		uint8_t previousState = 0;
		uint8_t currentState = 0;
		uint32_t startedMs = 0;
		float previousVoltage = 0;
		float currentVoltage = 0;
		uint8_t sourceFlags = 0;
		GridEventFormat::SettingsSnapshot settings = {};
		GridEventFormat::MeterSnapshot meter = {};
	};

	struct Capture {
		bool active = false;
		bool rootAssigned = false;
		bool rootSubmitted = false;
		bool rootWritten = false;
		bool continuationsSubmitted = false;
		bool erasePending = false;
		bool failed = false;
		uint32_t eventId = 0;
		uint32_t startedMs = 0;
		uint32_t endMs = 0;
		uint32_t rootStartedMs = 0;
		uint32_t firstSampleMs = 0;
		uint8_t pageCount = GridEventFormat::minPagesPerEvent;
		uint8_t rootSampleCount = 0;
		uint8_t continuationSampleCount[GridEventFormat::maxPagesPerEvent - 1] = {};
		uint8_t transitionCount = 0;
		uint8_t transitionCountPerPage[GridEventFormat::maxPagesPerEvent - 1] = {};
		bool firstSampleKnown = false;
		bool pagePending[GridEventFormat::maxPagesPerEvent] = {};
		bool pageWritten[GridEventFormat::maxPagesPerEvent] = {};
		GridEventFormat::ListHeader root = {};
		GridEventFormat::SettingsSnapshot settings = {};
		GridEventFormat::MeterSnapshot meter = {};
		GridEventFormat::Transition transitions[GridEventFormat::maxTransitions] = {};
		uint8_t* pages = nullptr;
	};

	NandPartition& partition;
	std::atomic<bool> formatPaused{false};
	std::atomic<uint16_t> activeCalls{0};
	QueueHandle_t completionQueue = nullptr;
	StaticQueue_t completionQueueStorage;
	uint8_t completionQueueData[(GridEventFormat::maxPagesPerEvent + 1) * sizeof(NandIoResult)] = {};
	QueueHandle_t recoveryQueue = nullptr;
	StaticQueue_t recoveryQueueStorage;
	uint8_t recoveryQueueData[sizeof(NandIoResult)] = {};
	uint8_t headerBuffer[sizeof(GridEventFormat::ListHeader)] = {};
	SemaphoreHandle_t scanMutex = nullptr;
	Candidate candidates[4] = {};
	phaseState neutralState;
	neutralSettings lastNeutralSettings;
	float neutralPreviousVoltage = NAN;
	uint8_t observedStates[4] = {};
	uint32_t observedStartedMs[4] = {};
	float observedVoltages[3] = {};
	bool statesReady = false;
	bool blockHasData[64] = {};
	uint16_t currentBlock = 0;
	uint8_t currentPage = 0;
	uint32_t nextEventId = 1;
	Capture capture;
	GridEventStoreMetrics storeMetrics;

	// Procesa una fase individual y abre o confirma candidatos segun sus estados pendientes.
	void observePhase(uint8_t index, const phaseState& phase, float voltage, const meterValues& meter,
		const gridSettings& settings, uint32_t sampleMs);
	// Detecta tension alta y recuperacion de neutro; meter/settings y sampleMs aportan contexto y tiempo.
	void observeNeutral(const meterValues& meter, const gridSettings& settings, uint32_t sampleMs);
	// Cancela index y libera una captura no confirmada cuando no quedan candidatos activos.
	void discardCandidate(uint8_t index);

	// Procesa el estado general derivado de las tres fases.
	void observeGrid(const phaseState* phases[3], gridCondition overall, const meterValues& meter,
		const gridSettings& settings, uint32_t sampleMs);

	// Registra los cambios observados al arrancar; phases/overall son los estados estables nuevos.
	void startStartupCapture(const phaseState* phases[3], gridCondition overall, const float voltages[3],
		const meterValues& meter, const gridSettings& settings, uint32_t sampleMs);

	// Conserva el contexto del primer cruce pendiente para una fase.
	void startCandidate(uint8_t index, const phaseState& phase, float voltage, const meterValues& meter,
		const gridSettings& settings);

	// Abre la captura RAM y reconstruye su tramo previo desde el anillo Fast.
	void armCapture(const Candidate& candidate, uint32_t sampleMs);

	// Confirma una transicion y la usa como raiz o como entrada de linea de tiempo.
	void confirmCandidate(const Candidate& candidate, uint32_t previousStartedMs, uint32_t sampleMs);

	// Crea una transicion sintetica para el estado general de red.
	void confirmGrid(gridCondition current, uint32_t previousStartedMs, uint32_t startedMs,
		const meterValues& meter, const gridSettings& settings, uint32_t sampleMs);

	// Copia muestras Fast del anillo dentro de un intervalo y las coloca en la pagina correspondiente.
	void copyFastRange(uint32_t fromMs, uint32_t toMs, bool preTrigger);

	// Agrega una muestra posterior a la raiz en la primera pagina con capacidad.
	void appendPostSample(const fastRMSData_t& sample, uint32_t sampleMs);

	// Agrega una transicion secundaria si su inicio pertenece a la ventana de captura.
	void addTransition(const Candidate& candidate, uint32_t previousStartedMs);

	// Copia configuracion y mediciones del instante del candidato en formatos persistentes.
	static void snapshotSettings(GridEventFormat::SettingsSnapshot& dest, const gridSettings& settings);
	static void snapshotMeter(GridEventFormat::MeterSnapshot& dest, const meterValues& meter);

	// Prepara la pagina raiz y calcula sus CRC sin modificar sus muestras ya copiadas.
	void prepareRootPage();

	// Prepara las paginas posteriores, ordena transiciones y calcula sus CRC.
	void prepareContinuationPages();

	// Encola paginaIndex en la posicion actual del slot; devuelve false si el destino aun no esta listo.
	bool submitPage(uint8_t pageIndex);

	// Solicita el borrado del bloque de destino cuando la vuelta del anillo lo requiere.
	bool ensureWritableBlock();

	// Determina si ya no quedan candidatos cuyo inicio cae dentro de la ventana capturada.
	bool hasPendingCandidatesInWindow() const;

	// Determina si continuationPage tiene algun candidato pendiente de confirmar.
	bool hasPendingCandidatesInPage(uint8_t continuationPage) const;

	// Finaliza un incidente persistido y avanza segun su cantidad real de paginas.
	void finishCapture(bool success);

	// Recupera el mayor eventId y la primera pagina disponible sin depender de un journal mutable.
	bool recoverCursor();

	// Lee solo la cabecera raiz de page; valid y erased describen su estado.
	bool readHeader(uint16_t block, uint8_t page, GridEventFormat::ListHeader& header, bool& valid, bool& erased);


	// Recorre cabeceras NAND y selecciona resultados transitorios para una consulta web.
	uint8_t scanEntries(uint32_t fromId, uint32_t toId, bool last, GridEventIndexEntry* entries, uint8_t maxEntries,
		GridEventCatalog& catalog);

	// Convierte un voltaje a centesimas sin desbordar el formato persistente.
	static uint16_t encodeVoltage(float voltage);

	// Calcula una duracion en unidades de 10 ms con saturacion del entero de salida.
	static uint32_t encodeDuration10Ms(uint32_t startedMs, uint32_t endedMs);

	// Compara eventId considerando el desborde modular de 32 bits.
	static bool isNewerEventId(uint32_t candidate, uint32_t current);
};

// Expone el Store Grid montado para la adquisicion sin acoplarla a StorageManager.
void gridEventStoreObserve(const phaseState& r, const phaseState& s, const phaseState& t, gridCondition overall,
	float vr, float vs, float vt, const meterValues& meter, const gridSettings& settings, uint32_t sampleMs);

// Entrega una muestra Fast al Store Grid montado.
void gridEventStorePushFastSample(const fastRMSData_t& sample, uint32_t sampleMs);

// Solicita una captura Grid artificial que sera creada desde MeterTask.
void gridEventStoreRequestTest();
