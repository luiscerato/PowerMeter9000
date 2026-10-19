#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct bl0942Status {
    uint32_t baud = 0;
    uint32_t fastRaw = 0;
    uint32_t voltageRaw = 0; // I_RMS: el canal de corriente mide ahora la tension auxiliar.
    uint32_t fastSampleUs = 0;
    uint32_t voltageSampleUs = 0;
    uint32_t fastReads = 0;
    uint32_t voltageReads = 0;
    uint32_t checksumErrors = 0;
    uint32_t timeouts = 0;
    uint32_t unexpectedBytes = 0;
    uint32_t rxBytes = 0, partialResponses = 0;
    uint32_t fastRequests = 0, rmsRequests = 0, completeResponses = 0;
    uint32_t emptyTimeouts = 0, lateResponses = 0, maxServiceGapUs = 0;
    uint32_t txBusy = 0;
    uint32_t missedPolls = 0;
    uint32_t maxLatenessUs = 0;
    uint32_t maxResponseUs = 0;
    uint32_t maxGapUs = 0;
    float voltage = NAN;
    float fastVoltage = NAN;
    bool ready = false;
};

struct neutralCalibration {
    const char* status = "idle";
    const char* error = "";
    uint8_t phase = 0;
    uint32_t samples = 0, rmsSamples = 0;
    float reference = 0;
    float rmsFactor = 1, fastFactor = 1;
};

class BL0942 : private HardwareSerial {
public:
    // Selecciona el periferico UART; serialPortNumber identifica la UART del ESP32.
    explicit BL0942(uint8_t serialPortNumber) : HardwareSerial(serialPortNumber) {}

    // Detecta la UART y configura el chip antes de iniciar MeterTask; rx y tx son los pines.
    void begin(int32_t rx, int32_t tx);

    // Atiende UART sin bloquear; allowSlowRead permite intercalar RMS comun despues de Fast.
    bool loop(bool allowSlowRead = true);

    // Inicia el turno disparado por el ADE; sin parametros, no espera la respuesta UART.
    bool startFastSample();
    // Indica si el turno actual sigue esperando; sin parametros, solo MeterTask.
    bool isFastSamplePending() const { return fastCyclePending; }
    // Devuelve exclusivamente el resultado del turno actual; sin parametros, NAN si fallo.
    float getCycleFastVoltage() const { return fastCycleVoltage; }

    // Devuelve la espera hasta el proximo servicio UART; sin parametros, usar solo desde MeterTask.
    uint32_t timeUntilPollUs() const;

    // Devuelve I_RMS escalado a voltios; sin parametros, usar desde MeterTask.
    float getVoltage() const { return state.voltage; }

    // Devuelve la estimacion rapida con la escala actual; sin parametros, usar desde MeterTask.
    float getFastVoltage() const { return state.fastVoltage; }

    // Verifica antiguedad maxima maxAgeUs de la ultima muestra; usar solo desde MeterTask.
    bool hasFastSample(uint32_t maxAgeUs) const {
        return state.fastReads && uint32_t(micros() - state.fastSampleUs) <= maxAgeUs;
    }

    // Copia valores y contadores para diagnostico desde cualquier tarea; sin parametros.
    bl0942Status getStatus();

    // Informa cambios de conexion y metricas verbose desde el loop principal; sin parametros.
    void pollDiagnostics();

    // Solicita calibracion asincrona; phase selecciona R=0, S=1 o T=2.
    bool startCalibration(uint8_t phase);
    // Copia el resultado y los factores vigentes; sin parametros, admite cualquier tarea.
    neutralCalibration getCalibration();
    // Acumula un segundo desde MeterTask; r/s/t son los voltajes RMS actuales del ADE.
    void pollCalibration(float r, float s, float t);

private:
    // Configura UART y recepcion inmediata de tramas cortas; baud es la velocidad requerida.
    void beginUart(uint32_t baud);
    // Reintenta deteccion y configuracion sin esperar respuestas; nowUs es el tiempo actual.
    void pollInitialization(uint32_t nowUs);
    // Reinicia la deteccion tras fallos; nowUs fija la pausa antes del siguiente intento.
    void retryInitialization(uint32_t nowUs);
    // Aplica configuracion; rx/tx son pines y currentBaud es la velocidad detectada.
    bool setup(int32_t rx, int32_t tx, uint32_t currentBaud);
    // Escribe reg con value durante el arranque; no usar desde MeterTask.
    void writeReg(uint8_t reg, uint32_t value);
    // Lee reg durante el arranque; devuelve -1 si no hay respuesta valida.
    int32_t readReg(uint8_t reg);
    // Solicita reg sin esperar; nowUs es el inicio de la transaccion.
    bool requestReg(uint8_t reg, uint32_t nowUs);
    // Valida la respuesta pendiente; nowUs es el instante en que se procesa.
    bool acceptResponse(uint32_t nowUs);
    // Recupera la UART despues de un fallo; nowUs indica cuando ocurrio.
    void recover(uint32_t nowUs);
    // Copia el estado privado a una instantanea protegida; sin parametros.
    void publishState();
    // Guarda los factores desde el loop principal; sin parametros, no bloquea MeterTask.
    void saveCalibration();

    // Conserva la escala anterior como referencia para IP a 4x; pendiente calibrar el divisor real.
    float voltageReference = 15873.35944299f;
    bl0942Status state, publishedState;
    portMUX_TYPE stateMutex = portMUX_INITIALIZER_UNLOCKED;
    uint32_t nextFastUs = 0;
    uint32_t nextVoltageUs = 0;
    uint32_t lastServiceUs = 0;
    uint32_t requestUs = 0;
    uint32_t recoveryUntilUs = 0;
    uint32_t diagnosticTimeMs = 0;
    uint32_t diagnosticFastReads = 0;
    uint32_t diagnosticVoltageReads = 0;
    uint8_t pendingRegister = 0;
    uint8_t response[4] = {};
    uint8_t responseLength = 0;
    bool recovering = false;
    bool diagnosticReady = false;
    bool fastCyclePending = false, slowSlotOpen = false;
    float fastCycleVoltage = NAN;
    int32_t uartRx = -1, uartTx = -1;
    uint32_t initializationDueUs = 0;
    uint8_t initializationStep = 0, baudIndex = 0, consecutiveFailures = 0;
    neutralCalibration calibration;
    uint32_t calibrationStartUs = 0, calibrationFastReads = 0, calibrationRmsReads = 0;
    double calibrationFastSum = 0, calibrationRmsSum = 0, calibrationReferenceSum = 0;
    float referenceMin = 0, referenceMax = 0;
    uint32_t fastMin = 0, fastMax = 0;
    float rmsFactor = 1, fastFactor = 1;
    float pendingRmsFactor = 1, pendingFastFactor = 1;
};
