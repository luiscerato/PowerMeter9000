const assert = require('node:assert/strict');
const test = require('node:test');
const {layoutEventHistoryPoints} = require('../data/event_history.js');

// Fabrica un marcador con captura id y posicion at; ids permite duplicados de una misma observacion.
function point(id, at, ids = [id]) { return {ids,start:at,point:1,key:String(id)}; }

test('los eventos cercanos comparten marca sin perder sus capturas ni aumentar altura',()=>{
    const layout = layoutEventHistoryPoints([point(45,502),point(43,500),point(44,501)],0,100000,300);
    assert.equal(layout.length,1);
    assert.deepEqual(layout[0].items.map(item=>item.ids[0]),[43,44,45]);
    assert.equal(layout[0].size,18);
    assert.equal(layout[0].anchor,1.5);
});

test('eventos separados mantienen marcas independientes contenidas en los extremos',()=>{
    const layout = layoutEventHistoryPoints([point(1,0),point(2,500),point(3,1000)],0,1000,300);
    assert.equal(layout.length,3);
    for(const marker of layout){assert.ok(marker.left>=0);assert.ok(marker.left+marker.size<=300);}
    assert.equal(layout[2].anchor,300);
});

test('conserva IDs de las observaciones compartidas',()=>{
    const layout = layoutEventHistoryPoints([point(4,500,[4,2])],0,1000,200);
    assert.deepEqual(layout[0].items[0].ids,[4,2]);
    assert.deepEqual(layoutEventHistoryPoints([],0,1000,200),[]);
});
