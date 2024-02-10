#include <Arduino.h>
#include <SPI.h>
#include "ESPAsyncWebServer.h"
#include "SPIFFS.h"
#include "Freenove_WS2812_Lib_for_ESP32.h"
#include "ST7920_SPI.h"
#include "pins.h"
#include "fonts/fonts.h"
#include "BQ25896.h"
#include "meter.h"
#include "esp_sntp.h"
#include "keyboard.h"


TactSwitch BtnEnter, BtnUp, BtnDown, BtnEsc, BtnNext; // Interfase de 5 botones
keyboard Keyboard;                           // Interfase de teclado

ST7920_SPI lcd(pinLcdSS, pinSpiClk, pinSpiSdo);
//ST7920_SPI(int8_t cs, int8_t sclk = -1, int8_t mosi = -1, int8_t miso = -1);

Freenove_ESP32_WS2812 strip = Freenove_ESP32_WS2812(5, pinPixelLed, 1);

BQ25896  battery_charging(Wire);

AngleRegs angles;

boardButtons_t readButtons();


void Draw_Main();
void Draw_Title(const char* Title);
void Draw_Fase();
void Draw_Angulos();
void Draw_Battery();
void UI();
boardButtons_t getButtons();

const uint8_t NOMINAL_INPUT_VOLTAGE = 220;
const float NOMINAL_INPUT_CURRENT = 1;/* Current nominal RMS for Input in Amperes */
const uint8_t INPUT_FREQUENCY = 50;/* Frequency in Hertz */
const uint8_t CALIBRATION_ANGLE_DEGREES = 60;/* Used for funtion transform current in Ohm */
const float BURDEN_RESISTOR = 11.24;/* Current nominal RMS for Input in Amperes */
const uint16_t TURNS_RATIO_TRANSFORMER = 5000;/* Used for funtion transform current */
const float ATTEUNATION_FACTOR = 1001;/* The defaul atteunation factor on board used funtion transform in Ohm/Ohm ((R1 + R2)/ R2) */
const uint8_t ACCUMULATION_TIME = 5;/* Accumulation time in seconds when EGY_TIME=7999, accumulation mode= sample based */

void initWiFi() {
  // Initialize WiFi
  IPAddress staticIP(192, 168, 1, 90);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 1, 30);
  IPAddress dns2(8, 8, 8, 8);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.config(staticIP, gateway, subnet, dns, dns);
  WiFi.begin("Wifi-Luis", "");
  Serial.print("Connecting to WiFi ..");

  WiFi.onEvent([](arduino_event_t* event) {
    Serial.println(WiFi.localIP());
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  WiFi.onEvent([](arduino_event_t* event) {
    Serial.println("Conectado a Wifi");
    configTime(-3600 * 3, 0, "ar.pool.ntp.org");
    }, ARDUINO_EVENT_WIFI_STA_CONNECTED);

  sntp_set_time_sync_notification_cb([](struct timeval* tv) {
    Serial.println("\n----Time Sync-----");
    Serial.println(tv->tv_sec);
    Serial.println(ctime(&tv->tv_sec));
    });
}


void setup(void)
{

  Serial.begin(115200);

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

  Wire.begin(pinSDA, pinSCL);
  Wire.setClock(400000);

  lcd.init();
  lcd.cls();
  lcd.display(0);

  strip.begin();
  strip.setAllLedsColor(0);

  initWiFi();

  battery_charging.begin();

  battery_charging.setPreCharge_Current_Limit(0.15);
  battery_charging.setFast_Charge_Current_Limit(0.35);
  battery_charging.setInput_Current_Limit(false);
  battery_charging.setChargeEnable(true);

  MeterInit();

  Serial.println("Set up finished");
}


uint32_t lastTime = 0, counter = 0, send = 0, bytes = 0, leds = 0, update = 0, screen = 0;
uint8_t color = 0;

void loop(void)
{
  Keyboard.scan();

  MeterLoop();

  UI();
  // ledcWriteNote(4, NOTE_A, 4);
      // ledcWrite(4, 0);


  if (micros() - send > 95999) {  //Enviar cada 96ms 
    send = micros();
    // for (uint8_t i = 0; i < 3; i++) {
    //   fillWaveBuffer();
    //   compressWaveBuffer12(i);
    // }
    // uint8_t* p = waveBuffer;
    // size_t size = sizeof(waveBuffer);
    // bytes += size;
    // counter++;
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



    uint32_t tbatt = micros();
    battery_charging.properties();
    tbatt = micros() - tbatt;
    Serial.printf("Tiempo de lectura de BQ25895: %u us\n\n", tbatt);

    // Serial.println("Battery Management System Parameter : \n===============================================================");
    // Serial.print("VBUS : "); Serial.println(battery_charging.getVBUS());
    // Serial.print("VSYS : "); Serial.println(battery_charging.getVSYS());
    // Serial.print("VBAT : "); Serial.println(battery_charging.getVBAT());
    // Serial.print("ICHG : "); Serial.println(battery_charging.getICHG(), 4);
    // Serial.print("TSPCT : "); Serial.println(battery_charging.getTSPCT());
    // Serial.print("Temperature : "); Serial.println(battery_charging.getTemperature());

    // Serial.print("FS_Current Limit : "); Serial.println(battery_charging.getFast_Charge_Current_Limit());
    // Serial.print("IN_Current Limit : "); Serial.println(battery_charging.getInput_Current_Limit());
    // Serial.print("PRE_CHG_Current Limit : "); Serial.println(battery_charging.getPreCharge_Current_Limit());
    // Serial.print("TERM_Current Limit : "); Serial.println(battery_charging.getTermination_Current_Limit());

    // Serial.print("Charging Status : "); Serial.println(battery_charging.getCHG_STATUS() == BQ25896::CHG_STAT::NOT_CHARGING ? " not charging" :
    //   (battery_charging.getCHG_STATUS() == BQ25896::CHG_STAT::PRE_CHARGE ? " pre charging" :
    //     (battery_charging.getCHG_STATUS() == BQ25896::CHG_STAT::FAST_CHARGE ? " Fast charging" : "charging done")));

    // Serial.print("VBUS Status : "); Serial.println(battery_charging.getVBUS_STATUS() == BQ25896::VBUS_STAT::NO_INPUT ? " not input" :
    //   (battery_charging.getVBUS_STATUS() == BQ25896::VBUS_STAT::USB_HOST ? " USB host" :
    //     (battery_charging.getVBUS_STATUS() == BQ25896::VBUS_STAT::ADAPTER ? " Adapter" : "OTG")));

    // Serial.print("VSYS Status : "); Serial.println(battery_charging.getVSYS_STATUS() == BQ25896::VSYS_STAT::IN_VSYSMIN ? " In VSYSMIN regulation (BAT < VSYSMIN)" :
    //   "Not in VSYSMIN regulation (BAT > VSYSMIN)");

    // Serial.print("Temperature rank : "); Serial.println(battery_charging.getTemp_Rank() == BQ25896::TS_RANK::NORMAL ? " Normal" :
    //   (battery_charging.getTemp_Rank() == BQ25896::TS_RANK::WARM ? " Warm" :
    //     (battery_charging.getTemp_Rank() == BQ25896::TS_RANK::COOL ? " Cool" :
    //       (battery_charging.getTemp_Rank() == BQ25896::TS_RANK::COLD ? " Cold" : "HOT"))));

    // Serial.print("Charger fault status  : "); Serial.println(battery_charging.getCHG_Fault_STATUS() == BQ25896::CHG_FAULT::NORMAL ? " Normal" :
    //   (battery_charging.getCHG_Fault_STATUS() == BQ25896::CHG_FAULT::INPUT_FAULT ? " Input Fault" :
    //     (battery_charging.getCHG_Fault_STATUS() == BQ25896::CHG_FAULT::THERMAL_SHUTDOWN ? " Thermal Shutdown" : "TIMER_EXPIRED")));

  }



}

struct tm getTime()
{
  struct tm time;
  if (!getLocalTime(&time, 500))
    Serial.println("Could not obtain time info");
  return time;
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
}


void Draw_Main()
{
  static uint32_t update = 0;
  char str[128];

  if (millis() - update > 199) {
    update = millis();
    lcd.fillRect(0, 0, 128, 64, 0);

    Draw_Title("MAIN");

    lcd.setFont(c64enh);
    lcd.setFont(Small5x7PLBold);
    snprintf(str, sizeof(str), "R: %03.2fV  %02.3fA %.1f", Meter.phaseR.Vrms, Meter.phaseR.Irms, Meter.phaseR.Watt);
    lcd.printStr(1, 10, str);

    snprintf(str, sizeof(str), "S: %03.2fV  %02.3fA %.1f", Meter.phaseS.Vrms, Meter.phaseS.Irms, Meter.phaseS.Watt);
    lcd.printStr(1, 19, str);

    snprintf(str, sizeof(str), "T: %03.2fV  %02.3fA %.1f", Meter.phaseT.Vrms, Meter.phaseT.Irms, Meter.phaseT.Watt);
    lcd.printStr(1, 28, str);

    snprintf(str, sizeof(str), "N: %02.3fA", Meter.neutral.Irms);
    lcd.printStr(1, 37, str);


    snprintf(str, sizeof(str), "V: %1.2f Vb: %1.2f I: %1.2f T: %2.1f", battery_charging.getVBUS(), battery_charging.getVBAT(), battery_charging.getICHG(), battery_charging.getTemperature());
    Serial.println(str);
    lcd.setFont(Small4x5PL);
    lcd.drawLineHfast(0, 127, 57, 1);
    lcd.printStr(0, 59, str);
    // lcd.fillRect(0, 57, 64, 6, 2);

    lcd.display(0);
  }
  Keys key = Keyboard.getNextKey();

  if (key == Keys::Enter)
    indexUI = 1;
  else if (key == Keys::Next)
    indexUI = 2;
  // else if (Keyboard.getNextKey() == Keys::Up)
  //   indexUI = 3;
}

void Draw_Fase()
{
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
    Draw_Title(vals.Name);

    lcd.setFont(Small5x7PLBold);
    snprintf(str, sizeof(str), "%03.2fV  %02.3fA %2.2fHz", vals.Vrms, vals.Irms, vals.Freq);
    lcd.printStr(1, 10, str);

    snprintf(str, sizeof(str), "%03.1fW  %3.1fVar %3.1fVa", vals.Watt, vals.VAR, vals.VA);
    lcd.printStr(1, 19, str);

    snprintf(str, sizeof(str), "%2.2fHz  %2.2fPF %3.1fg", vals.Freq, vals.PowerFactor, vals.AngleVI);
    lcd.printStr(1, 28, str);

    snprintf(str, sizeof(str), "%2.2fHz  %3.1fg %2.1fPF", vals.Freq, vals.AngleVI, vals.PowerFactor);
    lcd.printStr(1, 37, "Energia");

    lcd.printStr(1, 46, "THD");


    snprintf(str, sizeof(str), "V: %1.2f Vb: %1.2f I: %1.2f T: %2.1f", battery_charging.getVBUS(), battery_charging.getVBAT(), battery_charging.getICHG(), battery_charging.getTemperature());
    Serial.println(str);
    lcd.setFont(Small4x5PL);
    lcd.drawLineHfast(0, 127, 57, 1);
    lcd.printStr(0, 59, str);
    // lcd.fillRect(0, 57, 64, 6, 2);

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

    Draw_Title("ANGULOS");

    AngleRegs angles;
    ade.readAngleRegsnValues(&angles);

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


    snprintf(str, sizeof(str), "V: %1.2f Vb: %1.2f I: %1.2f T: %2.1f", battery_charging.getVBUS(), battery_charging.getVBAT(), battery_charging.getICHG(), battery_charging.getTemperature());
    Serial.println(str);
    lcd.setFont(Small4x5PL);
    lcd.drawLineHfast(0, 127, 57, 1);
    lcd.printStr(0, 59, str);

    lcd.display(0);
  }
  if (Keyboard.getNextKey() == Keys::Esc)
    indexUI = 0;
}

void Draw_Title(const char* Title)
{

  char str[128];
  uint32_t width;
  struct tm time = getTime();

  lcd.fillRect(0, 0, 128, 7, 0);

  lcd.setFont(Small4x5PL);
  snprintf(str, sizeof(str), "%02d:%02d:%02d", time.tm_hour, time.tm_min, time.tm_sec);
  lcd.printStr(0, 0, str);

  width = lcd.strWidth(Title);
  lcd.printStr(63 - (width / 2), 0, Title);

  snprintf(str, sizeof(str), "%02d/%02d/%02d", time.tm_mday, time.tm_mon + 1, time.tm_year - 100);
  width = lcd.strWidth(str);
  lcd.printStr(127 - width, 0, str);

  lcd.drawLineHfast(0, 127, 6, 1);
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




int32_t waveBufferRaw[2048];
uint8_t waveBuffer[4032 * 2];    //2048*3 * 0.875 * 0.75 => 4032 palabras después de la compresion

void fillWaveBuffer()
{
  uint32_t time = micros();
  float pi = 3.1415, frequency = 50, sampling_rate = 8000, factor;
  static uint16_t offset[8] = { 150, 0, 43, 53, 97, 107, 22, 0 }; //Ia, Va, Ib, Vb, Ic, Vc, In, nn
  static float amplitud[8] = { 0.21, 0.56, 0.2, 0.53, 0.265, 0.495, 0.085, 0 }; //Ia, Va, Ib, Vb, Ic, Vc, In, nn

  //Crear una onda base de 50hz
  float wave[160];  //frequency / sampling_rate
  factor = 2 * pi / 160;  //160 es la cantidad de muestras que ocupa un ciclo de 50hz
  for (uint8_t i = 0; i < 160; i++) {
    wave[i] = sin(factor * i) * 8388607;
  }

  //Meter la onda en cada canal, segun su desplazamiento y amplitud
  for (uint16_t j = 0; j < 2048; j += 8) {
    for (uint8_t i = 0; i < 8; i++) {
      waveBufferRaw[j + i] = (((int32_t)((wave[offset[i]] + (random() & 0x1FFff)) * amplitud[i])) << 4);
      if (++offset[i] >= 160) offset[i] = 0;
    }
  }
  time = micros() - time;
  // Serial.printf("Tiempo de llenado: %u us\n", time);

  // for (uint8_t i = 0; i < 8; i++) {
  //   Serial.printf("waveBufferRaw[%d]", i);
  //   for (uint16_t j = i; j < 2048; j += 8) {
  //     Serial.printf("%d, ", waveBufferRaw[j + i]>>4);
  //   }
  //   Serial.println();
  // }

}

void compressWaveBuffer12(uint16_t part)
{
  uint32_t time = micros();
  int32_t* raw = waveBufferRaw;
  uint8_t* buf = &waveBuffer[part * 2688];  //2688 es la cantidad de bytes resultantes de comprimir el buffer de 2048 words 


  int32_t val1, val2, k = 0, bytes = 0;
  for (int32_t i = 0; i < 2048; i++) {
    if ((i & 0x7) == 7) {  //Ignorar la muestra 7 ya que es un valor de relleno
      raw++;
      continue;
    };

    /*
        Como se comprime y se guardan 12 bits, se deben juntar de a 2 muestras y guardar 3 bytes
        Siempre se guarda después de leer la segunda muestra
    */
    if (!(k++ & 1)) {
      val1 = (*raw++) >> 16;  //Comprimir a 12 bits
      if (val1 > 2047) val1 = 2047;
      else if (val1 < -2048) val1 = -2048;
    }
    else {  //Solo guardar cada 2 words leidas
      val2 = (*raw++) >> 16;  //Comprimir a 12 bits
      if (val2 > 2047) val2 = 2047;
      else if (val2 < -2048) val2 = -2048;

      uint8_t a, b, c;

      *buf++ = a = val1 & 0xFF;
      *buf++ = b = uint8_t((val1 & 0x0F00) >> 8) | uint8_t((val2 & 0x000F) << 4);
      *buf++ = c = (val2 & 0xFF0) >> 4;
      bytes += 3;
    }
  }

  time = micros() - time;
  // Serial.printf("Tiempo de compresion: %u us, k: %d, bytes: %d\n", time, k, bytes);
}
