#include <Arduino.h>
#include "wifiUtils.h"
#include <SPI.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "Freenove_WS2812_Lib_for_ESP32.h"
#include "ST7920_SPI.h"
#include "pins.h"
#include "fonts/fonts.h"
#include "battery.h"
#include "meter.h"
#include "keyboard.h"
#include "webserver.h"
#include "SPIFFS.h"
#include "AsyncMqttClient.h"


TactSwitch BtnEnter, BtnUp, BtnDown, BtnEsc, BtnNext; // Interfase de 5 botones
keyboard Keyboard;                           // Interfase de teclado

ST7920_SPI lcd(pinLcdSS, pinSpiClk, pinSpiSdo);
//ST7920_SPI(int8_t cs, int8_t sclk = -1, int8_t mosi = -1, int8_t miso = -1);

Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(5, pinPixelLed, 1);

AngleRegs angles;

boardButtons_t readButtons();



void Draw_Window(const char* Title, const char* Status);
void Draw_Main();
void Draw_Title(const char* Title);
void Draw_Status(const char* Status);
void Draw_Fase();
void Draw_Angulos();
void Draw_CalibrarFase();
void Draw_Battery();
void Draw_CalibrateCorriente();
void UI();
boardButtons_t getButtons();


void testFormat()
{
  float vals[] = { 1.0, 12.0, 123.0, 1234.0, 12345.0, 123456.0, 1234567.0, 12345678.0, 123456789.0 };
  float divs[] = { 0.0001, 0.001, 0.01, 0.1, 1.0, 10.0, 100.0, 1000.0, 10000.0, 100000.0 };
  int32_t pos = 0, dec = 0;


  for (dec = 0; dec < 10; dec++) {
    Serial.printf("Paso: %d -> ", dec);
    for (pos = 0; pos < 9; pos++) {
      float x = vals[pos] * divs[dec];
      Serial.printf("%-20s ", ade.format(x, 5, "V").c_str());
    }
    Serial.println("");
  }
}


void setup(void)
{

  Serial.begin(115200);

  UtilsInitSettings();
  UtilsLoadDeafultSettings();
  DebugStart();

  pinMode(pinRelay, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);
  pinMode(pinLedOk, OUTPUT);
  pinMode(pinMemSS, OUTPUT);
  pinMode(pinMem2SS, OUTPUT);
  analogWrite(pinLcdBL, 128);

  digitalWrite(pinMemSS, 0);
  digitalWrite(pinMem2SS, 0);
  digitalWrite(pinBuzzer, 0);
  digitalWrite(pinRelay, 0);

  // Inicializar las teclas
  BtnEsc.begin(Keys::Esc, []()->bool { return getButtons() != buttonCancel;});
  BtnDown.begin(Keys::Down, []()->bool { return getButtons() != buttonDown;});
  BtnUp.begin(Keys::Up, []()->bool { return getButtons() != buttonUp;});
  BtnEnter.begin(Keys::Enter, []()->bool { return getButtons() != buttonOk;});
  BtnNext.begin(Keys::Next, []()->bool { return getButtons() != buttonNext;});

  // Agregar teclas al teclado
  Keyboard.AddButton(&BtnEnter);
  Keyboard.AddButton(&BtnUp);
  Keyboard.AddButton(&BtnDown);
  Keyboard.AddButton(&BtnEsc);
  Keyboard.AddButton(&BtnNext);

  ledcSetup(4, 1000, 10);
  ledcAttachPin(pinBuzzer, 4);

  lcd.init();
  lcd.cls();
  lcd.display(0);

  WifiStart();
  TimeStart();
  OTAStart();
  MQTTStart();

  WiFi.onEvent([](arduino_event_t* event) {
    Serial.println("Conectado a Wifi");
    Init_WebServer();
    }, ARDUINO_EVENT_WIFI_STA_CONNECTED);

  strip.begin();
  strip.setAllLedsColor(0);


  debugI("Iniciando sistema de archivos... ");
  if (!SPIFFS.begin(true))
    debugI("An error has occurred while mounting SPIFFS");
  else
    debugI("SPIFFS mounted successfully");
  MeterInit();

  Batt.Init();

  debugI("Inicialización terminada!");
}


uint32_t lastTime = 0, counter = 0, send = 0, bytes = 0, leds = 0, update = 0, screen = 0;
uint8_t color = 0;

void loop(void)
{

  Keyboard.scan();

  Batt.Loop();

  UtilsLoop();

  MeterLoop();

  UI();


  if (millis() - send > 999) {
    send = millis();

    if (mqtt.connected()) {
      static String data;
      static uint32_t batt = 0;

      Meter.getJsonBasic(data);
      mqtt.publish("PowerMeter9000/meter", 0, false, data.c_str());
      Meter.getJsonEnergy(data);
      mqtt.publish("PowerMeter9000/meter/energy", 0, false, data.c_str());
      Meter.getJsonFastMeasures(data);
      mqtt.publish("PowerMeter9000/meter/fast", 0, false, data.c_str());
      data = Meter.phaseR.getJson();
      mqtt.publish("PowerMeter9000/meter/r", 0, false, data.c_str());
      data = Meter.phaseS.getJson();
      mqtt.publish("PowerMeter9000/meter/s", 0, false, data.c_str());
      data = Meter.phaseT.getJson();
      mqtt.publish("PowerMeter9000/meter/t", 0, false, data.c_str());

      if (batt++ > 4) {
        batt = 0;
        Batt.getJson(data);
        mqtt.publish("PowerMeter9000/batery", 0, false, data.c_str());
      }

    }
  }

  if (millis() - leds > 24) {
    leds = millis();

    uint32_t time = micros();
    strip.setLedColorData(0, color, color, color);
    strip.setLedColorData(1, color, 0, 0);
    strip.setLedColorData(2, 0, color, 0);
    strip.setLedColorData(3, 0, 0, color);
    strip.setLedColorData(4, color, color, color);

    color++;
    color &= 0x1F;

    strip.show();
    time = micros() - time;
  }


  if (millis() - lastTime > 199) {
    lastTime = millis();
    static uint32_t p = 0, sum = 0, total;

    uint32_t update = micros();

    //lcd.display(0, p);
    update = micros() - update;

    sum += update;
    p++;
    if (p > 7) { p = 0;  total = sum;  sum = 0; };
    // Serial.printf("Velocidad de dibujo: %uus, total: %uus, p:%d , heap: %d\n", update, total, p, ESP.getFreeHeap());

    counter = 0;
    bytes = 0;
  }



}


uint32_t indexUI = 0;

void UI()
{
  if (indexUI == 0)
    Draw_Main();
  else if (indexUI == 1)
    Draw_Fase();
  else if (indexUI == 2)
    Draw_Angulos();
  else if (indexUI == 3)
    Draw_CalibrarFase();
  else if (indexUI == 4)
    Draw_CalibrateCorriente();
}


void Draw_Main()
{
  const uint32_t c1 = 3, c2 = 48, c3 = 87, c4 = 125;
  const uint32_t l1 = 9, l2 = 19, l3 = 29, l4 = 39, l5 = 49;

  static uint32_t update = 0;
  static int32_t index = 0, mode = 0;
  char str[128];

  if (millis() - update > 333) {
    update = millis();


    switch (index) {
    case 0:         //Dibujar la pantalla principal
      Draw_Window("MAIN", "Next");
      lcd.setFont(Small5x7PLBold);

      lcd.printStr(c1, l1, "R:");
      if (mode)
        lcd.printStr(c2, l1, ade.format(Meter.phaseR.VVrms, 5, "V").c_str(), TextAling::TopRight);
      else
        lcd.printStr(c2, l1, ade.format(Meter.phaseR.Vrms, 5, "V").c_str(), TextAling::TopRight);
      lcd.printStr(c3, l1, ade.format(Meter.phaseR.Irms, 5, "A").c_str(), TextAling::TopRight);
      lcd.printStr(c4, l1, ade.format(Meter.phaseR.Watt, 4, "W").c_str(), TextAling::TopRight);

      lcd.printStr(c1, l2, "S:");
      if (mode)
        lcd.printStr(c2, l2, ade.format(Meter.phaseS.VVrms, 5, "V").c_str(), TextAling::TopRight);
      else
        lcd.printStr(c2, l2, ade.format(Meter.phaseS.Vrms, 5, "V").c_str(), TextAling::TopRight);
      lcd.printStr(c3, l2, ade.format(Meter.phaseS.Irms, 5, "A").c_str(), TextAling::TopRight);
      lcd.printStr(c4, l2, ade.format(Meter.phaseS.Watt, 4, "W").c_str(), TextAling::TopRight);

      lcd.printStr(c1, l3, "T:");
      if (mode)
        lcd.printStr(c2, l3, ade.format(Meter.phaseT.VVrms, 5, "V", 1).c_str(), TextAling::TopRight);
      else
        lcd.printStr(c2, l3, ade.format(Meter.phaseT.Vrms, 5, "V", 1).c_str(), TextAling::TopRight);
      lcd.printStr(c3, l3, ade.format(Meter.phaseT.Irms, 5, "A", 1).c_str(), TextAling::TopRight);
      lcd.printStr(c4, l3, ade.format(Meter.phaseT.Watt, 4, "W").c_str(), TextAling::TopRight);

      lcd.printStr(c1, l4, "N:");
      lcd.printStr(c2, l4, ade.format(Meter.neutral.Vrms, 5, "V", 1).c_str(), TextAling::TopRight);
      lcd.printStr(c3, l4, ade.format(Meter.neutral.Irms, 5, "A", 1).c_str(), TextAling::TopRight);

      lcd.printStr(c1, l5, "E:");
      lcd.printStr(c2 + 10, l5, ade.format(Meter.energy.Watt_H, 5, "Wh", 1).c_str(), TextAling::TopRight);
      lcd.printStr(c3, l5, "Pt:", TextAling::TopRight);
      lcd.printStr(c4, l5, ade.format(Meter.power.Watt, 5, "W", 1).c_str(), TextAling::TopRight);
      break;

    case 1:     ///Pantalla energía
      Draw_Window("ENERGIA", "Next");
      lcd.setFont(Small5x7PLBold);
      lcd.printStr(c1, l1, "R:");
      lcd.printStr(c4, l1, ade.format(Meter.phaseR.Watt_H, 6, "Wh").c_str(), TextAling::TopRight);

      lcd.printStr(c1, l2, "S:");
      lcd.printStr(c4, l2, ade.format(Meter.phaseS.Watt_H, 6, "Wh").c_str(), TextAling::TopRight);

      lcd.printStr(c1, l3, "T:");
      lcd.printStr(c4, l3, ade.format(Meter.phaseT.Watt_H, 6, "Wh").c_str(), TextAling::TopRight);

      lcd.printStr(c1, l5, "TOTAL:");
      lcd.printStr(c4, l5, ade.format(Meter.energy.Watt_H, 6, "Wh", 1).c_str(), TextAling::TopRight);
      break;

    case 2:     ///Pantalla angulos
      Draw_Window("ANGULOS", "Next");
      lcd.setFont(Small5x7PLBold);

      lcd.printStr(c2, l1, "Volt", TextAling::TopRight);
      lcd.printStr(c3, l1, "VI", TextAling::TopRight);
      lcd.printStr(c4, l1, "Corr", TextAling::TopRight);

      lcd.printStr(c1, l2, "R:");
      lcd.printStr(c2, l2, ade.format(Meter.phaseR.AngleV, 5, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      lcd.printStr(c3, l2, ade.format(Meter.phaseR.AngleVI, 4, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      lcd.printStr(c4, l2, ade.format(Meter.phaseR.AngleI, 4, "g", formatNoPrefix).c_str(), TextAling::TopRight);

      lcd.printStr(c1, l3, "S:");
      lcd.printStr(c2, l3, ade.format(Meter.phaseS.AngleV, 5, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      lcd.printStr(c3, l3, ade.format(Meter.phaseS.AngleVI, 4, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      lcd.printStr(c4, l3, ade.format(Meter.phaseS.AngleI, 4, "g", formatNoPrefix).c_str(), TextAling::TopRight);

      lcd.printStr(c1, l4, "T:");
      lcd.printStr(c2, l4, ade.format(Meter.phaseT.AngleV, 5, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      lcd.printStr(c3, l4, ade.format(Meter.phaseT.AngleVI, 4, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      lcd.printStr(c4, l4, ade.format(Meter.phaseT.AngleI, 4, "g", formatNoPrefix).c_str(), TextAling::TopRight);
      break;
    }

    lcd.display(0);
  }
  Keys key = Keyboard.getNextKey();

  if (key == Keys::Esc)
    ;
  else if (key == Keys::Enter)
    ;
  else if (key == Keys::Next)
    mode++;
  else if (key == Keys::Up)
    index++;
  else if (key == Keys::Down)
    index--;

  if (index > 2) index = 0;
  else if (index < 0) index = 2;
  if (mode > 1) mode = 0;
}

void Draw_Fase()
{
  const uint32_t c1 = 0, c2 = 50, c3 = 64, c4 = 90;
  const uint32_t l1 = 9, l2 = 19, l3 = 29, l4 = 39, l5 = 49;

  static int32_t fase = 0;
  if (millis() - update > 199) {
    update = millis();

    char str[128];
    phaseValues vals;
    lcd.fillRect(0, 0, 128, 64, 0);
    if (fase == 0)
      vals = Meter.neutral;
    else if (fase == 1)
      vals = Meter.phaseR;
    else if (fase == 2)
      vals = Meter.phaseS;
    else if (fase == 3)
      vals = Meter.phaseT;

    lcd.fillRect(0, 0, 128, 64, 0);
    Draw_Window(vals.Name, "");

    lcd.setFont(Small5x7PLBold);
    lcd.printStr(c1, l1, ade.format(vals.Vrms, 6, "V").c_str());
    lcd.printStr(c2, l1, ade.format(vals.Irms, 5, "A").c_str());
    lcd.printStr(c4, l1, ade.format(vals.Freq, 5, "Hz").c_str());

    lcd.printStr(c1, l2, ade.format(vals.Vthd, 4, "%", formatNoPrefix).c_str());
    lcd.printStr(c2, l2, ade.format(vals.Ithd, 4, "%", formatNoPrefix).c_str());
    lcd.printStr(c4, l2, "THD");

    lcd.printStr(c1, l3, ade.format(vals.Watt, 5, "W").c_str());
    lcd.printStr(c3, l3, ade.format(vals.PowerFactor, 5, "PF", formatNoPrefix).c_str());

    lcd.printStr(c1, l4, ade.format(vals.VAR, 5, "VAR").c_str());
    lcd.printStr(c3, l4, ade.format(vals.AngleVI, 5, "°", formatNoPrefix).c_str());

    lcd.printStr(c1, l5, ade.format(vals.VA, 5, "VA").c_str());
    lcd.printStr(c3, l5, ade.format(vals.Watt_H, 5, "Wh").c_str());

    lcd.display(0);
  }

  Keys key = Keyboard.getNextKey();

  if (key == Keys::Esc)
    indexUI = 0;
  else if (key == Keys::Up) {
    fase++;
    if (fase > 3)
      fase = 0;
  }
  else if (key == Keys::Down) {
    fase--;
    if (fase < 0)
      fase = 3;
  }
}



void Draw_Angulos()
{
  if (millis() - update > 199) {
    update = millis();


    char str[128];
    lcd.fillRect(0, 0, 128, 64, 0);

    Draw_Window("ANGULOS", "");

    AngleRegs angles;
    // ade.readAngleRegsnValues(&angles);

    lcd.setFont(Small5x7PLBold);
    snprintf(str, sizeof(str), "R: %3.2f S:%3.2f T:%3.2f", angles.AngleValue_VA_IA, angles.AngleValue_VB_IB, angles.AngleValue_VC_IC);
    lcd.printStr(1, 10, str);

    snprintf(str, sizeof(str), "VRS:%3.2f  VRT:%3.2f", angles.AngleValue_VA_VB, angles.AngleValue_VA_VC);
    lcd.printStr(1, 19, str);

    snprintf(str, sizeof(str), "VST:%3.2f", angles.AngleValue_VB_VC);
    lcd.printStr(1, 28, str);

    snprintf(str, sizeof(str), "IRS:%3.2f  IRT:%3.2f", angles.AngleValue_IA_IB, angles.AngleValue_IA_IC);
    lcd.printStr(1, 37, str);

    snprintf(str, sizeof(str), "IST:%3.2f", angles.AngleValue_IB_IC);
    lcd.printStr(1, 46, str);

    lcd.display(0);
  }
  if (Keyboard.getNextKey() == Keys::Esc)
    indexUI = 0;
}


void Draw_CalibrarFase()
{
  const uint32_t c1 = 0, c2 = 10, c3 = 55, c4 = 100;
  const uint32_t l1 = 9, l2 = 19, l3 = 29, l4 = 39, l5 = 49;
  static  calibratePhaseResult r, s, t, n;

  if (millis() - update > 999) {
    update = millis();

    // r = ade.phaseCalibrate('R');
    // s = ade.phaseCalibrate('S');
    // t = ade.phaseCalibrate('T');
    n.angle = (r.angle + s.angle + t.angle) / 3;
    n.factor = ((int64_t)r.factor + (int64_t)s.factor + (int64_t)t.factor) / 3LL;

    char str[128];
    lcd.fillRect(0, 0, 128, 64, 0);

    lcd.fillRect(0, 0, 128, 64, 0);

    Draw_Window("CALIBRAR FASE", "");

    lcd.setFont(Small5x7PLBold);
    lcd.printStr(c1, l1, "R");
    lcd.printStr(c2, l1, ade.format(Meter.phaseR.Watt, 6, "W").c_str());
    lcd.printStr(c3, l1, ade.format(Meter.phaseR.VAR, 4, "VAR").c_str());
    lcd.printStr(c4, l1, ade.format(r.angle, 5, "°", formatNoPrefix).c_str());

    lcd.printStr(c1, l2, "S");
    lcd.printStr(c2, l2, ade.format(Meter.phaseS.Watt, 6, "W").c_str());
    lcd.printStr(c3, l2, ade.format(Meter.phaseS.VAR, 4, "VAR").c_str());
    lcd.printStr(c4, l2, ade.format(s.angle, 5, "°", formatNoPrefix).c_str());

    lcd.printStr(c1, l3, "T");
    lcd.printStr(c2, l3, ade.format(Meter.phaseT.Watt, 6, "W").c_str());
    lcd.printStr(c3, l3, ade.format(Meter.phaseT.VAR, 4, "VAR").c_str());
    lcd.printStr(c4, l3, ade.format(t.angle, 5, "°", formatNoPrefix).c_str());

    lcd.printStr(c1, l4, "N");
    lcd.printStr(c4, l4, ade.format(n.angle, 5, "°", formatNoPrefix).c_str());

    lcd.display(0);
  }

  Keys key = Keyboard.getNextKey();

  if (key == Keys::Esc)
    indexUI = 0;
  else if (key == Keys::Enter) {
    Serial.printf("Calibracion de fases: R:%.2f [0x%X], S:%.2f [0x%X], T:%.2f [0x%X], N:%.2f [0x%X]\n", r.angle, r.factor, s.angle, s.factor, t.angle, t.factor, n.angle, n.factor);
    indexUI = 0;
  }
}



void Draw_CalibrateCorriente()
{
  const float calStart[] = { 1.0, 22.5, 0.0, 235.0, 0.0, 0.0, 1000.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  const uint32_t c1 = 0, c2 = 10, c3 = 55, c4 = 100;
  const uint32_t l1 = 9, l2 = 19, l3 = 29, l4 = 39, l5 = 49;
  const float steps[] = { 0.0001, 0.001, 0.01, 0.1, 1 };
  static bool running = false;
  static uint32_t calTime = micros(), step = 0;
  static int32_t  samples = 0;
  static float realValue = 20.0, inc, multiplier = 1.0;
  static calibrationStep_t type = calCurrentGain;
  char str[64];
  static class calibrationInfo info;



  inc = steps[step];
  if (running) {
    if (micros() - calTime > 33332) {
      calTime = micros();
      samples = ade.updateCalibration(realValue * multiplier, &info);
    }
  }

  if (millis() - update > 199) {
    update = millis();

    char str[128];
    lcd.fillRect(0, 0, 128, 64, 0);

    lcd.fillRect(0, 0, 128, 64, 0);
    Draw_Window("CALIBRACION", "");


    if (running) {
      lcd.setFont(Small5x7PLBold);
      lcd.printStr(c1, l1, "R");
      lcd.printStr(c1, l2, "S");
      lcd.printStr(c1, l3, "T");

      switch (type) {
      case calCurrentGain:
      case calCurrentOffset:
      case calFundCurrentOffset:
      case calCurrentOneOffset:
      case calCurrentTenOffset:
        multiplier = 5.0;
        lcd.printStr(c3, l1, "N");
        lcd.printStr(c2, l1, ade.format(info.values.A / multiplier, 6, "A").c_str());
        lcd.printStr(c3 + 12, l1, ade.format(info.values.N / multiplier, 6, "A").c_str());
        lcd.printStr(c2, l2, ade.format(info.values.B / multiplier, 6, "A").c_str());
        lcd.printStr(c2, l3, ade.format(info.values.C / multiplier, 6, "A").c_str());
        break;

      case calVoltageGain:
      case calVoltageOffset:
      case calFundVoltageOffset:
      case calVoltageOneOffset:
      case calVoltageTenOffset:
        multiplier = 1;
        lcd.printStr(c2, l1, ade.format(info.values.A, 6, "V").c_str());
        lcd.printStr(c2, l2, ade.format(info.values.B, 6, "V").c_str());
        lcd.printStr(c2, l3, ade.format(info.values.C, 6, "V").c_str());
        break;

      case calPowerGain:
      case calActivePowerOffset:
      case calReactivePowerOffset:
      case calFundActivePowerOffset:
      case calFundReactivePowerOffset:
        lcd.printStr(c2, l1, "Aun no implementado");
        break;

      case calPhaseGain:
        multiplier = 1;
        lcd.printStr(c2, l1, ade.format(info.values.A, 6, "g", formatNoPrefix).c_str());
        lcd.printStr(c2, l2, ade.format(info.values.B, 6, "g", formatNoPrefix).c_str());
        lcd.printStr(c2, l3, ade.format(info.values.C, 6, "g", formatNoPrefix).c_str());
        lcd.printStr(c3 + 12, l1, ade.format(Meter.average.Irms / 5.0, 5, "A").c_str());
        break;
      }

      lcd.printStr(c4, l3, ade.format(samples, 3, "s", formatNoPrefix).c_str());
      snprintf(str, sizeof(str), "Real: %.4f.  I:%.4f", realValue, inc);
      lcd.printStr(c1, l4, str);
      lcd.printStr(c1, l5, "ENTER para terminar");
    }
    else {
      lcd.setFont(Small5x7PLBold);
      lcd.printStr(1, l2, "Presione ENTER para");
      lcd.printStr(1, l3, "empezar calibracion");

      static uint32_t blink = 0;
      if (millis() - blink > 999)
        blink = millis();
      else if (millis() - blink < 500)
        lcd.printStr(1, l4, calibrationStepString(type));
    }

    lcd.display(0);
  }

  Keys key = Keyboard.getNextKey();

  if (key == Keys::Esc) {
    indexUI = 0;
    if (running)
      ade.endCalibration(false);
    running = false;
  }
  else if (key == Keys::Up) {
    if (running)
      realValue += inc;
    else {
      type++;
      realValue = calStart[static_cast<int>(type)];
    }
  }
  else if (key == Keys::Down) {
    if (running)
      realValue -= inc;
    else {
      type--;
      realValue = calStart[static_cast<int>(type)];
    }
  }
  else if (key == Keys::Next) {
    if (running) {
      step++;
      if (step > 4) step = 0;
    }
  }
  else if (key == Keys::Enter) {
    if (!running) {
      if (type != calNone) {
        running = true;
        ade.startCalibration(type, true, true, true, true);
      }
      else {
        indexUI = 0;
        running = false;
      }
    }
    else {
      ade.endCalibration(true);
      indexUI = 0;
      running = false;
    }
  }
}



const uint8_t icon_cross[] = { 3, 5, 0x0A, 0x04, 0x0A };
const uint8_t icon_warn[] = { 3, 5,  0x00, 0x17, 0x00 };

const uint8_t icon_batt_empty[] = { 8, 5, 0x0E, 0x1F, 0x11, 0x11 , 0x11 , 0x11, 0x11, 0x1F };
const uint8_t icon_batt_1[] = { 8, 5, 0x0E, 0x1F, 0x11, 0x11 , 0x11 , 0x11, 0x1F, 0x1F };
const uint8_t icon_batt_2[] = { 8, 5, 0x0E, 0x1F, 0x11, 0x11 , 0x11 , 0x1F, 0x1F, 0x1F };
const uint8_t icon_batt_3[] = { 8, 5, 0x0E, 0x1F, 0x11, 0x11 , 0x1F , 0x1F, 0x1F, 0x1F };
const uint8_t icon_batt_4[] = { 8, 5, 0x0E, 0x1F, 0x11, 0x1F , 0x1F , 0x1F, 0x1F, 0x1F };
const uint8_t icon_batt_full[] = { 8, 5, 0x0E, 0x1F, 0x1F, 0x1F , 0x1F , 0x1F, 0x1F, 0x1F };

const uint8_t icon_wifi_1[] = { 8, 5, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t icon_wifi_2[] = { 8, 5, 0x00, 0x10, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00 };
const uint8_t icon_wifi_3[] = { 8, 5, 0x00, 0x10, 0x00, 0x18, 0x00, 0x1E, 0x00, 0x00 };
const uint8_t icon_wifi_4[] = { 8, 5, 0x00, 0x10, 0x00, 0x18, 0x00, 0x1E, 0x00, 0x1F };
const uint8_t icon_plug[] = { 8, 5, 0x0E, 0x0E, 0x0E, 0x0E, 0x1F, 0x1F, 0x0A, 0x0A };


void Draw_Window(const char* Title, const char* Status)
{
  lcd.fillRect(0, 0, 128, 64, 0);

  Draw_Title(Title);

  int32_t r = 2, x = 0, y = 6, w = 127, h = 57;
  lcd.drawLine(x + r, y, w - r, y, 1);    //Top
  lcd.drawLine(x + r, h, w - r, h, 1);    //Bottom
  lcd.drawLine(x, y + r, x, h - r, 1);    //Left
  lcd.drawLine(w, y + r, w, h - r, 1);    //Right

  lcd.drawLine(x + r, y, x, y + r, 1);    //TL
  lcd.drawLine(w - r, y, w, y + r, 1);    //TR
  lcd.drawLine(x + r, h, x, h - r, 1);    //BL
  lcd.drawLine(w - r, h, w, h - r, 1);    //Br

  Draw_Status(Status);
}

void Draw_Title(const char* Title)
{

  char str[128];
  int32_t posX = 0, posY = 0, lenLeft, lenRight, width;
  bool Invert = false;

  if (Invert) { posY++; posX++; }

  lcd.fillRect(0, 0, 128, 6, 0);
  lcd.setFont(Small4x5PL);

  //Power el icono del wifi
  lcd.printChar(posX, posY, 'W');
  lenLeft = posX = 5;
  char rssi = getWiFiRSSICode();
  if (WiFi.isConnected()) {
    if (rssi == WiFiChar_RSSI_1)
      lcd.drawBitmap(icon_wifi_1, posX, posY);
    else if (rssi == WiFiChar_RSSI_2)
      lcd.drawBitmap(icon_wifi_2, posX, posY);
    else if (rssi == WiFiChar_RSSI_3)
      lcd.drawBitmap(icon_wifi_3, posX, posY);
    else if (rssi == WiFiChar_RSSI_4)
      lcd.drawBitmap(icon_wifi_4, posX, posY);
    lenLeft += 8; //Ancho del icono
  }
  else {
    lcd.drawBitmap(icon_wifi_1, posX, posY);
    lcd.drawBitmap(icon_cross, posX + 3, posY);
    lenLeft += 8; //Ancho del icono
  }

  //Calcular ancho de los iconos de la derecha
  lenLeft += 3; //Separacion
  lenRight = 9 + 8 + 3; //Icono bateria y fuente y separacion
  if (Batt.getSource() == PowerSource::Battery) lenRight += 4;  //Icono de error
  if (Batt.getFault() != BatteryFault::None) lenRight += 5;  //Icono de error
  if (Invert) { lenLeft++; lenRight++; }

  //Dibujar el titulo, centrado, si pisa los iconos, moverlo.
  //Title = "lcd.drawBitmap(icon_batt_empty, posX, posY);";
  width = lcd.strWidth(Title);
  posX = (128 - width) / 2;
  if (posX < lenLeft) posX = lenLeft;
  lcd.printStr(posX, posY, Title);
  if (width > (128 - lenLeft - lenRight)) lcd.fillRect(128 - lenRight, posY, lenRight, 7, 0);

  //Dibujar los íconos de la batería y la fuente
  posX = 127 - lenRight + 3;  //Dejar 3 pix de separacion
  if (Batt.getSource() != PowerSource::Battery) {
    lcd.drawBitmap(icon_plug, posX, posY);
    posX += 10;
  }
  else {
    lcd.drawBitmap(icon_plug, posX, posY);
    lcd.drawBitmap(icon_cross, posX + 9, posY);
    posX += 13;
  }

  if (!Batt.isBatteryPresent()) {
    lcd.drawBitmap(icon_batt_empty, posX, posY);
    lcd.drawBitmap(icon_warn, posX + 9, posY);
  }
  else if (Batt.getFault() == BatteryFault::None || Batt.isCharging()) {
    static int32_t porcent = 0;
    if (Batt.isCharging()) {
      porcent += 10;
      if (porcent > 100) porcent = 0;
    }
    else
      porcent = Batt.getPercent();

    if (porcent < 10)
      lcd.drawBitmap(icon_batt_empty, posX, posY);
    else if (porcent < 30)
      lcd.drawBitmap(icon_batt_1, posX, posY);
    else if (porcent < 50)
      lcd.drawBitmap(icon_batt_2, posX, posY);
    else if (porcent < 70)
      lcd.drawBitmap(icon_batt_3, posX, posY);
    else if (porcent < 90)
      lcd.drawBitmap(icon_batt_4, posX, posY);
    else
      lcd.drawBitmap(icon_batt_full, posX, posY);
  }
  else {
    lcd.drawBitmap(icon_batt_empty, posX, posY);
    lcd.drawBitmap(icon_cross, posX + 9, posY);
  }

  // if (!Invert) lcd.drawLineHfast(0, 127, 6, 1);
  // else lcd.fillRect(0, 0, 128, 7, 2);


  // snprintf(str, sizeof(str), "%02d/%02d/%02d", time.tm_mday, time.tm_mon + 1, time.tm_year - 100);
  // width = lcd.strWidth(str);
  // lcd.printStr(127 - width, 0, str);
}

void Draw_Status(const char* Status)
{
  char str[64];
  int32_t posY = 59, posX = 1, width;
  struct tm time = getTime();

  lcd.setFont(Small4x5PL);
  if (mqtt.connected())
    lcd.printStr(posX, posY, "MT");
  else
    lcd.printStr(posX, posY, "..");

  if (Status)
    lcd.printStr(63, posY, Status, TextAling::TopCenter);

  snprintf(str, sizeof(str), "%02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);
  width = lcd.strWidth(str);
  lcd.printStr(128 - width, posY, str);

  // lcd.drawLineHfast(0, 127, 57, 1);
  // lcd.fillRect(0, 57, 128, 7, 2);
}




boardButtons_t checkButtons(bool reset = false)
{
  static boardButtons_t lastbtn;
  boardButtons_t btn = readButtons();

  if (lastbtn != btn) {
    lastbtn = btn;
  }
  return btn;
}

boardButtons_t readButtons()
{
  static uint32_t press;
  static boardButtons_t lastButton, button;

  uint32_t volt = analogReadMilliVolts(pinBtns), hyst = 100;

  if (volt > (miliVolt_ButtonCancel - hyst) && volt < (miliVolt_ButtonCancel + hyst))
    button = buttonCancel;
  else if (volt > (miliVolt_ButtonDown - hyst) && volt < (miliVolt_ButtonDown + hyst))
    button = buttonDown;
  else if (volt > (miliVolt_ButtonUp - hyst) && volt < (miliVolt_ButtonUp + hyst))
    button = buttonUp;
  else if (volt > (miliVolt_ButtonNext - hyst) && volt < (miliVolt_ButtonNext + hyst))
    button = buttonNext;
  else if (volt > (miliVolt_ButtonOk - hyst) && volt < (miliVolt_ButtonOk + hyst))
    button = buttonOk;
  else
    button = buttonNone;

  if (button != lastButton) {
    lastButton = button;
    button = buttonNone;
    press = millis();
  }
  else {
    if (millis() - press < 40)
      button = buttonNone;
  }
  return button;
}


boardButtons_t getButtons()
{
  uint32_t volt = analogReadMilliVolts(pinBtns), hyst = 100;

  if (volt > (miliVolt_ButtonCancel - hyst) && volt < (miliVolt_ButtonCancel + hyst))
    return buttonCancel;
  else if (volt > (miliVolt_ButtonDown - hyst) && volt < (miliVolt_ButtonDown + hyst))
    return buttonDown;
  else if (volt > (miliVolt_ButtonUp - hyst) && volt < (miliVolt_ButtonUp + hyst))
    return buttonUp;
  else if (volt > (miliVolt_ButtonNext - hyst) && volt < (miliVolt_ButtonNext + hyst))
    return buttonNext;
  else if (volt > (miliVolt_ButtonOk - hyst) && volt < (miliVolt_ButtonOk + hyst))
    return buttonOk;
  else
    return buttonNone;
}

