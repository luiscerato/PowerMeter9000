#include "bl0942.h"
#include "wifiUtils.h"
#include <Preferences.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <driver/uart.h>

static constexpr uint8_t readCommand = 0x58;
static constexpr uint8_t writeCommand = 0xA8;
static constexpr uint8_t fastRmsRegister = 0x05;
static constexpr uint8_t currentRmsRegister = 0x03;
static constexpr uint8_t antiCreepRegister = 0x14;
static constexpr uint8_t fastCyclesRegister = 0x16;
static constexpr uint8_t modeRegister = 0x19;
static constexpr uint8_t gainRegister = 0x1A;
static constexpr uint8_t writeProtectRegister = 0x1D;
static constexpr uint32_t modeValue = 0x397; // 38400 baud, 50 Hz, RMS 400 ms y Fast RMS despues del HPF.
static constexpr uint32_t fastIntervalUs = 10000;
static constexpr uint32_t voltageIntervalUs = 400000;
static constexpr uint32_t responseTimeoutUs = 3000;
static constexpr uint32_t initializationTimeoutUs = 20000;
static constexpr uint32_t recoveryIntervalUs = 2000;
static constexpr uint32_t slowReadBudgetUs = 4000;
static constexpr float currentVoltageRatio = (305978.0f / 73989.0f) * (4.0f / 16.0f);
static constexpr float fastVoltageRatio = 0.363f * currentVoltageRatio;

// Publica bytes sin esperar que se llene la FIFO; baud selecciona la velocidad de deteccion/lectura.
void BL0942::beginUart(uint32_t baud)
{
    HardwareSerial::begin(baud, SERIAL_8N1, uartRx, uartTx);
    const uart_port_t port = static_cast<uart_port_t>(_uart_nr);
    // Este core arranca en RS485 half-duplex; BL usa RX/TX separados y responde sin guarda larga.
    uart_set_mode(port, UART_MODE_UART);
    uart_set_rx_full_threshold(port, 1);
    uart_set_rx_timeout(port, 1);
}

// Detecta la UART, configura el chip y prepara los plazos; rx y tx son los pines del ESP32.
void BL0942::begin(int32_t rx, int32_t tx)
{
    uartRx = rx;
    uartTx = tx;
    state = {};
    Preferences options;
    if (options.begin("ADE9000", false)) {
        calibration.rmsFactor = options.getFloat("neutralRms", 1);
        calibration.fastFactor = options.getFloat("neutralFast", 1);
        if (!isfinite(calibration.rmsFactor) || calibration.rmsFactor <= 0) calibration.rmsFactor = 1;
        if (!isfinite(calibration.fastFactor) || calibration.fastFactor <= 0) calibration.fastFactor = 1;
        // Materializa tambien los valores iniciales para incluirlos en el respaldo de calibraciones.
        if (!options.isKey("neutralRms")) options.putFloat("neutralRms", calibration.rmsFactor);
        if (!options.isKey("neutralFast")) options.putFloat("neutralFast", calibration.fastFactor);
        options.end();
    }
    pendingRegister = 0;
    responseLength = 0;
    recovering = false;
    setTimeout(30);
    const uint32_t baudRates[] = {38400, 4800, 9600, 19200};
    uint32_t detectedBaud = 0;
    for (uint32_t baud : baudRates) {
        beginUart(baud);
        while (available()) { read(); ++state.rxBytes; ++state.unexpectedBytes; }
        if (readReg(modeRegister) >= 0) {
            detectedBaud = baud;
            break;
        }
    }
    if (detectedBaud) state.ready = setup(rx, tx, detectedBaud);
    nextFastUs = nextVoltageUs = micros();
    diagnosticTimeMs = millis();
    diagnosticReady = state.ready;
    publishState();
    if (state.ready)
        debugI("BL0942 listo: baud=38400 ganancia=4x Fast RMS=10ms HPF=on I_RMS=400ms VP=ignorado");
    else
        debugE("BL0942 sin respuesta o configuracion no verificada");
    if (!state.ready) retryInitialization(micros());
}

// Programa otra deteccion sin borrar las metricas; nowUs es el instante del fallo.
void BL0942::retryInitialization(uint32_t nowUs)
{
    state.ready = false;
    state.voltage = state.fastVoltage = NAN;
    pendingRegister = responseLength = 0;
    recovering = false;
    initializationStep = 0;
    initializationDueUs = nowUs + 250000U;
    consecutiveFailures = 0;
    fastCyclePending = slowSlotOpen = false;
    fastCycleVoltage = NAN;
}

// Sincroniza una consulta Fast con el ADE; sin parametros, descarta un turno anterior incompleto.
bool BL0942::startFastSample()
{
    slowSlotOpen = false;
    loop(false);
    slowSlotOpen = false;
    const uint32_t nowUs = micros();
    nextFastUs = nowUs + fastIntervalUs;
    fastCycleVoltage = NAN;
    fastCyclePending = false;
    // Los turnos ADE no deben cancelar una deteccion/configuracion UART en curso.
    if (!state.ready) return false;
    if (pendingRegister) {
        ++state.missedPolls;
        recover(nowUs);
    }
    if (recovering || available()) return false;
    fastCyclePending = requestReg(fastRmsRegister, nowUs);
    return fastCyclePending;
}

// Avanza una operacion UART por pasada; nowUs controla pausas sin bloquear MeterTask.
void BL0942::pollInitialization(uint32_t nowUs)
{
    if (pendingRegister || int32_t(nowUs - initializationDueUs) < 0) return;
    const uint32_t baudRates[] = {38400, 4800, 9600, 19200};
    if (initializationStep == 0) {
        state.baud = baudRates[baudIndex];
        baudIndex = (baudIndex + 1) % 4;
        beginUart(state.baud);
        initializationStep = 1;
        initializationDueUs = nowUs + 20000U;
        return;
    }
    if (initializationStep == 1 || initializationStep >= 9) {
        const uint8_t registers[] = {modeRegister, gainRegister, fastCyclesRegister, antiCreepRegister};
        requestReg(initializationStep == 1 ? modeRegister : registers[initializationStep - 9], nowUs);
        return;
    }
    if (initializationStep == 4) {
        beginUart(38400);
        state.baud = 38400;
    } else {
        const uint8_t registers[] = {writeProtectRegister, modeRegister, 0, gainRegister,
            fastCyclesRegister, antiCreepRegister, writeProtectRegister};
        const uint32_t values[] = {0x55, modeValue, 0, 1, 0, 0, 0};
        const uint8_t index = initializationStep - 2;
        uint8_t packet[] = {writeCommand, registers[index], uint8_t(values[index]),
            uint8_t(values[index] >> 8), uint8_t(values[index] >> 16), 0};
        packet[5] = uint8_t(~(packet[0] + packet[1] + packet[2] + packet[3] + packet[4]));
        if (availableForWrite() < int(sizeof(packet))) return;
        if (write(packet, sizeof(packet)) != sizeof(packet)) {
            retryInitialization(nowUs);
            return;
        }
    }
    ++initializationStep;
    initializationDueUs = nowUs + 20000U;
}

// Aplica y verifica ganancia 4x y medio ciclo; rx/tx son pines y currentBaud es el baud detectado.
bool BL0942::setup(int32_t rx, int32_t tx, uint32_t currentBaud)
{
    writeReg(writeProtectRegister, 0x55);
    writeReg(modeRegister, modeValue);
    if (currentBaud != 38400) beginUart(38400);
    state.baud = 38400;
    writeReg(gainRegister, 0x01);
    writeReg(fastCyclesRegister, 0x00);
    // VP esta desconectado: el detector de potencia sin carga no debe anular I_RMS.
    writeReg(antiCreepRegister, 0);
    writeReg(writeProtectRegister, 0);
    return readReg(modeRegister) == int32_t(modeValue) &&
        readReg(gainRegister) == 0x01 && readReg(fastCyclesRegister) == 0 &&
        readReg(antiCreepRegister) == 0;
}

// Escribe un registro durante el arranque; reg es su direccion y value sus 24 bits de datos.
void BL0942::writeReg(uint8_t reg, uint32_t value)
{
    uint8_t packet[] = {writeCommand, reg, uint8_t(value), uint8_t(value >> 8), uint8_t(value >> 16), 0};
    packet[5] = uint8_t(~(packet[0] + packet[1] + packet[2] + packet[3] + packet[4]));
    write(packet, sizeof(packet));
    flush();
    delay(1);
}

// Lee y valida reg solo durante el arranque; devuelve -1 si falta algun byte o falla el checksum.
int32_t BL0942::readReg(uint8_t reg)
{
    const uint8_t request[] = {readCommand, reg};
    uint8_t bytes[4] = {};
    while (available()) { read(); ++state.rxBytes; ++state.unexpectedBytes; }
    write(request, sizeof(request));
    flush();
    const size_t received = readBytes(bytes, sizeof(bytes));
    state.rxBytes += received;
    if (received && received < sizeof(bytes)) ++state.partialResponses;
    if (received != sizeof(bytes)) {
        ++state.timeouts;
        return -1;
    }
    if (bytes[3] != uint8_t(~(readCommand + reg + bytes[0] + bytes[1] + bytes[2]))) {
        ++state.checksumErrors;
        return -1;
    }
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16);
}

// Envia reg sin esperar respuesta; nowUs marca el comienzo de la transaccion.
bool BL0942::requestReg(uint8_t reg, uint32_t nowUs)
{
    if (availableForWrite() < 2) {
        ++state.txBusy;
        return false;
    }
    const uint8_t request[] = {readCommand, reg};
    if (write(request, sizeof(request)) != sizeof(request)) {
        ++state.txBusy;
        recover(nowUs);
        return false;
    }
    pendingRegister = reg;
    requestUs = nowUs;
    responseLength = 0;
    if (state.ready) {
        if (reg == fastRmsRegister) ++state.fastRequests;
        else ++state.rmsRequests;
    }
    return true;
}

// Deja vencer el estado de la UART tras un error; nowUs es el instante del fallo.
void BL0942::recover(uint32_t nowUs)
{
    if (pendingRegister == fastRmsRegister) {
        fastCyclePending = false;
        fastCycleVoltage = NAN;
    }
    slowSlotOpen = false;
    if (!state.ready || ++consecutiveFailures >= 8) {
        retryInitialization(nowUs);
        return;
    }
    pendingRegister = 0;
    responseLength = 0;
    recovering = true;
    recoveryUntilUs = nowUs + recoveryIntervalUs;
}

// Procesa una respuesta completa de pendingRegister; nowUs es su instante de recepcion por la tarea.
bool BL0942::acceptResponse(uint32_t nowUs)
{
    const uint8_t checksum = uint8_t(~(readCommand + pendingRegister + response[0] + response[1] + response[2]));
    if (checksum != response[3]) {
        ++state.checksumErrors;
        recover(nowUs);
        return false;
    }
    const uint32_t value = uint32_t(response[0]) | (uint32_t(response[1]) << 8) | (uint32_t(response[2]) << 16);
    consecutiveFailures = 0;
    if (!state.ready) {
        pendingRegister = responseLength = 0;
        if (initializationStep == 1) initializationStep = 2;
        else {
            const uint32_t expected[] = {modeValue, 1, 0, 0};
            if (value != expected[initializationStep - 9]) {
                retryInitialization(nowUs);
                return false;
            }
            if (++initializationStep == 13) {
                state.ready = true;
                nextFastUs = nextVoltageUs = nowUs;
            }
        }
        initializationDueUs = nowUs + 20000U;
        return false;
    }
    const uint32_t latency = nowUs - requestUs;
    ++state.completeResponses;
    if (latency >= responseTimeoutUs) ++state.lateResponses;
    if (latency > state.maxResponseUs) state.maxResponseUs = latency;
    if (pendingRegister == fastRmsRegister) {
        if (state.fastReads) {
            const uint32_t gap = nowUs - state.fastSampleUs;
            if (gap > state.maxGapUs) state.maxGapUs = gap;
        }
        state.fastRaw = value;
        state.fastVoltage = value * fastFactor / (voltageReference * fastVoltageRatio);
        state.fastSampleUs = nowUs;
        ++state.fastReads;
        fastCycleVoltage = state.fastVoltage;
        fastCyclePending = false;
        slowSlotOpen = true;
    } else {
        state.voltageRaw = value;
        state.voltage = value * rmsFactor / (voltageReference * currentVoltageRatio);
        state.voltageSampleUs = nowUs;
        ++state.voltageReads;
    }
    pendingRegister = 0;
    responseLength = 0;
    return true;
}

// Atiende recepcion y plazos desde MeterTask sin esperar bytes; devuelve true si hay una nueva medicion.
bool BL0942::loop(bool allowSlowRead)
{
    taskENTER_CRITICAL(&stateMutex);
    rmsFactor = calibration.rmsFactor;
    fastFactor = calibration.fastFactor;
    taskEXIT_CRITICAL(&stateMutex);
    bool updated = false;
    uint32_t nowUs = micros();
    if (pendingRegister) {
        const uint32_t serviceGap = nowUs - lastServiceUs;
        if (lastServiceUs && serviceGap > state.maxServiceGapUs) state.maxServiceGapUs = serviceGap;
    }
    lastServiceUs = nowUs;
    // Una respuesta completa ya recibida sigue perteneciendo a esta unica consulta pendiente.
    // El plazo limita esperar bytes faltantes, no procesar los que ya estan en UART.
    uint8_t budget = 32;
    while (budget-- && available()) {
        const int byte = read();
        if (byte < 0) break;
        ++state.rxBytes;
        if (!pendingRegister) {
            ++state.unexpectedBytes;
            continue;
        }
        response[responseLength++] = uint8_t(byte);
        if (responseLength == sizeof(response)) updated |= acceptResponse(micros());
    }
    nowUs = micros();
    const uint32_t timeoutUs = state.ready ? responseTimeoutUs : initializationTimeoutUs;
    if (pendingRegister && uint32_t(nowUs - requestUs) >= timeoutUs) {
        ++state.timeouts;
        if (responseLength) ++state.partialResponses;
        else ++state.emptyTimeouts;
        recover(nowUs);
    }
    if (recovering && int32_t(nowUs - recoveryUntilUs) >= 0) recovering = false;
    // No publicar indefinidamente el ultimo valor si el chip deja de responder.
    if (isfinite(state.fastVoltage) && uint32_t(nowUs - state.fastSampleUs) > 50000U) {
        state.fastVoltage = NAN;
        updated = true;
    }
    if (isfinite(state.voltage) && uint32_t(nowUs - state.voltageSampleUs) > 1200000U) {
        state.voltage = NAN;
        updated = true;
    }
    if (!state.ready) pollInitialization(nowUs);
    if (allowSlowRead && state.ready && slowSlotOpen && !fastCyclePending && !pendingRegister && !recovering && !available()) {
        if (int32_t(nowUs - nextFastUs) < 0 && int32_t(nowUs - nextVoltageUs) >= 0 &&
            nextFastUs - nowUs >= slowReadBudgetUs) {
            if (requestReg(currentRmsRegister, nowUs))
                nextVoltageUs += ((nowUs - nextVoltageUs) / voltageIntervalUs + 1) * voltageIntervalUs;
            slowSlotOpen = false;
        }
    }
    publishState();
    return updated;
}

// Devuelve cuanto puede dormir MeterTask; sin parametros, sondea a 1 ms mientras espera la UART.
uint32_t BL0942::timeUntilPollUs() const
{
    return !state.ready || pendingRegister || recovering ? 1000 : fastIntervalUs;
}

// Publica una copia coherente de valores y contadores para otras tareas; sin parametros.
void BL0942::publishState()
{
    taskENTER_CRITICAL(&stateMutex);
    publishedState = state;
    taskEXIT_CRITICAL(&stateMutex);
}

// Devuelve una copia de valores y contadores sin acceder a la UART; sin parametros.
bl0942Status BL0942::getStatus()
{
    taskENTER_CRITICAL(&stateMutex);
    const bl0942Status snapshot = publishedState;
    taskEXIT_CRITICAL(&stateMutex);
    return snapshot;
}

// Informa conexion y metricas verbose; sin parametros, nunca imprime desde MeterTask.
void BL0942::pollDiagnostics()
{
    saveCalibration();
    const uint32_t nowMs = millis();
    const bl0942Status snapshot = getStatus();
    if (snapshot.ready != diagnosticReady) {
        diagnosticReady = snapshot.ready;
        if (snapshot.ready) debugI("BL0942 comunicacion recuperada: 38400 baud, IP 4x");
        else debugW("BL0942 sin comunicacion: sin datos, reintentando deteccion");
    }
    const uint32_t elapsed = nowMs - diagnosticTimeMs;
    if (elapsed < 5000) return;
    diagnosticTimeMs = nowMs;
    debugV("BL0942 IP %ums: ready=%u rx=%u partial=%u fast=%u slow=%u U~%.3fV rmsRaw=%u fastRaw=%u fastV~%.3fV crc=%u timeout=%u missedTotal=%u txBusy=%u junk=%u lateMax=%uus gapMax=%uus responseMax=%uus",
        elapsed, snapshot.ready, snapshot.rxBytes, snapshot.partialResponses, snapshot.fastReads - diagnosticFastReads, snapshot.voltageReads - diagnosticVoltageReads,
        snapshot.voltage, snapshot.voltageRaw, snapshot.fastRaw, snapshot.fastVoltage, snapshot.checksumErrors, snapshot.timeouts,
        snapshot.missedPolls, snapshot.txBusy, snapshot.unexpectedBytes, snapshot.maxLatenessUs,
        snapshot.maxGapUs, snapshot.maxResponseUs);
    debugV("BL0942 UART: fastTx=%u rmsTx=%u validRx=%u emptyTimeout=%u lateRx=%u serviceGapMax=%uus",
        snapshot.fastRequests, snapshot.rmsRequests, snapshot.completeResponses, snapshot.emptyTimeouts,
        snapshot.lateResponses, snapshot.maxServiceGapUs);
    diagnosticFastReads = snapshot.fastReads;
    diagnosticVoltageReads = snapshot.voltageReads;
}

// Devuelve una instantanea del comando y sus resultados; sin parametros.
neutralCalibration BL0942::getCalibration()
{
    taskENTER_CRITICAL(&stateMutex);
    const neutralCalibration result = calibration;
    taskEXIT_CRITICAL(&stateMutex);
    return result;
}

// Encola una captura sin tocar UART ni NVS; phase identifica la fase de referencia del ADE.
bool BL0942::startCalibration(uint8_t phase)
{
    taskENTER_CRITICAL(&stateMutex);
    const bool busy = !strcmp(calibration.status, "queued") || !strcmp(calibration.status, "sampling") ||
        !strcmp(calibration.status, "saving");
    const bool accepted = publishedState.ready && phase < 3 && !busy;
    if (accepted) {
        calibration.status = "queued";
        calibration.error = "";
        calibration.phase = phase;
        calibration.samples = calibration.rmsSamples = 0;
        calibration.reference = 0;
    }
    taskEXIT_CRITICAL(&stateMutex);
    return accepted;
}

// Procesa solo muestras nuevas durante un segundo; r/s/t son RMS del ADE en voltios.
void BL0942::pollCalibration(float r, float s, float t)
{
    neutralCalibration result = getCalibration();
    const uint32_t nowUs = micros();
    if (!strcmp(result.status, "queued")) {
        calibrationStartUs = nowUs;
        calibrationFastReads = state.fastReads;
        calibrationRmsReads = state.voltageReads;
        calibrationFastSum = calibrationRmsSum = calibrationReferenceSum = 0;
        referenceMin = FLT_MAX;
        referenceMax = 0;
        fastMin = UINT32_MAX;
        fastMax = 0;
        result.status = "sampling";
    } else if (strcmp(result.status, "sampling")) return;
    const float references[] = {r, s, t};
    const float reference = references[result.phase];
    if (!isfinite(reference) || reference < 50 || reference > 300) {
        result.status = "error";
        result.error = "invalid_reference";
    } else {
        if (state.fastReads != calibrationFastReads) {
            calibrationFastReads = state.fastReads;
            calibrationFastSum += state.fastRaw;
            calibrationReferenceSum += reference;
            referenceMin = fminf(referenceMin, reference);
            referenceMax = fmaxf(referenceMax, reference);
            if (state.fastRaw < fastMin) fastMin = state.fastRaw;
            if (state.fastRaw > fastMax) fastMax = state.fastRaw;
            ++result.samples;
        }
        if (state.voltageReads != calibrationRmsReads) {
            calibrationRmsReads = state.voltageReads;
            calibrationRmsSum += state.voltageRaw;
            ++result.rmsSamples;
        }
        if (uint32_t(nowUs - calibrationStartUs) >= 1000000U) {
            result.status = "error";
            if (result.samples < 80 || result.rmsSamples < 2) result.error = "insufficient_samples";
            else {
                result.reference = calibrationReferenceSum / result.samples;
                const double fastMean = calibrationFastSum / result.samples;
                const double rmsMean = calibrationRmsSum / result.rmsSamples;
                if (fastMean < 1000 || rmsMean < 1000) result.error = "missing_signal";
                else if (referenceMax - referenceMin > result.reference * 0.02f ||
                    fastMax - fastMin > fastMean * 0.05) result.error = "unstable_signal";
                else {
                    // Factores absolutos: recalibrar no acumula correcciones anteriores.
                    result.rmsFactor = result.reference * voltageReference * currentVoltageRatio / rmsMean;
                    result.fastFactor = result.reference * voltageReference * fastVoltageRatio / fastMean;
                    if (result.rmsFactor < 0.1f || result.rmsFactor > 10 ||
                        result.fastFactor < 0.1f || result.fastFactor > 10) result.error = "invalid_factors";
                    else result.status = "saving";
                }
            }
        }
    }
    taskENTER_CRITICAL(&stateMutex);
    // Los factores activos solo cambian despues de confirmar el guardado en NVS.
    const float oldRms = calibration.rmsFactor, oldFast = calibration.fastFactor;
    if (!strcmp(result.status, "saving")) {
        pendingRmsFactor = result.rmsFactor;
        pendingFastFactor = result.fastFactor;
    }
    calibration = result;
    calibration.rmsFactor = oldRms;
    calibration.fastFactor = oldFast;
    taskEXIT_CRITICAL(&stateMutex);
}

// Persiste una captura terminada desde el loop principal; sin parametros.
void BL0942::saveCalibration()
{
    neutralCalibration result = getCalibration();
    if (strcmp(result.status, "saving")) return;
    taskENTER_CRITICAL(&stateMutex);
    result.rmsFactor = pendingRmsFactor;
    result.fastFactor = pendingFastFactor;
    taskEXIT_CRITICAL(&stateMutex);
    Preferences options;
    bool saved = false;
    if (options.begin("ADE9000", false)) {
        saved = options.putFloat("neutralRms", result.rmsFactor) == sizeof(float) &&
            options.putFloat("neutralFast", result.fastFactor) == sizeof(float);
        options.end();
    }
    taskENTER_CRITICAL(&stateMutex);
    calibration.status = saved ? "done" : "error";
    calibration.error = saved ? "" : "nvs_write_failed";
    if (saved) {
        calibration.rmsFactor = result.rmsFactor;
        calibration.fastFactor = result.fastFactor;
    }
    taskEXIT_CRITICAL(&stateMutex);
    debugI("BL0942 calibracion: %s ref=%.3f rmsFactor=%.7f fastFactor=%.7f muestras=%u/%u",
        saved ? "guardada" : "fallo NVS", result.reference, result.rmsFactor, result.fastFactor,
        result.samples, result.rmsSamples);
}
