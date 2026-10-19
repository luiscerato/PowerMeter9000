const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.join(__dirname, '..');
const historyUi = fs.readFileSync(path.join(root, 'data/history_ui.js'), 'utf8');

// Calcula el CRC-16 usado por las páginas históricas; data es el rango a validar.
function crc16(data) {
    let crc = 0xFFFF;
    for (const value of data) {
        crc ^= value << 8;
        for (let bit = 0; bit < 8; bit++) crc = crc & 0x8000 ? (crc << 1 ^ 0x1021) & 0xFFFF : crc << 1 & 0xFFFF;
    }
    return crc;
}

// Escribe un entero little-endian dentro del stream de bits; offset y length están en bits.
function writeBits(data, offset, length, value) {
    for (let bit = 0; bit < length; bit++) if (value & (1 << bit)) data[(offset + bit) >> 3] |= 1 << ((offset + bit) & 7);
}

// Construye una página mínima válida; config, version y sample definen su payload.
function makePage(config, version, sample) {
    const page = new Uint8Array(2048), view = new DataView(page.buffer);
    view.setUint32(0, 1767225600, true);
    page[4] = 1;
    page[7] = config & 255;
    page[8] = config >> 8 & 255;
    page[9] = config >> 16 & 255;
    view.setUint16(10, 10, true);
    view.setUint16(12, 1, true);
    page[14] = version;
    page.set(sample, 20);
    view.setUint16(16, crc16(page.subarray(0, 16)), true);
    view.setUint16(18, crc16(sample), true);
    return page;
}

// Comprueba el mapa V1 completo, el orden canónico de grupos y la lectura temporal de V2.
function run() {
    const legacyConfig = 2196;
    const legacyEnergyProfileConfig = 148;
    const temporaryV2Config = 1 | 4 | (2 << 3) | (2 << 5) | (1 << 9) | (2 << 12) | (1 << 16) | (1 << 17);
    const legacyPage = makePage(legacyConfig, 1, new Uint8Array(40));
    const legacyEnergyProfilePage = makePage(legacyEnergyProfileConfig, 1, new Uint8Array(38));
    const currentSample = new Uint8Array(52);
    [10, 20, 30, 3].forEach((value, index) => writeBits(currentSample, 316 + index * 24, 24, value));
    const temporaryV2Page = makePage(temporaryV2Config, 2, currentSample);
    const batteryConfig = 1 << 13;
    const batterySample = new Uint8Array(21);
    writeBits(batterySample, 144, 7, 85);
    writeBits(batterySample, 151, 7, 94);
    writeBits(batterySample, 158, 7, 10);
    writeBits(batterySample, 165, 3, 4);
    const batteryUnknownSample = new Uint8Array(21);
    writeBits(batteryUnknownSample, 144, 7, 85);
    writeBits(batteryUnknownSample, 151, 7, 94);
    writeBits(batteryUnknownSample, 158, 7, 10);
    writeBits(batteryUnknownSample, 165, 3, 7);
    const batteryPage = makePage(batteryConfig, 1, batterySample);
    const batteryUnknownPage = makePage(batteryConfig, 1, batteryUnknownSample);
    const combinedConfig = 0xC894 | (1 << 13);
    const combinedSample = new Uint8Array(55);
    [10, 20, 30].forEach((value, index) => writeBits(combinedSample, 316 + index * 24, 24, value));
    writeBits(combinedSample, 388, 24, 3);
    writeBits(combinedSample, 412, 7, 85);
    writeBits(combinedSample, 419, 7, 94);
    writeBits(combinedSample, 426, 7, 10);
    writeBits(combinedSample, 433, 3, 4);
    const combinedPage = makePage(combinedConfig, 1, combinedSample);

    const fullConfig = 1 | (2 << 1) | (2 << 3) | (2 << 5) | (1 << 7) | (1 << 8) | (1 << 9) |
        (2 << 10) | (1 << 12) | (1 << 13) | (1 << 14) | (7 << 15);
    const fullSample = new Uint8Array(97);
    [101, 202, 303].forEach((value, index) => writeBits(fullSample, 411 + index * 10, 10, value));
    [11, 22, 33].forEach((value, index) => writeBits(fullSample, 441 + index * 10, 10, value));
    writeBits(fullSample, 471, 16, 5034);
    [7200, 1800, 3600].forEach((value, index) => writeBits(fullSample, 487 + index * 13, 13, value));
    [101, 202, 303, 11, 22, 33, 44, 55, 66].forEach((value, index) => writeBits(fullSample, 526 + index * 24, 24, value));
    writeBits(fullSample, 742, 7, 85);
    writeBits(fullSample, 749, 7, 94);
    writeBits(fullSample, 756, 7, 10);
    writeBits(fullSample, 763, 3, 4);
    const fullPage = makePage(fullConfig, 1, fullSample);

    const energyModes = [];
    const extraBytes = [0, 3, 3, 6, 9, 9, 12, 18];
    for (let mode = 0; mode < 8; mode++) {
        const config = (1 << 14) | (mode << 15);
        const sample = new Uint8Array(27 + extraBytes[mode]);
        [1, 2, 3].forEach((value, index) => writeBits(sample, 144 + index * 24, 24, value));
        for (let index = 0; index < extraBytes[mode] / 3; index++) writeBits(sample, 216 + index * 24, 24, index + 1);
        energyModes.push(makePage(config, 1, sample));
    }

    const pages = [legacyPage, legacyEnergyProfilePage, temporaryV2Page, batteryPage, batteryUnknownPage, combinedPage, fullPage, ...energyModes];
    const file = new Uint8Array(pages.length * 2048);
    pages.forEach((page, index) => file.set(page, index * 2048));
    const output = vm.runInNewContext(historyUi + `
        JSON.stringify({
            legacySize: getHistorySampleSize(${legacyConfig}, 1),
            legacyEnergyProfileSize: getHistorySampleSize(${legacyEnergyProfileConfig}, 1),
            temporaryV2Size: getHistorySampleSize(${temporaryV2Config}, 2),
            batterySize: getHistorySampleSize(${batteryConfig}, 1),
            combinedSize: getHistorySampleSize(${combinedConfig}, 1),
            fullSize: getHistorySampleSize(${fullConfig}, 1),
            defaultSize: getHistorySampleSize(0xC894, 1),
            energyModeSizes: [0, 1, 2, 3, 4, 5, 6, 7].map((mode) => getHistorySampleSize(16384 | (mode << 15), 1)),
            records: decodeHistoryPages(new Uint8Array(${JSON.stringify([...file])}).buffer),
            invalidPages: historyViewer.invalidPages
        });
    `);
    const result = JSON.parse(output);
    assert.equal(result.legacySize, 40);
    assert.equal(result.legacyEnergyProfileSize, 38);
    assert.equal(result.temporaryV2Size, 52);
    assert.equal(result.batterySize, 21);
    assert.equal(result.combinedSize, 55);
    assert.equal(result.fullSize, 97);
    assert.equal(result.defaultSize, 52);
    assert.deepEqual(result.energyModeSizes, [27, 30, 30, 33, 36, 36, 39, 45]);
    const energyRecords = getEnergyModeRecords(result.records);
    assert.equal(energyRecords.length, 8);
    assert.equal(energyRecords[0].reactiveEnergyTotal, undefined);
    assert.equal(energyRecords[1].reactiveEnergyTotal, 100);
    assert.equal(energyRecords[2].apparentEnergyTotal, 100);
    assert.equal(energyRecords[3].reactiveEnergyTotal, 100);
    assert.equal(energyRecords[3].apparentEnergyTotal, 200);
    assert.equal(energyRecords[4].reactiveEnergyT, 30);
    assert.equal(energyRecords[5].apparentEnergyT, 30);
    assert.equal(energyRecords[6].reactiveEnergyT, 30);
    assert.equal(energyRecords[6].apparentEnergyTotal, 400);
    assert.equal(energyRecords[7].reactiveEnergyT, 30);
    assert.equal(energyRecords[7].apparentEnergyT, 60);
    assert.equal(result.records.length, 15);
    assert.equal(result.records[0].activeEnergyR, undefined);
    assert.equal(result.records[1].activeEnergyR, undefined);
    assert.equal(result.records[2].activeEnergyR, 100);
    assert.equal(result.records[2].activeEnergyS, 200);
    assert.equal(result.records[2].activeEnergyT, 300);
    assert.equal(result.records[2].reactiveEnergyTotal, 300);
    assert.ok(Math.abs(result.records[3].batteryVoltage - 4.004) < 0.001);
    assert.equal(result.records[3].batteryInputVoltage, 12);
    assert.equal(result.records[3].batteryCurrent, 0.5);
    assert.equal(result.records[3].batteryLevel, 80);
    assert.equal(result.records[4].batteryVoltage, null);
    assert.equal(result.records[4].batteryCurrent, null);
    assert.equal(result.records[4].batteryLevel, null);
    assert.equal(result.records[5].reactiveEnergyTotal, 300);
    assert.ok(Math.abs(result.records[5].batteryVoltage - 4.004) < 0.001);
    assert.equal(result.records[6].voltageThdR, 101);
    assert.equal(result.records[6].currentThdT, 33);
    assert.equal(result.records[6].frequency, 50.34);
    assert.equal(result.records[6].voltageAngleRS, 360);
    assert.equal(result.records[6].reactiveEnergyR, 110);
    assert.equal(result.records[6].apparentEnergyT, 660);
    assert.equal(result.records[6].batteryLevel, 80);
    assert.equal(result.invalidPages, 0);
}

// Selecciona las ocho paginas de prueba de energia que siguen a los perfiles base.
function getEnergyModeRecords(records) { return records.slice(7); }

run();
console.log('PASS: formato histórico V1 completo, orden ENERGY/BATTERY y compatibilidad V2 temporal.');
