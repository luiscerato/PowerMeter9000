#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "ui/lcd_ui.h"
#include "ui/lcd_ui/ui_widgets.h"
#include "ui/screens/menu_ui.h"

class BaseConfigUi {
public:
    bool begin(lcdUi& ui);
    void openMenu();
    // Guarda la red seleccionada y elimina la clave si no usa seguridad.
    // network contiene el SSID y el tipo de autenticacion detectados durante el escaneo.
    void saveWifi(const WifiScanNetwork& network);

private:
    lcdUi* ui_ = nullptr;
    uiMenu menu_;

    uiMenuItem stationItems_[9];
    uiMenuItem accessPointItems_[7];
    uiMenuItem otaItems_[2];
    uiMenuItem timeItems_[5];
    uiMenuItem mqttItems_[6];
    uiMenuItem mainItems_[8];

    static BaseConfigUi* instance_;

    // Configura todos los items del menu base.
    void buildMenuItems();
    // Conecta el callback comun de configuracion base a un item persistente.
    static void bindBaseSettingItem(uiMenuItem& item);
    // Abre el selector de redes WiFi.
    static void openWifiSelector(lcdUi& ui);
    // Guarda la red seleccionada por el selector WiFi.
    static void onWifiSelected(uiDialogResult result, WifiScanNetwork network);
    // Abre el editor de fecha y hora del sistema.
    static void openTimeEditor(lcdUi& ui);
    // Recibe cambios de cualquier item persistente de configuracion base.
    static void onBaseSettingEdited(const uiMenuItem& item, uiDialogResult result, void* value);
    // Restaura la configuracion base por defecto.
    static void loadDefaultSettings(lcdUi& ui);
    // Reinicia el ESP32 desde el menu base.
    static void resetEsp(lcdUi& ui);
    // Ejecuta la restauracion si el usuario confirma la pregunta.
    static void onLoadDefaultsQuestion(uiDialogResult result);
    // Ejecuta el reinicio si el usuario confirma la pregunta.
    static void onResetEspQuestion(uiDialogResult result);
};
