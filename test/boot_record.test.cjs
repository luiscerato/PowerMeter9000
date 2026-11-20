const assert = require('node:assert/strict');
const {codec, gridFixture} = require('./grid_fixture.cjs');
const {buildEventHistory} = require('../data/event_history.js');

// Fabrica una marca normal de arranque; reason es la causa ESP sin modificar los tamanos del formato.
function bootFixture(reason) {
    const bytes = gridFixture(), view = new DataView(bytes.buffer);
    view.setUint16(11, 4 | 1 << 6, true);
    view.setUint8(13, 32 | 8);
    view.setUint32(14, 0, true);
    view.setUint16(76, 0xb000 | reason, true);
    view.setUint16(28, codec.gridCrc16(bytes, 0, 28), true);
    const offset = 234 + view.getUint8(26) * 16;
    view.setUint16(offset, codec.gridCrc16(bytes, 30, offset - 30), true);
    return bytes;
}

for (const [reason, label] of [[1, 'encendido'], [3, 'reinicio por software'], [4, 'fallo / panic'], [9, 'brownout']]) {
    const event = codec.decodeGridEvent(bootFixture(reason).buffer);
    assert.equal(event.startup, true);
    assert.equal(event.settings.resetReason, reason);
    assert.equal(codec.gridStartupReason(event), label);
    assert.equal(event.previousDurationMs, 0);
    assert.equal(event.states.previous, 0);
    assert.equal(event.states.current, 1);
    const result = buildEventHistory([{id: event.id, timestamp: event.timestamp, startup: true,
        records: [{offsetMs: 0, previousDurationMs: 0, root: true, states: {...event.states}}]}], event.timestamp - 1000, event.timestamp + 1000);
    assert.equal(result.intervals.filter(item => item.point || item.state > 1).length, 0, 'arranque normal no cuenta como corte');
}
console.log('PASS: causa de arranque protegida por CRC, formato intacto y normalidad sin corte inferido.');
