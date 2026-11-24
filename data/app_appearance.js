const appAppearanceDefaults = {R: '#804000', S: '#000000', T: '#e02020', N: '#38bdf8', opacity: 70};
let appAppearance = loadAppAppearance();

// Lee los colores locales validados; sin parametros, usa valores iniciales si no hay almacenamiento.
function loadAppAppearance() {
    try { return validateAppAppearance(JSON.parse(localStorage.getItem('powerMeterAppearance'))) || {...appAppearanceDefaults}; }
    catch (_) { return {...appAppearanceDefaults}; }
}

// Valida value como cuatro colores RGB y una opacidad porcentual; devuelve una copia o null.
function validateAppAppearance(value) {
    if (!value || !['R','S','T','N'].every(phase => /^#[0-9a-f]{6}$/i.test(value[phase]))) return null;
    if (!Number.isInteger(value.opacity) || value.opacity < 10 || value.opacity > 100) return null;
    return Object.fromEntries(Object.keys(appAppearanceDefaults).map(key => [key,value[key]]));
}

// Obtiene el color de phase; variant aclara de 0 a 1 para distinguir magnitudes de la misma fase.
function appPhaseColor(phase, variant = 0) {
    const hex = appAppearance[String(phase).toUpperCase()] || '#607078';
    const rgb = [1,3,5].map(offset => Math.round(parseInt(hex.slice(offset,offset+2),16) * (1-variant) + 255*variant));
    return `rgba(${rgb.join(',')},${appAppearance.opacity/100})`;
}

// Crea los selectores de color de la aplicacion; sin parametros, no modifica ajustes del medidor.
function createAppearanceSettingsHtml() {
    return '<section class="settingsSection mb-3"><div class="settingsSectionHeader settingsCompactHeader"><div><h2>Colores de fases</h2><p>Preferencias de este navegador para todas las graficas.</p></div><div class="settingsActions"><button id="appAppearanceReload" type="button" class="btn btn-sm btn-outline-secondary" title="Recargar colores" aria-label="Recargar colores">&#8635;</button><button type="submit" form="appAppearanceForm" class="btn btn-sm btn-primary" title="Guardar colores" aria-label="Guardar colores"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h12l4 4v12a2 2 0 0 1-2 2ZM17 21v-8H7v8M7 3v5h8"/></svg></button></div></div>' +
        '<form id="appAppearanceForm" class="appAppearanceFields">' + ['R','S','T','N'].map(phase => `<label>${phase === 'N' ? 'Neutro' : 'Fase '+phase}<input id="appColor${phase}" type="color" required aria-label="Color fase ${phase}"></label>`).join('') +
        '<label>Opacidad<input id="appColorOpacity" type="number" min="10" max="100" step="1" required> %</label></form><div id="appAppearanceStatus" class="small" role="status"></div></section>' +
        '<section class="settingsSection mb-3"><div class="settingsSectionHeader settingsCompactHeader"><div><h2>Unidades del histórico</h2><p>Preferencia compartida en este medidor; no modifica los datos guardados.</p></div></div><form id="appHistoryUnitsForm" class="appAppearanceFields"><label>Potencia y energía <select id="appHistoryUnits" class="form-select form-select-sm"><option value="0">W / Wh</option><option value="1">kW / kWh</option></select></label><button type="submit" class="btn btn-sm btn-primary">Guardar</button></form><div id="appHistoryUnitsStatus" class="small" role="status"></div></section>';
}

// Completa el formulario con los ajustes guardados; sin parametros.
function fillAppAppearanceSettings() {
    appAppearance = loadAppAppearance();
    for (const phase of ['R','S','T','N']) { const input = document.getElementById('appColor'+phase); if (input) input.value = appAppearance[phase]; }
    const opacity = document.getElementById('appColorOpacity'); if (opacity) opacity.value = appAppearance.opacity;
    const units = document.getElementById('appHistoryUnits'); if (units && typeof historyKiloUnits !== 'undefined') units.value = historyKiloUnits ? '1' : '0';
}

// Actualiza colores de las graficas creadas sin consultas de red; sin parametros.
function refreshAppAppearance() {
    const charts = [];
    for (const phase of ['R','S','T','N']) document.documentElement.style.setProperty('--phase'+phase,appPhaseColor(phase));
    if (typeof Scope !== 'undefined') {
        for (const wave of Scope.Data?.waves || []) wave.color = appPhaseColor(wave.id.slice(1),wave.id[0] === 'i' ? .22 : 0);
        if (Scope.chart) charts.push(Scope.chart);
    }
    if (typeof ScopeFft !== 'undefined' && ScopeFft.chart) charts.push(ScopeFft.chart);
    if (typeof historyViewer !== 'undefined') charts.push(...Object.values(historyViewer.charts));
    if (typeof gridEventsViewer !== 'undefined') charts.push(gridEventsViewer.voltageChart,gridEventsViewer.currentChart);
    if (typeof gridWaveViewer !== 'undefined') charts.push(gridWaveViewer.chart);
    if (typeof eventHistoryPreview !== 'undefined') charts.push(...eventHistoryPreview.charts);
    for (const chart of charts.filter(Boolean)) {
        for (const series of chart.options.data || []) {
            const phase = series.appPhase || (/\b([RSTN])(?: RMS)?$/.exec(series.name || '') || [])[1] ||
                (/^I([RSTN])$/.exec(series.name || '') || [])[1] || (/neutro/i.test(series.name || '') ? 'N' : null);
            if (phase) series.color = appPhaseColor(phase,series.appVariant ?? (series.signalKind === 'current' || series.signalUnit === 'A' ? .22 : 0));
        }
        chart.render();
        if (typeof historyViewer !== 'undefined' && Object.values(historyViewer.charts).includes(chart)) alignHistoryPlot(chart);
    }
}

// Conecta guardar/recargar de preferencias locales; sin parametros, aplica colores iniciales al osciloscopio.
function initAppAppearance() {
    fillAppAppearanceSettings(); refreshAppAppearance();
    document.getElementById('appAppearanceReload')?.addEventListener('click', () => {fillAppAppearanceSettings(); refreshAppAppearance();});
    document.getElementById('appHistoryUnitsForm')?.addEventListener('submit', async event => {
        event.preventDefault();
        const status = document.getElementById('appHistoryUnitsStatus'), kilo = document.getElementById('appHistoryUnits').value === '1';
        status.textContent = 'Guardando...';
        try { await writeOptions('web', {kilo: kilo ? 1 : 0}); setSharedHistoryUnits(kilo); status.textContent = 'Guardado en el medidor.'; }
        catch (_) { status.textContent = 'No se pudo guardar la unidad en el medidor.'; }
    });
    document.getElementById('appAppearanceForm')?.addEventListener('submit', event => {
        event.preventDefault();
        const value = Object.fromEntries(['R','S','T','N'].map(phase => [phase,document.getElementById('appColor'+phase).value]));
        value.opacity = Number(document.getElementById('appColorOpacity').value);
        const status = document.getElementById('appAppearanceStatus'), valid = validateAppAppearance(value);
        if (!valid) {status.textContent = 'Revise los colores y la opacidad (10 a 100%).'; return;}
        try {localStorage.setItem('powerMeterAppearance',JSON.stringify(valid)); appAppearance = valid; refreshAppAppearance(); status.textContent = 'Colores guardados en este navegador.';}
        catch (_) {status.textContent = 'No se pudieron guardar los colores en este navegador.';}
    });
}
