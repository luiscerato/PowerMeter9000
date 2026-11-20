const fs = require('node:fs');
const zlib = require('node:zlib');
const width = 128, height = 60;
const pixels = new Uint8Array(width * height);
const glyphs = {
    A:['01110','10001','10001','11111','10001','10001','10001'],
    D:['11110','10001','10001','10001','10001','10001','11110'],
    E:['11111','10000','10000','11110','10000','10000','11111'],
    M:['10001','11011','10101','10101','10001','10001','10001'],
    O:['01110','10001','10001','10001','10001','10001','01110'],
    P:['11110','10001','10001','11110','10000','10000','10000'],
    R:['11110','10001','10001','11110','10100','10010','10001'],
    T:['11111','00100','00100','00100','00100','00100','00100'],
    W:['10001','10001','10001','10101','10101','10101','01010'],
    '0':['01110','10001','10011','10101','11001','10001','01110'],
    '9':['01110','10001','10001','01111','00001','00001','01110']
};

// Enciende un pixel dentro del lienzo; x/y son coordenadas enteras.
function pixel(x, y) {
    if (x >= 0 && x < width && y >= 0 && y < height) pixels[y * width + x] = 1;
}

// Dibuja un segmento sin antialias; x0/y0 y x1/y1 son sus extremos.
function line(x0, y0, x1, y1) {
    const steps = Math.max(Math.abs(x1 - x0), Math.abs(y1 - y0));
    for (let step = 0; step <= steps; ++step) {
        const ratio = steps ? step / steps : 0;
        pixel(Math.round(x0 + (x1 - x0) * ratio), Math.round(y0 + (y1 - y0) * ratio));
    }
}

// Dibuja texto pixelado; text/x/y fijan contenido y origen, scale su ampliacion entera.
function text(text, x, y, scale = 1) {
    for (const character of text) {
        const glyph = glyphs[character];
        if (glyph) for (let row = 0; row < 7; ++row) for (let col = 0; col < 5; ++col) {
            if (glyph[row][col] !== '1') continue;
            for (let dy = 0; dy < scale; ++dy) for (let dx = 0; dx < scale; ++dx)
                pixel(x + col * scale + dx, y + row * scale + dy);
        }
        x += 6 * scale;
    }
}

// Forma un chunk PNG con CRC; type identifica el bloque y data contiene sus bytes.
function chunk(type, data) {
    const name = Buffer.from(type), body = Buffer.concat([name, data]);
    let crc = 0xffffffff;
    for (const byte of body) {
        crc ^= byte;
        for (let bit = 0; bit < 8; ++bit) crc = crc >>> 1 ^ (crc & 1 ? 0xedb88320 : 0);
    }
    const result = Buffer.alloc(data.length + 12);
    result.writeUInt32BE(data.length); body.copy(result, 4); result.writeUInt32BE((crc ^ 0xffffffff) >>> 0, result.length - 4);
    return result;
}

// Exporta el bitmap real ampliado sin interpolacion; scale es entero, progress ocupa 0..120 pixeles.
function png(scale, progress = null) {
    const outputHeight = (progress === null ? height : 64) * scale;
    const header = Buffer.alloc(13); header.writeUInt32BE(width * scale); header.writeUInt32BE(outputHeight, 4);
    header[8] = 8;
    const rows = Buffer.alloc((width * scale + 1) * outputHeight);
    for (let y = 0; y < outputHeight; ++y) for (let x = 0; x < width * scale; ++x) {
        const px = Math.floor(x / scale), py = Math.floor(y / scale);
        let on = py < height && pixels[py * width + px];
        if (progress !== null && py >= 60) on = px >= 3 && px <= 124 &&
            (py === 60 || py === 63 || px === 3 || px === 124 || px >= 4 && px < 4 + progress);
        rows[y * (width * scale + 1) + 1 + x] = on ? 0 : 255;
    }
    return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]), chunk('IHDR', header), chunk('IDAT', zlib.deflateSync(rows)), chunk('IEND', Buffer.alloc(0))]);
}

// El emblema combina una senoide y esquinas de instrumento, sin trazos menores que un pixel.
for (const [x, direction] of [[5, 1], [43, -1]]) for (const [y, vertical] of [[8, 1], [43, -1]]) {
    line(x, y, x + direction * 6, y); line(x, y, x, y + vertical * 5);
}
for (let x = 8; x <= 40; ++x) {
    const y = Math.round(26 - 11 * Math.sin((x - 8) / 32 * 2 * Math.PI));
    if (x > 8) line(x - 1, Math.round(26 - 11 * Math.sin((x - 9) / 32 * 2 * Math.PI)), x, y);
    pixel(x, y + 1);
}
text('POWER', 57, 9, 2);
text('METER', 57, 29, 2);
line(17, 52, 34, 52); line(94, 52, 111, 52);
text('ADE9000', 44, 49);

const packed = Buffer.alloc(width * height / 8);
for (let index = 0; index < pixels.length; ++index) if (pixels[index]) packed[index >> 3] |= 0x80 >> (index & 7);
const bytes = Array.from({length: height}, (_, row) => '    ' + [...packed.subarray(row * 16, row * 16 + 16)]
    .map(byte => '0x' + byte.toString(16).padStart(2, '0')).join(', ')).join(',\n');
fs.mkdirSync('assets/boot', {recursive: true});
fs.writeFileSync('assets/boot/power-meter-128x60.png', png(1));
fs.writeFileSync('assets/boot/power-meter-preview.png', png(6, 72));
fs.writeFileSync('src/ui/boot_splash_bitmap.h', '#pragma once\n\n#include <Arduino.h>\n\n// Generado por tools/build_boot_splash.cjs: 128x60, filas de 16 bytes, MSB primero.\nstatic const uint8_t bootSplashBitmap[] PROGMEM = {\n' + bytes + '\n};\n');
console.log('Boot splash: 128x60, 960 bytes en flash; PNG y vista ampliada generados.');
