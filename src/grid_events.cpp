#include "grid_events.h"
#include "wifiUtils.h"
#include "manager_api.h"
#include "meter_timing.h"
#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"

static const uint32_t gridStartupGuardMs = 5000;
static const uint32_t persistedGridMagic = 0x47524431;
static const char* gridStorageName = "gridEvents";
static const char* gridStorageKeys[] = { "phaseR", "phaseS", "phaseT", "overall" };

struct gridStateSnapshot {
	char state[16];
	uint64_t startedAtMs;
	uint32_t startedMs;
	bool localStartValid;
};

struct gridTracker {
	gridStateSnapshot snapshot = {};
	bool initialized = false;
};

struct pendingGridSnapshot {
	gridStateSnapshot snapshot = {};
	uint32_t revision = 0;
	bool pending = false;
};
static pendingGridSnapshot pendingSnapshots[4];
static portMUX_TYPE snapshotMutex = portMUX_INITIALIZER_UNLOCKED;
static uint8_t persistNextSlot = 0;
static bool persistRetry = false;
static uint32_t persistRetryAtMs = 0;

struct persistedGridState {
	uint32_t magic;
	uint8_t version;
	uint8_t valid;
	uint8_t reserved[2];
	char state[16];
	uint64_t startedAtMs;
};

static gridTracker trackers[4];
static uint8_t startupStates[4] = {};
static bool startupResolved = false;
static uint32_t startupGuardDeadlineMs = 0;
static bool startupGuardStarted = false;
// Copia text dentro de un campo fijo y garantiza su terminacion.
// dest/destSize describen el destino; text es el texto de origen.
static void copyText(char* dest, size_t destSize, const char* text)
{
	if (!destSize) return;
	strncpy(dest, text ? text : "", destSize - 1);
	dest[destSize - 1] = '\0';
}

// Convierte sampleMs monotonico a Unix ms, o devuelve cero sin reloj valido.
// sampleMs es el instante local que se quiere convertir.
static uint64_t unixTimestampAt(uint32_t sampleMs)
{
	const uint64_t now = managerNowMs();
	if (!now) return 0;
	const uint32_t elapsed = uint32_t(millis() - sampleMs);
	return now > elapsed ? now - elapsed : 0;
}

// Devuelve true cuando deadlineMs ya vencio, incluyendo el wrap de millis.
// deadlineMs es el limite monotonicamente calculado.
static bool deadlineReached(uint32_t deadlineMs)
{
	return int32_t(millis() - deadlineMs) >= 0;
}

// Guarda el ultimo estado conocido del slot indicado.
// slot identifica R, S, T o red general; snapshot contiene el estado.
static bool saveSnapshot(uint8_t slot, const gridStateSnapshot& snapshot)
{
	meterTimingScope timing(meterTimingPart::gridNvs);
	persistedGridState stored = {};
	stored.magic = persistedGridMagic;
	stored.version = 4;
	stored.valid = 1;
	stored.startedAtMs = snapshot.startedAtMs;
	copyText(stored.state, sizeof(stored.state), snapshot.state);
	Preferences prefs;
	if (!prefs.begin(gridStorageName, false)) return false;
	const bool saved = prefs.putBytes(gridStorageKeys[slot], &stored, sizeof(stored)) == sizeof(stored);
	prefs.end();
	return saved;
}

// Conserva solo la ultima linea base del slot; snapshot se copia sin acceder a NVS ni esperar una cola.
static void queueSnapshot(uint8_t slot, const gridStateSnapshot& snapshot)
{
	if (slot >= 4) return;
	portENTER_CRITICAL(&snapshotMutex);
	pendingGridSnapshot& entry = pendingSnapshots[slot];
	entry.snapshot = snapshot;
	++entry.revision;
	entry.pending = true;
	portEXIT_CRITICAL(&snapshotMutex);
}

// Persiste un solo slot por pasada; sin parametros, el loop principal es el unico consumidor.
void gridEventsPersistLoop()
{
	if (persistRetry && !deadlineReached(persistRetryAtMs)) return;
	persistRetry = false;
	for (uint8_t index = 0; index < 4; ++index) {
		const uint8_t slot = (persistNextSlot + index) % 4;
		gridStateSnapshot snapshot = {};
		uint32_t revision = 0;
		bool pending = false;
		portENTER_CRITICAL(&snapshotMutex);
		pending = pendingSnapshots[slot].pending;
		if (pending) {
			snapshot = pendingSnapshots[slot].snapshot;
			revision = pendingSnapshots[slot].revision;
		}
		portEXIT_CRITICAL(&snapshotMutex);
		if (!pending) continue;
		const bool saved = saveSnapshot(slot, snapshot);
		persistNextSlot = (slot + 1) % 4;
		if (saved) {
			portENTER_CRITICAL(&snapshotMutex);
			// No borrar un estado nuevo que MeterTask haya publicado mientras escribiamos el anterior.
			if (pendingSnapshots[slot].revision == revision) pendingSnapshots[slot].pending = false;
			portEXIT_CRITICAL(&snapshotMutex);
		} else {
			persistRetry = true;
			persistRetryAtMs = millis() + 1000U;
			debugW("No se pudo guardar estado grid %u; se reintentara", slot);
		}
		return;
	}
}

// Restaura el ultimo estado conocido del slot indicado.
// tracker recibe el estado; slot identifica R, S, T o red general.
static bool loadSnapshot(gridTracker& tracker, uint8_t slot)
{
	Preferences prefs;
	if (!prefs.begin(gridStorageName, true)) return false;
	persistedGridState stored = {};
	const bool validSize = prefs.getBytesLength(gridStorageKeys[slot]) == sizeof(stored);
	if (validSize) prefs.getBytes(gridStorageKeys[slot], &stored, sizeof(stored));
	prefs.end();
	if (!validSize || stored.magic != persistedGridMagic || stored.version != 4 || !stored.valid)
		return false;
	stored.state[sizeof(stored.state) - 1] = '\0';
	if (!stored.state[0]) return false;
	tracker = gridTracker{};
	tracker.initialized = true;
	tracker.snapshot.startedAtMs = stored.startedAtMs;
	copyText(tracker.snapshot.state, sizeof(tracker.snapshot.state), stored.state);
	return true;
}

// Actualiza RAM y solicita persistencia diferida para la referencia de la proxima transicion.
// slot identifica el tracker; state/startedMs describen el nuevo estado.
static void setCurrentState(uint8_t slot, const char* state, uint32_t startedMs)
{
	gridTracker& tracker = trackers[slot];
	tracker = gridTracker{};
	tracker.initialized = true;
	tracker.snapshot.startedMs = startedMs;
	tracker.snapshot.startedAtMs = unixTimestampAt(startedMs);
	tracker.snapshot.localStartValid = true;
	copyText(tracker.snapshot.state, sizeof(tracker.snapshot.state), state);
	queueSnapshot(slot, tracker.snapshot);
}

// Completa en NVS una fecha de inicio cuando NTP aparece durante el mismo arranque.
// slot identifica el tracker que se actualiza una sola vez.
static void refreshStartTimestamp(uint8_t slot)
{
	gridTracker& tracker = trackers[slot];
	if (!tracker.initialized || !tracker.snapshot.localStartValid ||
		tracker.snapshot.startedAtMs || !managerClockSynchronized()) return;
	tracker.snapshot.startedAtMs = unixTimestampAt(tracker.snapshot.startedMs);
	queueSnapshot(slot, tracker.snapshot);
}

// Registra una transicion de fase y avanza el tracker sin esperar MQTT.
// slot identifica R/S/T; phase contiene el estado medido.
static void updatePhase(uint8_t slot, const phaseState& phase)
{
	gridTracker& tracker = trackers[slot];
	const char* state = phaseStateName(phase.state);
	if (!tracker.initialized) {
		setCurrentState(slot, state, phase.changedAtMs);
		return;
	}
	if (!strcmp(tracker.snapshot.state, state)) {
		refreshStartTimestamp(slot);
		return;
	}
	setCurrentState(slot, state, phase.changedAtMs);
}

// Devuelve el instante mas reciente en que cambio alguna fase.
// phases contiene R/S/T y sampleMs se usa si no hay referencia valida.
static uint32_t latestPhaseChangeMs(const phaseState* phases[3], uint32_t sampleMs)
{
	uint32_t changedMs = sampleMs;
	uint32_t shortestAge = UINT32_MAX;
	for (uint8_t i = 0; i < 3; i++) {
		const uint32_t age = uint32_t(sampleMs - phases[i]->changedAtMs);
		if (age < shortestAge) {
			shortestAge = age;
			changedMs = phases[i]->changedAtMs;
		}
	}
	return changedMs;
}

// Registra una transicion general y avanza el tracker sin esperar MQTT.
// overall es el estado derivado; phases/sampleMs permiten estimar su inicio.
static void updateOverall(gridCondition overall, const phaseState* phases[3], uint32_t sampleMs)
{
	gridTracker& tracker = trackers[3];
	const char* state = gridStateName(overall);
	const uint32_t changedMs = latestPhaseChangeMs(phases, sampleMs);
	if (!tracker.initialized) {
		setCurrentState(3, state, changedMs);
		return;
	}
	if (!strcmp(tracker.snapshot.state, state)) {
		refreshStartTimestamp(3);
		return;
	}
	setCurrentState(3, state, changedMs);
}

void gridEventsInit()
{
	for (uint8_t i = 0; i < 4; i++) {
		trackers[i] = gridTracker{};
		startupStates[i] = 0;
		if (!loadSnapshot(trackers[i], i)) continue;
		for (uint8_t state = 1; state <= (i == 3 ? 3 : 4); ++state) {
			const char* name = i == 3 ? gridStateName(gridCondition(state)) : phaseStateName(phaseCondition(state));
			if (!strcmp(trackers[i].snapshot.state, name)) startupStates[i] = state;
		}
	}
}

void gridEventsUpdate(const phaseState& r, const phaseState& s, const phaseState& t,
	gridCondition overall, float vr, float vs, float vt, uint32_t sampleMs)
{
	const phaseState* phases[] = { &r, &s, &t };
	if (r.state == phaseCondition::unknown || s.state == phaseCondition::unknown ||
		t.state == phaseCondition::unknown || overall == gridCondition::unknown) return;
	if (!startupResolved) {
		if (!startupGuardStarted) {
			startupGuardDeadlineMs = sampleMs + gridStartupGuardMs;
			startupGuardStarted = true;
			return;
		}
		if (!deadlineReached(startupGuardDeadlineMs)) return;
		for (uint8_t slot = 0; slot < 3; ++slot)
			setCurrentState(slot, phaseStateName(phases[slot]->state), sampleMs);
		setCurrentState(3, gridStateName(overall), sampleMs);
		startupResolved = true;
		return;
	}
	updatePhase(0, r);
	updatePhase(1, s);
	updatePhase(2, t);
	updateOverall(overall, phases, sampleMs);
}

// Devuelve si los estados iniciales ya fueron establecidos sin generar eventos de arranque.
bool gridEventsReadyForStorage()
{
	return startupResolved;
}

// Devuelve la linea base NVS del arranque; slot identifica R, S, T o red.
uint8_t gridEventsStartupState(uint8_t slot)
{
	return slot < 4 ? startupStates[slot] : 0;
}
