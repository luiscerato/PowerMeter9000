const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');

// Lee source y elimina comentarios para verificar las llamadas que realmente se ejecutan.
function readCode(source) {
    return fs.readFileSync(path.join(root, source), 'utf8')
        .replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\r\n]*/g, '');
}

// Extrae el cuerpo delimitado por llaves de signature en source.
function body(source, signature) {
    const start = source.indexOf(signature);
    assert.ok(start >= 0, signature);
    const open = source.indexOf('{', start);
    let end = open + 1, depth = 1;
    while (depth && end < source.length) {
        if (source[end] === '{') depth++;
        if (source[end] === '}') depth--;
        end++;
    }
    assert.equal(depth, 0);
    return source.slice(open + 1, end - 1);
}

const meter = readCode('src/meter.cpp');
const task = body(meter, 'void MeterTask(');
const irq = body(task, 'while (digitalRead(pinAdeInt0) == 0)');
const wave = irq.indexOf('if (status0.PAGE_FULL)');
assert.ok(wave > irq.indexOf('ade.clearStatusBit0(status0.raw)'));
assert.equal((irq.match(/meterReadWaveBuffer\(\)/g) || []).length, 1);
for (const flag of ['RMSONERDY', 'RMS1012RDY', 'THD_PF_RDY', 'EGYRDY'])
    assert.ok(wave < irq.indexOf(`if (status0.${flag})`), `Wave antes de ${flag}`);
for (const later of ['eventStoreServiceCapture();', 'if (meterTestEventRequested', 'oi .raw ='])
    assert.ok(task.indexOf(later) > task.indexOf(irq) + irq.length, later);
assert.doesNotMatch(task, /\bdebug[IEWVD]\s*\(|Serial\.(?:print|printf|println)\s*\(/);
const header = readCode('src/meter.h');
for (const signature of ['bool startCapture(', 'void stopCapture(', 'void pushEvent(const char * event)', 'void printEvent()'])
    assert.doesNotMatch(body(header, signature), /\bdebug[IEWVD]\s*\(|Serial\.(?:print|printf|println)\s*\(/);
assert.match(meter, /waveReadHalf = lastPage >= 14 \|\| lastPage < 3 \? 1 : 0/);
assert.match(body(meter, 'static void meterSendScopeFrame('), /ESP.getFreeHeap\(\) < 50 \* 1024/);
console.log('PASS: Wave primero, comandos despues de IRQ0, sin prints en adquisicion; selector y limite de heap conservados.');
