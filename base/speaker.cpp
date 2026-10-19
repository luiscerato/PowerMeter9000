#include "speaker.h"

#include "debug.h"
#include "esp32-hal-gpio.h"
#include "esp_arduino_version.h"

static constexpr const char DEBUG_MODULE[] = "speaker";

#define NOTE_H   0
#define NOTE_B0  31
#define NOTE_C1  33
#define NOTE_CS1 35
#define NOTE_D1  37
#define NOTE_DS1 39
#define NOTE_E1  41
#define NOTE_F1  44
#define NOTE_FS1 46
#define NOTE_G1  49
#define NOTE_GS1 52
#define NOTE_A1  55
#define NOTE_AS1 58
#define NOTE_B1  62
#define NOTE_C2  65
#define NOTE_CS2 69
#define NOTE_D2  73
#define NOTE_DS2 78
#define NOTE_E2  82
#define NOTE_F2  87
#define NOTE_FS2 93
#define NOTE_G2  98
#define NOTE_GS2 104
#define NOTE_A2  110
#define NOTE_AS2 117
#define NOTE_B2  123
#define NOTE_C3  131
#define NOTE_CS3 139
#define NOTE_D3  147
#define NOTE_DS3 156
#define NOTE_E3  165
#define NOTE_F3  175
#define NOTE_FS3 185
#define NOTE_G3  196
#define NOTE_GS3 208
#define NOTE_A3  220
#define NOTE_AS3 233
#define NOTE_B3  247
#define NOTE_C4  262
#define NOTE_CS4 277
#define NOTE_D4  294
#define NOTE_DS4 311
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_FS4 370
#define NOTE_G4  392
#define NOTE_GS4 415
#define NOTE_A4  440
#define NOTE_AS4 466
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_CS5 554
#define NOTE_D5  587
#define NOTE_DS5 622
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_FS5 740
#define NOTE_G5  784
#define NOTE_GS5 831
#define NOTE_A5  880
#define NOTE_AS5 932
#define NOTE_B5  988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976
#define NOTE_C7  2093
#define NOTE_CS7 2217
#define NOTE_D7  2349
#define NOTE_DS7 2489
#define NOTE_E7  2637
#define NOTE_F7  2794
#define NOTE_FS7 2960
#define NOTE_G7  3136
#define NOTE_GS7 3322
#define NOTE_A7  3520
#define NOTE_AS7 3729
#define NOTE_B7  3951
#define NOTE_C8  4186
#define NOTE_CS8 4435
#define NOTE_D8  4699
#define NOTE_DS8 4978

#define OCTAVE_OFFSET 0

void Speaker::setEnabled(bool enabled)
{
    enabled_ = enabled;
}

bool Speaker::isEnabled() const
{
    return enabled_;
}

void Speaker::setVolume(uint16_t volume)
{
    volume_ = constrain(volume, 0U, 1023U);
}

uint16_t Speaker::getVolume() const
{
    return volume_;
}

class RtttlSpeaker::RtttlPlayer {
public:
    // Inicializa el generador PWM para reproducir RTTTL.
    // outputPin indica la salida PWM y outputChannel el canal LEDC.
    RtttlPlayer(byte outputPin, byte outputChannel);
    ~RtttlPlayer();

    // Carga una melodia RTTTL con volumen por defecto.
    // song contiene la melodia a reproducir.
    void loadSong(const char* song);

    // Carga una melodia RTTTL con volumen indicado.
    // song contiene la melodia y volume define el duty PWM.
    void loadSong(const char* song, uint16_t volume);

    // Avanza la reproduccion no bloqueante.
    void play();

    // Detiene la reproduccion actual.
    void stop();

    // Devuelve true si hay una melodia en curso.
    bool isPlaying() const;

    // Devuelve true si el PWM quedo inicializado.
    bool isReady() const;

private:
    const int notes[49] = { 0,
        NOTE_C4, NOTE_CS4, NOTE_D4, NOTE_DS4, NOTE_E4, NOTE_F4, NOTE_FS4, NOTE_G4, NOTE_GS4, NOTE_A4, NOTE_AS4, NOTE_B4,
        NOTE_C5, NOTE_CS5, NOTE_D5, NOTE_DS5, NOTE_E5, NOTE_F5, NOTE_FS5, NOTE_G5, NOTE_GS5, NOTE_A5, NOTE_AS5, NOTE_B5,
        NOTE_C6, NOTE_CS6, NOTE_D6, NOTE_DS6, NOTE_E6, NOTE_F6, NOTE_FS6, NOTE_G6, NOTE_GS6, NOTE_A6, NOTE_AS6, NOTE_B6,
        NOTE_C7, NOTE_CS7, NOTE_D7, NOTE_DS7, NOTE_E7, NOTE_F7, NOTE_FS7, NOTE_G7, NOTE_GS7, NOTE_A7, NOTE_AS7, NOTE_B7
    };

    const char* buffer = "";
    byte defaultDur = 4;
    byte defaultOct = 5;
    int bpm = 63;
    long wholenote = 0;
    int8_t pin = -1;
    unsigned long noteDelay = 0;
    bool playing = false;
    int8_t channel = -1;
    uint16_t volume = 10;
    bool ready = false;

    // Avanza hasta la proxima nota de la melodia.
    void nextNote();

    // Apaga la salida PWM.
    void noTone();

    // Inicia una nota durante el tiempo indicado.
    // frq es la frecuencia y duration la duracion en ms.
    void tone(int frq, int duration);
};

RtttlSpeaker::RtttlPlayer::RtttlPlayer(byte outputPin, byte outputChannel)
{
    pin = outputPin;
    channel = outputChannel;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ready = ledcAttach(pin, 1000, 10);
#else
    ready = ledcSetup(channel, 1000, 10) > 0;
    if (ready)
        ledcAttachPin(pin, channel);
#endif
}

RtttlSpeaker::RtttlPlayer::~RtttlPlayer()
{
    stop();
    if (!ready)
        return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcDetach(pin);
#else
    ledcDetachPin(pin);
#endif
    ready = false;
}

void RtttlSpeaker::RtttlPlayer::loadSong(const char* song)
{
    loadSong(song, 10);
}

void RtttlSpeaker::RtttlPlayer::loadSong(const char* song, uint16_t newVolume)
{
    if (!ready || song == nullptr || song[0] == '\0') {
        stop();
        return;
    }

    buffer = song;
    defaultDur = 4;
    defaultOct = 6;
    bpm = 63;
    playing = true;
    noteDelay = 0;
    volume = newVolume;

    noTone();

    int num;
    while (*buffer != ':' && *buffer != '\0')
        buffer++;
    if (*buffer == ':')
        buffer++;

    if (*buffer == 'd') {
        buffer += 2;
        num = 0;
        while (isdigit(*buffer))
            num = (num * 10) + (*buffer++ - '0');
        if (num > 0)
            defaultDur = num;
        if (*buffer == ',')
            buffer++;
    }

    if (*buffer == 'o') {
        buffer += 2;
        num = *buffer++ - '0';
        if (num >= 3 && num <= 7)
            defaultOct = num;
        if (*buffer == ',')
            buffer++;
    }

    if (*buffer == 'b') {
        buffer += 2;
        num = 0;
        while (isdigit(*buffer))
            num = (num * 10) + (*buffer++ - '0');
        bpm = num;
        if (*buffer == ':')
            buffer++;
    }

    if (bpm <= 0)
        bpm = 63;
    wholenote = (60 * 1000L / bpm) * 4;
}

void RtttlSpeaker::RtttlPlayer::noTone()
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(pin, 0);
#else
    ledcWrite(channel, 0);
#endif
}

void RtttlSpeaker::RtttlPlayer::tone(int frq, int duration)
{
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWriteTone(pin, frq);
    ledcWrite(pin, volume);
#else
    ledcWriteTone(channel, frq);
    ledcWrite(channel, volume);
#endif
    noteDelay = millis() + duration;
}

void RtttlSpeaker::RtttlPlayer::nextNote()
{
    long duration;
    byte note;
    byte scale;

    noTone();

    if (*buffer == '\0') {
        stop();
        return;
    }

    int num = 0;
    while (isdigit(*buffer))
        num = (num * 10) + (*buffer++ - '0');
    if (num)
        duration = wholenote / num;
    else
        duration = wholenote / defaultDur;

    note = 0;
    switch (*buffer) {
    case 'c': note = 1; break;
    case 'd': note = 3; break;
    case 'e': note = 5; break;
    case 'f': note = 6; break;
    case 'g': note = 8; break;
    case 'a': note = 10; break;
    case 'b': note = 12; break;
    case 'p':
    default: note = 0;
    }
    buffer++;

    if (*buffer == '#') {
        note++;
        buffer++;
    }

    if (*buffer == '.') {
        duration += duration / 2;
        buffer++;
    }

    if (isdigit(*buffer)) {
        scale = *buffer - '0';
        buffer++;
    }
    else {
        scale = defaultOct;
    }
    scale += OCTAVE_OFFSET;

    if (*buffer == '.') {
        duration += duration / 2;
        buffer++;
    }

    if (*buffer == ',')
        buffer++;

    if (note == 0) {
        noTone();
        noteDelay = millis() + duration;
        return;
    }

    int noteIndex = (scale - 4) * 12 + note;
    if (noteIndex < 1 || noteIndex >= 49) {
        noTone();
        noteDelay = millis() + duration;
        return;
    }
    tone(notes[noteIndex], duration);
}

void RtttlSpeaker::RtttlPlayer::play()
{
    if (!playing)
        return;

    unsigned long now = millis();
    if (static_cast<int32_t>(now - noteDelay) < 0)
        return;

    if (*buffer == '\0') {
        stop();
        return;
    }

    nextNote();
}

void RtttlSpeaker::RtttlPlayer::stop()
{
    if (playing) {
        noTone();
        playing = false;
    }
}

bool RtttlSpeaker::RtttlPlayer::isPlaying() const
{
    return playing;
}

bool RtttlSpeaker::RtttlPlayer::isReady() const
{
    return ready;
}

uint8_t RtttlSpeaker::nextChannel_ = 0;

RtttlSpeaker::RtttlSpeaker() = default;

RtttlSpeaker::RtttlSpeaker(uint8_t pin)
{
    begin(pin);
}

RtttlSpeaker::RtttlSpeaker(uint8_t pin, uint8_t channel)
{
    begin(pin, channel);
}

RtttlSpeaker::~RtttlSpeaker()
{
    release();
}

bool RtttlSpeaker::begin(uint8_t pin)
{
    return begin(pin, nextChannel_++);
}

bool RtttlSpeaker::begin(uint8_t pin, int8_t channel)
{
    release();

    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin) || channel < 0 || channel > 15)
        return false;

    player_ = new RtttlPlayer(pin, static_cast<uint8_t>(channel));
    if (player_ == nullptr || !player_->isReady()) {
        release();
        return false;
    }

    pin_ = static_cast<int8_t>(pin);
    channel_ = channel;
    return true;
}

bool RtttlSpeaker::isReady() const
{
    return player_ != nullptr && player_->isReady();
}

void RtttlSpeaker::run()
{
    if (player_ == nullptr)
        return;

    player_->play();
    if (repeat_ && !player_->isPlaying() && songBuffer_[0] != '\0')
        player_->loadSong(songBuffer_, volume_);
}

bool RtttlSpeaker::playRtttl(const char* rtttl, bool repeat)
{
    if (!enabled_ || !isReady() || rtttl == nullptr || rtttl[0] == '\0')
        return false;

    const size_t length = strlen(rtttl);
    truncated_ = length > maxRtttlLength;
    if (truncated_)
        debugW("RTTTL truncado: largo=%u, max=%u", static_cast<unsigned>(length), static_cast<unsigned>(maxRtttlLength));
    strncpy(songBuffer_, rtttl, maxRtttlLength);
    songBuffer_[maxRtttlLength] = '\0';
    repeat_ = repeat;

    player_->loadSong(songBuffer_, volume_);
    return player_->isPlaying();
}

void RtttlSpeaker::stop()
{
    repeat_ = false;
    if (player_ != nullptr)
        player_->stop();
    songBuffer_[0] = '\0';
}

bool RtttlSpeaker::isPlaying() const
{
    return player_ != nullptr && player_->isPlaying();
}

bool RtttlSpeaker::isRepeating() const
{
    return repeat_;
}

void RtttlSpeaker::setEnabled(bool enabled)
{
    Speaker::setEnabled(enabled);
    if (!enabled_)
        stop();
}

void RtttlSpeaker::release()
{
    if (player_ != nullptr) {
        player_->stop();
        delete player_;
        player_ = nullptr;
    }
    repeat_ = false;
    truncated_ = false;
    songBuffer_[0] = '\0';
    pin_ = -1;
    channel_ = -1;
}
