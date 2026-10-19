#include "base_config_ui.h"

#include "device_base.h"

#include <ESP.h>
#include <Preferences.h>
#include <string.h>

namespace {

const uiMenuOption wifiModeOptions[] = {
    {0, "Desactivado"},
    {1, "Station"},
    {2, "Access Point"},
    {3, "Station + AP"},
};

}

BaseConfigUi* BaseConfigUi::instance_ = nullptr;

bool BaseConfigUi::begin(lcdUi& ui)
{
    ui_ = &ui;
    instance_ = this;

    buildMenuItems();
    return menu_.begin(*ui_, "@base_config_menu");
}

void BaseConfigUi::openMenu()
{
    if (ui_ != nullptr)
        menu_.showMenu("Config base", mainItems_, sizeof_menu(mainItems_));
}

// Guarda la red seleccionada y elimina la clave si no usa seguridad.
// network contiene el SSID y el tipo de autenticacion detectados durante el escaneo.
void BaseConfigUi::saveWifi(const WifiScanNetwork& network)
{
    device.updateSettingField(BaseSettingsKeys::staSsid, network.ssid);
    if (network.auth == WIFI_AUTH_OPEN)
        device.updateSettingField(BaseSettingsKeys::staPass, "");
}

// Conecta el callback comun de configuracion base a un item persistente.
void BaseConfigUi::bindBaseSettingItem(uiMenuItem& item)
{
    item.setEditCloseCallback(BaseConfigUi::onBaseSettingEdited);
}

// Configura todos los items del menu base.
void BaseConfigUi::buildMenuItems()
{
    const BaseSettingsConfig& config = device.settingsConfig();
    const char* pref = device.preferencesNamespace();

    stationItems_[0].setPrefString("SSID", pref, BaseSettingsKeys::staSsid, config.staSsid, 32);
    bindBaseSettingItem(stationItems_[0]);
    stationItems_[1].setAction("Buscar red", openWifiSelector);
    stationItems_[2].setPrefPassword("Clave", pref, BaseSettingsKeys::staPass, config.staPass, 32);
    bindBaseSettingItem(stationItems_[2]);
    stationItems_[3].setPrefBool("IP estatica", pref, BaseSettingsKeys::staticIp, config.staticIp);
    bindBaseSettingItem(stationItems_[3]);
    stationItems_[4].setPrefIp("IP", pref, BaseSettingsKeys::staIp, config.staIp);
    bindBaseSettingItem(stationItems_[4]);
    stationItems_[5].setPrefIp("Mascara", pref, BaseSettingsKeys::staMask, config.staMask);
    bindBaseSettingItem(stationItems_[5]);
    stationItems_[6].setPrefIp("Gateway", pref, BaseSettingsKeys::staGateway, config.staGateway);
    bindBaseSettingItem(stationItems_[6]);
    stationItems_[7].setPrefIp("DNS 1", pref, BaseSettingsKeys::staDns1, config.staDns1);
    bindBaseSettingItem(stationItems_[7]);
    stationItems_[8].setPrefIp("DNS 2", pref, BaseSettingsKeys::staDns2, config.staDns2);
    bindBaseSettingItem(stationItems_[8]);

    accessPointItems_[0].setPrefString("SSID", pref, BaseSettingsKeys::apSsid, config.apSsid, 32);
    bindBaseSettingItem(accessPointItems_[0]);
    accessPointItems_[1].setPrefPassword("Clave", pref, BaseSettingsKeys::apPass, config.apPass, 32);
    bindBaseSettingItem(accessPointItems_[1]);
    accessPointItems_[2].setPrefBool("Oculto", pref, BaseSettingsKeys::apHide, config.apHide);
    bindBaseSettingItem(accessPointItems_[2]);
    accessPointItems_[3].setPrefBool("IP propia", pref, BaseSettingsKeys::apCustomIp, config.apCustomIp);
    bindBaseSettingItem(accessPointItems_[3]);
    accessPointItems_[4].setPrefIp("IP", pref, BaseSettingsKeys::apIp, config.apIp);
    bindBaseSettingItem(accessPointItems_[4]);
    accessPointItems_[5].setPrefIp("Mascara", pref, BaseSettingsKeys::apMask, config.apMask);
    bindBaseSettingItem(accessPointItems_[5]);
    accessPointItems_[6].setPrefIp("Gateway", pref, BaseSettingsKeys::apGateway, config.apGateway);
    bindBaseSettingItem(accessPointItems_[6]);

    otaItems_[0].setPrefBool("Habilitado", pref, BaseSettingsKeys::otaEnabled, config.otaEnabled);
    bindBaseSettingItem(otaItems_[0]);
    otaItems_[1].setPrefPassword("Clave", pref, BaseSettingsKeys::otaPass, config.otaPass, 32);
    bindBaseSettingItem(otaItems_[1]);

    timeItems_[0].setPrefBool("NTP habilitado", pref, BaseSettingsKeys::timeSync, config.timeSync);
    bindBaseSettingItem(timeItems_[0]);
    timeItems_[1].setPrefString("Servidor", pref, BaseSettingsKeys::timeServer, config.timeServer, 63);
    bindBaseSettingItem(timeItems_[1]);
    timeItems_[2].setPrefNumber("UTC offset", pref, BaseSettingsKeys::timeMinutesWest, config.timeMinutesWest, -720, 840, 15, "min");
    bindBaseSettingItem(timeItems_[2]);
    timeItems_[3].setPrefNumber("Horario verano", pref, BaseSettingsKeys::timeDstMinutes, config.timeDstMinutes, -180, 180, 15, "min");
    bindBaseSettingItem(timeItems_[3]);
    timeItems_[4].setAction("Editar fecha/hora", openTimeEditor);

    mqttItems_[0].setPrefBool("Habilitado", pref, BaseSettingsKeys::mqttEnabled, config.mqttEnabled);
    bindBaseSettingItem(mqttItems_[0]);
    mqttItems_[1].setPrefString("Servidor", pref, BaseSettingsKeys::mqttServer, config.mqttServer, 63);
    bindBaseSettingItem(mqttItems_[1]);
    mqttItems_[2].setPrefNumber("Puerto", pref, BaseSettingsKeys::mqttPort, static_cast<uint32_t>(config.mqttPort), 1U, 65535U, 1U);
    bindBaseSettingItem(mqttItems_[2]);
    mqttItems_[3].setPrefString("Usuario", pref, BaseSettingsKeys::mqttUser, config.mqttUser, 32);
    bindBaseSettingItem(mqttItems_[3]);
    mqttItems_[4].setPrefPassword("Clave", pref, BaseSettingsKeys::mqttPass, config.mqttPass, 32);
    bindBaseSettingItem(mqttItems_[4]);
    mqttItems_[5].setPrefString("Base topic", pref, BaseSettingsKeys::mqttBaseTopic, config.mqttBaseTopic, 63);
    bindBaseSettingItem(mqttItems_[5]);

    mainItems_[0].setPrefOptions("Modo WiFi", pref, BaseSettingsKeys::wifiMode, config.wifiMode, wifiModeOptions, sizeof_menu(wifiModeOptions));
    bindBaseSettingItem(mainItems_[0]);
    mainItems_[1].setMenu("Station", stationItems_, sizeof_menu(stationItems_));
    mainItems_[2].setMenu("Access Point", accessPointItems_, sizeof_menu(accessPointItems_));
    mainItems_[3].setMenu("OTA", otaItems_, sizeof_menu(otaItems_));
    mainItems_[4].setMenu("Hora", timeItems_, sizeof_menu(timeItems_));
    mainItems_[5].setMenu("MQTT", mqttItems_, sizeof_menu(mqttItems_));
    mainItems_[6].setAction("Cargar defaults", loadDefaultSettings);
    mainItems_[7].setAction("Reset ESP32", resetEsp);
}

// Abre el selector de redes WiFi.
void BaseConfigUi::openWifiSelector(lcdUi& ui)
{
    (void)ui;
    if (instance_ == nullptr)
        return;

    Preferences settings;
    String ssid;
    if (settings.begin(device.preferencesNamespace(), true)) {
        ssid = settings.getString(BaseSettingsKeys::staSsid, device.settingsConfig().staSsid);
        settings.end();
    }
    ui.showWifiSelector(ssid.c_str(), onWifiSelected);
}

// Guarda la red seleccionada por el selector WiFi.
void BaseConfigUi::onWifiSelected(uiDialogResult result, WifiScanNetwork network)
{
    if (instance_ == nullptr || result != uiDialogResult::Ok)
        return;
    instance_->saveWifi(network);
}

// Abre el editor de fecha y hora del sistema.
void BaseConfigUi::openTimeEditor(lcdUi& ui)
{
    (void)ui;
    if (instance_ == nullptr)
        return;

    uiTimeAndDateConfig config;
    config.title = "Fecha y hora";
    config.mode = uiTimeAndDateMode::DateTime;
    instance_->ui_->showTimeAndDateEditor(config);
}

// Recibe cambios de cualquier item persistente de configuracion base.
void BaseConfigUi::onBaseSettingEdited(const uiMenuItem& item, uiDialogResult result, void* value)
{
    if (result != uiDialogResult::Ok || value == nullptr)
        return;

    const char* key = item.preferenceKey();
    if (key != nullptr && strcmp(key, BaseSettingsKeys::mqttBaseTopic) == 0)
        device.setMainTopic(static_cast<String*>(value)->c_str());
}

// Restaura la configuracion base por defecto.
void BaseConfigUi::loadDefaultSettings(lcdUi& ui)
{
    ui.showQuestionBox("Cargar defaults", "Restaurar valores\nde fabrica?", screenQuestion::OkCancel, onLoadDefaultsQuestion);
}

// Reinicia el ESP32 desde el menu base.
void BaseConfigUi::resetEsp(lcdUi& ui)
{
    ui.showQuestionBox("Reset ESP32", "Reiniciar ahora?", screenQuestion::OkCancel, onResetEspQuestion);
}

// Ejecuta la restauracion si el usuario confirma la pregunta.
void BaseConfigUi::onLoadDefaultsQuestion(uiDialogResult result)
{
    if (instance_ == nullptr || instance_->ui_ == nullptr || result != uiDialogResult::Ok)
        return;

    lcdUi& ui = *instance_->ui_;
    if (device.loadDefaultSettings())
        ui.showMessageBox("Config base", "Defaults cargados\nReinicie ESP32", 2500);
    else
        ui.showMessageBox("Config base", "Error al cargar\ndefaults", 2500);
}

// Ejecuta el reinicio si el usuario confirma la pregunta.
void BaseConfigUi::onResetEspQuestion(uiDialogResult result)
{
    if (instance_ == nullptr || instance_->ui_ == nullptr || result != uiDialogResult::Ok)
        return;

    lcdUi& ui = *instance_->ui_;
    ui.showMessageBox("Reset", "Reiniciando...", 600);
    delay(700);
    ESP.restart();
}
