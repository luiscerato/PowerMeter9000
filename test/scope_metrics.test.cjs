const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");

const elements = Object.fromEntries(["scopeMetricsRows", "scopeMetricsTotal", "scopeMetricsWindow", "scopeMetricsStatus"]
    .map((id) => [id, {innerHTML: "", textContent: ""}]));
const context = vm.createContext({console, document: {getElementById: (id) => elements[id]}});
vm.runInContext(fs.readFileSync("data/scope.js", "utf8") + "\nglobalThis.scope = Scope;", context);

const scope = context.scope;
const sampleCount = 480;
const phaseShift = Math.acos(300 / 460);
scope.Data.waves.forEach((wave) => { wave.visible = true; });
scope.Data.waves[1].visible = false;
scope.Data.waves[1].data = Array.from({length: sampleCount}, (_, index) =>
    325 * Math.sin(2 * Math.PI * 50 * index * 125 / 1000000));
scope.Data.waves[0].data = Array.from({length: sampleCount}, (_, index) =>
    2 * Math.SQRT2 * Math.sin(2 * Math.PI * 50 * index * 125 / 1000000 - phaseShift));
scope.Data.waves[3].data = scope.Data.waves[1].data;
scope.Data.waves[5].data = scope.Data.waves[1].data;
scope.Data.waves[2].data = scope.Data.waves[0].data;
scope.Data.waves[4].data = scope.Data.waves[0].data;
scope.Data.waves[6].data = Array(sampleCount).fill(0.5);
scope.updateMetrics({points: sampleCount, periodUs: 125, clipMask: 0});

assert.equal((elements.scopeMetricsRows.innerHTML.match(/<tr/g) || []).length, 4);
assert.match(elements.scopeMetricsRows.innerHTML, /scopeMetricTrace isHidden">V/);
assert.match(elements.scopeMetricsRows.innerHTML, /scopeMetricTrace isVisible">I/);
assert.match(elements.scopeMetricsRows.innerHTML, /229\.\d V/);
assert.match(elements.scopeMetricsRows.innerHTML, /2\.000 A/);
assert.match(elements.scopeMetricsRows.innerHTML, /299\.\d W/);
assert.match(elements.scopeMetricsRows.innerHTML, /50\.00 Hz/);
assert.match(elements.scopeMetricsRows.innerHTML, /scopeMetricTrace isUnavailable">V/);
assert.match(elements.scopeMetricsRows.innerHTML, /Sin canal de tensión/);
scope.Data.waves[2].data = scope.Data.waves[3].data = scope.Data.waves[4].data = scope.Data.waves[5].data = [];
scope.updateMetrics({points: sampleCount, periodUs: 125, clipMask: 0});
assert.match(elements.scopeMetricsTotal.textContent, /Potencia activa disponible: 299\.\d W/, 'sumar solo pares V/I disponibles');
console.log("PASS: tabla compacta muestra trazas visibles y métricas escaladas por fase.");
