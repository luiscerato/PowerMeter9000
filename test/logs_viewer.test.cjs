const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const logsSource = fs.readFileSync(path.join(__dirname, "../data/logs_ui.js"), "utf8");
const html = fs.readFileSync(path.join(__dirname, "../data/index.html"), "utf8");
const css = fs.readFileSync(path.join(__dirname, "../data/style.css"), "utf8");

// Verifica que el visor pagina el NAND y ofrece exportación, colores y configuración por módulo.
test("visor de logs usa lecturas pequeñas y controles de diagnóstico", () => {
    assert.match(logsSource, /"\/debug\/nand\?"/);
    assert.match(logsSource, /requestRecentLogs\(\{before: logsViewer\.ramFirstSequence, count: 16\}/);
    assert.match(logsSource, /Math\.min\(16, logsViewer\.maxRows - logsViewer\.items\.length\)/);
    assert.match(logsSource, /delayLogRequest\(300\)/);
    assert.doesNotMatch(logsSource, /fetchLogJson\(board\.IP \+ "\/debug\/binary/);
    assert.match(logsSource, /URL\.createObjectURL/);
    assert.match(logsSource, /"\/debug\/config"/);
    assert.match(html, /id="logsPreviousBoot"/);
    assert.match(html, /id="logsExport"/);
    assert.doesNotMatch(html, /id="logsNand"|id="logsStats"/);
    for (const level of ["err", "wrn", "inf", "vrb", "dbg"])
        assert.match(css, new RegExp("\\.logLine--" + level));
});
