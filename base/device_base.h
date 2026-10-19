#pragma once

#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <AsyncMqttClient.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include "esp_sntp.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "keypad.h"
#include "speaker.h"
#include "ui/lcd_ui/ui_types.h"
#include "webserver.h"

class lcdUi;

using mqttConnectedCallback = void (*)(AsyncMqttClient& client);

struct BaseSettingsKeys {
	static constexpr const char* settings = "settings";
	static constexpr const char* wifiMode = "wifi-mode";
	static constexpr const char* staSsid = "sta-ssid";
	static constexpr const char* staPass = "sta-pass";
	static constexpr const char* staticIp = "staticIp";
	static constexpr const char* staIp = "sta-ip";
	static constexpr const char* staMask = "sta-mask";
	static constexpr const char* staGateway = "sta-gw";
	static constexpr const char* staDns1 = "sta-dns1";
	static constexpr const char* staDns2 = "sta-dns2";
	static constexpr const char* apSsid = "ap-ssid";
	static constexpr const char* apPass = "ap-pass";
	static constexpr const char* apHide = "ap-hide";
	static constexpr const char* apCustomIp = "ap-custom-ip";
	static constexpr const char* apIp = "ap-ip";
	static constexpr const char* apMask = "ap-mask";
	static constexpr const char* apGateway = "ap-gw";
	static constexpr const char* otaEnabled = "ota-enabled";
	static constexpr const char* otaPass = "ota-pass";
	static constexpr const char* timeStart = "time-start";
	static constexpr const char* timeSync = "time-sync";
	static constexpr const char* timeServer = "time-server";
	static constexpr const char* timeMinutesWest = "time-minwest";
	static constexpr const char* timeDstMinutes = "time-dsttime";
	static constexpr const char* mqttEnabled = "mqtt-enabled";
	static constexpr const char* mqttServer = "mqtt-server";
	static constexpr const char* mqttPort = "mqtt-port";
	static constexpr const char* mqttUser = "mqtt-user";
	static constexpr const char* mqttPass = "mqtt-pass";
	static constexpr const char* mqttBaseTopic = "mqtt-base-topic";
};

struct BaseSettingsConfig {
	const char* appName = "ESP32 App";
	const char* hostName = "esp32-device";
	const char* preferencesNamespace = "base-config";

	int32_t wifiMode = static_cast<int32_t>(WIFI_MODE_STA);
	const char* staSsid = "";
	const char* staPass = "";
	bool staticIp = false;
	const char* staIp = "192.168.1.1";
	const char* staMask = "255.255.255.0";
	const char* staGateway = "192.168.1.1";
	const char* staDns1 = "8.8.8.8";
	const char* staDns2 = "4.4.4.4";

	const char* apSsid = "ESP32-AP";
	const char* apPass = "";
	bool apHide = false;
	bool apCustomIp = false;
	const char* apIp = "192.168.1.1";
	const char* apMask = "255.255.255.0";
	const char* apGateway = "192.168.1.1";

	bool otaEnabled = true;
	const char* otaPass = "";

	uint32_t timeStart = 1704078000;
	bool timeSync = true;
	const char* timeServer = "pool.ntp.org";
	int32_t timeMinutesWest = 0;
	int32_t timeDstMinutes = 0;

	bool mqttEnabled = false;
	const char* mqttServer = "";
	uint16_t mqttPort = 1883;
	const char* mqttUser = "";
	const char* mqttPass = "";
	const char* mqttBaseTopic = "esp32-device";
};

using BaseSettingsDefaultsCallback = void (*)(BaseSettingsConfig& config);

enum class Wifi_Mode : int32_t
{
	Off = WIFI_MODE_NULL,
	Station = WIFI_MODE_STA,
	SoftAP = WIFI_MODE_AP,
	Station_AP = WIFI_MODE_APSTA,
};

enum WiFiChar : char
{
	WiFiChar_Off = 0x1,
	WiFiChar_Lock,
	WiFiChar_RSSI_1 = 4,
	WiFiChar_RSSI_2,
	WiFiChar_RSSI_3,
	WiFiChar_RSSI_4,
};

enum WiFiLevel : char
{
	WiFiLevel_Off = 0x0,
	WiFiLevel_RSSI_1,
	WiFiLevel_RSSI_2,
	WiFiLevel_RSSI_3,
};

enum class MqttConnectionState : uint8_t {
	Disabled,
	WaitingWifi,
	Connecting,
	Connected,
	Disconnected
};

struct MqttStatus {
	MqttConnectionState state = MqttConnectionState::Disabled;
	int8_t reasonCode = -1;
	uint32_t disconnectCount = 0;
	uint32_t connectAttempts = 0;
	uint32_t subscriptions = 0;
	uint32_t messages = 0;
	uint32_t changedAtMs = 0;
	uint32_t lastMessageAtMs = 0;
};

enum class WifiScanState : uint8_t {
	Idle,
	Requested,
	Running,
	Completed,
	Failed
};

struct WifiScanNetwork {
	char ssid[33] = "";
	int32_t rssi = 0;
	uint8_t auth = 0;
	uint8_t channel = 0;
};

constexpr uint8_t WifiScanMaxNetworks = 16;

class DeviceBase {
public:
	using KeypadEventProvider = bool (*)(keypadEvent& event);

	// Inicializa la base.
	// ui es la pantalla local/remota y defaultsCallback personaliza defaults genericos.
	bool begin(lcdUi& ui, BaseSettingsDefaultsCallback defaultsCallback = nullptr);

	// Ejecuta el ciclo base.
	// No recibe parametros; procesa entrada, sonido y servicios periodicos.
	void loop();

	// Registra el callback de JSON de estado de hardware.
	// callback escribe el estado de la aplicacion cuando el webserver lo pide.
	void setHardwareStatusJsonCallback(webServerJsonCallback callback);

	// Registra el callback de JSON de estado de aplicacion.
	// callback escribe el estado general de la aplicacion para el endpoint /app.
	void setApplicationStatusJsonCallback(webServerJsonCallback callback);

	// Registra endpoints web propios de la aplicacion.
	// callback recibe el servidor base para agregar endpoints propios.
	void setApplicationEndpointsCallback(webServerEndpointsCallback callback);

	// Registra el callback de conexion MQTT.
	// callback se ejecuta cada vez que MQTT queda conectado.
	void setMqttConnectedCallback(mqttConnectedCallback callback);

	// Registra la fuente de eventos de teclado.
	// provider entrega eventos, behavior define acciones UI y source identifica el origen.
	void setKeypadEventProvider(KeypadEventProvider provider, const uiKeyBehavior& behavior, uiInputSource source = uiInputSource::Physical);

	// Registra el destino fisico de audio.
	// speaker reproduce los sonidos locales de la base.
	void setSpeaker(Speaker& speaker);

	// Encola un sonido semantico de UI.
	// event identifica el sonido y loop indica si debe repetirse.
	bool playSound(uiSoundEvent event, bool loop = false);

	// Encola una melodia RTTTL.
	// rtttl debe apuntar a memoria valida hasta que el comando sea procesado.
	// Si loop es true, el puntero debe seguir valido hasta llamar a stopSound().
	bool playSound(const char* rtttl, bool loop = false);

	// Detiene el sonido actual.
	// No recibe parametros; cancela cualquier loop activo.
	bool stopSound();

	// Devuelve true si la interfaz WiFi esta conectada.
	bool isWifiConnected() const;

	// Devuelve true si el cliente MQTT esta conectado.
	bool isMqttConnected() const;

	// Devuelve una copia consistente del estado y ultimo error MQTT.
	// No recibe parametros; el diagnostico se conserva solamente en RAM.
	MqttStatus mqttStatus() const;

	// Devuelve el nombre publico de un estado de conexion MQTT.
	// state es el estado interno que se desea serializar.
	static const char* mqttConnectionStateName(MqttConnectionState state);

	// Devuelve la descripcion publica de un codigo de desconexion MQTT.
	// reasonCode usa los valores de AsyncMqttClientDisconnectReason.
	static const char* mqttDisconnectReasonName(int8_t reasonCode);

	// Devuelve el cliente MQTT usado por la base.
	// No recibe parametros; permite publicar o consultar estado desde la app.
	AsyncMqttClient& mqttClient();

	// Devuelve la cantidad de clientes conectados a servicios web remotos.
	uint32_t webClientCount() const;

	// Devuelve la configuracion base activa.
	// No recibe parametros; contiene defaults listos para leer o escribir.
	const BaseSettingsConfig& settingsConfig() const;

	// Devuelve el nombre de Preferences usado por la configuracion base.
	// No recibe parametros; nunca devuelve null ni texto vacio.
	const char* preferencesNamespace() const;

	// Devuelve el hostname usado por WiFi, OTA y servicios remotos.
	// No recibe parametros; nunca devuelve null ni texto vacio.
	const char* hostName() const;

	// Devuelve el topico principal MQTT activo en memoria.
	// No recibe parametros; nunca devuelve null ni texto vacio.
	const char* mainTopic() const;

	// Actualiza el topico principal MQTT activo en memoria.
	// topic es el valor nuevo; si esta vacio usa el hostname.
	void setMainTopic(const char* topic);

	// Restaura la configuracion base persistente usando los defaults activos.
	// No recibe parametros; devuelve true si pudo escribir Preferences.
	bool loadDefaultSettings();

	// Devuelve true si key pertenece a la configuracion base persistente.
	// key es el nombre de Preferences a verificar.
	bool isSettingsKey(const char* key) const;

	// Guarda un campo de configuracion base respetando el tipo persistido.
	// key identifica la clave y value contiene el texto recibido.
	bool updateSettingField(const char* key, const char* value);

	// Guarda campos de configuracion base desde texto form-url-encoded.
	// formText contiene pares key=value&key=value.
	bool updateSettingsFromForm(const char* formText);

	// Construye el JSON de configuracion base actual.
	// buffer recibe el texto JSON y capacity es su tamano disponible.
	bool getConfigJson(char* buffer, size_t capacity) const;

	// Solicita un nuevo escaneo WiFi.
	// force permite reemplazar un pedido o escaneo pendiente.
	void requestWifiScan(bool force = false);

	// Devuelve una copia consistente del ultimo escaneo WiFi.
	// networks recibe las redes, capacity es su tamano y count devuelve cuantas se copiaron.
	WifiScanState wifiScanSnapshot(WifiScanNetwork* networks, uint8_t capacity, uint8_t& count);

	// Devuelve RSSI de la red WiFi actual.
	// No recibe parametros; si no hay conexion devuelve 0.
	int32_t wifiRssi();

	// Devuelve el SSID WiFi actual.
	// No recibe parametros; si no hay conexion devuelve texto vacio.
	String wifiSsid();

	// Devuelve el codigo de intensidad WiFi para la UI.
	// No recibe parametros; usa el RSSI actual.
	uint16_t wifiLevelCode();

	// Devuelve el caracter de intensidad para rssi.
	// rssi es la intensidad recibida en dBm.
	char wifiRssiCode(int32_t rssi);

	// Devuelve el caracter de intensidad WiFi actual.
	// No recibe parametros; usa el RSSI actual.
	char wifiRssiCode();

	// Devuelve la hora actual del sistema.
	// timer se conserva por compatibilidad con time().
	time_t timeNow(time_t* timer = nullptr);

	// Devuelve la hora local descompuesta.
	// No recibe parametros; usa la zona horaria configurada.
	struct tm localTime();

	// Setea la hora actual del sistema.
	// seconds es epoch time en segundos.
	void setTimeTo(uint32_t seconds);

	// Aplica la zona horaria indicada.
	// timeZone contiene offset y daylight en minutos.
	String setTimeZone(timezone* timeZone);

	// Aplica la zona horaria indicada.
	// offset y daylight estan expresados en segundos.
	String setTimeZone(int32_t offset, int32_t daylight);

	// Imprime la hora local por debug.
	// No recibe parametros.
	void printLocalTime();

	// Devuelve el nombre de un evento WiFi.
	// event es el codigo emitido por Arduino WiFi.
	const char* wifiEventName(WiFiEvent_t event) const;

	// Devuelve la UI asociada a la base, o nullptr si todavia no inicio.
	lcdUi* ui() const { return ui_; }

private:
	enum class SoundCommandType : uint8_t {
		Play = 0,
		Stop
	};

	struct SoundCommand {
		SoundCommandType type = SoundCommandType::Play;
		const char* rtttl = nullptr;
		bool loop = false;
	};

	lcdUi* ui_ = nullptr;
	Speaker* speaker_ = nullptr;
	Preferences settings_;
	AsyncMqttClient mqtt_;
	BaseSettingsConfig settingsConfig_;
	char mainTopic_[64] = "";
	timezone localTimeZone_ = { 0, -180 };
	bool usingOTA_ = false;
	bool usingDebug_ = false;
	bool usingTimeServer_ = false;
	bool usingMQTT_ = false;
	mutable portMUX_TYPE mqttStatusMux_ = portMUX_INITIALIZER_UNLOCKED;
	MqttStatus mqttStatus_;
	portMUX_TYPE wifiScanMux_ = portMUX_INITIALIZER_UNLOCKED;
	WifiScanNetwork wifiScanPublishedNetworks_[WifiScanMaxNetworks] = {};
	WifiScanState wifiScanPublishedState_ = WifiScanState::Idle;
	uint8_t wifiScanPublishedCount_ = 0;
	bool wifiScanRequested_ = false;
	bool wifiScanRunning_ = false;
	uint32_t wifiScanStartedMs_ = 0;
	uint32_t wifiScanLastPollMs_ = 0;
	QueueHandle_t soundQueue_ = nullptr;
	KeypadEventProvider keypadEventProvider_ = nullptr;
	uiKeyBehavior keypadBehavior_;
	uiInputSource keypadSource_ = uiInputSource::Physical;
	webServerJsonCallback hardwareStatusJsonCallback_ = nullptr;
	webServerJsonCallback applicationStatusJsonCallback_ = nullptr;
	webServerEndpointsCallback applicationEndpointsCallback_ = nullptr;
	mqttConnectedCallback mqttConnectedCallback_ = nullptr;

	// Aplica callbacks configurados antes de begin().
	// No recibe parametros; sincroniza callbacks guardados con los modulos base.
	void applyRegisteredCallbacks();

	// Prepara la UI para trabajar con la base.
	// ui es la pantalla que recibira eventos y sonidos.
	void prepareUi(lcdUi& ui);

	// Inicializa los servicios base.
	// No recibe parametros; arranca debug, settings, WiFi, hora, OTA, MQTT y webserver.
	void beginServices();

	// Procesa la entrada registrada.
	// No recibe parametros; pasa eventos del proveedor de teclado a la UI.
	void loopInput();

	// Procesa el sonido local/remoto.
	// No recibe parametros; vacia la cola y actualiza el speaker fisico.
	void loopSound();

	// Crea la cola de sonido si todavia no existe.
	// No recibe parametros; devuelve true si la cola esta disponible.
	bool ensureSoundQueue();

	// Procesa los comandos de sonido pendientes.
	// No recibe parametros; consume la cola sin bloquear.
	void processSoundQueue();

	// Envia una melodia RTTTL a todos los destinos configurados.
	// rtttl es la melodia a reproducir y loop indica si debe repetirse.
	void routeRtttl(const char* rtttl, bool loop);

	// Devuelve la melodia RTTTL asociada a un evento de UI.
	// event identifica el evento de UI que genero el sonido.
	const char* rtttlForSoundEvent(uiSoundEvent event) const;

	// Prepara los defaults base y permite que la app los personalice.
	// callback recibe una copia editable de los valores genericos.
	void configureSettings(BaseSettingsDefaultsCallback callback);

	// Inicializa Preferences con defaults si todavia no existe configuracion.
	// No recibe parametros.
	void beginSettings();

	// Ejecuta tareas periodicas de conectividad base.
	// No recibe parametros.
	void loopBaseServices();

	// Registra eventos de debug remoto.
	// No recibe parametros.
	void beginDebug();

	// Inicializa WiFi desde Preferences.
	// No recibe parametros.
	void beginWifi();

	// Reconecta WiFi usando los datos persistidos.
	// No recibe parametros.
	void reconnectWifi();

	// Inicializa OTA desde Preferences.
	// No recibe parametros.
	void beginOta();

	// Inicializa MQTT desde Preferences.
	// No recibe parametros.
	void beginMqtt();

	// Actualiza el diagnostico MQTT compartido por callbacks y servidor web.
	// state es el estado nuevo, reasonCode actualiza el ultimo error si es no negativo
	// y countDisconnect incrementa el contador de desconexiones.
	void publishMqttStatus(MqttConnectionState state, int8_t reasonCode = -1,
		bool countDisconnect = false);

	// Registra un intento real de conexion y publica el estado Connecting.
	// No recibe parametros; incrementa el contador volatil de intentos.
	void recordMqttConnectAttempt();

	// Registra una confirmacion SUBACK recibida desde el broker.
	// No recibe parametros; incrementa las suscripciones de la sesion actual.
	void recordMqttSubscription();

	// Registra el primer fragmento de un mensaje MQTT recibido.
	// No recibe parametros; actualiza contador y hora del ultimo mensaje.
	void recordMqttMessage();

	// Inicializa hora local y NTP desde Preferences.
	// No recibe parametros.
	void beginTime();

	// Configura el servidor NTP persistido.
	// No recibe parametros.
	void setTimeServer();

	// Avanza el escaneo WiFi sin bloquear el loop principal.
	// No recibe parametros.
	void loopWifiScan();

	// Publica una copia estable del ultimo escaneo WiFi.
	// state es el estado nuevo, networks contiene redes opcionales y count indica cuantas copiar.
	void publishWifiScan(WifiScanState state, const WifiScanNetwork* networks = nullptr, uint8_t count = 0);

	// Inicia un escaneo WiFi asincronico.
	// No recibe parametros.
	void startWifiScan();
};

extern DeviceBase device;
