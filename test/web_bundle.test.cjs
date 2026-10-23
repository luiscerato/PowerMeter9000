const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const zlib = require('node:zlib');
const image = fs.readFileSync('.pio/build/webfs.fs32');

// Extrae un recurso de la imagen construida; name es la ruta relativa dentro de WebFS.
function readAsset(name) {
    const count=image.readUInt16LE(12),table=image.readUInt32LE(16),size=image.readUInt16LE(6);
    for(let i=0;i<count;++i) {
        const at=table+i*size,path=image.subarray(at,at+96).toString().split('\0')[0];
        if(path!==name&&path!=='/'+name)continue;
        const offset=image.readUInt32LE(at+96),length=image.readUInt32LE(at+100);
        const bytes=image.subarray(offset,offset+length);
        return (image[at+118]===1?zlib.gunzipSync(bytes):bytes).toString('utf8');
    }
    throw Error('Falta recurso '+name);
}

const source=fs.readFileSync('data/index.html','utf8'),html=readAsset('index.html');
const paths=JSON.parse(source.match(/const bootScripts = (\[[^;]+\]);/)[1]);
const bundled=JSON.parse(html.match(/const bootScripts = (\[[^;]+\]);/)[1]);
assert.deepEqual(bundled,[...paths.filter(path=>path.startsWith('lib/')),'app.js']);
const expected=paths.filter(path=>!path.startsWith('lib/')).map(path=>';\n'+fs.readFileSync('data/'+path,'utf8')).join('\n');
assert.equal(readAsset('app.js'),expected);
new vm.Script(expected,{filename:'app.js'});
assert.equal(readAsset('index.js'),fs.readFileSync('data/index.js','utf8'));
console.log('PASS: bundle ordenado, JavaScript valido y fuentes individuales conservados.');
