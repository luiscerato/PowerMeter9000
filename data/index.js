let tableRows = [
    {name: "Voltaje", unit: "vrms", visible: true, decimals: 2, source: "meter"},
    {name: "Voltaje Fases", unit: "vvrms", visible: true, decimals: 2, source: "meter"},
    {name: "Corriente", unit: "irms", visible: true, decimals: 3, source: "meter"},
    {name: "Potencia", unit: "watt", visible: true, decimals: 1, source: "meter"},
    {name: "Potencia Reactiva", unit: "var", visible: true, decimals: 1, source: "meter"},
    {name: "Potencia Aparente", unit: "va", visible: true, decimals: 1, source: "meter"},
    {name: "Frecuencia", unit: "freq", visible: true, decimals: 2, source: "meter"},
    {name: "Facrtor de potencia", unit: "fp", visible: true, decimals: 2, source: "meter"},
    {name: "Energía activa", unit: "wattH", visible: true, decimals: 2, source: "energia"},
    {name: "Energía ractiva", unit: "varH", visible: true, decimals: 2, source: "energia"},
    {name: "Energía aparente", unit: "vaH", visible: true, decimals: 2, source: "energia"},
    {name: "Desfasaje fases", unit: "degv", visible: true, decimals: 2, source: "angulos"},
    {name: "Desfasaje corriente", unit: "degvi", visible: true, decimals: 2, source: "angulos"},
    {name: "Desfasaje fases corriente", unit: "degi", visible: true, decimals: 2, source: "angulos"},
    {name: "Distorción Voltaje", unit: "thdV", visible: true, decimals: 2, source: "thd"},
    {name: "Distorción Corriente", unit: "thdI", visible: true, decimals: 2, source: "thd"},
];

let decimalPlaces = {};

let tableHeaders = [
    {name: "Variable", short: "", visible: true},
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

    board.getResponseRepeat("meter?angles", angleOk, angleFail, "slow");

    board.getResponseRepeat("meter?thd", thdOk, thdFail, "slow");


    $.getScript("scope.js")
        .done(function () {
            $.getScript("lib/canvasjs.min.js")
                .done(function () {
                    console.log("Script canvasjs cargado!");
                    Scope.begin("chartContainer", null, "event");
                })
                .fail(function () {
                    console.error("Error al cargar el script de eventos");
                });
        })
        .fail(function () {
            console.error("Error al cargar el script de eventos");
        });

        //Cargar parte de la interfase que está en scope.html
    // $("#eventData").load("scope.html #scopeUI");


    $(window).on("resize", function () {
        let width = $(window).width();
        let height = $(window).height();
        let size = 200;

        if (width > height) {
            //landscape
            size = width / 4.5;
            if (size > height / 1.5) size = height / 1.5;
        } else {
            size = height / 2.5;
            if (size > width / 1.5) size = width / 1.5;
        }

        size = Math.round(size / 25.0);
        size *= 25;
        if (size < 180) size = 180;
        if (size > 320) size = 320;

        console.log(width, height, size);
    });

    // Escuchar el evento en los botones de los tabs
    $('button[data-bs-toggle="tab"]').on("shown.bs.tab", function (e) {
        // Obtener el ID del tab panel objetivo (data-bs-target)
        const tabTarget = $(e.target).attr("data-bs-target");

        // Verificar si es el tab "Events"
        if (tabTarget !== "#events") return;

        meterLoadEventsList();
    });
}

function meterLoadEventsList() {
    board.loadNodeFileList(function (res) {
        console.log("Lista de archivos cargada!");
        let events = [];
        res.forEach((path) => {
            //Path: /share/PowerMeterLogs/ACevent_1970-7-21_13-29-36.csv
            let data = path.replaceAll("/share/PowerMeterLogs/", "");
            let fase = "",
                type = "",
                time,
                format = "csv";
            if (data.endsWith(".json")) {
                data = data.split(".");
                time = data[0];
                fase = data[1];
                type = data[2];
                format = "json";
            } else if (data.endsWith(".csv")) {
                data = data.replaceAll("ACevent_", "");
                time = data.split(".")[0];
            }
            else 
                return; //No es un archivo de evento válido ( se rompía con archivos que no eran .json o .csv)

            let [datePart, timePart] = time.split(".")[0].split("_");
            let [year, month, day] = datePart.split("-").map(Number); // "7" → 7
            let [hours, minutes, seconds] = timePart.split("-").map(Number);
            const date = new Date(year, month - 1, day, hours, minutes, seconds);

            events.push({path: path, date: date, phase: fase, type: type});
        });
        //Ordenar por fecha
        events.sort((a, b) => b.date - a.date);
        //Hacer lista ordenada por fecha y con el nombre del evento

        let list = "";
        events.forEach((event) => {
            detail = event.phase.length > 0 ? " | " + event.phase : "";
            if (detail.length > 0 && event.type.length > 0) detail += " | ";
            detail += event.type;
            list += '<a href="' + event.path + '" class="list-group-item">' + event.date.toLocaleString() + detail + "</a>";
        });

        $("#eventList").html("<ul class='list-group'>" + list + "</ul>");

        $("#eventList").on("click", ".list-group-item", function (e) {
            e.preventDefault();
            const path = $(this).attr("href"); // Si el href tenía la ruta completa
            board.loadNodeFile(
                path,
                function (res) {
                    // $("#eventData").html("");
                    // let lines = res.split("\n");
                    // lines.forEach((line) => {
                    //     $("#eventData").append(line + "<br>");
                    // });
                    // $("#eventData").text(res);
                    // console.log(res);
                    let event = meterParseEventFile(res);
                    Scope.loadEventData(event);
                },
                function (res) {
                    $("#eventData").text("Fallo al leer el archivo!");
                }
            );
        });
    });
}

function meterParseEventFile(data) {
    data = JSON.parse(data);
    let resp = {};
    resp.eventInfo = data.eventInfo;
    resp.waves = {t: [], vr: [], vs: [], vt: [], ir: [], is: [], it: [], in: []};

    data.datos.forEach((s) => {
        resp.waves.t.push(s[0]);
        resp.waves.vr.push(s[1]);
        resp.waves.vs.push(s[2]);
        resp.waves.vt.push(s[3]);
        resp.waves.ir.push(s[4]);
        resp.waves.is.push(s[5]);
        resp.waves.it.push(s[6]);
        resp.waves.in.push(s[7]);
    });
    return resp;
}

function meterOk(resp) {
    if (meterOk.fail) $(".meter").removeClass("notLoading");
    meterOk.fail = false;

    //Actualizar los gauge
    $("#gaugeVoltaje").simpleGauge("setValue", resp.vrms.avg);
    $("#gaugeVoltajeF").simpleGauge("setValue", resp.vvrms.avg);
    $("#gaugeCorriente").simpleGauge("setValue", resp.irms.avg);
    $("#gaugePotencia").simpleGauge("setValue", (resp.watt.avg / 1000).toFixed(1));

    // //Llenar las celdas de voltaje
    $(".vvrmsR").text(formatNumber(resp.vvrms.r, "V"));
    $(".vvrmsS").text(formatNumber(resp.vvrms.s, "V"));
    $(".vvrmsT").text(formatNumber(resp.vvrms.t, "V"));
    $(".vvrmsAvg").text(formatNumber(resp.vvrms.avg, "V"));

    $(".vrmsR").text(formatNumber(resp.vrms.r, "V"));
    $(".vrmsS").text(formatNumber(resp.vrms.s, "V"));
    $(".vrmsT").text(formatNumber(resp.vrms.t, "V"));
    $(".vrmsN").text(formatNumber(resp.vrms.n, "V"));
    $(".vrmsAvg").text(formatNumber(resp.vrms.avg, "V"));

    // //Llenar las celdas de corriente
    $(".irmsR").text(formatNumber(resp.irms.r, "A"));
    $(".irmsS").text(formatNumber(resp.irms.s, "A"));
    $(".irmsT").text(formatNumber(resp.irms.t, "A"));
    $(".irmsN").text(formatNumber(resp.irms.n, "A"));
    $(".irmsAvg").text(formatNumber(resp.irms.avg, "A"));

    // //Potencia activa
    $(".wattR").text(formatNumber(resp.watt.r, "W"));
    $(".wattS").text(formatNumber(resp.watt.s, "W"));
    $(".wattT").text(formatNumber(resp.watt.t, "W"));
    $(".wattAvg").text(formatNumber(resp.watt.avg, "W"));
    $(".wattTot").text(formatNumber(resp.watt.tot, "W"));

    // //Potencia reactiva
    $(".varR").text(formatNumber(resp.var.r, "VAR"));
    $(".varS").text(formatNumber(resp.var.s, "VAR"));
    $(".varT").text(formatNumber(resp.var.t, "VAR"));
    $(".varAvg").text(formatNumber(resp.var.avg, "VAR"));
    $(".varTot").text(formatNumber(resp.var.tot, "VAR"));

    // //Potencia aparente
    $(".vaR").text(formatNumber(resp.va.r, "VA"));
    $(".vaS").text(formatNumber(resp.va.s, "VA"));
    $(".vaT").text(formatNumber(resp.va.t, "VA"));
    $(".vaAvg").text(formatNumber(resp.va.avg, "VA"));
    $(".vaTot").text(formatNumber(resp.va.tot, "VA"));

    $(".freqR").text(formatNumber(resp.freq, "Hz"));
    $(".freqS").text(formatNumber(resp.freq, "Hz"));
    $(".freqT").text(formatNumber(resp.freq, "Hz"));
    $(".freqAvg").text(formatNumber(resp.freq, "Hz"));
}

function meterFail(response) {
    $(".meter").addClass("notLoading");
    meterOk.fail = true;
}

function energyOk(resp) {
    if (energyOk.fail) $(".energia").removeClass("notLoading");
    energyOk.fail = false;

    // //Energía activa
    $(".wattHR").text(formatNumber(resp.wattH.r / 1000.0, "kWh"));
    $(".wattHS").text(formatNumber(resp.wattH.s / 1000.0, "kWh"));
    $(".wattHT").text(formatNumber(resp.wattH.t / 1000.0, "kWh"));
    $(".wattHTot").text(formatNumber(resp.wattH.tot / 1000.0, "kWh"));

    // //Energía reactiva
    $(".varHR").text(formatNumber(resp.varH.r / 1000.0, "kVARh"));
    $(".varHS").text(formatNumber(resp.varH.s / 1000.0, "kVARh"));
    $(".varHT").text(formatNumber(resp.varH.t / 1000.0, "kVARh"));
    $(".varHTot").text(formatNumber(resp.varH.tot / 1000.0, "kVARh"));

    // //Energía aparente
    $(".vaHR").text(formatNumber(resp.vaH.r / 1000.0, "kVAh"));
    $(".vaHS").text(formatNumber(resp.vaH.s / 1000.0, "kVAh"));
    $(".vaHT").text(formatNumber(resp.vaH.t / 1000.0, "kVAh"));
    $(".vaHTot").text(formatNumber(resp.vaH.tot / 1000.0, "kVAh"));
}

function energyFail(response) {
    energyOk.fail = true;
    $(".energia").addClass("notLoading");
}

function angleOk(resp) {
    // //Ángulos entre fases de voltaje
    if (angleOk.fail) $(".angulos").removeClass("notLoading");
    angleOk.fail = false;

    $(".degvR").text(formatNumber(resp.voltage.r, "°"));
    $(".degvS").text(formatNumber(resp.voltage.s, "°"));
    $(".degvT").text(formatNumber(resp.voltage.t, "°"));

    // //Ángulos entre fases de voltaje y fases de corriente
    $(".degviR").text(formatNumber(resp.vi.r, "°"));
    $(".degviS").text(formatNumber(resp.vi.s, "°"));
    $(".degviT").text(formatNumber(resp.vi.t, "°"));

    // //Ángulos entre fases de corriente
    $(".degiR").text(formatNumber(resp.current.r, "°"));
    $(".degiS").text(formatNumber(resp.current.s, "°"));
    $(".degiT").text(formatNumber(resp.current.t, "°"));
}

function angleFail(response) {
    $(".angulos").addClass("notLoading");
    angleOk.fail = true;
}

function thdOk(resp) {
    if (thdOk.fail) $(".thd").removeClass("notLoading");
    thdOk.fail = false;

    $(".thdVR").text(formatNumber(resp.voltageTHD.r, "%"));
    $(".thdVS").text(formatNumber(resp.voltageTHD.s, "%"));
    $(".thdVT").text(formatNumber(resp.voltageTHD.t, "%"));
    $(".thdVAvg").text(formatNumber((resp.voltageTHD.r + resp.voltageTHD.s + resp.voltageTHD.t) / 3.0, "%"));

    $(".thdIR").text(formatNumber(resp.currentTHD.r, "%"));
    $(".thdIS").text(formatNumber(resp.currentTHD.s, "%"));
    $(".thdIT").text(formatNumber(resp.currentTHD.t, "%"));
    $(".thdIAvg").text(formatNumber((resp.currentTHD.r + resp.currentTHD.s + resp.currentTHD.t) / 3.0, "%"));
}

function thdFail(response) {
    $(".thd").addClass("notLoading");
    thdOk.fail = true;
}

function formatNumber(value, unit) {
    let places = 2;
    if (unit == "V") places = decimalPlaces.vrms;
    else if (unit == "A") places = decimalPlaces.irms;
    else if (unit == "W") places = decimalPlaces.watt;
    else if (unit == "VAR") places = decimalPlaces.var;
    else if (unit == "VA") places = decimalPlaces.va;
    else if (unit == "kWh") places = 3;
    else if (unit == "kVARh") places = 3;
    else if (unit == "kVAh") places = 3;
    else if (unit == "Wh") places = decimalPlaces.wattH;
    else if (unit == "°") places = decimalPlaces.degv;
    else if (unit == "°") places = decimalPlaces.degvi;
    else if (unit == "°") places = decimalPlaces.degi;

    return value.toFixed(places) + " " + unit;
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
    tableRows.forEach((row) => {
        decimalPlaces[row.unit] = row.decimals;
    });

    //Crear tabla
    let header = '<thead class="table"><tr class="valueTableHead">';
    let rows = [];
    let row;

    tableHeaders.forEach((headerItem, index) => {
        if (headerItem.visible) header += '<th scope="col" class="valueTableRow">' + headerItem.name + "</th>";
    });
    header += " </tr></thead>";

    tableRows.forEach((rowItem, index) => {
        if (rowItem.visible) {
            row = "<tr>";
            tableHeaders.forEach((headerItem, indexHead) => {
                if (rowItem.visible) {
                    let id = rowItem.unit + headerItem.short;
                    if (indexHead == 0) row += `<td scope="row">${rowItem.name}</td>`;
                    else row += `<td id="pt_${id}" class="valueTable ${rowItem.source} ${id}"></td>`;
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
