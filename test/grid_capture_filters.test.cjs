const assert = require('node:assert/strict');
const test = require('node:test');
const {gridCaptureMatches} = require('../data/grid_events_ui.js');

// Construye un registro de estado; subject, previous y current definen la transicion, extra sus banderas.
function record(subject, previous, current, extra = {}) {
    return {offsetMs:0, states:{subject, previous, current, ...extra}};
}

test('filtra una fase secundaria y exige tipo y fase en el mismo registro', () => {
    const event = {timestamp:1000, records:[record(0,0,0,{isPoint:true,pointType:1}), record(1,1,4)]};
    assert.equal(gridCaptureMatches(event,new Set(['outage']),new Set([1]),0,2000),true);
    assert.equal(gridCaptureMatches(event,new Set(['dip']),new Set([1]),0,2000),false);
});

test('incluye retornos, reinicios y limites horarios sin confundir corriente y voltaje', () => {
    const event = {timestamp:1000, startup:true, records:[record(0,5,1,{isCurrent:true}),record(4,0,1)]};
    assert.equal(gridCaptureMatches(event,new Set(['current']),new Set([0]),1000,2000),true);
    assert.equal(gridCaptureMatches(event,new Set(['high']),new Set([0]),0,2000),false);
    assert.equal(gridCaptureMatches(event,new Set(['boot']),new Set([4]),0,2000),true);
    assert.equal(gridCaptureMatches(event,new Set(['boot']),new Set([4]),1001,2000),false);
});
