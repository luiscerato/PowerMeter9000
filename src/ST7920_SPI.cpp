#include "ST7920_SPI.h"

#define CONVERT_POLISH
#define fontbyte(x) pgm_read_byte(&cfont.font[x])

#include <SPI.h>

// ----------------------------------------------------------------
ST7920_SPI::ST7920_SPI(int8_t cs, int8_t sclk, int8_t mosi, int8_t miso)
{
	if (cs > -1) csPin = cs;
	if (sclk > -1) sclkPin = sclk;
	if (mosi > -1) mosiPin = mosi;
	if (miso > -1) misoPin = miso;
}
// ----------------------------------------------------------------
void ST7920_SPI::init()
{
	scrWd = SCR_WD / 8;
	scrHt = SCR_HT;
	isNumberFun = &isNumber;
	cr = 0;
	cfont.font = NULL;
	dualChar = 0;
	pinMode(csPin, OUTPUT);
	digitalWrite(csPin, LOW);
	SPI.begin(sclkPin, misoPin, mosiPin);

	sendCmd(LCD_BASIC);
	sendCmd(LCD_BASIC);
	sendCmd(LCD_CLS); delay(2);
	sendCmd(LCD_ADDRINC);
	sendCmd(LCD_DISPLAYON);
	setGfxMode(true);
}
// ----------------------------------------------------------------
void ST7920_SPI::sendCmd(byte b)
{
	SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
	digitalWrite(csPin, HIGH);
	SPI.transfer(0xF8);
	SPI.transfer(b & 0xF0);
	SPI.transfer(b << 4);
	digitalWrite(csPin, LOW);
	SPI.endTransaction();
}
// ----------------------------------------------------------------

void ST7920_SPI::sendData(byte b)
{
	SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
	digitalWrite(csPin, HIGH);
	SPI.transfer(0xFA);
	SPI.transfer(b & 0xF0);
	SPI.transfer(b << 4);
	digitalWrite(csPin, LOW);
	SPI.endTransaction();
}

// ----------------------------------------------------------------
// y =  0..63  -> buffer #0
// y = 64..127 -> buffer #1
void ST7920_SPI::gotoXY(byte x, byte y)
{
	if (y >= 32 && y < 64) { y -= 32; x += 8; }
	else
		if (y >= 64 && y < 64 + 32) { y -= 32; x += 0; }
		else
			if (y >= 64 + 32 && y < 64 + 64) { y -= 64; x += 8; }
	sendCmd(LCD_ADDR | y); // 6-bit (0..63)
	sendCmd(LCD_ADDR | x); // 4-bit (0..15)
}
// ----------------------------------------------------------------
// true - graphics mode
// ----------------------------------------------------------------
// false - text mode
void ST7920_SPI::setGfxMode(bool mode)
{
	if (mode) {
		sendCmd(LCD_EXTEND);
		sendCmd(LCD_GFXMODE);
	}
	else {
		sendCmd(LCD_EXTEND);
		sendCmd(LCD_TXTMODE);
	}
}
// ----------------------------------------------------------------
void ST7920_SPI::sleep(bool mode)
{
	//sendCmd(mode?LCD_STANDBY:LCD_EXTEND);
	if (mode) sendCmd(LCD_STANDBY);
	else { sendCmd(LCD_BASIC); sendCmd(LCD_EXTEND); sendCmd(LCD_GFXMODE); }
}
// ----------------------------------------------------------------
void ST7920_SPI::switchBuf(byte b)
{
	sendCmd(LCD_SCROLL);
	sendCmd(LCD_SCROLLADDR + 32 * b);
}
// ----------------------------------------------------------------
// Arduino Uno: 33-34ms -> 29fps
// optimized for full 128x64 frame
// 0 - buffer #0
// 1 - buffer #1
void ST7920_SPI::display(int buf)
{
	byte i, j, b;
	for (j = 0;j < 64 / 2;j++) {
		gotoXY(0, j + buf * 64);
		SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
		digitalWrite(csPin, HIGH);
		SPI.transfer(0xFA); // data
		for (i = 0;i < 16;i++) {  // 16 bytes from line #0+
			b = scr[i + j * 16];
			SPI.transfer(b & 0xF0);  SPI.transfer(b << 4);
		}
		for (i = 0;i < 16;i++) {  // 16 bytes from line #32+
			b = scr[i + (j + 32) * 16];
			SPI.transfer(b & 0xF0);  SPI.transfer(b << 4);
		}
		digitalWrite(csPin, LOW);
		SPI.endTransaction();
	}
}

void ST7920_SPI::display(int buf, int part)
{
	byte i, j, b;
	part &= 0x7;	//Solo 8 partes
	for (j = part * 4;j < ((part + 1) * 4);j++) {
		gotoXY(0, j + buf * 64);
		SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
		digitalWrite(csPin, HIGH);
		SPI.transfer(0xFA); // data
		for (i = 0;i < 16;i++) {  // 16 bytes from line #0+
			b = scr[i + j * 16];
			SPI.transfer(b & 0xF0);  SPI.transfer(b << 4);
		}
		for (i = 0;i < 16;i++) {  // 16 bytes from line #32+
			b = scr[i + (j + 32) * 16];
			SPI.transfer(b & 0xF0);  SPI.transfer(b << 4);
		}
		digitalWrite(csPin, LOW);
		SPI.endTransaction();
	}
}
// ----------------------------------------------------------------
// copy only part of framebuffer
void ST7920_SPI::copy(uint8_t x16, uint8_t y, uint8_t w16, uint8_t h, uint8_t buf)
{
	byte i, j, b;
	for (j = 0;j < h;j++) {
		gotoXY(x16, y + j + buf * 64);
		SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE3));
		digitalWrite(csPin, HIGH);
		SPI.transfer(0xFA); // data
		for (i = 0;i < w16;i++) {
			b = scr[(i + j * w16) * 2 + 0];
			SPI.transfer(b & 0xF0);  SPI.transfer(b << 4);
			b = scr[(i + j * w16) * 2 + 1];
			SPI.transfer(b & 0xF0);  SPI.transfer(b << 4);
		}
		digitalWrite(csPin, LOW);
		SPI.endTransaction();
	}
}
// ----------------------------------------------------------------
void ST7920_SPI::cls()
{
	memset(scr, 0, SCR_WD * SCR_HT / 8);
}

bool ST7920_SPI::setWindow(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	if (x >= SCR_WD || y >= SCR_HT) return false;
	if (x + w > SCR_WD) w = SCR_WD - x;
	if (y + h > SCR_HT) h = SCR_HT - y;
	winX = x;
	winY = y;
	winW = w;
	winH = h;
	return true;
}

// ----------------------------------------------------------------
void ST7920_SPI::drawPixel(uint8_t x, uint8_t y, uint8_t col)
{
	x += winX; y += winY;							//Trasladar coordenadas
	if (x >= winW || y >= winH) return; 			//X o Y fuera de ventana
	switch (col) {
	case 1: scr[y * scrWd + x / 8] |= (0x80 >> (x & 7)); break;
	case 0: scr[y * scrWd + x / 8] &= ~(0x80 >> (x & 7)); break;
	case 2: scr[y * scrWd + x / 8] ^= (0x80 >> (x & 7)); break;
	}
}
// ----------------------------------------------------------------
void ST7920_SPI::drawLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, uint8_t col)
{

	if (x1 < x0) { int x = x0;  x0 = x1; x1 = x; }	//Invertir coordenadas
	if (y1 < y0) { int y = y0;  y0 = y1; y1 = y; }	//Invertir coordenadas
	if (x0 >= winW || y0 >= winH) return; 			//X o Y fuera de ventana
	int dx = x1 - x0;
	int dy = y1 - y0;
	int err = dx - dy;
	int32_t xMax = winX + winW;
	int32_t yMax = winY + winH;

	while (1) {
		drawPixel(x0, y0, col);
		if (x0 == x1 && y0 == y1) return;
		int err2 = err + err;
		if (err2 > -dy) { err -= dy; x0++; }
		if (err2 < dx) { err += dx; y0++; }
		if (x0 >= winW || y0 >= winH) return;		//Terminar de dibujar si se sale de la ventana o la pantalla
	}
}
// ----------------------------------------------------------------
void ST7920_SPI::drawLineH(uint8_t x0, uint8_t x1, uint8_t y, uint8_t col)
{
	if (x1 < x0) { int x = x0;  x0 = x1; x1 = x; }	//Invertir coordenadas X
	if (x0 >= winW || y >= winH) return; 			//X o Y fuera de ventana
	if (x1 > (winX + winW)) x1 = winX + winW - 1;			//Limitar el largo de la línea

	for (uint8_t x = x0; x <= x1; x++)
		drawPixel(x, y, col);
}
// ----------------------------------------------------------------
void ST7920_SPI::drawLineV(uint8_t x, uint8_t y0, uint8_t y1, uint8_t col)
{
	if (y1 < y0) { int y = y0;  y0 = y1; y1 = y; }	//Invertir coordenadas Y
	if (x >= winW || y0 >= winH) return; 			//X o Y fuera de ventana
	if (y1 > (winY + winH)) y1 = winY + winH - 1;			//Limitar el largo de la línea

	for (uint8_t y = y0; y <= y1; y++)
		drawPixel(x, y, col);
}
// ----------------------------------------------------------------
// about 4x faster than regular drawLineV
void ST7920_SPI::drawLineVfast(uint8_t x, uint8_t y0, uint8_t y1, uint8_t col)
{
	// Serial.printf("drawLineVfast:   %d, %d, %d\n", x, y0, y1);
	if (y1 < y0) { int y = y0; y0 = y1; y1 = y; }	//Invertir coordenadas Y
	if (x >= winW || y0 >= winH) return; 			//X o Y fuera de ventana
	x += winX; y0 += winY; y1 += winY;				//Trasladar coordenadas
	if (y1 >= (winY + winH)) y1 = winY + winH - 1;			//Limitar el largo de la línea

	uint8_t mask = 0x80 >> (x & 7);
	switch (col) {
	case 1:
		for (int y = y0; y <= y1; y++)
			scr[y * scrWd + x / 8] |= mask;
		break;
	case 0:
		for (int y = y0; y <= y1; y++)
			scr[y * scrWd + x / 8] &= ~mask;
		break;
	case 2:
		for (int y = y0; y <= y1; y++)
			scr[y * scrWd + x / 8] ^= mask;
		break;
	}
}
// ----------------------------------------------------------------
// limited to pattern #8
void ST7920_SPI::drawLineVfastD(uint8_t x, uint8_t y0, uint8_t y1, uint8_t col)
{
	if (y1 < y0) { int y = y0; y0 = y1; y1 = y; }	//Invertir coordenadas Y
	if (x >= winW || y0 >= winH) return; 			//X o Y fuera de ventana
	x += winX; y0 += winY; y1 += winY;				//Trasladar coordenadas
	if (y1 >= (winX + winW)) y1 = winY + winH - 1;			//Limitar el largo de la línea

	uint8_t mask = 0x80 >> (x & 7);
	if (((x & 1) == 1 && (y0 & 1) == 0) || ((x & 1) == 0 && (y0 & 1) == 1)) y0++;
	switch (col) {
	case 1:
		for (int y = y0; y <= y1; y += 2)
			scr[y * scrWd + x / 8] |= mask;
		break;
	case 0:
		for (int y = y0; y <= y1; y += 2)
			scr[y * scrWd + x / 8] &= ~mask;
		break;
	case 2:
		for (int y = y0; y <= y1; y += 2)
			scr[y * scrWd + x / 8] ^= mask;
		break;
	}
}
// ----------------------------------------------------------------
byte ST7920_SPI::xstab[8] = { 0xff,0x7f,0x3f,0x1f,0x0f,0x07,0x03,0x01 };
byte ST7920_SPI::xetab[8] = { 0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff };
byte ST7920_SPI::pattern[4] = { 0xaa,0x55,0xaa,0x55 };
// about 40x faster than regular drawLineH
void ST7920_SPI::drawLineHfast(uint8_t x0, uint8_t x1, uint8_t y, uint8_t col)
{
	if (x1 < x0) { int x = x0;  x0 = x1; x1 = x; }	//Invertir coordenadas X
	if (x0 >= winW || y >= winH) return; 			//X o Y fuera de ventana
	x0 += winX; x1 += winX; y += winY;				//Trasladar coordenadas
	if (x1 >= (winX + winW)) x1 = winX + winW - 1;			//Limitar el largo de la línea

	int yadd = y * scrWd;
	int x8s = x0 / 8, x8e = x1 / 8;
	switch (col) {
	case 1:
		if (x8s == x8e) scr[yadd + x8s] |= (xstab[x0 & 7] & xetab[x1 & 7]);
		else { scr[yadd + x8s] |= xstab[x0 & 7]; scr[yadd + x8e] |= xetab[x1 & 7]; }
		for (int x = x8s + 1; x < x8e; x++) scr[yadd + x] = 0xff;
		break;
	case 0:
		if (x8s == x8e) scr[yadd + x8s] &= ~(xstab[x0 & 7] & xetab[x1 & 7]);
		else { scr[yadd + x8s] &= ~xstab[x0 & 7]; scr[yadd + x8e] &= ~xetab[x1 & 7]; }
		for (int x = x8s + 1; x < x8e; x++) scr[yadd + x] = 0x00;
		break;
	case 2:
		if (x8s == x8e)
			scr[yadd + x8s] ^= (xstab[x0 & 7] & xetab[x1 & 7]);
		else {
			scr[yadd + x8s] ^= xstab[x0 & 7];
			scr[yadd + x8e] ^= xetab[x1 & 7];
		}
		for (int x = x8s + 1; x < x8e; x++)
			scr[yadd + x] ^= 0xff;
		break;
	}
}
// ----------------------------------------------------------------
// dithered version
void ST7920_SPI::drawLineHfastD(uint8_t x0, uint8_t x1, uint8_t y, uint8_t col)
{
	if (x1 < x0) { int x = x0;  x0 = x1; x1 = x; }	//Invertir coordenadas X
	if (x0 >= winW || y >= winH) return; 			//X o Y fuera de ventana
	x0 += winX; x1 += winX; y += winY;				//Trasladar coordenadas
	if (x1 >= (winX + winW)) x1 = winX + winW - 1;			//Limitar el largo de la línea

	int yadd = y * scrWd;
	int x8s = x0 / 8, x8e = x1 / 8;
	switch (col) {
	case 1:
		if (x8s == x8e) scr[yadd + x8s] |= (xstab[x0 & 7] & xetab[x1 & 7] & pattern[y & 3]);
		else { scr[yadd + x8s] |= (xstab[x0 & 7] & pattern[y & 3]); scr[yadd + x8e] |= (xetab[x1 & 7] & pattern[y & 3]); }
		for (int x = x8s + 1; x < x8e; x++) scr[yadd + x] = pattern[y & 3];
		break;
	case 0:
		if (x8s == x8e) scr[yadd + x8s] &= ~(xstab[x0 & 7] & xetab[x1 & 7] & pattern[y & 3]);
		else { scr[yadd + x8s] &= ~(xstab[x0 & 7] & pattern[y & 3]); scr[yadd + x8e] &= ~(xetab[x1 & 7] & pattern[y & 3]); }
		for (int x = x8s + 1; x < x8e; x++) scr[yadd + x] = ~pattern[y & 3];
		break;
	case 2:
		if (x8s == x8e) scr[yadd + x8s] ^= (xstab[x0 & 7] & xetab[x1 & 7] & pattern[y & 3]);
		else { scr[yadd + x8s] ^= (xstab[x0 & 7] & pattern[y & 3]); scr[yadd + x8e] ^= (xetab[x1 & 7] & pattern[y & 3]); }
		for (int x = x8s + 1; x < x8e; x++) scr[yadd + x] ^= pattern[y & 3];
		break;
	}
}
// ----------------------------------------------------------------
void ST7920_SPI::drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t col)
{
	if (x >= winW || y >= winH) return; 		//X o Y fuera de ventana
	drawLineHfast(x, x + w - 1, y, col);								//Top line
	drawLineVfast(x, y + 1, y + h - 1, col);							//Left line
	if (h <= winH) drawLineHfast(x + 1, x + w - 2, y + h - 1, col);		//Bottom line
	if (w <= winW) drawLineVfast(x + w - 1, y + 1, y + h - 1, col);		//Right line
}
// ----------------------------------------------------------------
// dithered version (50% of brightness)
void ST7920_SPI::drawRectD(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t col)
{
	if (x >= winW || y >= winH) return; 		//X o Y fuera de ventana
	setDither(8);
	drawLineHfastD(x, x + w - 1, y, col);								//Top line
	drawLineVfastD(x, y + 1, y + h - 1, col);							//Left line
	if (h <= winH) drawLineHfastD(x + 1, x + w - 2, y + h - 1, col);		//Bottom line
	if (w <= winW) drawLineVfastD(x + w - 1, y + 1, y + h - 1, col);		//Right line
}
// ----------------------------------------------------------------
void ST7920_SPI::fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t col)
{
	if (x >= winW || y >= winH) return; 		//X o Y fuera de ventana
	if (x + w > winW) w = winW - x;
	if (y + h > winH) h = winH - y;
	for (int i = y;i < y + h;i++)
		drawLineHfast(x, x + w - 1, i, col);
}
// ----------------------------------------------------------------
// dithered version (50% of brightness)
void ST7920_SPI::fillRectD(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t col)
{
	if (x >= winW || y >= winH) return; 		//X o Y fuera de ventana
	if (x + w > winW) w = winW - x;
	if (y + h > winH) h = winH - y;
	for (int i = y;i < y + h;i++)
		drawLineHfastD(x, x + w - 1, i, col);
}
// ----------------------------------------------------------------
// circle
void ST7920_SPI::drawCircle(uint8_t x0, uint8_t y0, uint8_t radius, uint8_t col)
{
	if ((int)x0 - (int)radius >= winW || (int)y0 - (int)radius >= winH) return; 		//X o Y fuera de ventana
	int f = 1 - (int)radius;
	int ddF_x = 1;
	int ddF_y = -2 * (int)radius;
	int x = 0;
	int y = radius;

	drawPixel(x0, y0 + radius, col);
	drawPixel(x0, y0 - radius, col);
	drawPixel(x0 + radius, y0, col);
	drawPixel(x0 - radius, y0, col);

	while (x < y) {
		if (f >= 0) {
			y--; ddF_y += 2; f += ddF_y;
		}
		x++; ddF_x += 2; f += ddF_x;
		drawPixel(x0 + x, y0 + y, col);
		drawPixel(x0 - x, y0 + y, col);
		drawPixel(x0 + x, y0 - y, col);
		drawPixel(x0 - x, y0 - y, col);
		drawPixel(x0 + y, y0 + x, col);
		drawPixel(x0 - y, y0 + x, col);
		drawPixel(x0 + y, y0 - x, col);
		drawPixel(x0 - y, y0 - x, col);
	}
}
// ----------------------------------------------------------------
void ST7920_SPI::fillCircle(uint8_t x0, uint8_t y0, uint8_t r, uint8_t col)
{
	if ((int)x0 - (int)r >= winW || (int)y0 - (int)r >= winH) return; 		//X o Y fuera de ventana
	drawLineHfast(x0 - r, x0 - r + 2 * r + 1, y0, col);
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;
		drawLineHfast(x0 - x, x0 - x + 2 * x + 1, y0 + y, col);
		drawLineHfast(x0 - y, x0 - y + 2 * y + 1, y0 + x, col);
		drawLineHfast(x0 - x, x0 - x + 2 * x + 1, y0 - y, col);
		drawLineHfast(x0 - y, x0 - y + 2 * y + 1, y0 - x, col);
	}
}
// ----------------------------------------------------------------
// dithered version (50% of brightness)
void ST7920_SPI::fillCircleD(uint8_t x0, uint8_t y0, uint8_t r, uint8_t col)
{
	if ((int)x0 - (int)r >= winW || (int)y0 - (int)r >= winH) return; 		//X o Y fuera de ventana
	drawLineHfastD(x0 - r, x0 - r + 2 * r + 1, y0, col);
	int16_t f = 1 - r;
	int16_t ddF_x = 1;
	int16_t ddF_y = -2 * r;
	int16_t x = 0;
	int16_t y = r;

	while (x < y) {
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;
		drawLineHfastD(x0 - x, x0 - x + 2 * x + 1, y0 + y, col);
		drawLineHfastD(x0 - y, x0 - y + 2 * y + 1, y0 + x, col);
		drawLineHfastD(x0 - x, x0 - x + 2 * x + 1, y0 - y, col);
		drawLineHfastD(x0 - y, x0 - y + 2 * y + 1, y0 - x, col);
	}
}
// ----------------------------------------------------------------

const byte ST7920_SPI::ditherTab[4 * 17] PROGMEM = {
  0x00,0x00,0x00,0x00, // 0

  0x88,0x00,0x00,0x00, // 1
  0x88,0x00,0x22,0x00, // 2
  0x88,0x00,0xaa,0x00, // 3
  0xaa,0x00,0xaa,0x00, // 4
  0xaa,0x44,0xaa,0x00, // 5
  0xaa,0x44,0xaa,0x11, // 6
  0xaa,0x44,0xaa,0x55, // 7

  0xaa,0x55,0xaa,0x55, // 8

  0xaa,0xdd,0xaa,0x55, // 9
  0xaa,0xdd,0xaa,0x77, // 10
  0xaa,0xdd,0xaa,0xff, // 11
  0xaa,0xff,0xaa,0xff, // 12
  0xbb,0xff,0xaa,0xff, // 13
  0xbb,0xff,0xee,0xff, // 14
  0xbb,0xff,0xff,0xff, // 15

  0xff,0xff,0xff,0xff  // 16
};

void ST7920_SPI::setDither(uint8_t s)
{
	if (s > 16) return;
	pattern[0] = pgm_read_byte(ditherTab + s * 4 + 0);
	pattern[1] = pgm_read_byte(ditherTab + s * 4 + 1);
	pattern[2] = pgm_read_byte(ditherTab + s * 4 + 2);
	pattern[3] = pgm_read_byte(ditherTab + s * 4 + 3);
}
// ----------------------------------------------------------------

int ST7920_SPI::drawBitmap(const uint8_t* bmp, uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
	uint8_t wdb = w;

	if (x >= winW || y >= winH) return 0; 		//X o Y fuera de ventana
	x += winX; y += winY;						//Trasladar coordenadas
	if (w > winW) w = winW;						//Limitar tamaño dentro de la ventada disponible
	if (h > winH) h = winH;

	byte i, j8, d, b, ht8 = (h + 7) / 8;
	for (j8 = 0; j8 < ht8; j8++) {
		for (i = 0; i < w; i++) {
			byte mask = 0x80 >> ((x + i) & 7);
			d = pgm_read_byte(bmp + wdb * j8 + i);
			int lastbit = h - j8 * 8;
			if (lastbit > 8) lastbit = 8;
			for (b = 0; b < lastbit; b++) {
				if (d & 1) scr[(y + j8 * 8 + b) * scrWd + (x + i) / 8] |= mask;
				d >>= 1;
			}
		}
	}
	return x + w;
}
// ----------------------------------------------------------------
int ST7920_SPI::drawBitmap(const uint8_t* bmp, uint8_t x, uint8_t y)
{
	uint8_t w = pgm_read_byte(bmp + 0);
	uint8_t h = pgm_read_byte(bmp + 1);
	return drawBitmap(bmp + 2, x, y, w, h);
}
// ----------------------------------------------------------------
// text rendering
// ----------------------------------------------------------------
void ST7920_SPI::setFont(const uint8_t* font)
{
	cfont.font = font;
	cfont.xSize = fontbyte(0);
	cfont.ySize = fontbyte(1);
	cfont.firstCh = fontbyte(2);
	cfont.lastCh = fontbyte(3);
	cfont.minDigitWd = 0;
	cfont.minCharWd = 0;
	isNumberFun = &isNumber;
	spacing = 1;
	cr = 0;
	invertCh = 0;
}
// ----------------------------------------------------------------
int ST7920_SPI::charWidth(uint8_t c, bool last)
{
	c = convertPolish(c);
	if (c < cfont.firstCh || c > cfont.lastCh)
		return c == ' ' ? 1 + cfont.xSize / 2 : 0;
	if (cfont.xSize > 0) return cfont.xSize;
	int ys8 = (cfont.ySize + 7) / 8;
	int idx = 4 + (c - cfont.firstCh) * (-cfont.xSize * ys8 + 1);
	int wd = pgm_read_byte(cfont.font + idx);
	int wdL = 0, wdR = spacing; // default spacing before and behind char
	if ((*isNumberFun)(c)) {
		if (cfont.minDigitWd > wd) {
			wdL = (cfont.minDigitWd - wd) / 2;
			wdR += (cfont.minDigitWd - wd - wdL);
		}
	}
	else
		if (cfont.minCharWd > wd) {
			wdL = (cfont.minCharWd - wd) / 2;
			wdR += (cfont.minCharWd - wd - wdL);
		}
	return last ? wd + wdL + wdR : wd + wdL + wdR - spacing;  // last!=0 -> get rid of last empty columns 
}
// ----------------------------------------------------------------
int ST7920_SPI::strWidth(const char* str)
{
	int wd = 0;
	while (*str) wd += charWidth(*str++);
	return wd;
}
// ----------------------------------------------------------------
int ST7920_SPI::printChar(int xpos, int ypos, unsigned char c)
{
	if (xpos >= winW || ypos >= winH)  return 0; 		//X o Y fuera de ventana
	int fht8 = (cfont.ySize + 7) / 8, wd, fwd = cfont.xSize;
	if (fwd < 0)  fwd = -fwd;

	c = convertPolish(c);
	if (c < cfont.firstCh || c > cfont.lastCh)  return c == ' ' ? 1 + fwd / 2 : 0;

	int x, y8, b, cdata = (c - cfont.firstCh) * (fwd * fht8 + 1) + 4;
	byte d;
	wd = fontbyte(cdata++);
	int wdL = 0, wdR = spacing;
	if ((*isNumberFun)(c)) {
		if (cfont.minDigitWd > wd) {
			wdL = (cfont.minDigitWd - wd) / 2;
			wdR += (cfont.minDigitWd - wd - wdL);
		}
	}
	else
		if (cfont.minCharWd > wd) {
			wdL = (cfont.minCharWd - wd) / 2;
			wdR += (cfont.minCharWd - wd - wdL);
		}
	if (xpos + wd + wdL + wdR > winW) wdR = max((int)winW - xpos - wdL - wd, 0);
	if (xpos + wd + wdL + wdR > winW) wd = max((int)winW - xpos - wdL, 0);
	if (xpos + wd + wdL + wdR > winW) wdL = max((int)winW - xpos, 0);
	xpos += winX; ypos += winY;							//Trasladar coordenadas

	int32_t yMax = winY + winH;	//Posición máxima en Y
	for (x = 0; x < wd; x++) {
		byte mask = 0x80 >> ((xpos + x + wdL) & 7);
		for (y8 = 0; y8 < fht8; y8++) {
			d = fontbyte(cdata + x * fht8 + y8);
			int lastbit = cfont.ySize - y8 * 8;
			if (lastbit > 8) lastbit = 8;
			for (b = 0; b < lastbit; b++) {
				int32_t y = ypos + y8 * 8 + b;	//Posición Y absoluta en la pantalla
				if ((d & 1) && (y < yMax)) scr[y * scrWd + (xpos + x + wdL) / 8] |= mask;  //drawPixel(xpos+x, ypos+y8*8+b, 1);
				d >>= 1;
			}
		}
	}
	return wd + wdR + wdL;
}
// ----------------------------------------------------------------
int ST7920_SPI::printStr(int xpos, int ypos, const char* str)
{
	unsigned char ch;
	int stl, row;
	int x = xpos;
	int y = ypos;
	int wd = strWidth(str);

	if (x == -1) // right = -1
		x = SCR_WD - wd;
	else if (x < 0) // center = -2
		x = (SCR_WD - wd) / 2;
	if (x < 0) x = 0; // left

	while (*str) {
		int wd = printChar(x, y, *str++);
		x += wd;
		if (cr && x >= SCR_WD) {
			x = 0;
			y += cfont.ySize;
			if (y > SCR_HT) y = 0;
		}
	}
	if (invertCh) fillRect(xpos, x - 1, y, y + cfont.ySize + 1, 2);
	return x;
}

// ----------------------------------------------------------------
int ST7920_SPI::printStr(int xpos, int ypos, const char* str, TextAling aling, bool clearBG)
{
	unsigned char ch;
	int stl, row, wd, ht;
	int x = xpos;
	int y = ypos;

	if (aling != TextAling::TopLeft && aling != TextAling::MiddleLeft && aling != TextAling::BottomLeft || clearBG)
		wd = strWidth(str);
	ht = cfont.ySize;

	if (aling == TextAling::TopCenter || aling == TextAling::MiddleCenter || aling == TextAling::BottomCenter)	//Alineacion al centro
		x = x - (wd / 2) + 1;
	else if (aling == TextAling::TopRight || aling == TextAling::MiddleRight || aling == TextAling::BottomRight)	//Alineacion a la derecha
		x = x - wd + 1;

	if (aling == TextAling::MiddleLeft || aling == TextAling::MiddleCenter || aling == TextAling::MiddleRight)	//Alineacion al medio
		y = y - (ht / 2) + 1;
	else if (aling == TextAling::BottomLeft || aling == TextAling::BottomCenter || aling == TextAling::BottomRight)	//Alineacion abajo
		y = y - ht + 1;

	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > (SCR_WD - 1) || y > (SCR_HT - 1)) return 0;

	xpos = x;
	if (clearBG) fillRect(x, y, wd, ht + 1, 0);
	while (*str) {
		int w = printChar(x, y, *str++);
		x += w;
		wd += w;
		if (cr && x >= SCR_WD) break;
	}
	if (invertCh) fillRect(xpos, y, wd, y + cfont.ySize + 1, 2);
	return x;
}
// ----------------------------------------------------------------
void ST7920_SPI::printTxt(uint8_t pos, char* str)
{
	sendCmd(LCD_BASIC);
	sendCmd(pos);
	while (*str) sendData(*str++);
}
// ----------------------------------------------------------------
void ST7920_SPI::printTxt(uint8_t pos, uint16_t* signs)
{
	sendCmd(LCD_BASIC);
	sendCmd(pos);
	while (*signs) { sendData(*signs >> 8); sendData(*signs & 0xff); signs++; }
}
// ----------------------------------------------------------------
bool ST7920_SPI::isNumber(uint8_t ch)
{
	return isdigit(ch) || ch == ' ';
}
// ---------------------------------
bool ST7920_SPI::isNumberExt(uint8_t ch)
{
	return isdigit(ch) || ch == '-' || ch == '+' || ch == '.' || ch == ' ';
}
// ----------------------------------------------------------------
unsigned char ST7920_SPI::convertPolish(unsigned char _c)
{
	unsigned char pl, c = _c;
	if (c == 196 || c == 197 || c == 195 || c == 194) {
		dualChar = c;
		return 0;
	}
	if (dualChar) { // UTF8 coding
		switch (_c) {
		case 176: pl = 1; break; // '°'
		case 177: pl = 2; break; // 'ñ'
		case 145: pl = 3; break; // 'Ñ'
		case 161: pl = 4; break; // 'á'
		case 169: pl = 5; break; // 'é'
		case 173: pl = 6; break; // 'í'
		case 179: pl = 7; break; // 'ó'
		case 186: pl = 8; break; // 'ú'
		case 129: pl = 9; break; // 'Á'
		case 137: pl = 10; break; // 'É'
		case 141: pl = 11; break; // 'Í'
		case 147: pl = 12; break; // 'Ó'
		case 154: pl = 13; break; // 'Ú'
		default:  return c; break;
		}
		dualChar = 0;
	}
	else
		switch (_c) {  // Windows coding
		case 165: pl = 1; break; // �
		case 198: pl = 2; break; // �
		case 202: pl = 3; break; // �
		case 163: pl = 4; break; // �
		case 209: pl = 5; break; // �
		case 211: pl = 6; break; // �
		case 140: pl = 7; break; // �
		case 143: pl = 8; break; // �
		case 175: pl = 9; break; // �
		case 185: pl = 10; break; // �
		case 230: pl = 11; break; // �
		case 234: pl = 12; break; // �
		case 179: pl = 13; break; // �
		case 241: pl = 14; break; // �
		case 243: pl = 15; break; // �
		case 156: pl = 16; break; // �
		case 159: pl = 17; break; // �
		case 191: pl = 18; break; // �
		default:  return c; break;
		}
	return pl + '~' + 1;
}
// ---------------------------------
