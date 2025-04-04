#ifndef __lcd_ui_h
#define __lcd_ui_h

#include "keyboard.h"
#include "lwip/apps/sntp.h"
#include "sound.h"
#include "ST7920_SPI.h"
#include "fonts/fonts.h"

#include <time.h>

using namespace std;

#define sizeof_menu(x) (sizeof(x) / (sizeof(x[0]))) // Calcula la cantidad de elementos de un men� (o un array)

#define constrain_overrun(amt, low, high) ((amt) < (low) ? (high) : ((amt) > (high) ? (low) : (amt))) // Limita un valor a determinado rango, pero si lo sobrepasa, va al l�mite opuesto (overrun)

#define UI_Max_Screens 32 // N�mero m�ximo de pantallas que se pueden contener
#define UI_Max_Levels 16  // Profundidad m�xima de niveles de pantallas

#define ID_UI_Black -10
#define ID_UI_SetVal -1
#define ID_UI_SetString -2
#define ID_UI_SetIP -3
#define ID_UI_SetTime -4

// forward-declare classes and structures
class lcd_ui; // Solo para proveer visivilidad a los objetos por encima de la implementacion de la clase.
class lcd_ui_screen;

/*
        Enumeraci�n del tipo de sonido que debe generar la interfase de usuario
*/
enum class UI_Sound
{
    None = 0, // Ning�n sonido es generado
    Beep,     // Se emite un sonido de pulsaci�n de tecla normal.
    Enter,    // Se genera un sonido cuando se presiona la tecla enter. (Entra a una pantalla o menu)
    Closing,  // Se genera un sonido cuando se presiona la tecla esc. (Vuelve atr�s una pantalla o menu)
};

/*
        Enumeraci�n del tipo de acci�n que debe ejecutar la funci�n de la pantalla que est� corriendo
*/
enum class UI_Action
{
    None = 0, // No debe realizar ninguna funci�n
    Init,     // Solo debe ejecutar funciones de inicio, es para preparar el entorno antes de ejecutar esa pantalla. Se llama cuando se ejecuta ui->Show();
    Closing,  // Solo debe ejecutar funciones para terminar la visualizaci�n de la pantalla. Se llama una vez que la pantalla se cierra, cuando se llama ui->Close();
    Restore,  // Se ejecuta antes de volver a ser visible la pantalla, es decir luego de llamar a ui->Close();
    Run       // La funci�n de la pantalla se ejecuta normalmente. En este caso puede realizar cualquier funci�n.
};

/*
        Enumeraci�n del tipo de resultado al cerrarse una pantalla.

        Mismos nombres y valores que VB.net
        https://docs.microsoft.com/en-us/dotnet/api/system.windows.forms.dialogresult?view=netcore-3.1
*/
enum class UI_DialogResult : int32_t
{
    None = 0, // No se devolvi� ning�n resultado
    Ok,       // Indicar que la pantalla se cerr� aceptando la opci�n seleccionada
    Cancel,   // Indicar que la pantalla se cerr� rechazando la opci�n seleccionada
    Abort,    // Indicar que la pantalla se cerr� seleecionando la opci�n 'ABORTAR'
    Retry,    // Indicar que la pantalla se cerr� seleecionando la opci�n 'REINTENTAR'
    Ignore,   // Indicar que la pantalla se cerr� seleecionando la opci�n 'IGNORAR'
    Yes,      // Indicar que la pantalla se cerr� seleecionando la opci�n 'SI'
    No,       // Indicar que la pantalla se cerr� seleecionando la opci�n 'NO'
};

enum class TextPos
{
    Left = 0,
    Center,
    Right
};

enum class UI_Window_Mode : uint32_t
{
    Normal = 0,         //Pantalla normal, se muestra titulo, status, y si es necesario borde con sombra
    Small,              //Pantalla con mas espacio, no se muestra la pantalla de estado, con borde con sombra si es necesario
    Compact,            //Pantalla con un título más chico y borde sin sombra, sin estado
    Empty,              //Pantalla vacía, se deja en blanco y se aprovecha todo el espacio asignado
};

struct UI_Point {
    int32_t x;
    int32_t y;

    UI_Point() {
        x = y = 0;
    };
    UI_Point(int32_t x, int32_t y) {
        this->x = x;
        this->y = y;
    };
};

class UI_Window
{
public:
    static int32_t maxW, maxH;
    int32_t posX, posY;
    int32_t width, height;
    int32_t titleSize, statusSize, borderSize;
    UI_Window_Mode mode = UI_Window_Mode::Normal;
    String Title;

    UI_Window() {
        mode = UI_Window_Mode::Normal;
        posX = 0;
        posY = 0;
        width = maxW;
        height = maxH;
        titleSize = 11;
        statusSize = 7;
        borderSize = 2;
        Title = "Window";
    };

    inline bool isFullScreen() {
        return (width == maxW && height == maxH);
    };

    void setFullScreen() {
        posX = 0;
        posY = 0;
        width = maxW;
        height =maxH;
    };

    bool setPosAndSize(int32_t x, int32_t y, int32_t w, int32_t h) {
        if (x >= maxW || y >= maxH) return false;
        posX = x;
        posY = y;
        width = w > maxW ? maxW : w;
        height = h > maxH ? maxH : h;
        return true;
    };

    void setWindowMode(UI_Window_Mode mode) {
        this->mode = mode;
        switch (mode) {
        case UI_Window_Mode::Normal:
            titleSize = 11;
            statusSize = 7;
            borderSize = 3;
            break;

        case UI_Window_Mode::Small:
            titleSize = 11;
            statusSize = 0;
            borderSize = 3;
            break;

        case UI_Window_Mode::Compact:
            titleSize = 9;
            statusSize = 0;
            borderSize = 2;
            break;

        case UI_Window_Mode::Empty:
            titleSize = 0;
            statusSize = 0;
            borderSize = 3;
            break;
        }
        if (isFullScreen()) borderSize = 0;
    }

    UI_Point getWindowPos() {
        UI_Point pos;
        pos.x = posX;
        pos.y = posY;
        return pos;
    }

    UI_Point getUserWindowPos() {
        UI_Point pos;
        pos.x = posX + (borderSize ? 1 : 0);
        pos.y = posY + titleSize + (borderSize ? 1 : 0);
        return pos;
    }

    UI_Point getUserWindowSize() {
        UI_Point size;
        size.x = width - borderSize;
        size.y = height - titleSize - statusSize - borderSize;
        return size;
    }

    UI_Point getTitlePos() {
        UI_Point pos;
        pos.x = posX + (borderSize ? 1 : 0);
        pos.y = posY + (borderSize ? 1 : 0);
        return pos;
    }

    UI_Point getTitleSize() {
        UI_Point size;
        size.x = width - borderSize;
        size.y = titleSize;
        return size;
    }

    UI_Point getStatusPos() {
        UI_Point pos;
        pos.x = posX + (borderSize ? 1 : 0);
        pos.y = posY + height - statusSize - (borderSize > 0 ? borderSize - 1 : 0);
        return pos;
    }

    UI_Point getStatusSize() {
        UI_Point size;
        size.x = width - borderSize;
        size.y = statusSize;
        return size;
    }
};

/*
        Estructura que contiene los datos necesarios para llamar a una pantalla.
*/
struct UI_Screen_t
{
    const char* Name; // Puntero a un nombre de la pantalla.
    int32_t ID;       // Identificador de la pantalla. En el caso de pantallas del sistema es menor a 0, para las de usuario es mayor a 0
    union
    {                                                // Uni�n de los dos tipos de funciones a llamar cuando est� corriendo la pantalla
        bool (*function)(lcd_ui*, UI_Action);       // Caller para la funci�n de la pantalla del usuario.
        bool (lcd_ui::* member)(lcd_ui*, UI_Action); // Caller para las funciones de pantalla del sistema.
    };
    lcd_ui_screen* ui_screen;

    enum Type
    {
        Undefined = 0,
        Function,
        Member,
        Class_Screen,
    };

    // Constructor
    UI_Screen_t()
    {
        init();
    };

    // Iniciar laestructura a los valores de defecto (devuelve isEmpty true)
    void init()
    {
        Name = nullptr;
        ID = 0;
        function = nullptr;
        ui_screen = nullptr;
    };

    // Chequea si la estructura est� inicializada a cero; no est� a puntando a ninguna pantalla. (est� vac�a)
    bool isEmpty()
    {
        if (Name == nullptr && ID == 0 && function == nullptr)
            return true;
        return false;
    };

    /*
            Llama a la funci�n que se quiere ejecutar, dependiendo del tipo de funci�n que es.
            Si ID > 0: ejecuta un llamada a funci�n (funci�n de usuario)
            Si ID < 0: ejecuta una llamada a un miembro de la clase lcd_ui, que deber�a ser una funci�n de pantalla del sistema
            Si ID == 0: no ejecuta ninguna llamada y devuelve false.

    */
    bool Execute(lcd_ui* ui, UI_Action action);

    Type getType()
    {
        if (ui_screen)
            return Class_Screen;
        else if (ID < 0)
            return Member;
        else if (ID > 0)
            return Function;
        return Undefined;
    };

    const char* ToString()
    {
        if (ui_screen)
            return "Class_Screen";
        else if (ID < 0)
            return "Member";
        else if (ID > 0)
            return "Function";
        return "Undefined";
    };

    UI_Window window;
};

/*
        *****************Helpers****************

        Clases y funciones para simplificar y mejorar las funciones de la clase base
*/


/*
        Esta clase es para simplificar el calculo de offset en una pantalla con varios
        items. En una pantalla de 4 lineas si se quieren mostrar 10 items y elegir uno
        de ellos al cambiar el elegido se van cambiando los que est�n en pantalla solo
        cuando sea necesario.
*/
class OffsetIndex
{
    int32_t Index;  // Indice seleccionado
    int32_t Offset; // Offset de la ventana
    int32_t Min;    // Valor m�nimo que puede tener index
    int32_t Max;    // Valor m�ximo que puede tener index
    int32_t WItems; // Cantidad de items que puede tener la ventana
    bool RollOver;  // Indica si se produce overflow o se mantiene en el l�mite

    void check()
    {
        if (Min > Max) {
            int32_t i = Max;
            Max = Min;
            Min = i;
        }
        if (Index < Min) {
            if (RollOver)
                Index = Max;
            else
                Index = Min;
        }
        else if (Index > Max) {
            if (RollOver)
                Index = Min;
            else
                Index = Max;
        }

        if (Index < Offset)
            Offset = Index;
        else if (Index > Offset + (WItems - 1))
            Offset = Index - (WItems - 1);
    };

public:
    OffsetIndex()
    {
        setParams(0, 1, 1, false);
    };

    OffsetIndex(int32_t IndexMin, int32_t IndexMax, int32_t WindowItems, bool DoRoolOver = true)
    {
        setParams(IndexMin, IndexMax, WindowItems);
        Index = IndexMin;
        Offset = IndexMin;
    };

    void setParams(int32_t IndexMin, int32_t IndexMax, int32_t WindowItems, bool DoRoolOver = true)
    {
        Min = IndexMin;
        Max = IndexMax;
        WItems = WindowItems;
        RollOver = DoRoolOver;
        check();
    };

    void Print()
    {
        Serial.printf("Min:%i, Max:%i, Win:%i, Index: %i, Offset: %i\n", Min, Max, WItems, Index, Offset);
    };

    int32_t getOffset()
    {
        check();
        return Offset;
    };

    inline int32_t getMax() { return Max; };
    inline int32_t getMin() { return Min; };
    inline int32_t getItemWindow() { return WItems; };

    int32_t operator++(int)
    {
        Index++;
        check();
        return Index;
    };

    int32_t operator--(int)
    {
        Index--;
        check();
        return Index;
    };

    void operator=(int32_t Val)
    {
        Index = Val;
        check();
    };

    bool operator==(int32_t Val)
    {
        return Val == Index;
    };

    operator int() const
    {
        return Index;
    };
};


class lcd_ui_screen
{
    int32_t MyID;
    lcd_ui* ui;

protected:
    virtual bool Show();

public:
    /*
            M�todo principal.

            Cuando se redefina esta funci�n se debe incluir todo el c�digo de visualizaci�n.
            No se debe bloquear nunca la ejecuci�n del loop.
    */
    virtual bool Run(lcd_ui* ui, UI_Action action);

    lcd_ui_screen();

    ~lcd_ui_screen();

    virtual bool Close(UI_DialogResult Result);

    bool begin(lcd_ui* main_ui, const char* Name, int32_t ID);

    friend class lcd_ui;
};


class Screen_Message : public lcd_ui_screen
{
    String Title = "Mensaje";
    String Message = "";
    uint32_t Timeout, LastTime;

public:
    Screen_Message()
    {
        Timeout = 10000;
        LastTime = 0;
    };

    bool Run(lcd_ui* ui, UI_Action action);

    bool ShowMessage(char* Title, char* Msg);

    bool ShowMessage(char* Title, char* Msg, uint32_t Timeout);
};

struct Option
{
    int32_t Val;
    char* Name;
    char* Info;

    constexpr Option(int32_t Val, char* Name) : Val(Val), Name(Name), Info(nullptr) {};

    constexpr Option(int32_t Val, char* Name, char* Info) : Val(Val), Name(Name), Info(Info) {};

    Option()
    {
        this->Val = 0;
        this->Name = nullptr;
        this->Info = nullptr;
    };
};

class Screen_Option : public lcd_ui_screen
{
    String Title = "Seleccione";
    int32_t* MyVal;
    int32_t Count;
    Option* List;
    bool ToErase;

public:
    Screen_Option()
    {
        List = nullptr;
        MyVal = nullptr;
    };

    bool Run(lcd_ui* ui, UI_Action action);

    bool ShowList(char* Title, int32_t* myVal, const Option* Options, uint32_t Count);

    bool ShowList(char* Title, int32_t* myVal, int32_t* Val, char** Names, char** Info, uint32_t Count);
};

class Screen_Question : public lcd_ui_screen
{
public:
    enum Options
    {
        Ok = 0,
        OkCancel,
        OkCancelRetry
    };

private:
    const char* Title;
    const char* Info;
    Options Opts;
    int32_t Index;
    void (*Function)(UI_DialogResult);

public:
    Screen_Question()
    {
        Title = "Mensaje";
        Info = nullptr;
        Opts = OkCancel;
        Function = nullptr;
        Index = 0;
    };

    bool Run(lcd_ui* ui, UI_Action action);

    bool ShowQuestion(const char* Title, const char* Info);

    bool ShowQuestion(const char* Title, const char* Info, Options Opt);

    bool ShowQuestion(const char* Title, const char* Info, Options Opt, void (*Function)(UI_DialogResult));
};

class Screen_Date : public lcd_ui_screen
{
public:
    enum Options
    {
        Ok = 0,
        OkCancel,
        OkCancelRetry
    };

private:
    const char* Title;
    const char* Info;
    time_t* TimeP, Secs;
    struct tm Time;
    struct timezone Zone, * ZoneP;
    bool Changed;
    int32_t Index;
    void (*Function)(UI_DialogResult);

public:
    Screen_Date()
    {
        Title = "Fecha y hora";
        // Info = nullptr;
        // TimeP = nullptr;
        ZoneP->tz_dsttime = 0;
        ZoneP->tz_minuteswest = 0;
        Function = nullptr;
        Index = 0;
    };

    bool Run(lcd_ui* ui, UI_Action action);

    bool ShowDlg(const char* Title, const char* Info);

    bool ShowDlg(const char* Title, const char* Info, time_t* Time);

    bool ShowDlg(const char* Title, const char* Info, time_t* Time, struct timezone* TimeZone);

    void setOnClose(void (*Function)(UI_DialogResult));

    /*
            Usa timezone UTC
    */
    void setUTCZone();

    static time_t timegm(tm* t);

    static char* setTimeZone(int32_t offset, int32_t daylight);

    static void setTimeZone(struct timezone* TimeZone);

    static void LimitFields(struct tm* date);

    static void TruncFields(tm* date);
};


/*
        **********************************************************
        class lcd_ui
*/
class lcd_ui
{
    UI_Screen_t Screens[UI_Max_Screens];            // Pantallas
    int32_t Screen_Stack[UI_Max_Levels];            // Pila de niveles de pantallas
    int32_t Screen_Index = 0;                       // Nivel en la pila de pantallas
    UI_Sound SoundType = UI_Sound::None;            // Sonido a reproducir
    UI_DialogResult Result = UI_DialogResult::None; // Resultado de la �ltima funci�n Close();
    bool ClearScreenOnScreenSwitch = false;         // Indica si se debe borrar todo el lcd al cambiar de pantalla.

    String Title = "";
    int32_t NewID = 10000;

    uint64_t LastTime;                         //�ltima vez que se actualiz�
    uint32_t UpdateTime = 250;                 // Tiempo de actualizaci�n de UI
    uint32_t UpdateStep = 0;                    //Paso de actualización
    uint32_t UpdateScreenIndex;                //Pantalla siendo actualizada
    uint32_t UpdateLcdStep;                     //Parte del LCD siendo actualizada
    static const uint32_t DftUpdateTime = 100; // Tiempo de actualizaci�n por defecto
    UI_Window*  uiWindow;                      //Puntero a la ventana actual

    void ResetUpdateTime();
    bool GoBack = false;
    bool GoHome = false;
    bool InFunct = false;

    bool ShowByIndex(int32_t Index);

    struct
    {
        int32_t Widht;
        int32_t Height;
    } LCD;

    Sound Speaker;

    bool UsingKeyEnter = true;
    bool UsingKeyTab = true;
    bool UsingKeyEsc = true;


    bool (*backgroundDrawer)(lcd_ui* parent, UI_Window* win);

public:
    lcd_ui(ST7920_SPI& screen, keyboard& key, int8_t SoundPin = -1);

    ~lcd_ui();

    ST7920_SPI& lcd; // Instancia deLCD usada

    keyboard& keys; // Instancia de teclado

    inline int32_t getNewID() { return NewID++; };

    inline int32_t getWidht() { return LCD.Widht; };

    inline int32_t getHeight() { return LCD.Height; };


    // Screen_Message Msg;

    // Screen_Option OptionBox;

    // Screen_Question Question;

    void Using_KeyEnter(bool Using) { UsingKeyEnter = Using; };

    void Using_KeyTab(bool Using) { UsingKeyTab = Using; };

    void Using_KeyEsc(bool Using) { UsingKeyEsc = Using; };

    void begin();

    /*
            Indica cual es la pantalla principal. Esta pantalla se muestra cuando no queda
            ninguna pantalla en la pila de pantallas o se llama Home();
    */
    bool setMainScreen(const char* UI);

    /*
            Ejecuta las tareas del stack de la interfaz de usuario
    */
    void Run();

    int32_t GetScreenIndex();

    /*
            Muestra otra pantalla por nombre.

            Usando esta funci�n se agrega la pantalla solicitada a la pila de pantallas y se la muestra

            Antes de que esta pantalla se ejecute se la llama con la action==Init.

            Si no se encuentra la pantalla no se hace nada.
    */
    bool Show(const char* UI);

    /*
            Muestra otra pantalla por �ndice.

            Usando esta funci�n se agrega la pantalla solicitada a la pila de pantallas y se la muestra

            Antes de que esta pantalla se ejecute se la llama con la action==Init

            Si no se encuentra la pantalla no se hace nada.
    */
    bool Show(int32_t UI_ID);

    /*
            Termina la ejecuci�n de la pantalla actual y vuelve a mostrar la pantalla anterior.

            Se pasa el resultado de la ejecuci�n, para que la anterior pueda recuperarlo, y as� determinar
            el cusro de ejecuci�n.
    */
    bool Close(UI_DialogResult result);

    /*
            Vuelve a la pantalla principal, terminando la ejecuci�n de todas las pantallas en la pila.

            Cuando se llame a cada pantalla se lo hace con la action==Closing
    */
    bool Home();

    /*
            Devulve el valor con el que se cerr� la pantalla anterior.
    */
    UI_DialogResult getDialogResult() { return Result; };

    /*
            Selecciona el tiempo de refresco de la pantalla actual.
    */
    void SetUpdateTime(uint32_t Ms = DftUpdateTime);

    /*
            Consulta al teclado y devuelve la �ltima tecla presionada.

            Tambi�n, dependiendo el c�digo de la tecla, actualiza el valor de Blinker.
    */
    Keys GetKeys();

    /*
            Indica si cada vez que se hace un cambio de pantalla se debe borrar el LCD

            Se da un cambio de pantalla cada vez que se llama Show() o Close()
    */
    void setClearScreenOnScreenSwitch(bool val) { ClearScreenOnScreenSwitch = val; };

    /*
            Devuelve el estado de la funci�n ClearScreenOnScreenSwitch.
    */
    bool getClearScreenOnScreenSwitch() { return ClearScreenOnScreenSwitch; };

    /*
            Escribe un texto en pantalla con la alineaci�n indicada.

            Widht indica el ancho m�ximo que se puede ocupar. Si == -1 ocupa todo el ancho de la pantalla.

            Antes de llamar esta funci�n se debe posicionar el cursor en la ubicai�n deseada.
    */
    void PrintText(const char* Text, TextPos Pos, int32_t Widht = -1);

    int32_t Add_UI(int32_t ID, const char* Name, bool (*func)(lcd_ui*, UI_Action));

    int32_t Add_UI(int32_t ID, const char* Name, bool (lcd_ui::* member)(lcd_ui*, UI_Action));

    int32_t Add_UI(int32_t ID, const char* Name, lcd_ui_screen& ui_screen);

    int32_t Remove_UI(const char* Name);

    int32_t Remove_UI(int32_t ID);

    void setBackgroundDrawer(bool (*func)(lcd_ui* parent, UI_Window* win)) { backgroundDrawer = func; };

    void PrintState();

    /*
            Imprime una lista de todas las pantallas disponibles
    */
    void PrintScreenList();


    bool UI_Black(lcd_ui* ui, UI_Action action);

    UI_Window* getWindow();

    bool isTopScreen();
};


#endif
