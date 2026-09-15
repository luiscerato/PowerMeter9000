const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const source = fs.readFileSync(path.join(__dirname, '../data/scope.js'), 'utf8');
const context = vm.createContext({ console, ArrayBuffer, Uint8Array });
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

// Un fragmento real de 128 muestras/canal no debe anexarse al buffer anterior al gap.
scope.Data.waves.forEach((wave) => { wave.gain = 1; });
scope.Data.window = 800;
scope.newData(new ArrayBuffer(1344));
assert.ok(scope.Buffer.every((buffer) => buffer.length === 128));
assert.ok(scope.Buffer.every((buffer) => buffer.every((value) => value === 0)));
scope.newData('scope_gap');
assert.ok(scope.Buffer.every((buffer) => buffer.length === 0));

scope.mode = 'event';
scope.Buffer[0].push(123);
scope.newData('scope_gap');
assert.equal(scope.Buffer[0][0], 123);
assert.equal(renders, 2);
console.log('PASS: gap vacia el osciloscopio, permite reanudar y no altera el visor de eventos.');
