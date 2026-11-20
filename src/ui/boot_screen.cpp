#include "boot_screen.h"
#include "../ST7920_SPI.h"
#include "boot_splash_bitmap.h"

static uint8_t progressPixels = 0;
static uint32_t releaseAtMs = 0;
static bool finished = false;

// Copia desde flash al framebuffer existente, sin otra reserva RAM; screen debe tener el SPI preparado.
void bootScreenShow(ST7920_SPI& screen)
{
    progressPixels = 0;
    finished = false;
    screen.setFullScreen();
    screen.cls();
    memcpy_P(screen.scr, bootSplashBitmap, sizeof(bootSplashBitmap));
    screen.drawRect(3, 60, 122, 4, 1);
    screen.display(0);
}

// Avanza pixels, acotados a 120; screen solo recibe las palabras afectadas de las filas 61 y 62.
void bootScreenAdvance(ST7920_SPI& screen, uint8_t pixels)
{
    if (finished || !pixels || progressPixels == 120) return;
    const uint8_t next = uint16_t(progressPixels) + pixels > 120 ? 120 : progressPixels + pixels;
    const uint8_t firstWord = (4 + progressPixels) / 16;
    const uint8_t lastWord = (4 + next - 1) / 16;
    screen.fillRect(4 + progressPixels, 61, next - progressPixels, 2, 1);
    if (screen.displayRegion(firstWord, 61, lastWord - firstWord + 1, 2)) progressPixels = next;
}

// Inicia el plazo solo despues de completar la barra; screen es el mismo framebuffer compartido con la UI.
void bootScreenFinish(ST7920_SPI& screen)
{
    if (finished) return;
    bootScreenAdvance(screen, 120);
    releaseAtMs = millis() + 500U;
    finished = true;
}

// Usa resta con signo para soportar overflow de millis; no recibe parametros ni duerme el procesador.
bool bootScreenHold()
{
    return !finished || int32_t(millis() - releaseAtMs) < 0;
}
