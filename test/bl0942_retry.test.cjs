const assert = require('node:assert/strict');
const fs = require('node:fs');
const source = fs.readFileSync('src/bl0942/bl0942.cpp', 'utf8');

// Ejecuta los pasos reales del driver con UART simulada; name identifica el metodo C++.
function loadMethod(name) {
    const match = source.match(new RegExp(`void BL0942::${name}\\(uint32_t nowUs\\)\\n?\\r?\\n?\\{([\\s\\S]*?)^\\}`, 'm'));
    assert.ok(match, name);
    const body = match[1]
        .replace(/const (?:uint32_t|uint8_t) (\w+)\[\] =/g, 'const $1 =')
        .replace(/uint8_t packet\[\] =/g, 'let packet =')
        .replace(/const uint8_t /g, 'const ')
        .replace(/= \{([^{}]*)\};/g, '= [$1];')
        .replace(/HardwareSerial::begin/g, 'uartBegin')
        .replace(/sizeof\(packet\)/g, 'packet.length')
        .replace(/\b(?:uint8_t|int32_t|int)\(/g, 'Number(')
        .replace(/(\d+)U\b/g, '$1');
    return new Function('context', 'nowUs', `with (context) { ${body} }`);
}

const poll = loadMethod('pollInitialization');
const retry = loadMethod('retryInitialization');
const recover = loadMethod('recover');
const writes = [], reads = [], bauds = [];
const context = {
    state: {ready: false}, pendingRegister: 0, responseLength: 0, recovering: false,
    initializationStep: 0, initializationDueUs: 0, consecutiveFailures: 0, baudIndex: 0,
    uartRx: 13, uartTx: 12, SERIAL_8N1: 0, NAN: NaN,
    fastRmsRegister: 0x05, modeRegister: 0x19, gainRegister: 0x1a, fastCyclesRegister: 0x16,
    antiCreepRegister: 0x14, writeProtectRegister: 0x1d, modeValue: 0x397,
    writeCommand: 0xa8, recoveryIntervalUs: 2000, recoveryUntilUs: 0,
    fastCyclePending: false, fastCycleVoltage: NaN, slowSlotOpen: false,
    uartBegin: baud => bauds.push(baud), availableForWrite: () => 128,
    beginUart: baud => bauds.push(baud),
    write: packet => { writes.push(packet); return packet.length; },
    requestReg: reg => { reads.push(reg); context.pendingRegister = reg; },
    retryInitialization: nowUs => retry(context, nowUs)
};
for (let attempt = 0; attempt < 12; ++attempt) {
    retry(context, attempt * 300000);
    poll(context, context.initializationDueUs - 1);
    assert.equal(context.initializationStep, 0);
    poll(context, context.initializationDueUs);
    poll(context, context.initializationDueUs);
    assert.equal(reads.at(-1), 0x19);
}
assert.deepEqual(bauds, Array(3).fill([38400, 4800, 9600, 19200]).flat());
context.pendingRegister = 0;
context.initializationStep = 2;
while (context.initializationStep < 9) poll(context, context.initializationDueUs);
assert.deepEqual(writes.map(packet => packet[1]), [0x1d, 0x19, 0x1a, 0x16, 0x14, 0x1d]);
for (let step = 9; step <= 12; ++step) {
    context.initializationStep = step;
    context.pendingRegister = 0;
    poll(context, context.initializationDueUs);
}
assert.deepEqual(reads.slice(-4), [0x19, 0x1a, 0x16, 0x14]);
context.state.ready = true;
for (let failure = 0; failure < 8; ++failure) recover(context, 5000000);
assert.equal(context.state.ready, false);
assert.ok(Number.isNaN(context.state.voltage));
assert.equal(context.initializationStep, 0);
assert.equal(context.initializationDueUs, 5250000);
console.log('BL0942: reintentos continuos, configuracion y recuperacion OK');
