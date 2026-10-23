const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const test = require('node:test');

const html = fs.readFileSync('data/index.html', 'utf8');
const server = fs.readFileSync('src/web/webfs_http.cpp', 'utf8');
const loader = html.match(/<script>([\s\S]*?)<\/script>/)[1].replace('bootApp();', '');
const context = vm.createContext({});
vm.runInContext(loader, context);

// Comprueba que las dependencias no cambien de URL con cada version de la app; sin parametros.
test('las librerias tienen una version independiente del codigo propio', () => {
    assert.equal(vm.runInContext('cacheBust("lib/canvasjs.min.js")', context), 'lib/canvasjs.min.js?v=vendor-1');
    assert.equal(vm.runInContext('cacheBust("style.css")', context), 'style.css');
    assert.equal(vm.runInContext('cacheBust("index.js?test=1")', context), 'index.js?test=1');
    assert.equal(vm.runInContext('cacheBust("lib/jquery.simplegauge.js?test=1")', context), 'lib/jquery.simplegauge.js?test=1&v=vendor-1');
});

// Verifica revalidacion de archivos propios sin reservar un lector NAND; sin parametros.
test('el codigo propio exige revalidacion y responde 304 antes de reservar RAM', () => {
    assert.match(server, /path\.startsWith\("\/lib\/"\)/);
    assert.match(server, /library \? "public, max-age=31536000, immutable" : "private, no-cache, must-revalidate, no-transform"/);
    assert.ok(server.indexOf('request->beginResponse(304)') < server.indexOf('WebFsReadSession* session = findWebFsReadSession();'));
    assert.match(server, /validator == etag/);
    assert.match(server, /strcmp\(validator.c_str\(\) \+ 2, etag.c_str\(\)\) == 0/);
    assert.doesNotMatch(html, /const bootBuild|no-store/);
});
