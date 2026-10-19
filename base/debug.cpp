#include "debug.h"
#include "webserver.h"
#include <string.h>

DebugLevel currentDebugLevel = DEBUG_LEVEL_INFO; // Default level

static bool showModuleName = true;
static bool showTimestamp = true;

static constexpr uint8_t kMaxDebugModules = 16;
static constexpr uint8_t kDebugModuleNameMax = 16;

struct DebugModuleEntry {
    char moduleName[kDebugModuleNameMax + 1];
    DebugLevel level;
};

static DebugModuleEntry debugModules[kMaxDebugModules] = {};
static uint8_t debugModuleCount = 0;

// Busca la configuracion puntual de moduleName y devuelve su indice.
// moduleName es el nombre exacto del modulo que se desea buscar.
static int8_t findDebugModule(const char* moduleName)
{
    if (moduleName == nullptr)
        return -1;
    for (uint8_t i = 0; i < debugModuleCount; ++i) {
        if (strcmp(moduleName, debugModules[i].moduleName) == 0)
            return i;
    }
    return -1;
}

// Obtiene el nivel puntual de moduleName o el nivel global si no esta registrado.
// moduleName es el modulo que genera el mensaje.
static DebugLevel debugGetEffectiveLevel(const char* moduleName)
{
    const int8_t index = findDebugModule(moduleName);
    return index >= 0 ? debugModules[index].level : currentDebugLevel;
}

// Borra la configuracion puntual ubicada en index.
// index es la posicion de la excepcion dentro de debugModules.
static void removeDebugModule(uint8_t index)
{
    if (index >= debugModuleCount)
        return;
    for (uint8_t i = index; i + 1 < debugModuleCount; ++i)
        debugModules[i] = debugModules[i + 1];
    debugModuleCount--;
}

// Borra las excepciones que tienen el mismo nivel que el global.
// level es el nivel global contra el que se comparan las excepciones.
static void removeDebugModulesWithLevel(DebugLevel level)
{
    for (uint8_t i = 0; i < debugModuleCount;) {
        if (debugModules[i].level == level)
            removeDebugModule(i);
        else
            i++;
    }
}

// Avisa a la web que cambio la configuracion de debug.
// No recibe parametros; si el servidor no esta listo, la llamada no hace nada.
static void notifyDebugConfigChanged()
{
    webServerBroadcastDebugConfig();
}

static void debugEmit(DebugLevel level, const char* moduleName, uint32_t line, const char* format, va_list args)
{
    if (level > debugGetEffectiveLevel(moduleName))
        return;

    char message[256];
    vsnprintf(message, sizeof(message), format, args);

    const char* levelStr = getDebugLevelString(level);
    char prefix[160];
    prefix[0] = '\0';

    size_t used = 0;
    used += snprintf(prefix + used, sizeof(prefix) - used, "[%s]", levelStr);
    if (showTimestamp) {
        used += snprintf(prefix + used, sizeof(prefix) - used, "[%lu]", static_cast<unsigned long>(millis()));
    }
    if (showModuleName && moduleName != nullptr && moduleName[0] != '\0') {
        used += snprintf(prefix + used, sizeof(prefix) - used, "[%.*s:%lu]", kDebugModuleNameMax, moduleName, static_cast<unsigned long>(line));
    }

    char output[512];
    snprintf(output, sizeof(output), "%s %s", prefix, message);

    Serial.println(output);

    webServerQueueDebugMessage(output);
}

void setDebugLevel(DebugLevel level) {
    currentDebugLevel = level;
    removeDebugModulesWithLevel(level);
    notifyDebugConfigChanged();
}

bool setDebugLevel(const char* moduleName, DebugLevel level)
{
    if (moduleName == nullptr || moduleName[0] == '\0') {
        setDebugLevel(level);
        return true;
    }

    const int8_t index = findDebugModule(moduleName);
    if (level == currentDebugLevel) {
        if (index >= 0)
            removeDebugModule(index);
        notifyDebugConfigChanged();
        return true;
    }

    if (index >= 0) {
        debugModules[index].level = level;
        notifyDebugConfigChanged();
        return true;
    }

    if (debugModuleCount >= kMaxDebugModules || strlen(moduleName) > kDebugModuleNameMax)
        return false;

    DebugModuleEntry& entry = debugModules[debugModuleCount++];
    strcpy(entry.moduleName, moduleName);
    entry.level = level;
    notifyDebugConfigChanged();
    return true;
}

bool clearDebugLevel(const char* moduleName)
{
    const int8_t index = findDebugModule(moduleName);
    if (index < 0)
        return false;
    removeDebugModule(index);
    notifyDebugConfigChanged();
    return true;
}

void setDebugShowModuleName(bool enabled)
{
    showModuleName = enabled;
}

void setDebugShowTimestamp(bool enabled)
{
    showTimestamp = enabled;
}

DebugLevel getDebugLevel(const char* moduleName)
{
    return debugGetEffectiveLevel(moduleName);
}

bool debugIsEnabled(const char* moduleName, DebugLevel level)
{
    return level <= debugGetEffectiveLevel(moduleName);
}

uint8_t getDebugModuleCount()
{
    return debugModuleCount;
}

const char* getDebugModuleName(uint8_t index)
{
    return index < debugModuleCount ? debugModules[index].moduleName : nullptr;
}

const char* getDebugLevelString(DebugLevel level) {
    switch (level) {
    case DEBUG_LEVEL_NONE: return "NONE";
    case DEBUG_LEVEL_ERROR: return "ERROR";
    case DEBUG_LEVEL_WARN: return "WARN";
    case DEBUG_LEVEL_INFO: return "INFO";
    case DEBUG_LEVEL_VERBOSE: return "VERBOSE";
    case DEBUG_LEVEL_DEBUG: return "DEBUG";
    default: return "UNKNOWN";
    }
}

void debugPrint(DebugLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    debugEmit(level, nullptr, 0, format, args);
    va_end(args);
}

void debugPrintEx(DebugLevel level, const char* moduleName, uint32_t line, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    debugEmit(level, moduleName, line, format, args);
    va_end(args);
}
