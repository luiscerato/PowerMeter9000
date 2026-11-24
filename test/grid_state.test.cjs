const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const root = path.join(__dirname, '..');
const output = path.join(root, '.pio/build/esp32doit-devkit-v1/grid_state_test.cpp');
const implementation = fs.readFileSync(path.join(root, 'src/grid_state.h'), 'utf8')
    .replace('#pragma once', '').replace(/\binline\b/g, 'constexpr');
const harness = `
${implementation}
constexpr gridSettings config{};
// Alimenta phase con volts cada 10 ms entre start y end, ambos inclusive.
constexpr void feed(phaseState& phase, float volts, uint32_t start, uint32_t end) {
    for (uint32_t ms = start; ms <= end; ms += 10)
        updatePhaseState(phase, volts, ms, config.phases[0], config);
}
// Comprueba confirmacion, transiciones exclusivas e histeresis; sin parametros.
constexpr bool transitions() {
    phaseState phase;
    feed(phase, 220, 0, 250);
    if (phase.state != phaseCondition::normal) return false;
    feed(phase, 0, 260, 500);
    if (phase.state != phaseCondition::normal) return false;
    feed(phase, 0, 510, 510);
    if (phase.state != phaseCondition::phaseLoss || phase.changedAtMs != 260) return false;
    feed(phase, 24, 520, 1000);
    if (phase.state != phaseCondition::phaseLoss) return false;
    feed(phase, 100, 1010, 4700);
    if (phase.state != phaseCondition::phaseLoss) return false;
    feed(phase, 100, 4710, 4710);
    if (phase.state != phaseCondition::undervoltage || phase.changedAtMs != 1010) return false;
    feed(phase, 189, 4720, 5200);
    if (phase.state != phaseCondition::undervoltage) return false;
    feed(phase, 220, 5210, 5460);
    return phase.state == phaseCondition::normal;
}
// Verifica que un hueco o lectura invalida reinicie la confirmacion; sin parametros.
constexpr bool gaps() {
    phaseState phase;
    feed(phase, 0, 0, 200);
    updatePhaseState(phase, 0, 1000, config.phases[0], config);
    if (phase.state != phaseCondition::unknown) return false;
    updatePhaseState(phase, -1, 1010, config.phases[0], config);
    feed(phase, 0, 1020, 1260);
    return phase.state == phaseCondition::unknown;
}
// Verifica que pulsos cortos y distintas fases no compartan temporizadores; sin parametros.
constexpr bool shortPulses() {
    phaseState r, s;
    feed(r, 220, 0, 250);
    feed(s, 220, 0, 250);
    feed(r, 0, 260, 450);
    feed(r, 220, 460, 710);
    if (r.state != phaseCondition::normal || s.state != phaseCondition::normal) return false;
    feed(r, 180, 720, 1710);
    feed(r, 220, 1720, 1970);
    return r.state == phaseCondition::normal && s.state == phaseCondition::normal;
}
// Verifica confirmacion de sobretension y retorno tras wrap del contador; sin parametros.
constexpr bool highAndWrap() {
    phaseState phase;
    feed(phase, 260, 0, 5000);
    if (phase.state != phaseCondition::overvoltage) return false;
    feed(phase, 246, 5010, 5300);
    if (phase.state != phaseCondition::overvoltage) return false;
    phase.hasSample = false;
    for (uint32_t offset = 0; offset <= 250; offset += 10)
        updatePhaseState(phase, 220, uint32_t(0xffffff80U + offset), config.phases[0], config);
    return phase.state == phaseCondition::normal;
}
static_assert(transitions(), "Transiciones e histeresis");
static_assert(gaps(), "No confirmar muestras ausentes");
static_assert(shortPulses(), "No contaminar estados con transitorios");
// Rechaza bandas superpuestas y tiempos invalidos; sin parametros.
constexpr bool settingsValidation() {
    gridSettings values;
    if (!validGridSettings(values)) return false;
    values.phases[1].lowVoltage = 245;
    if (validGridSettings(values)) return false;
    values = gridSettings{};
    values.lossConfirmMs = 0;
    if (validGridSettings(values)) return false;
    values = gridSettings{};
    values.hysteresis = -1;
    if (validGridSettings(values)) return false;
    values = gridSettings{}; values.abnormalConfirmMs = 249;
    if (validGridSettings(values)) return false;
    values.abnormalConfirmMs = 1500;
    if (!validGridSettings(values)) return false;
    values.abnormalConfirmMs = 1501;
    return !validGridSettings(values);
}
static_assert(settingsValidation(), "Validacion de ajustes");
static_assert(highAndWrap(), "Sobretension y wrap");
static_assert(deriveGridState(phaseCondition::phaseLoss, phaseCondition::phaseLoss, phaseCondition::phaseLoss) == gridCondition::outage, "Corte total");
static_assert(deriveGridState(phaseCondition::normal, phaseCondition::phaseLoss, phaseCondition::phaseLoss) == gridCondition::abnormal, "Retorno parcial");
static_assert(deriveGridState(phaseCondition::normal, phaseCondition::normal, phaseCondition::normal) == gridCondition::normal, "Red normal");
static_assert(deriveGridState(phaseCondition::normal, phaseCondition::unknown, phaseCondition::normal) == gridCondition::unknown, "Arranque no inventa normalidad");
// Comprueba filtrado de anormalidad, corte inmediato, retorno y configuracion cero; sin parametros.
constexpr bool gridFilter() {
    gridStateFilter filter;
    if (!updateGridStateFilter(filter, gridCondition::normal, 0, 0, 1000)) return false;
    for (uint32_t t=10; t<1000; t+=10) {
        updateGridStateFilter(filter, gridCondition::abnormal, 10, t, 1000);
        if (filter.state != gridCondition::normal) return false;
    }
    if (!updateGridStateFilter(filter, gridCondition::outage, 1000, 1000, 1000) || filter.changedAtMs != 1000) return false;
    for (uint32_t t=1010; t<=2010; t+=10) updateGridStateFilter(filter, gridCondition::abnormal, 1010, t, 1000);
    if (filter.state != gridCondition::abnormal || filter.changedAtMs != 1010) return false;
    if (!updateGridStateFilter(filter, gridCondition::normal, 2020, 2020, 1000)) return false;
    return updateGridStateFilter(filter, gridCondition::abnormal, 2030, 2030, 0);
}
static_assert(gridFilter(), "Red anormal diferida y corte inmediato");
// Verifica que intervalos breves se descarten y que un hueco no confirme el candidato; sin parametros.
constexpr bool gridFilterTransient() {
    gridStateFilter filter;
    updateGridStateFilter(filter, gridCondition::normal, 0, 0, 1000);
    for (uint32_t t=10; t<=200; t+=10) updateGridStateFilter(filter, gridCondition::abnormal, 10, t, 1000);
    if (!updateGridStateFilter(filter, gridCondition::outage, 210, 210, 1000) || filter.state != gridCondition::outage) return false;
    for (uint32_t t=220; t<=600; t+=10) updateGridStateFilter(filter, gridCondition::abnormal, 220, t, 1000);
    if (!updateGridStateFilter(filter, gridCondition::normal, 610, 610, 1000)) return false;
    updateGridStateFilter(filter, gridCondition::abnormal, 620, 620, 1000);
    updateGridStateFilter(filter, gridCondition::abnormal, 620, 1200, 1000);
    if (filter.state != gridCondition::normal || filter.pendingSinceMs != 1200) return false;
    for (uint32_t t=1210; t<=2200; t+=10) updateGridStateFilter(filter, gridCondition::abnormal, 620, t, 1000);
    return filter.state == gridCondition::abnormal && filter.changedAtMs == 1200;
}
static_assert(gridFilterTransient(), "No registrar anormalidades breves ni confirmar huecos");
`;
fs.mkdirSync(path.dirname(output), {recursive: true});
fs.writeFileSync(output, harness);
const compiler = path.join(process.env.USERPROFILE, '.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-g++.exe');
execFileSync(compiler, ['-std=c++14', '-I', path.join(root, 'src'), '-c', output, '-o', output + '.o'], {stdio: 'inherit'});
console.log('PASS: estados de fase/red, tiempos, histeresis, muestras ausentes y wrap.');
