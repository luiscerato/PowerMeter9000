const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, '..');

// Lee un archivo del proyecto; name es su ruta relativa.
function readCode(name) { return fs.readFileSync(path.join(root, name), 'utf8'); }

for (const name of ['src/event_store.cpp', 'src/event_store.h', 'src/event_trigger.h', 'src/event_test_pattern.h'])
    assert.equal(fs.existsSync(path.join(root, name)), false, name);
for (const name of ['src/meter.cpp', 'src/meter.h', 'src/webserver.cpp', 'src/webserver.h',
    'src/grid_events.cpp', 'src/grid_events.h', 'src/wifiUtils.cpp', 'src/manager_api.cpp', 'src/manager_api.h']) {
    assert.doesNotMatch(readCode(name), /eventStore|EventsData|capturedEvent|acEvents|gridNotice|gridEventNotification|managerPublishEventNotification|"\/events(?:["/])|events\/notification/, name);
}
const meter = readCode('src/meter.cpp'), server = readCode('src/webserver.cpp');
assert.match(meter, /gridEventStorePushFastSample\(/);
assert.match(meter, /gridEventStoreObserve\(/);
assert.match(meter, /gridEventsPersistLoop\(\)/);
assert.match(meter, /bl0942.startFastSample\(\)/);
assert.doesNotMatch(meter, /ade.setDipDetectionLevels\(/);
assert.doesNotMatch(meter, /ade.setSwellDetectionLevels\(/);
assert.match(meter, /ade.enableOverCurrentDetection\(/);
assert.doesNotMatch(meter, /meterReadDipSwell\(\)/);
assert.match(meter, /detector.update\(/);
assert.match(meter, /socket->binaryAll\(data, size\)/);
assert.match(server, /server.addHandler\(&scopeWS\)/);
assert.match(server, /server.on\("\/grid-events\/data"/);
assert.match(readCode('src/manager_api.cpp'), /eventNotifications\\":false/);
const html = readCode('data/index.html');
assert.doesNotMatch(html, /id="events"|eventRamList|eventTestBtn/);
assert.match(html, /id="gridEvents"/);
assert.match(html, /id="gridEventContext"/);
assert.match(html, /title="Eventos"/);
for (const name of ['data/index.js', 'data/script.js', 'data/board.js'])
    assert.doesNotMatch(readCode(name), /loadRamEventsList|loadNodeFile|\/events\b|eventPrevSamples|eventPostSamples|eventMaxSamples/, name);
console.log('PASS: camino antiguo eliminado, NAND y detectores presentes, scope en vivo y web sin consultas antiguas.');
