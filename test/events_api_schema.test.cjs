const assert=require('node:assert/strict');
const fs=require('node:fs');
const vm=require('node:vm');
const source=fs.readFileSync('src/web/events_http.cpp','utf8');
const fields=source.slice(source.indexOf('const configField fields[]'),source.indexOf('#undef GRID'));
const keys=[...fields.matchAll(/\{"([a-zA-Z.]+)"/g)].map(match=>match[1]);
const context={};vm.createContext(context);vm.runInContext(fs.readFileSync('data/index.js','utf8'),context);
const mapped=vm.runInContext('[...Object.values(eventGridFields),...Object.values(eventPointFields)]',context);
const expected=[...mapped,'wave.pre','wave.on','wave.dip','wave.swell','wave.oc'].sort();
assert.deepEqual(keys.sort(),expected);
assert.equal(new Set(keys).size,27);
assert.match(source,/char buffer\[1024\]/);assert.match(source,/char buffer\[768\]/);
assert.doesNotMatch(source,/Preferences|putBytes|\.reserve\(|new\s+\w/);
for(const error of ['duplicate_field','unknown_field','invalid_config','rev_conflict','save_busy','invalid_since','unknown_group'])
    assert.ok(source.includes('"'+error+'"'),error);
const settings=fs.readFileSync('src/event_settings.cpp','utf8');
assert.equal((settings.match(/\.putBytes\(/g)||[]).length,1);
assert.ok(settings.indexOf('const bool saved')<settings.indexOf('meterQueueEventSettings'));
console.log('PASS: 27 campos firmware/web coinciden, JSON acotado, validaciones y una unica escritura NVS.');
