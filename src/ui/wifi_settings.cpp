#include "wifi_settings.h"

#include <WiFi.h>
#include "wifiUtils.h"

namespace {

enum class wifiView : uint8_t { menu, networks, password, ipFields, ipOctet };

struct scannedNetwork
{
	String ssid;
	uint8_t auth = WIFI_AUTH_OPEN;
	int32_t rssi = -100;
};

static wifiView view = wifiView::menu;
static wifiStationConfig config;
static scannedNetwork networks[20];
static int32_t networkCount = -1;
static int32_t selected = 0;
static int32_t selectedField = 0;
static uint8_t selectedOctet = 0;
static uint8_t passwordChar = 0;
static bool scanStarted = false;
static bool connectPending = false;
static uint32_t connectStartedAt = 0;
static String password;
static String message;
static const char passwordChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!\"#$%&'()*+,-./:;<=>?@[\\]^_{|}~ ";

static const uint8_t wifiBars1[] PROGMEM = { 8, 5, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t wifiBars2[] PROGMEM = { 8, 5, 0x00, 0x10, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t wifiBars3[] PROGMEM = { 8, 5, 0x00, 0x10, 0x00, 0x18, 0x00, 0x1E, 0x00, 0x00 };
static const uint8_t wifiBars4[] PROGMEM = { 8, 5, 0x00, 0x10, 0x00, 0x18, 0x00, 0x1E, 0x00, 0x1F };

// Imprime una fila completa para borrar restos de textos anteriores.
// ui: interfaz LCD; y: coordenada vertical; text: contenido alineado a la izquierda.
void drawRow(lcd_ui* ui, uint8_t y, const char* text)
{
	char row[33];
	snprintf(row, sizeof(row), "%-32.32s", text);
	ui->lcd.printStr(1, y, row);
}

// Devuelve un campo IP editable; index selecciona IP, mascara, gateway o DNS.
String* ipField(uint8_t index)
{
	switch (index) {
	case 0: return &config.ip;
	case 1: return &config.mask;
	case 2: return &config.gateway;
	case 3: return &config.dns1;
	default: return &config.dns2;
	}
}

// Dibuja un candado abierto o cerrado para una red escaneada.
// ui: interfaz LCD; x e y: posicion del icono; locked: indica si requiere clave.
void drawLock(lcd_ui* ui, uint8_t x, uint8_t y, bool locked)
{
	ui->lcd.drawRect(x, y + 3, 5, 4, 1);
	if (locked) {
		ui->lcd.drawLine(x + 1, y + 3, x + 1, y + 1, 1);
		ui->lcd.drawLine(x + 3, y + 3, x + 3, y + 1, 1);
		ui->lcd.drawLine(x + 1, y + 1, x + 3, y + 1, 1);
	}
	else {
		ui->lcd.drawLine(x + 1, y + 3, x + 1, y + 1, 1);
		ui->lcd.drawLine(x + 1, y + 1, x + 3, y + 1, 1);
	}
}

// Dibuja barras de señal y RSSI en dBm; ui, x, y y rssi describen la lectura.
void drawSignal(lcd_ui* ui, uint8_t x, uint8_t y, int32_t rssi)
{
	const char code = getWiFiRSSICode(rssi);
	const uint8_t* icon = code == WiFiChar_RSSI_1 ? wifiBars1 :
		code == WiFiChar_RSSI_2 ? wifiBars2 :
		code == WiFiChar_RSSI_3 ? wifiBars3 : wifiBars4;
	ui->lcd.drawBitmap(icon, x, y);
	char value[8];
	snprintf(value, sizeof(value), "%d", rssi);
	ui->lcd.printStr(x + 9, y, value);
}

// Inicia un escaneo y prepara la vista de redes; no recibe parametros.
void startNetworkScan()
{
	view = wifiView::networks;
	networkCount = -1;
	selected = 0;
	if (!config.enabled) WiFi.mode(WIFI_MODE_STA);
	wifiScanStart();
	scanStarted = true;
	message = "Buscando redes...";
}

// Guarda SSID, clave y politica IP y comienza la conexion.
// ui: pantalla activa; openNetwork: red abierta, que no necesita editor de clave.
void selectNetwork(lcd_ui* ui, bool openNetwork)
{
	config.ssid = networks[selected].ssid;
	config.enabled = true;
	config.password = openNetwork ? "" : password;
	if (wifiSaveStationConfig(config)) {
		connectPending = true;
		connectStartedAt = millis();
		message = "Conectando...";
		view = wifiView::menu;
	}
	else message = "No se pudo guardar";
	ui->getWindow()->Title = "Wi-Fi STA";
}

} // namespace

// Muestra la configuracion principal y procesa sus opciones; ui y action son de la pantalla.
bool drawWifiSettings(lcd_ui* ui, UI_Action action)
{
	static const char* const menuItems[] = {
		"Estacion", "Direccion IP", "Editar IP", "Buscar red", "Guardar y aplicar", "Volver"
	};
	static const char* const fieldNames[] = { "IP", "Mascara", "Gateway", "DNS 1", "DNS 2", "Guardar", "Volver" };
	UI_Window* win = ui->getWindow();

	if (action == UI_Action::Init) {
		win->setFullScreen();
		win->setWindowMode(UI_Window_Mode::Normal);
		win->Title = "Wi-Fi STA";
		view = wifiView::menu;
		selected = 0;
		selectedField = 0;
		networkCount = -1;
		connectPending = false;
		message = wifiLoadStationConfig(config) ? "Config. cargada" : "Error leyendo NVS";
	}
	else if (action == UI_Action::Run) {
		ui->lcd.setFont(Small4x5PL);
		if (connectPending && WiFi.isConnected() && WiFi.SSID() == config.ssid) {
			connectPending = false;
			message = "Conectado";
		}
		else if (connectPending && uint32_t(millis() - connectStartedAt) > 20000) {
			connectPending = false;
			message = "Fallo de conexion";
		}

		if (view == wifiView::menu) {
			win->Title = "Wi-Fi STA";
			for (int32_t row = 0; row < 3; ++row) {
				const int32_t index = selected > 1 ? selected - 1 + row : row;
				if (index >= 6) continue;
				char line[33];
				if (index == 0) snprintf(line, sizeof(line), "%c Estacion: %s", index == selected ? '>' : ' ', config.enabled ? "ACTIVA" : "APAGADA");
				else if (index == 1) snprintf(line, sizeof(line), "%c IP: %s", index == selected ? '>' : ' ', config.staticIp ? "ESTATICA" : "DHCP");
				else snprintf(line, sizeof(line), "%c %s", index == selected ? '>' : ' ', menuItems[index]);
				drawRow(ui, row * 10, line);
			}
		char info[33];
		snprintf(info, sizeof(info), "Red: %.18s", config.ssid.c_str());
		drawRow(ui, 31, info);
		drawRow(ui, 41, message.c_str());

			const Keys key = ui->GetKeys();
			if (key == Keys::Esc) ui->Close(UI_DialogResult::Cancel);
			else if (key == Keys::Up) selected = (selected + 5) % 6;
			else if (key == Keys::Down) selected = (selected + 1) % 6;
			else if (key == Keys::Enter) {
				if (selected == 0) { config.enabled = !config.enabled; message = "Cambio pendiente"; }
				else if (selected == 1) { config.staticIp = !config.staticIp; message = "Cambio pendiente"; }
				else if (selected == 2) { view = wifiView::ipFields; selectedField = 0; }
				else if (selected == 3) startNetworkScan();
				else if (selected == 4) {
					if (wifiSaveStationConfig(config)) {
						connectPending = config.enabled && !config.ssid.isEmpty();
						connectStartedAt = millis();
						message = connectPending ? "Guardado; conectando" : "Config. guardada";
					}
					else message = "Error al guardar IP";
				}
				else ui->Close(UI_DialogResult::Cancel);
			}
		}
		else if (view == wifiView::networks) {
			win->Title = "Redes RSSI dBm";
			if (scanStarted) {
				const int32_t result = wifiScanStatus();
				if (result >= 0) {
					networkCount = result < 20 ? result : 20;
					for (int32_t i = 0; i < networkCount; ++i)
						wifiScanGetNetwork(i, networks[i].ssid, networks[i].auth, networks[i].rssi);
					scanStarted = false;
					message = networkCount ? "Elige red" : "No hay redes";
				}
				else if (result == WIFI_SCAN_FAILED) {
					networkCount = 0;
					scanStarted = false;
					message = "Fallo el escaneo";
				}
			}
			if (networkCount < 0) drawRow(ui, 10, "Buscando redes...");
			else if (networkCount == 0) drawRow(ui, 10, message.c_str());
			else {
				const int32_t first = selected > 1 ? selected - 1 : 0;
				for (int32_t row = 0; row < 3; ++row) {
					const int32_t index = first + row;
					if (index >= networkCount) { drawRow(ui, row * 10, ""); continue; }
					char line[24];
					snprintf(line, sizeof(line), "%c %-19.19s", index == selected ? '>' : ' ', networks[index].ssid.c_str());
					drawRow(ui, row * 10, line);
					drawLock(ui, 87, row * 10, networks[index].auth != WIFI_AUTH_OPEN);
					drawSignal(ui, 96, row * 10, networks[index].rssi);
				}
			}
			drawRow(ui, 31, message.c_str());
			drawRow(ui, 41, "Up/Dn elige OK conecta Nx=scan");

			const Keys key = ui->GetKeys();
			if (key == Keys::Esc) {
				if (!config.enabled) WiFi.mode(WIFI_MODE_NULL);
				view = wifiView::menu;
			}
			else if (key == Keys::Up && networkCount > 0) selected = (selected + networkCount - 1) % networkCount;
			else if (key == Keys::Down && networkCount > 0) selected = (selected + 1) % networkCount;
			else if (key == Keys::Next) startNetworkScan();
			else if (key == Keys::Enter && networkCount > 0) {
				password = "";
				if (networks[selected].auth == WIFI_AUTH_OPEN) selectNetwork(ui, true);
				else { view = wifiView::password; passwordChar = 0; }
			}
		}
		else if (view == wifiView::password) {
			win->Title = "Clave Wi-Fi";
			char line[33];
			drawRow(ui, 0, networks[selected].ssid.c_str());
			snprintf(line, sizeof(line), "Clave: %.24s", password.c_str());
			drawRow(ui, 9, line);
			snprintf(line, sizeof(line), "Caracter: %c", passwordChars[passwordChar]);
			drawRow(ui, 18, line);
			drawRow(ui, 27, "Up/Down cambia; Next agrega");
			drawRow(ui, 36, "Izq borra; OK guarda; Esc sale");
			const Keys key = ui->GetKeys();
			if (key == Keys::Esc) view = wifiView::networks;
			else if (key == Keys::Up) passwordChar = (passwordChar + sizeof(passwordChars) - 2) % (sizeof(passwordChars) - 1);
			else if (key == Keys::Down) passwordChar = (passwordChar + 1) % (sizeof(passwordChars) - 1);
			else if (key == Keys::Next && password.length() < 63) password += passwordChars[passwordChar];
			else if (key == Keys::Left && password.length()) password.remove(password.length() - 1);
			else if (key == Keys::Enter) selectNetwork(ui, false);
		}
		else if (view == wifiView::ipFields) {
			win->Title = "Editar IP";
			for (int32_t row = 0; row < 3; ++row) {
				const int32_t index = selectedField > 1 ? selectedField - 1 + row : row;
				if (index >= 7) continue;
				char line[33];
				if (index < 5) snprintf(line, sizeof(line), "%c %-8s %.15s", index == selectedField ? '>' : ' ', fieldNames[index], ipField(index)->c_str());
				else snprintf(line, sizeof(line), "%c %s", index == selectedField ? '>' : ' ', fieldNames[index]);
				drawRow(ui, row * 10, line);
			}
			drawRow(ui, 31, "OK edita/guarda; Esc vuelve");
			drawRow(ui, 41, message.c_str());
			const Keys key = ui->GetKeys();
			if (key == Keys::Esc) view = wifiView::menu;
			else if (key == Keys::Up) selectedField = (selectedField + 6) % 7;
			else if (key == Keys::Down) selectedField = (selectedField + 1) % 7;
			else if (key == Keys::Enter) {
				if (selectedField < 5) { view = wifiView::ipOctet; selectedOctet = 0; }
				else if (selectedField == 5) {
					if (wifiSaveStationConfig(config)) {
						connectPending = config.enabled && !config.ssid.isEmpty();
						connectStartedAt = millis();
						message = connectPending ? "Guardado; conectando" : "Config. guardada";
					}
					else message = "IP invalida o error NVS";
					view = wifiView::menu;
				}
				else view = wifiView::menu;
			}
		}
		else if (view == wifiView::ipOctet) {
			win->Title = "Editar octeto";
			IPAddress address;
			address.fromString(*ipField(selectedField));
			String shown;
			for (uint8_t i = 0; i < 4; ++i) {
				if (i) shown += '.';
				if (i == selectedOctet) shown += '[';
				shown += String(address[i]);
				if (i == selectedOctet) shown += ']';
			}
			char line[33];
			snprintf(line, sizeof(line), "%s: %s", fieldNames[selectedField], shown.c_str());
			drawRow(ui, 2, line);
			snprintf(line, sizeof(line), "Octeto %u/4 = %u", selectedOctet + 1, address[selectedOctet]);
			drawRow(ui, 12, line);
			drawRow(ui, 23, "Up/Down cambia; Next avanza");
			drawRow(ui, 34, "OK aplica; Esc cancela");
			const Keys key = ui->GetKeys();
			if (key == Keys::Esc) view = wifiView::ipFields;
			else if (key == Keys::Up) address[selectedOctet]++;
			else if (key == Keys::Down) address[selectedOctet]--;
			else if (key == Keys::Next) selectedOctet = (selectedOctet + 1) % 4;
			else if (key == Keys::Enter) {
				*ipField(selectedField) = address.toString();
				view = wifiView::ipFields;
			}
		}
	}
	return true;
}
