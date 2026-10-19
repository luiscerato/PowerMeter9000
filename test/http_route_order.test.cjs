const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.join(__dirname, '..');
const webServer = fs.readFileSync(path.join(root, 'src/webserver.cpp'), 'utf8');
const webFs = fs.readFileSync(path.join(root, 'src/web/webfs_http.cpp'), 'utf8');

// Verifica que las subrutas se registren antes de las rutas padre; no recibe parametros.
test('las rutas hijas se registran antes de sus raices', () => {
    const checks = [
        [webServer, 'server.on("/history/data"', 'registerHistoryRoot();'],
        [webServer, 'server.on("/grid-events/list"', 'registerGridEventsRoot();'],
        [webServer, 'server.on("/storage/test-download"', 'server.on("/storage", HTTP_GET'],
        [webFs, 'server.on("/webfs/image", HTTP_GET', 'registerWebFsStatus();'],
        [webFs, 'server.on("/webfs/image", HTTP_POST', 'registerWebFsStatus();'],
        [webFs, 'server.on("/webfs/rollback", HTTP_POST', 'registerWebFsStatus();']
    ];

    for (const [source, childRoute, parentRegistration] of checks) {
        assert.ok(source.indexOf(childRoute) < source.indexOf(parentRegistration), childRoute);
    }
    assert.equal(webFs.includes('server.on("/webfs/source"'), false);
    assert.equal(webFs.includes('WebAssetSource'), false);
    assert.doesNotMatch(webFs, /<select|source=spiffs|origen de la web/i);
});
