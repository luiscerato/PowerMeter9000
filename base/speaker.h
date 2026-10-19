#pragma once

#include <Arduino.h>

// Destino abstracto de audio. Permite reemplazar el reproductor fisico.
class Speaker {
public:
    static constexpr uint16_t defaultVolume = 512;
    static constexpr size_t maxRtttlLength = 512;

    virtual ~Speaker() = default;

    // Inicializa el destino de audio.
    // pin indica la salida fisica usada por la implementacion.
    virtual bool begin(uint8_t pin) { (void)pin; return false; }

    // Ejecuta tareas periodicas del destino de audio.
    virtual void run() {}

    // Reproduce una melodia RTTTL.
    // rtttl contiene la melodia y repeat indica si debe reiniciar al terminar.
    virtual bool playRtttl(const char* rtttl, bool repeat = false) = 0;

    // Detiene la reproduccion actual.
    virtual void stop() {}

    // Devuelve true si hay una melodia en reproduccion.
    virtual bool isPlaying() const { return false; }

    // Devuelve true si hay una melodia configurada en repeticion.
    virtual bool isRepeating() const { return false; }

    // Configura si el destino fisico esta habilitado.
    // enabled define si se permite reproducir sonido local.
    virtual void setEnabled(bool enabled);

    // Devuelve true si el destino fisico esta habilitado.
    virtual bool isEnabled() const;

    // Configura el volumen por defecto.
    // volume es el nivel usado por la implementacion concreta.
    virtual void setVolume(uint16_t volume);

    // Devuelve el volumen por defecto.
    virtual uint16_t getVolume() const;

protected:
    bool enabled_ = true;
    uint16_t volume_ = defaultVolume;
};

// Efectos cortos pensados para feedback de UI o estados del sistema.
struct SpeakerSounds {
    static constexpr const char* escape = "escape:d=32,o=6,b=240:e,c";
    static constexpr const char* clickHigh = "up:d=32,o=7,b=300:c";
    static constexpr const char* clickLow = "down:d=32,o=6,b=300:b";
    static constexpr const char* left = "left:d=32,o=6,b=260:c,g";
    static constexpr const char* right = "right:d=32,o=6,b=260:g,c7";
    static constexpr const char* accept = "enter:d=32,o=6,b=260:g,c7,e7";
    static constexpr const char* endOfList = "end:d=16,o=6,b=220:c,p,c";
    static constexpr const char* limitLow = "low:d=32,o=5,b=220:g,e,c";
    static constexpr const char* limitHigh = "high:d=32,o=7,b=220:c,e,g";
    static constexpr const char* saved = "saved:d=16,o=6,b=220:c,e,g,c7";
    static constexpr const char* warning = "warning:d=16,o=6,b=190:g,p,g,e";
    static constexpr const char* error = "error:d=16,o=6,b=180:c,p,c,g5";
    static constexpr const char* alarm = "alarm:d=8,o=6,b=180:c,p,c,p,g5,p,g5,p";
    static constexpr const char* criticalAlarm = "critical:d=16,o=6,b=220:c,p,c,p,c,p,g5,p,g5";
    static constexpr const char* connected = "connected:d=16,o=6,b=220:c,e,g";
    static constexpr const char* disconnected = "disconnected:d=16,o=5,b=180:g,e,c";
    static constexpr const char* startup = "startup:d=16,o=6,b=220:c,e,g,c7";
    static constexpr const char* shutdown = "shutdown:d=16,o=6,b=180:c7,g,e,c";
    static constexpr const char* notification = "notify:d=16,o=6,b=220:e,g";
    static constexpr const char* confirm = "confirm:d=16,o=6,b=260:g,c7";
    static constexpr const char* cancel = "cancel:d=16,o=6,b=220:e,c";
};

// Melodias mas largas para pruebas, avisos o usos especificos de la app.
struct SpeakerMelodies {
    static constexpr const char* odeToJoy = "ode:d=4,o=5,b=120:e,e,f,g,g,f,e,d,c,c,d,e,e.,8d,2d";
    static constexpr const char* twinkle = "twinkle:d=4,o=5,b=100:c,c,g,g,a,a,2g,f,f,e,e,d,d,2c";
    static constexpr const char* londonBridge = "bridge:d=4,o=5,b=120:g,a,g,f,e,f,g,d,e,f,e,f,g,g,a,g,f,e,f,g,d,g,e,c";
    static constexpr const char* weddingMarch = "wedding:d=4,o=5,b=100:c,f,f,f,c,g,e,f,g,a";
    static constexpr const char* charge = "charge:d=8,o=5,b=160:g,c6,e6,g6,4e6,4g6";
    static constexpr const char* shaveAndHaircut = "shave:d=8,o=5,b=140:c,g,g,a,g,4b,4c6";
    static constexpr const char* scaleUp = "scaleup:d=16,o=5,b=180:c,d,e,f,g,a,b,c6";
    static constexpr const char* scaleDown = "scaledown:d=16,o=5,b=180:c6,b,a,g,f,e,d,c";
};

class RtttlSpeaker : public Speaker {
public:
    RtttlSpeaker();
    explicit RtttlSpeaker(uint8_t pin);
    RtttlSpeaker(uint8_t pin, uint8_t channel);
    ~RtttlSpeaker() override;

    // Inicializa el reproductor RTTTL en el proximo canal disponible.
    // pin indica la salida PWM usada para audio.
    bool begin(uint8_t pin) override;

    // Inicializa el reproductor RTTTL en un canal especifico.
    // pin indica la salida PWM y channel el canal LEDC.
    bool begin(uint8_t pin, int8_t channel);

    // Devuelve true si el backend PWM quedo listo.
    bool isReady() const;

    // Avanza la reproduccion RTTTL no bloqueante.
    void run() override;

    // Reproduce una melodia RTTTL.
    // rtttl contiene la melodia y repeat indica si debe reiniciar al terminar.
    bool playRtttl(const char* rtttl, bool repeat = false) override;

    // Detiene la melodia actual.
    void stop() override;

    // Devuelve true si hay una melodia en reproduccion.
    bool isPlaying() const override;

    // Devuelve true si la melodia actual esta configurada en repeticion.
    bool isRepeating() const override;

    // Habilita o silencia el reproductor fisico.
    // enabled define si se permite reproducir sonido local.
    void setEnabled(bool enabled) override;

private:
    class RtttlPlayer;

    static uint8_t nextChannel_;

    int8_t pin_ = -1;
    int8_t channel_ = -1;
    RtttlPlayer* player_ = nullptr;
    char songBuffer_[maxRtttlLength + 1] = {};
    bool repeat_ = false;
    bool truncated_ = false;

    // Libera recursos del reproductor actual.
    void release();
};
