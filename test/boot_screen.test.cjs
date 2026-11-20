const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const main = fs.readFileSync('src/main.cpp', 'utf8');
const boot = fs.readFileSync('src/ui/boot_screen.cpp', 'utf8');
const driver = fs.readFileSync('src/ST7920_SPI.cpp', 'utf8');
const bitmap = fs.readFileSync('src/ui/boot_splash_bitmap.h', 'utf8');

// Extrae el cuerpo real de name desde source para probarlo sin hardware.
function method(source, name) {
    const start = source.indexOf(name + '('), body = source.indexOf('{', start);
    assert.ok(start >= 0 && body > start, name);
    let end = body + 1, depth = 1;
    while (depth) {if (source[end] === '{') ++depth; if (source[end] === '}') --depth; ++end;}
    return source.slice(start, end);
}

assert.equal((bitmap.match(/0x[0-9a-f]{2}/gi) || []).length, 960);
const png = fs.readFileSync('assets/boot/power-meter-128x60.png');
assert.equal(png.readUInt32BE(16), 128);
assert.equal(png.readUInt32BE(20), 60);
assert.ok(main.indexOf('ui.begin();') > main.indexOf('Batt.Init();'));
assert.ok(main.indexOf('bootScreenShow(lcd)') < main.indexOf('storageManager.begin('));
assert.match(main, /if\s*\(!bootScreenHold\(\)\)\s*ui.Run\(\);/);
assert.doesNotMatch(boot, /\bdelay\s*\(|\bnew\b|\bmalloc\s*\(/);

const harness = `
#include <cstdint>
#include <cstddef>
using byte = uint8_t;
constexpr int HIGH=1, LOW=0;
constexpr void digitalWrite(int, int) {}
struct Spi {unsigned count=0; byte sent[128]{};
    constexpr void transfer(byte b) {sent[count++]=b;}
};
struct ST7920_SPI {
    Spi spi; byte scr[1024]{}, displayedScr[1024]{};
    int scrWd=16, csPin=0; bool accepted=true;
    uint8_t lastWord=0,lastRow=0,lastWords=0,lastRows=0; unsigned calls=0, width=0;
    constexpr void gotoXY(uint8_t word,uint8_t row) {lastWord=word;lastRow=row;}
    constexpr bool beginSpiTransaction() {return accepted;}
    constexpr void endSpiTransaction() {}
    constexpr void memcpy(byte* out,const byte* in,size_t n) {for(size_t i=0;i<n;++i)out[i]=in[i];}
    constexpr bool ${method(driver, 'ST7920_SPI::displayRegion').replace('ST7920_SPI::displayRegion', 'writeRegion')}
    constexpr bool displayRegion(uint8_t word,uint8_t row,uint8_t words,uint8_t rows) {
        ++calls;lastWord=word;lastRow=row;lastWords=words;lastRows=rows;return accepted;
    }
    constexpr void fillRect(unsigned,unsigned,unsigned w,unsigned,unsigned) {width+=w;}
};
struct Boot {
    uint8_t progressPixels=0; uint32_t releaseAtMs=0,now=0; bool finished=false;
    constexpr uint32_t millis() {return now;}
    constexpr void ${method(boot, 'bootScreenAdvance')}
    constexpr void ${method(boot, 'bootScreenFinish')}
    constexpr bool ${method(boot, 'bootScreenHold')}
};
// Comprueba transferencia, recorte y memoria sombra; no recibe parametros.
constexpr bool regionCases() {
    ST7920_SPI lcd;
    lcd.scr[978]=0xAB; lcd.scr[994]=0xCD;
    if(!lcd.writeRegion(1,61,2,2) || lcd.spi.count!=18) return false;
    if(lcd.spi.sent[0]!=0xFA || lcd.spi.sent[1]!=0xA0 || lcd.spi.sent[2]!=0xB0) return false;
    if(lcd.displayedScr[978]!=0xAB || lcd.displayedScr[994]!=0xCD || lcd.displayedScr[977]) return false;
    ST7920_SPI edge;
    if(!edge.writeRegion(7,63,255,255) || edge.spi.count!=5) return false;
    if(edge.writeRegion(8,0,1,1) || edge.writeRegion(0,64,1,1) || edge.writeRegion(0,0,0,1)) return false;
    ST7920_SPI failed; failed.accepted=false; failed.scr[978]=0xAB;
    return !failed.writeRegion(1,61,2,2) && !failed.spi.count && !failed.displayedScr[978];
}
// Comprueba avance acotado, reintento y espera no bloqueante con overflow; no recibe parametros.
constexpr bool progressCases() {
    Boot b; ST7920_SPI lcd;
    for(unsigned i=0;i<10;++i) {
        b.bootScreenAdvance(lcd,12);
        if(lcd.lastRows!=2 || lcd.lastWords>2 || lcd.lastRow!=61) return false;
    }
    if(b.progressPixels!=120 || lcd.width!=120 || lcd.calls!=10) return false;
    b.bootScreenAdvance(lcd,255); if(lcd.calls!=10) return false;
    b.now=0xFFFFFF00; b.bootScreenFinish(lcd);
    if(!b.bootScreenHold()) return false;
    b.now+=499; if(!b.bootScreenHold()) return false;
    ++b.now; if(b.bootScreenHold()) return false;
    Boot retry; ST7920_SPI failed; failed.accepted=false;
    retry.bootScreenAdvance(failed,12); if(retry.progressPixels) return false;
    failed.accepted=true; retry.bootScreenAdvance(failed,12);
    if(retry.progressPixels!=12) return false;
    retry.bootScreenAdvance(failed,255);
    return retry.progressPixels==120;
}
static_assert(regionCases(), "Region fisica y sombra sin sobrescribir otras filas");
static_assert(progressCases(), "Barra y espera visual sin bloquear el loop");
`;
const output = path.join('.pio', 'build', 'boot-screen-test.cpp');
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-fsyntax-only', output], {stdio: 'inherit'});
console.log('PASS: bitmap 128x60, actualizacion parcial, limites de barra y espera visual de 500 ms.');
