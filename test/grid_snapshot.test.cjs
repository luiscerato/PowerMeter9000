const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.join(__dirname, '..');
const source = fs.readFileSync(path.join(root, 'data/grid_events_ui.js'), 'utf8');
const context = vm.createContext({Uint8Array, DataView});
vm.runInContext(source, context);

// Genera tres paginas validas; sin parametros, con contexto distinguible para verificar offsets.
function fixture() {
    const bytes = Buffer.alloc(6144);
    bytes[0] = 4; bytes.writeUInt32LE(136, 1); bytes[13] = 32; bytes[27] = 3;
    for (let index = 0; index < 33; ++index) bytes.writeFloatLE(index + 0.25, 78 + index * 4);
    for (let index = 0; index < 3; ++index) bytes.writeDoubleLE(10000000000.125 + index, 210 + index * 8);
    bytes.writeUInt16LE(context.gridCrc16(bytes, 30, 204), 234);
    bytes.writeUInt16LE(context.gridCrc16(bytes, 0, 28), 28);
    for (let page = 1; page < 3; ++page) {
        const offset = page * 2048;
        bytes.writeUInt32LE(136, offset); bytes[offset + 4] = page; bytes[offset + 5] = 3;
        bytes.writeUInt16LE(context.gridCrc16(bytes, offset, 154, offset + 8), offset + 8);
    }
    return bytes;
}

// Decodifica bytes sin incluir memoria ajena del Buffer de Node.
function decode(bytes) {
    return context.decodeGridEvent(bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.length));
}

const bytes = fixture(), event = decode(bytes), snapshot = event.snapshot;
for (const [key, first, length] of [['voltage', 0, 4], ['current', 4, 4], ['activePower', 8, 3],
    ['reactivePower', 11, 3], ['apparentPower', 14, 3], ['powerFactor', 17, 3], ['totalPower', 20, 3],
    ['frequency', 23, 1], ['activeEnergy', 24, 3], ['reactiveEnergy', 27, 3], ['apparentEnergy', 30, 3]]) {
    assert.deepEqual(Array.from(snapshot[key]), Array.from({length}, (_, index) => first + index + 0.25));
}
assert.equal(snapshot.totalEnergy[2], 10000000002.125);
const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
view.setFloat32(78, NaN, true); view.setFloat64(210, Infinity, true);
assert.equal(context.decodeGridSnapshot(view).voltage[0], null);
assert.equal(context.decodeGridSnapshot(view).totalEnergy[0], null);
assert.equal(decode(bytes).snapshot, null);
assert.equal(decode(bytes).invalidPages, 1);

// Comprueba el panel real en escritorio y movil; sin parametros, sin acceder al medidor.
async function run() {
    const runtime = path.join(process.env.USERPROFILE, '.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
    const {chromium} = require(require.resolve('playwright', {paths: [root, runtime]}));
    const browser = await chromium.launch({headless: true, channel: 'msedge'});
    try {
        for (const width of [1280, 390]) {
            const page = await browser.newPage({viewport: {width, height: 900}});
            await page.setContent('<html lang="es"><head><meta name="viewport" content="width=device-width, initial-scale=1"></head><body></body></html>');
            await page.addStyleTag({path: path.join(root, 'data/lib/bootstrap.min.css')});
            await page.addScriptTag({path: path.join(root, 'data/lib/jquery-3.6.4.min.js')});
            await page.addScriptTag({content: source});
            await page.evaluate(({html, snapshot}) => {
                const doc = new DOMParser().parseFromString(html, 'text/html');
                document.body.append(doc.getElementById('gridEventContextDetails'));
                renderGridSnapshot(snapshot);
            }, {html: fs.readFileSync(path.join(root, 'data/index.html'), 'utf8'), snapshot});
            assert.equal(await page.locator('details').evaluate(element => element.open), false);
            await page.locator('summary').click();
            assert.equal(await page.locator('table:visible').count(), 3);
            assert.match(await page.locator('#gridEventContext').innerText(), /23,250 Hz/);
            assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), true);
            await page.screenshot({path: path.join(root, 'tmp/grid-context-' + width + '.png'), fullPage: true});
            await page.locator('summary').click();
            assert.equal(await page.locator('table:visible').count(), 0);
            await page.evaluate(() => renderGridSnapshot(null));
            assert.match(await page.locator('#gridEventContext').textContent(), /CRC/);
            await page.close();
        }
    } finally { await browser.close(); }
    console.log('PASS: snapshot offsets, doubles, valores no validos, CRC y panel colapsable desktop/mobile.');
}
run().catch(error => { console.error(error); process.exitCode = 1; });
