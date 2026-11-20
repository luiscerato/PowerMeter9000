const assert = require('node:assert/strict');
const fs = require('node:fs');
const document = fs.readFileSync('ENDPOINTS.md', 'utf8');
const paths = new Map();
for (const file of ['src/webserver.cpp', 'src/web/events_http.cpp', 'src/web/webfs_http.cpp']) {
    const source = fs.readFileSync(file, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/^\s*\/\/.*$/gm, '');
    for (const match of source.matchAll(/server\.on\("([^"]+)",\s*((?:HTTP_[A-Z]+\s*\|?\s*)+)/g)) {
        assert.ok(document.includes('`' + match[1] + '`'), 'Ruta no documentada: ' + match[1]);
        const methods = match[2].match(/HTTP_[A-Z]+/g).map(method => method.slice(5));
        paths.set(match[1], new Set([...(paths.get(match[1]) || []), ...methods]));
    }
}
const pairs = [...paths.values()].reduce((count, methods) => count + methods.size, 0);
assert.ok(document.includes('**' + paths.size + ' rutas HTTP explicitas y ' + pairs + ' combinaciones metodo/ruta**'));
const scope = fs.readFileSync('src/scope_stream.cpp', 'utf8');
for (const command of ['scope_alive', 'scope_get', 'scope_view,', 'quality_set,', 'scope_fft_on,']) {
    assert.ok(scope.includes('"' + command));
    assert.ok(document.includes(command), 'Comando no documentado: ' + command);
}
console.log('PASS: las ' + paths.size + ' rutas HTTP explicitas y ' + pairs + ' combinaciones metodo/ruta figuran en ENDPOINTS.md; comandos WS incluidos.');
