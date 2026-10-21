const assert = require('node:assert/strict');
const fs = require('node:fs');
const {codec, gridFixture} = require('./grid_fixture.cjs');
const fixture = gridFixture(), event = codec.decodeGridEvent(fixture.buffer);
assert.equal(event.samples.length,4); assert.equal(event.hasNeutral,true); assert.equal(event.partial,false);
assert.equal(event.samples[0].vn,20); assert.equal(event.samples[0].vr,220); assert.equal(event.samples[0].inn,3);
assert.deepEqual(Array.from(event.samples,sample => sample.relativeMs),[-10,0,10,20]);
const invalid = fixture.slice(); invalid[26] = 114;
assert.throws(() => codec.decodeGridEvent(invalid.buffer),/inválida/);
for (const oldFormat of [1,2,3]) {const old = fixture.slice(); old[0] = oldFormat; assert.throws(() => codec.decodeGridEvent(old.buffer),/no se admiten/);}
console.log('PASS: unico formato, ocho canales de 14 bits, neutro, tiempos, limites y rechazo de formatos anteriores.');
for (const filename of process.argv.slice(2)) {
    const bytes = fs.readFileSync(filename), event = codec.decodeGridEvent(bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.length));
    console.log(JSON.stringify({filename,id:event.id,samples:event.samples.length,partial:event.partial,invalidPages:event.invalidPages,
        point:event.states.isPoint,current:event.states.isCurrent,settings:event.settings,transitions:event.transitions.length}));
}
