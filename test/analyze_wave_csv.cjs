const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const csvPath = process.argv[2];
if (!csvPath) throw new Error('Indicar el CSV Wave a analizar');
// Import-Csv resuelve el formato CSV; csvPath es una ruta literal, nunca codigo PowerShell.
const csvJson = execFileSync('powershell.exe', ['-NoProfile', '-Command',
    `Import-Csv -LiteralPath '${csvPath.replaceAll("'", "''")}' | ConvertTo-Json -Compress`],
    {encoding: 'utf8', maxBuffer: 16 * 1024 * 1024});
const rows = JSON.parse(csvJson);
const channels = ['irA', 'vrV', 'isA', 'vsV', 'itA', 'vtV', 'inA'];
const scales = [0.1, 0.25, 0.1, 0.25, 0.1, 0.25, 0.1];
const values = rows.map(row => channels.map(channel => Number(row[channel])));
const tracePath = process.argv[3] || path.join(__dirname,
    '../.pio/build/esp32doit-devkit-v1/wave-diagnostics-boot-123d-1.json');
const trace = JSON.parse(fs.readFileSync(tracePath, 'utf8'));

// Reproduce el empaquetado de produccion; samples contiene siete magnitudes fisicas por fila.
function packSamples(samples) {
    const words = samples.flatMap(row => row.map((v, c) => {
        const q = Math.round(v / scales[c]);
        if (!Number.isFinite(v) || q < -2048 || q > 2047 || Math.abs(q * scales[c] - v) > 1e-6)
            throw new Error(`Valor fuera de int12/escala: canal ${channels[c]}, ${v}`);
        return q & 4095;
    }));
    const bytes = Buffer.alloc(words.length * 3 / 2);
    for (let i = 0, j = 0; i < words.length; i += 2, j += 3) {
        bytes[j] = words[i] & 255;
        bytes[j + 1] = (words[i] >> 8) | ((words[i + 1] & 15) << 4);
        bytes[j + 2] = words[i + 1] >> 4;
    }
    return bytes;
}

// Calcula el mismo FNV1a del diagnostico de meter; bytes es un fragmento comprimido.
function hashBytes(bytes) {
    let hash = 2166136261;
    for (const byte of bytes) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
    return hash;
}

const parts = [];
for (let start = 0; start + 128 <= rows.length; start += 128) {
    const bytes = packSamples(values.slice(start, start + 128));
    const hash = hashBytes(bytes);
    const duplicates = parts.filter(p => p.bytes.equals(bytes)).map(p => p.start);
    const matches = trace.fragments.filter(f => f.partHashFnv1a === hash)
        .map(f => ({eventId: f.eventId, block: f.block, part: f.part, beforePage: f.beforePage, afterPage: f.afterPage}));
    parts.push({start, hash, bytes, duplicates, matches});
}
const jumps = channels.map((channel, c) => {
    const steps = values.slice(1).map((row, n) => ({sample: n + 1, from: values[n][c], to: row[c],
        delta: Number((row[c] - values[n][c]).toFixed(4))}));
    steps.sort((a, b) => Math.abs(b.delta) - Math.abs(a.delta));
    return {channel, largest: steps.slice(0, 6)};
});
const frameBoundaries = [];
for (let sample = 768; sample < values.length; sample += 768) {
    frameBoundaries.push({sample, voltages: [1, 3, 5].map(c => ({channel: channels[c],
        from: values[sample - 1][c], to: values[sample][c],
        delta: Number((values[sample][c] - values[sample - 1][c]).toFixed(4))}))});
}
const voltageCycles = [1, 3, 5].map(c => {
    const crossings = [];
    for (let i = 1; i < values.length; i++) {
        if (values[i - 1][c] < 0 && values[i][c] >= 0)
            crossings.push(i - 1 - values[i - 1][c] / (values[i][c] - values[i - 1][c]));
    }
    const periods = crossings.slice(1).map((v, i) => v - crossings[i]);
    return {channel: channels[c], cycleCount: periods.length,
        minPeriodSamples: Math.min(...periods), maxPeriodSamples: Math.max(...periods)};
});
console.log(JSON.stringify({sampleCount: rows.length, firstTime: rows[0]?.time, lastTime: rows.at(-1)?.time,
    nonSequentialIndices: rows.flatMap((r, i) => Number(r.sampleIndex) !== i ? [i] : []),
    parts: parts.map(({bytes, ...p}) => p), jumps, frameBoundaries, voltageCycles}, null, 2));
