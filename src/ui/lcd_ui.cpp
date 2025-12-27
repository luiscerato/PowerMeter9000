#include "lcd_ui.h"
#include "arduino.h"
#include <stdlib.h>
#include <time.h>
#include <RemoteDebug.h>

// #define LOG_LOCAL_LEVEL ESP_LOGD
// #include <esp_log.h>
// static const char* TAG = "ui";

using namespace std;
extern RemoteDebug Debug;	//Acceso a las funciones de debugger remoto


int32_t UI_Window::maxW = SCR_WD;  //Ancho máximo de la pantalla
int32_t UI_Window::maxH = SCR_HT;  //Altura máxima de la pantalla

void lcd_ui::ResetUpdateTime()
{
    UpdateTime = DftUpdateTime;
}

lcd_ui::lcd_ui(ST7920_SPI& screen, keyboard& key, int8_t SoundPin) : lcd(screen), keys(key)
{
    debugI( "");
    LCD.Widht = lcd.getWidth();
    LCD.Height = lcd.getHeight();

    for (int32_t i = 0; i < UI_Max_Screens; i++) {
        Screens[i].init();
        Screens[i].window.setPosAndSize(0, 0, lcd.getWidth(), lcd.getHeight());

    }
    for (int32_t i = 0; i < UI_Max_Levels; i++)
        Screen_Stack[i] = 0;

    if (SoundPin > 0)
        Speaker.Init(SoundPin);

    debugI( "LCD Widht: %d, Height: %d", LCD.Widht, LCD.Height);
}

lcd_ui::~lcd_ui()
{
    debugI( "");
}

void lcd_ui::begin()
{
    debugI( "");

    // Agregar las ui del sistema
    Add_UI(ID_UI_Black, "@ui_black", &lcd_ui::UI_Black);
    // Add_UI(ID_UI_SetVal, "@ui_set_val", &lcd_ui::UI_SetVal);
    // Add_UI(ID_UI_SetString, "@ui_set_str", &lcd_ui::UI_SetStr);
    // Add_UI(ID_UI_SetIP, "@ui_set_ip", &lcd_ui::UI_SetIP);

    // Msg.begin(this, "ui_message", 189);

    // OptionBox.begin(this, "ui_optionbox", 156);

    // Question.begin(this, "ui_question", 0x1324);

    // DateTime.begin(this, "ui_datetime", 0x1000);

    Show(ID_UI_Black);

    SoundType = UI_Sound::None;
}

bool UI_Screen_t::Execute(lcd_ui* ui, UI_Action action)
{
    if (ui_screen)
        return ui_screen->Run(ui, action);
    else if (ID < 0)
        return (ui->*member)(ui, action);
    else if (ID > 0)
        return function(ui, action);
    else
        return false;
};


UI_Window* lcd_ui::getWindow()
{
    // int32_t screen;
    // if (InFunct) 
    //     screen = Screen_Stack[UpdateScreenIndex];
    // else 
    //     screen = Screen_Stack[Screen_Index];
    // return &Screens[screen].window;
    if (uiWindow == nullptr) {
        int32_t screen = Screen_Stack[Screen_Index];
        uiWindow = &Screens[screen].window;
    }
    return uiWindow;
}

uint32_t tBack, tUpdate, tScreen, tReal, tClear;
void lcd_ui::Run()
{
    bool update = false;

    keys.scan();
    Speaker.Run();

    if (keys.isSomeKeyPressed()) // || Blinker.HasChanged()
        update = true;

    if ((millis() - LastTime >= UpdateTime) || UpdateStep)
        update = true;

    if (update == false) return;

    LastTime = millis();

    if (UpdateStep == 0) {  //  Pintar fondo del LCD
        tClear = micros();
        if (millis() - RestartLCDTime > 60000) {
            //Reiniciar el LCD cada 60 segundos para evitar fallos de visualización
            lcd.init();
            RestartLCDTime = millis();
        }
        lcd.cls();
        tClear = micros() - tClear;
        UpdateLcdStep = 0;
        //Buscar la primer pantalla fullscreen
        for (UpdateScreenIndex = Screen_Index; UpdateScreenIndex > 0; UpdateScreenIndex--) {
            int32_t screen = Screen_Stack[UpdateScreenIndex];
            if (Screens[screen].window.isFullScreen())  break;  //Si la pantalla es fullscreen, terminar de buscar.
        }
        UpdateStep++;
    }
    else if (UpdateStep == 1) {
        int32_t screen = Screen_Stack[UpdateScreenIndex];
        if (!Screens[screen].isEmpty()) {
            tBack = micros();
            if (backgroundDrawer) backgroundDrawer(this, &Screens[screen].window);
            tBack = micros() - tBack;

            tUpdate = micros();
            InFunct = true;
            uiWindow = &Screens[screen].window; //Apuntar a la ventana actual
            Screens[screen].Execute(this, UI_Action::Run);
            InFunct = false;
            tUpdate = micros() - tUpdate;
            // Serial.printf("UpdateWindow: %d, name: %s\n", UpdateScreenIndex, Screens[screen].Name);
        }
        if (UpdateScreenIndex < Screen_Index)
            UpdateScreenIndex++;
        else {
            UpdateStep++;
        }
        tReal = 0;
    }
    else { //Actualizar LCD
        if (UpdateLcdStep < 8) {
            uint32_t time = micros();
            lcd.display(0, UpdateLcdStep);
            time = micros() - time;
            tReal += time;
            // if (UpdateLcdStep == 7 ) Serial.printf("Tiempo de actualizacion: %d us\n", time);
            UpdateLcdStep++;
        }
        else {
            UpdateStep = 0;
            tScreen = micros() - tScreen;
            // Serial.printf("UI update cicle -> clear: %uus, window: %uus, update: %uus, screen: %uus, real screen: %uus\n", tClear, tBack, tUpdate, tScreen, tReal);
        }
    }

    if (GoBack) {
        GoBack = false;
        Close(Result);
        SoundType = UI_Sound::Closing;
        UpdateStep = 0;
    }
    if (GoHome) {
        GoHome = false;
        Home();
        SoundType = UI_Sound::Closing;
        UpdateStep = 0;
    }

    if (SoundType != UI_Sound::None) {
        if (SoundType == UI_Sound::Beep)
            Speaker.PlayTone(2500, 50);
        else if (SoundType == UI_Sound::Enter)
            Speaker.PlayTone(3500, 100);
        else if (SoundType == UI_Sound::Closing)
            Speaker.PlayTone(1500, 100);
        SoundType = UI_Sound::None;
    }
}

bool lcd_ui::ShowByIndex(int32_t Index)
{
    debugI( "UI: %i", Index);
    if (Index < 0 || Index >= UI_Max_Screens) {
        debugI( "Error -> Index %i fuera de rango [%i..%i]", Index, 0, UI_Max_Screens);
        return false;
    }

    ResetUpdateTime();
    if (ClearScreenOnScreenSwitch) {
        lcd.cls(); // Limpiar el lcd antes de mostrar la nueva pantalla
    }
    keys.ClearKeys();
    SoundType = UI_Sound::Enter;

    Screen_Index++;
    Screen_Stack[Screen_Index] = Index;
    UI_Window*  prevWindow = uiWindow;
    uiWindow = &Screens[Index].window;
    Screens[Index].Execute(this, UI_Action::Init);
    uiWindow = prevWindow;

    return true;
}

bool lcd_ui::Show(const char* UI)
{
    debugI( "UI: %s", UI);
    int32_t Index;
    if (UI == nullptr) {
        debugI( "Error -> nullpointer");
        return false;
    }
    if (Screen_Index == (UI_Max_Levels - 1)) {
        debugI( "Error -> no hay espacio para mas pantallas [max: %i]", UI_Max_Levels);
        return false;
    }

    for (Index = 0; Index < UI_Max_Screens; Index++) {
        if (!Screens[Index].isEmpty()) {
            if (strcmp(UI, Screens[Index].Name) == 0) {
                return ShowByIndex(Index);
            }
        }
    }
    if (Index == UI_Max_Screens) {
        debugI( "Error -> no se encuentra pantalla '%s'", UI);
        return false;
    }
    return false;
}

bool lcd_ui::Show(int32_t UI_ID)
{
    debugI( "UI: 0x%0.8X", UI_ID);
    int32_t Index;

    if (Screen_Index == (UI_Max_Levels - 1)) {
        debugI( "Error -> no hay espacio para mas pantallas [max: %i]", UI_Max_Levels);
        return false;
    }

    for (Index = 0; Index < UI_Max_Screens; Index++) {
        if (!Screens[Index].isEmpty()) {
            if (Screens[Index].ID == UI_ID)
                return ShowByIndex(Index);
        }
    }
    if (Index == UI_Max_Screens) {
        debugI( "Error -> no se encuentra pantalla ID: %i", UI_ID);
        return false;
    }
    return false;
}

int32_t lcd_ui::GetScreenIndex()
{
    return Screen_Index;
}

bool lcd_ui::Close(UI_DialogResult result)
{
    bool res = false;
    debugI( "Index: %i, result: %u", Screen_Index, result);
    Result = result; // Cargar el estado actual
    UI_Window*  prevWindow = uiWindow;

    if (InFunct) { // Cuando se la llama desde una funci�n UI
        GoBack = true;
        if (Screen_Index)
            res = true;
    }
    else { // Cuando se la llama fuera de la funci�n UI
        GoBack = false;
        if (Screen_Index < 1)
            return false;

        SoundType = UI_Sound::Closing;
        int32_t screen = Screen_Stack[Screen_Index];
        Screen_Index--;
        int32_t lastIndex = Screen_Index; // Guardar el �ndice actual de la pantalla

        if (!Screens[screen].isEmpty()) { // LLamar a la pantalla que se est� cerrando
            uiWindow = &Screens[screen].window;
            res = Screens[screen].Execute(this, UI_Action::Closing);
        }
        keys.ClearKeys(); // Limpiar teclado
        if (ClearScreenOnScreenSwitch) {
            lcd.clear(); // Limpiar el lcd antes de mostrar la nueva pantalla
        }
        ResetUpdateTime();      // Tiempo de atualizaci�n por defecto

        if (lastIndex == Screen_Index) {         // Si el �ndice cambi� es porque se abri� una pantalla mientras se cerraba la actual
            screen = Screen_Stack[Screen_Index]; // Pedir a la anterior que recupere el entorno
            if (!Screens[screen].isEmpty()) {
                uiWindow = &Screens[screen].window;
                res = Screens[screen].Execute(this, UI_Action::Restore);
            }
        }
        uiWindow = prevWindow;
    }
    return res;
}

bool lcd_ui::setMainScreen(const char* UI)
{
    debugI( "UI: %s", UI);
    int32_t Index;
    if (UI == nullptr) {
        debugI( "Error -> nullpointer");
        return false;
    }

    for (Index = 0; Index < UI_Max_Screens; Index++) {
        if (!Screens[Index].isEmpty()) {
            if (strcmp(UI, Screens[Index].Name) == 0) {
                Screen_Stack[0] = Index;
                break;
            }
        }
    }
    if (Index == UI_Max_Screens) {
        debugI( "Error -> no se encuentra pantalla '%s'", UI);
        return false;
    }
    Home();
    Screens[Index].Execute(this, UI_Action::Init);
    return true;
}

/*
        Borrar el stack de ventanas y mostrar la principal
*/
bool lcd_ui::Home()
{
    if (InFunct) {
        GoHome = true;
        return true;
    }
    while (Close(UI_DialogResult::Cancel))
        ;
    GoHome = false;
    return true;
}

void lcd_ui::SetUpdateTime(uint32_t Ms)
{
    UpdateTime = Ms;
}

Keys lcd_ui::GetKeys()
{
    static Keys LastKey = Keys::None;
    // debugI( "isTopScreen(): %d", isTopScreen());

    if (!isTopScreen()) {return Keys::None;}  //Solo la patalla superior puede leer eventos del teclado

    Keys key = keys.getNextKey();

    // if (key != Keys::None)
    //	Serial.printf("-Key: %d\n", key);

    if (!UsingKeyEnter && key == Keys::Esc)
        key = Keys::Enter;

    if (key != Keys::None) {
        switch (key) {
        case Keys::Left:
        case Keys::Right:
        case Keys::Esc:
        case Keys::Enter:
        case Keys::Ok:
        case Keys::Next:
            // Blinker.Reset();
            break;
        default:
            // Blinker.Set();
            break;
        }
        if (LastKey != key) {
            LastKey = key;
            SoundType = UI_Sound::Beep;
        }
    }
    else if (LastKey != Keys::None) {
        if (keys.isKeyUp(LastKey))
            LastKey = Keys::None;
    }
    return key;
}

void lcd_ui::PrintText(const char* Text, TextPos Pos, int32_t Widht)
{
    // debugI( "LCD Widht: %d, Height: %d", LCD.Widht, LCD.Height);
    if (Widht < 1 || Widht > LCD.Widht)
        Widht = LCD.Widht;

    // if (Pos == TextPos::Left) {
    //     lcd->printf("%-*.*s", Widht, Widht, Text);
    //     // debugI( "PrintText: W=%d -> '%-*.*s'", Widht, Widht, Widht, Text);
    // } else { // if (Pos == TextPos::Center) {
    //     int32_t len = strlen(Text), esp, count;
    //     if (len > Widht)
    //         len = Widht;
    //     esp = Widht - len;
    //     if (Pos == TextPos::Center)
    //         esp /= 2;
    //     count = esp;
    //     while (count-- > 0)
    //         lcd->write(' ');

    //     count = Widht - esp;
    //     lcd->printf("%.*s", count, Text);
    //     count -= len;
    //     while (count-- > 0)
    //         lcd->write(' ');
    // }
}

int32_t lcd_ui::Add_UI(int32_t ID, const char* Name, bool (lcd_ui::* member)(lcd_ui*, UI_Action))
{
    int32_t Index;
    if (member == nullptr || Name == nullptr || ID >= 0) {
        debugI( "Error -> nullpointer or ID > 0");
        return -2;
    }
    debugI( "Sys ui name: %s, ID:0x%0.8X at 0x%0.8X", Name, ID, member);

    for (Index = 0; Index < UI_Max_Screens; Index++) {
        if (Screens[Index].isEmpty())
            break;
        else {
            if (strcmp(Name, Screens[Index].Name) == 0) {
                debugI( "Error -> pantalla '%s' ya existe", Name);
                return -3;
            }
            else if (ID == Screens[Index].ID) {
                debugI( "Error -> pantalla ID=%i ya existe", ID);
                return -4;
            }
        }
    }
    if (Index == UI_Max_Screens) {
        debugI( "Error -> no se pueden agregar mas pantallas [max=%i]", UI_Max_Screens);
        return -1;
    }
    Screens[Index].Name = Name;
    Screens[Index].ID = ID;
    Screens[Index].member = member;
    return 0;
}

int32_t lcd_ui::Add_UI(int32_t ID, const char* Name, bool (*func)(lcd_ui*, UI_Action))
{
    int32_t Index;
    if (func == nullptr || Name == nullptr || ID < 0) {
        debugI( "Error -> nullpointer or ID < 0");
        return -2;
    }
    debugI( "Name: %s, ID:0x%0.8X at 0x%0.8X", Name, ID, func);

    for (Index = 0; Index < UI_Max_Screens; Index++) {
        if (Screens[Index].isEmpty())
            break;
        else {
            if (strcmp(Name, Screens[Index].Name) == 0) {
                debugI( "Error -> pantalla '%s' ya existe", Name);
                return -3;
            }
            else if (ID == Screens[Index].ID) {
                debugI( "Error -> pantalla ID=%i ya existe", ID);
                return -4;
            }
        }
    }
    if (Index == UI_Max_Screens) {
        debugI( "Error -> no se pueden agregar mas pantallas [max=%i]", UI_Max_Screens);
        return -1;
    }
    Screens[Index].Name = Name;
    Screens[Index].ID = ID;
    Screens[Index].function = func;
    return 0;
}

int32_t lcd_ui::Add_UI(int32_t ID, const char* Name, lcd_ui_screen& ui_screen)
{
    int32_t Index;
    if (Name == nullptr || ID <= 0) {
        debugI( "Error -> nullpointer or ID <= 0");
        return -2;
    }

    debugI( "lcd_ui_screen: %s, ID:0x%0.8X at 0x%0.8X", Name, ID, ui_screen);

    for (Index = 0; Index < UI_Max_Screens; Index++) {
        if (Screens[Index].isEmpty())
            break;
        else {
            if (strcmp(Name, Screens[Index].Name) == 0) {
                debugI( "Error -> pantalla '%s' ya existe", Name);
                return -3;
            }
            else if (ID == Screens[Index].ID) {
                debugI( "Error -> pantalla ID=%i ya existe", ID);
                return -4;
            }
        }
    }
    if (Index == UI_Max_Screens) {
        debugI( "Error -> no se pueden agregar mas pantallas [max=%i]", UI_Max_Screens);
        return -1;
    }
    Screens[Index].Name = Name;
    Screens[Index].ID = ID;
    Screens[Index].ui_screen = &ui_screen;
    return 0;
}

int32_t lcd_ui::Remove_UI(const char* Name)
{
    if (Name == nullptr) {
        debugI( "Error -> nullpointer");
        return -2;
    }

    for (int32_t Index = 0; Index < UI_Max_Screens; Index++) {
        if (!Screens[Index].isEmpty()) {
            if (strcmp(Name, Screens[Index].Name) == 0) {
                Screens[Index].init();
                return 0;
            }
        }
    }
    debugI( "Error -> no se encuentra pantalla '%s'", Name);
    return -1;
}

int32_t lcd_ui::Remove_UI(int32_t ID)
{
    if (ID == 0)
        return -2;

    for (int32_t Index = 0; Index < UI_Max_Screens; Index++) {
        if (!Screens[Index].isEmpty()) {
            if (ID == Screens[Index].ID) {
                Screens[Index].init();
                return 0;
            }
        }
    }
    debugI( "Error -> no se encuentra pantalla ID='%i'", ID);
    return -1;
}

void lcd_ui::PrintState()
{
    Serial.printf("UI screen index: %u\n", Screen_Index);
    Serial.printf("Screen stack:\n", Screen_Index);
    for (int32_t i = Screen_Index; i >= 0; i--) {
        Serial.printf("Pos: %.2X, ID:0x%0.4X, Func:0x%0.8X, Name: %s, Title: %s\n",
            i, Screens[Screen_Stack[i]].ID, Screens[Screen_Stack[i]].function, Screens[Screen_Stack[i]].Name, Screens[Screen_Stack[i]].window.Title.c_str());
    }
}

void lcd_ui::PrintScreenList()
{
    Serial.printf("Lista de pantallas caragadas en UI\n", Screen_Index);
    int i;
    for (i = 0; i < UI_Max_Screens; i++) {
        if (!Screens[i].isEmpty()) {
            Serial.printf(" Index: %u -> [%s] ID:%p '%s'\n", i, Screens[i].ToString(), Screens[i].ID, Screens[i].Name);
        }
    }
    Serial.printf("Se encontraron %i pantallas\n", Screen_Index);

    if (i > 0)
        Serial.printf("La pantalla principal es '%s'\n", Screens[Screen_Stack[0]].Name);
}

bool lcd_ui::isTopScreen() {
    // debugI( "UpdateScreenIndex: %d, Screen_Index: %d",UpdateScreenIndex, Screen_Index);
    return UpdateScreenIndex == Screen_Index;
};


bool lcd_ui::UI_Black(lcd_ui* ui, UI_Action action)
{
    UI_Window* win = getWindow();
    if (action == UI_Action::Init) {
        debugI( "UI_Action::Init");
        return true;
    }
    else if (action == UI_Action::Closing) {
        debugI( "UI_Action::Closing");
        return true;
    }
    else if (action == UI_Action::Run) {
        ui->Title = "LCD UI by LC";

        win->setPosAndSize(0, 0, 128, 64);
        win->setWindowMode(UI_Window_Mode::Small);

        char str[64];
        time_t rawtime;
        struct tm timeinfo;
        time(&rawtime);
        timeinfo = *localtime(&rawtime);
        ui->lcd.setFont(Chicago21x18);

        UI_Point size = win->getUserWindowSize();

        snprintf(str, sizeof(str), "%02d/%02d/%02d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year - 100);
        lcd.printStr(size.x / 2, size.y - 2, str, TextAling::BottomCenter);

        lcd.fillRect(0, 2, size.x, 20, 0);
        snprintf(str, sizeof(str), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        lcd.printStr(size.x / 2, 2, str, TextAling::TopCenter);

        // Keys key = getNextKey();

        // if (key == Keys::Esc)
        //     ui->Home();
        // else if (key == Keys::Enter)
        //     ;
        // else if (key == Keys::Next)
        //     mode++;
        // else if (key == Keys::Up) {
        //     index++;
        // }
        // else if (key == Keys::Down) {
        //     index--;
        // }

        return true;
    }
    else if (action == UI_Action::Restore) {
        debugI( "UI_Action::Restore");
        lcd.clear();
        return true;
    }
    debugI( "UI_Action::Unknown -> %i", action);
    return false;
}


/*
        Constructores de la clase
*/
lcd_ui_screen::lcd_ui_screen()
{
    debugI( "");
    MyID = 20000;
}

lcd_ui_screen::~lcd_ui_screen()
{
    debugI( "");
}

/**/
bool lcd_ui_screen::Run(lcd_ui* ui, UI_Action action)
{
    return true;
}

bool lcd_ui_screen::Show()
{
    if (ui == nullptr)
        return false;
    return ui->Show(MyID);
}

bool lcd_ui_screen::Close(UI_DialogResult Result)
{
    debugI( "Result: %u", Result);
    if (ui == nullptr)
        return false;
    return ui->Close(Result);
}

bool lcd_ui_screen::begin(lcd_ui* main_ui, const char* Name, int32_t ID)
{
    debugI( "%p", main_ui);
    ui = main_ui;
    MyID = ID;
    if (ui == nullptr) {
        debugI( "No se puede iniciar con ui==null");
        return false;
    }

    ui->Add_UI(MyID, Name, *this);
    return true;
}