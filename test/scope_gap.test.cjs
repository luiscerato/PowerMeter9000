const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '../data/scope.js'), 'utf8');
const context = vm.createContext({ console, ArrayBuffer, Uint8Array, document: {getElementById: () => null} });
vm.runInContext(source + '\nglobalThis.scope = Scope;', context);
const scope = context.scope;
let renders = 0;
scope.drawWaves = () => { renders++; };
scope.Buffer.forEach((buffer) => buffer.push(10, 20));
scope.Data.waves.forEach((wave) => { wave.data = [10, 20]; });
scope.newData('unrelated_message');
assert.equal(scope.Buffer[0].length, 2);
scope.newData('scope_gap');
assert.ok(scope.Buffer.every((buffer) => buffer.length === 0));
assert.ok(scope.Data.waves.every((wave) => wave.data.length === 0));
assert.equal(renders, 1);

// Una trama actual de 128 muestras/canal debe reemplazar los datos tras el gap.
scope.Data.waves.forEach((wave) => { wave.gain = 1; });
scope.Data.window = 800;
const frame = new Uint8Array(24 + 1344);
frame.set([0x53, 0x43, 1]);
new DataView(frame.buffer).setUint16(4, 128, true);
new DataView(frame.buffer).setUint16(6, 0xffff, true);
new DataView(frame.buffer).setUint16(8, 125, true);
scope.newData(frame.buffer);
assert.ok(scope.Buffer.every((buffer) => buffer.length === 0));
assert.equal(scope.Data.framePoints, 128);
assert.ok(scope.Data.waves.every((wave) => wave.data.length === 128));
assert.ok(scope.Data.waves.every((wave) => wave.data.every((value) => value === 0)));
scope.newData('scope_gap');
assert.ok(scope.Data.waves.every((wave) => wave.data.length === 0));

scope.mode = 'event';
scope.Buffer[0].push(123);
scope.newData('scope_gap');
assert.equal(scope.Buffer[0][0], 123);
assert.equal(renders, 3);
console.log('PASS: gap vacia el osciloscopio, permite reanudar y no altera el visor de eventos.');
