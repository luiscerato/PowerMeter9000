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

// tableRows = ["Fase R", "Fase S", "Fase T", "Neutro", "Total", "Promedio"];
tableRows = [
    {name: "Fase R", short: "R", visible: true},
    {name: "Fase S", short: "S", visible: true},
    {name: "Fase T", short: "T", visible: true},
    {name: "Neutro", short: "N", visible: true},
    {name: "Total", short: "Tot", visible: true},
    {name: "Promedio", short: "Avg", visible: true},
];


function initUI() {
    
$.fn.gauge = function(opts) {
    this.each(function() {
      var $this = $(this),
          data = $this.data();
  
      if (data.gauge) {
        data.gauge.stop();
        delete data.gauge;
      }
      if (opts !== false) {
        data.gauge = new Gauge(this).setOptions(opts);
      }
    });
    return this;
  };
  

    $("#gauge1").gauge();

    createTable();


    board.getResponseRepeat("meter", meterOk, meterFail, "fast");

    board.getResponseRepeat("meter?energy", energyOk, energyFail, "slow");

    board.getResponseRepeat("meter?angles", angleOk, angleFail);
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

function meterOk(response) {
    // let resp = JSON.parse(response); //bug
    resp = response;

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
