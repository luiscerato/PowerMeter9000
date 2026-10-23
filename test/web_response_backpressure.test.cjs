const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const output = path.resolve('.pio/build/web-response-patch-test');
const target = path.join(output, 'test/ESPAsyncWebServer/src/WebResponses.cpp');
const allocation = '    uint8_t *buf = (uint8_t *)malloc(outLen + headLen);';
const source = `before\n${allocation}\nafter\n`;
fs.mkdirSync(path.dirname(target), {recursive: true});
fs.writeFileSync(target, source);
const python = path.join(process.env.USERPROFILE, '.esphome-venv/Scripts/python.exe');
const script = `import runpy,sys
class Env(dict):
    # Resuelve el directorio de dependencias recibido por argv.
    def subst(self, value): return sys.argv[1]
runpy.run_path("tools/patch_async_webserver.py", init_globals={"Import": lambda name: None, "env": Env(PIOENV="test")})
`;
execFileSync(python, ['-c', script, output], {stdio: 'inherit'});
const patched = fs.readFileSync(target, 'utf8');
assert.equal(patched.replace(/\r\n/g, '\n'), source.replace(allocation, '    if (!outLen) {\n      return 0;\n    }\n\n' + allocation));
execFileSync(python, ['-c', script, output], {stdio: 'inherit'});
assert.equal(fs.readFileSync(target, 'utf8'), patched, 'Patch is idempotent');
fs.writeFileSync(target, 'a newer implementation without the expected allocation');
assert.throws(() => execFileSync(python, ['-c', script, output], {stdio: 'pipe'}), 'Unknown versions must fail visibly');
console.log('PASS: salida TCP llena espera antes de malloc, parche idempotente y cambio de version detectado.');
