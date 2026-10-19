#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdint.h>

// Niveles de debug
enum DebugLevel {
    DEBUG_LEVEL_NONE = 0,
    DEBUG_LEVEL_ERROR = 1,
    DEBUG_LEVEL_WARN = 2,
    DEBUG_LEVEL_INFO = 3,
    DEBUG_LEVEL_VERBOSE = 4,
    DEBUG_LEVEL_DEBUG = 5
};

// Variable global para el nivel de debug actual
extern DebugLevel currentDebugLevel;

// Configuracion de salida
void setDebugShowModuleName(bool enabled);
void setDebugShowTimestamp(bool enabled);

// Establece el nivel global usado por los modulos sin configuracion puntual.
void setDebugLevel(DebugLevel level);

// Registra o actualiza el nivel puntual de moduleName.
// moduleName identifica el modulo y level indica el nivel que debe utilizar.
bool setDebugLevel(const char* moduleName, DebugLevel level);

// Borra el nivel puntual de moduleName para que use el nivel global.
// moduleName identifica el modulo que se desea quitar de la lista.
bool clearDebugLevel(const char* moduleName);

// Funciones de acceso
DebugLevel getDebugLevel(const char* moduleName);
bool debugIsEnabled(const char* moduleName, DebugLevel level);
// Devuelve la cantidad de modulos que tienen un nivel puntual registrado.
uint8_t getDebugModuleCount();

// Devuelve el nombre del modulo registrado en index o nullptr si no existe.
// index es la posicion dentro de la lista de configuraciones puntuales.
const char* getDebugModuleName(uint8_t index);

// Funciones para imprimir debug con formato y nivel
void debugPrint(DebugLevel level, const char* format, ...);
void debugPrintEx(DebugLevel level, const char* moduleName, uint32_t line, const char* format, ...);

// Permite compilar una imagen OTA temporal sin trazas ni sus textos asociados.
#ifndef DEBUG_LOGS_ENABLED
#define DEBUG_LOGS_ENABLED 1
#endif

#if DEBUG_LOGS_ENABLED
#define debugI(...) debugPrintEx(DEBUG_LEVEL_INFO, DEBUG_MODULE, __LINE__, __VA_ARGS__)
#define debugE(...) debugPrintEx(DEBUG_LEVEL_ERROR, DEBUG_MODULE, __LINE__, __VA_ARGS__)
#define debugW(...) debugPrintEx(DEBUG_LEVEL_WARN, DEBUG_MODULE, __LINE__, __VA_ARGS__)
#define debugV(...) debugPrintEx(DEBUG_LEVEL_VERBOSE, DEBUG_MODULE, __LINE__, __VA_ARGS__)
#define debugD(...) debugPrintEx(DEBUG_LEVEL_DEBUG, DEBUG_MODULE, __LINE__, __VA_ARGS__)
#else
#define debugI(...) do {} while (false)
#define debugE(...) do {} while (false)
#define debugW(...) do {} while (false)
#define debugV(...) do {} while (false)
#define debugD(...) do {} while (false)
#endif

// Función para obtener el string del nivel
const char* getDebugLevelString(DebugLevel level);
