let tableHeader = [
    {name: "Fase", unit: "", visible: true, decimals: 2},
    {name: "Voltaje", unit: "vrms", visible: true, decimals: 2},
    {name: "Corriente", unit: "irms", visible: true, decimals: 3},
    {name: "Potencia", unit: "watt", visible: true, decimals: 1},
    {name: "Potencia Reactiva", unit: "var", visible: true, decimals: 1},
    {name: "Potencia Aparente", unit: "va", visible: true, decimals: 1},
    {name: "Ang fases", unit: "degv", visible: true, decimals: 2},
    {name: "Ang vi", unit: "degvi", visible: true, decimals: 2},
    {name: "Frecuencia", unit: "freq", visible: true, decimals: 2},
    {name: "Energía", unit: "wattH", visible: true, decimals: 2},
];

let decimalPlaces = {};

tableRows = [
    {name: "Fase R", short: "R", visible: true},
    {name: "Fase S", short: "S", visible: true},
    {name: "Fase T", short: "T", visible: true},
    {name: "Neutro", short: "N", visible: true},
    {name: "Total", short: "Tot", visible: true},
    {name: "Promedio", short: "Avg", visible: true},
];

function initUI() {
    $("#gaugeVoltaje").simpleGauge(getGaugeOptions("Voltaje"));
    $("#gaugeVoltajeF").simpleGauge(getGaugeOptions("Voltaje Fases"));
    $("#gaugeCorriente").simpleGauge(getGaugeOptions("Corriente"));
    $("#gaugePotencia").simpleGauge(getGaugeOptions("Potencia"));

    createTable();

    board.getResponseRepeat("meter", meterOk, meterFail, "fast");

    board.getResponseRepeat("meter?energy", energyOk, energyFail, "slow");

    board.getResponseRepeat("meter?angles", angleOk, angleFail);
}

function getGaugeOptions(signal) {
    let options = {
        min: 0,
        max: 100,
        value: 0,
        type: "analog digital",
        container: {
            scale: 95, // scale of gauge, in percent
            style: "background: #ddd; background: linear-gradient(335deg, #ddd 0%, #fff 30%, #fff 50%, #bbb 100%); border-radius: 1000px; border: 8px solid #bbb;",
        },
        title: {
            text: signal,
            style: "color: #555; font-size: 20px; padding: 5px;",
        },
        digital: {
            text: "{value.2} V", // value with number of decimals
            style: "color: auto; font-size: 30px; z-index=10; ",
        },
        analog: {
            minAngle: -150,
            maxAngle: 150,
        },
        labels: {
            text: "{value}",
            count: 10,
            scale: 72,
            style: "font-size: 20px;",
        },
        ticks: {
            count: 14,
            scale1: 84,
            scale2: 93,
            style: "width: 2px; color: #999;",
        },
        subTicks: {
            count: 5,
            scale1: 93,
            scale2: 96,
            style: "width: 1px; color: #bbb;",
        },
        bars: {
            scale1: 88,
            scale2: 94,
        },
        pointer: {
            scale: 95,
            style: "color: #000000; opacity: 0.8; filter: drop-shadow(-3px 3px 2px rgba(0, 0, 0, .7));",
        },
    };

    options.title.text = "";

    if (signal.toLowerCase() == "voltaje") {
        options.digital.text = "{value.1} V";
        options.min = 180;
        options.max = 260;
        options.labels.count = 8;
        options.ticks.count = 8;
        options.bars.colors = [
            [180, "#dd2222", 0, 0],
            [190, "#ffa500", 0, 0],
            [200, "#378618", 0, 0],
            [240, "#ffa500", 0, 0],
            [250, "#dd2222", 0, 0],
        ];
    } else if (signal.toLowerCase() == "voltaje fases") {
        options.digital.text = "{value.1} V";
        options.min = 340;
        options.max = 420;
        options.labels.count = 8;
        options.ticks.count = 16;
        options.bars.colors = [
            [340, "#dd2222", 0, 0],
            [350, "#ffa500", 0, 0],
            [360, "#378618", 0, 0],
            [400, "#ffa500", 0, 0],
            [410, "#dd2222", 0, 0],
        ];
    } else if (signal.toLowerCase() == "corriente") {
        options.digital.text = "{value.3} A";
        options.min = 0;
        options.max = 100;
        options.labels.count = 10;
        options.bars.colors = [
            [0, "#378618", 0, 0],
            [70, "#ffa500", 0, 0],
            [85, "#dd2222", 0, 0],
        ];
    } else if (signal.toLowerCase() == "potencia") {
        options.digital.text = "{value.3} kW";
        options.min = 0;
        options.max = 20;
        options.labels.count = 10;
        options.bars.colors = [
            [0, "#378618", 0, 0],
            [14, "#ffa500", 0, 0],
            [17, "#dd2222", 0, 0],
        ];
    }

    return options;
}

function createTable() {
    decimalPlaces = {}; //Crear una tabla que contenga la cantidad de lugrares decimales por variable
    tableHeader.forEach((element) => {
        decimalPlaces[element.unit] = element.decimals;
    });

    //Crear tabla
    let header = '<thead class="table"><tr class="valueTableHead">';
    let rows = [],
        row;
    tableHeader.forEach((element, index) => {
        if (element.visible) header += '<th scope="col" class="valueTableRow">' + element.name + "</th>";
    });
    header += " </tr></thead>";

    tableRows.forEach((element, index) => {
        if (element.visible) {
            row = "<tr>";
            tableHeader.forEach((head, indexHead) => {
                if (element.visible) {
                    let id = head.unit + element.short;
                    if (indexHead == 0) row += `<th scope="row">${element.name}</th>`;
                    else row += `<td id="${id}" class="valueTable"></td>`;
                }
            });
            row += "</tr>";
            rows.push(row);
        }
    });

    let body = '<tbody class="table-group-divider">';
    rows.forEach((element) => {
        body += element;
    });
    body += "</tbody>";
    $("#powerTable").html('<table class="table table-bordered">' + header + body + "</table>");
}

function meterOk(resp) {
    $("#gaugeVoltaje").simpleGauge("setValue", resp.vrms.avg);
    $("#gaugeVoltajeF").simpleGauge("setValue", resp.vvrms.avg);
    $("#gaugeCorriente").simpleGauge("setValue", resp.irms.avg);
    $("#gaugePotencia").simpleGauge("setValue", resp.watt.avg);

    $("#cellVR").text(formatNumber(resp.vrms.r, "V"));
    $("#cellVS").text(formatNumber(resp.vrms.s, "V"));
    $("#cellVT").text(formatNumber(resp.vrms.t, "V"));
    $("#cellVavg").text(formatNumber(resp.vrms.avg, "V"));
    $("#cellVRS").text(formatNumber(resp.vvrms.r, "V"));
    $("#cellVST").text(formatNumber(resp.vvrms.s, "V"));
    $("#cellVTR").text(formatNumber(resp.vvrms.t, "V"));
    $("#cellVVavg").text(formatNumber(resp.vvrms.avg, "V"));

    $("#cellIR").text(formatNumber(resp.irms.r, "A"));
    $("#cellIS").text(formatNumber(resp.irms.s, "A"));
    $("#cellIT").text(formatNumber(resp.irms.t, "A"));
    $("#cellIN").text(formatNumber(resp.irms.n, "A"));

    $("#vrmsR").text(formatNumber(resp.vrms.r, "V"));
    $("#vrmsS").text(formatNumber(resp.vrms.s, "V"));
    $("#vrmsT").text(formatNumber(resp.vrms.t, "V"));
    $("#vrmsN").text(formatNumber(resp.vrms.n, "V"));
    $("#vrmsAvg").text(formatNumber(resp.vrms.avg, "V"));

    $("#irmsR").text(formatNumber(resp.irms.r, "A"));
    $("#irmsS").text(formatNumber(resp.irms.s, "A"));
    $("#irmsT").text(formatNumber(resp.irms.t, "A"));
    $("#irmsN").text(formatNumber(resp.irms.n, "A"));

    $("#wattR").text(formatNumber(resp.watt.r, "W"));
    $("#wattS").text(formatNumber(resp.watt.s, "W"));
    $("#wattT").text(formatNumber(resp.watt.t, "W"));
    $("#wattAvg").text(formatNumber(resp.watt.avg, "W"));
    $("#wattTot").text(formatNumber(resp.watt.tot, "W"));

    $("#varR").text(formatNumber(resp.var.r, "VAR"));
    $("#varS").text(formatNumber(resp.var.s, "VAR"));
    $("#varT").text(formatNumber(resp.var.t, "VAR"));
    $("#varAvg").text(formatNumber(resp.var.avg, "VAR"));
    $("#varTot").text(formatNumber(resp.var.tot, "VAR"));

    $("#vaR").text(formatNumber(resp.va.r, "VA"));
    $("#vaS").text(formatNumber(resp.va.s, "VA"));
    $("#vaT").text(formatNumber(resp.va.t, "VA"));
    $("#vaAvg").text(formatNumber(resp.va.avg, "VA"));
    $("#vaTot").text(formatNumber(resp.va.tot, "VA"));

    $("#freqR").text(formatNumber(resp.freq, "Hz"));
    $("#freqS").text(formatNumber(resp.freq, "Hz"));
    $("#freqT").text(formatNumber(resp.freq, "Hz"));
    $("#freqAvg").text(formatNumber(resp.freq, "Hz"));
}

function meterFail(response) {}

function energyOk(response) {
    // let resp = JSON.parse(response); //bug
    resp = response;

    $("#wattHR").text(formatNumber(resp.wattH.r, "Wh"));
    $("#wattHS").text(formatNumber(resp.wattH.s, "Wh"));
    $("#wattHT").text(formatNumber(resp.wattH.t, "Wh"));
    $("#wattHTot").text(formatNumber(resp.wattH.tot, "Wh"));

    $("#varHR").text(formatNumber(resp.varH.r, "VARh"));
    $("#varHS").text(formatNumber(resp.varH.s, "VARh"));
    $("#varHT").text(formatNumber(resp.varH.t, "VARh"));
    $("#varHTot").text(formatNumber(resp.varH.tot, "VARh"));

    $("#vaHR").text(formatNumber(resp.vaH.r, "VAh"));
    $("#vaHS").text(formatNumber(resp.vaH.s, "VAh"));
    $("#vaHT").text(formatNumber(resp.vaH.t, "VAh"));
    $("#vaHTot").text(formatNumber(resp.vaH.tot, "VAh"));
}

function energyFail(response) {}

function angleOk(response) {
    // let resp = JSON.parse(response); //bug
    resp = response;

    $("#degvR").text(formatNumber(resp.voltage.r, "°"));
    $("#degvS").text(formatNumber(resp.voltage.s, "°"));
    $("#degvT").text(formatNumber(resp.voltage.t, "°"));

    $("#degviR").text(formatNumber(resp.vi.r, "°"));
    $("#degviS").text(formatNumber(resp.vi.s, "°"));
    $("#degviT").text(formatNumber(resp.vi.t, "°"));
}

function angleFail(response) {}

function formatNumber(value, unit) {
    let places = 2;
    if (unit == "V") places = decimalPlaces.vrms;
    else if (unit == "A") places = decimalPlaces.irms;
    else if (unit == "W") places = decimalPlaces.watt;
    else if (unit == "VAR") places = decimalPlaces.var;
    else if (unit == "VA") places = decimalPlaces.va;
    else if (unit == "Wh") places = decimalPlaces.wattH;
    else if (unit == "°") places = decimalPlaces.degv;
    else if (unit == "°") places = decimalPlaces.degvi;

    return value.toFixed(places) + " " + unit;
}
