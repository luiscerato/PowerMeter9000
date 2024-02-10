$(document).ready(function () {
    //Cargar nav y mostrar el cuadro de carga de IP si es necesario
    $("#navbar").load("nav.html #codeNavBar", function (status) {
        if (status == "error") {
            $("#navbar").text("Fallo al cargar la barar de navegación!");
        } else {
            $("#navInicio").addClass("active");
            if (editBoardURL) $("#navServerIP").removeClass("d-none");

            $("#inputBoardURL").val(boardIP);
            $("#btnReloadIP").click(function () {
                if (saveBoardURL($("#inputBoardURL").val())) location.reload();
            });
        }
    });

    $("#divFooter").load("nav.html #codeFooter", function (status) {
        if (status == "error") {
            $("#divFooter").text("Fallo al cargar la barar de estado!");
        }
    });

    initUI();
});

function initUI() {
    let ws = board.IP + "/ws";
    Scope.begin("chartContainer", board.getWebSocketURL(ws));

    updateUI();

    //Switchs de canales
    $("input[name='radioChkCanal']").change(function () {
        // Obtiene el valor del botón de opción de radio seleccionad
        let ch = $("input[name='radioChkCanal']:checked").val();
        onBtnsChannels(ch);
        Scope.setOptions(getSwitchsChannels());
        updateTrigger();
    });

    $(".corriente").change(function () {
        updateBtnsChannels();
        Scope.setOptions(getSwitchsChannels());
        updateTrigger();
    });

    $(".voltaje").change(function () {
        updateBtnsChannels();
        Scope.setOptions(getSwitchsChannels());
        updateTrigger();
    });

    //Escalas
    $("#rangeVolt").change(() => {
        let volts = parseInt($("#rangeVolt")[0].value);
        Scope.setOptions({voltDiv: volts});
        updateScales();
    });
    $("#rangeCorr").change(() => {
        let corriente = parseInt($("#rangeCorr")[0].value);
        Scope.setOptions({corrDiv: corriente});
        updateScales();
    });
    $("#rangeTiempo").change(() => {
        let tiempo = parseInt($("#rangeTiempo")[0].value);
        Scope.setOptions({timeDiv: tiempo});
        updateScales();
    });

    //Trigger
    $("#rangeTrigger").change(() => {
        let value = parseInt($("#rangeTrigger")[0].value);
        Scope.setOptions({triggerLevel: value});
        updateTrigger();
    });
    $("#selectTriggerCh").change(() => {
        let ch = $("#selectTriggerCh").val();
        Scope.setOptions({triggerChannel: ch});
        updateTrigger();
    });
    $("input[name='radioTriggerEdge']").change(function () {
        // Obtiene el valor del botón de opción de radio seleccionad
        let edge = $("input[name='radioTriggerEdge']:checked").val();
        Scope.setOptions({triggerEdge: edge});
    });

    $("#btnStartStop").click(() => {
        if (Scope.getState().state == "ready" || Scope.getState().state == "connecting") Scope.stop();
        else Scope.start();
    });

    let timer = setInterval(() => {
        let status = Scope.getState();
        if (status.state == "ready") $("#btnStartStop").text("Detener");
        else if (status.state == "connecting") $("#btnStartStop").text("Conectando");
        else $("#btnStartStop").text("Iniciar");

        if (status.state == "ready") $("#lblSpeed").text((Scope.bandWidth / 1024).toFixed(2) + "kb/s");
        else $("#lblSpeed").text("");
    }, 500);
}

//Actualizar todos los elementos de la UI según el estado del  osciloscopio
//  Botones grupo de canales
//  Switch de canales individuales
//  Escalas
//  Trigger
function updateUI() {
    updateScales();
    updateSwitchsChannels();
    updateBtnsChannels();
    updateTrigger();
}

function onBtnsChannels(ch) {
    if (typeof ch != "string") return;
    ch = ch.toLowerCase();
    let corrR = ch === "corriente" || ch === "todos" || ch == "fase-r";
    let corrS = ch === "corriente" || ch === "todos" || ch == "fase-s";
    let corrT = ch === "corriente" || ch === "todos" || ch == "fase-t";
    let corrN = ch === "corriente" || ch === "todos";
    let voltR = ch === "voltaje" || ch === "todos" || ch == "fase-r";
    let voltS = ch === "voltaje" || ch === "todos" || ch == "fase-s";
    let voltT = ch === "voltaje" || ch === "todos" || ch == "fase-t";

    $("#switchCorrienteR").prop("checked", corrR);
    $("#switchCorrienteS").prop("checked", corrS);
    $("#switchCorrienteT").prop("checked", corrT);
    $("#switchCorrienteN").prop("checked", corrN);
    $("#switchVoltR").prop("checked", voltR);
    $("#switchVoltS").prop("checked", voltS);
    $("#switchVoltT").prop("checked", voltT);
}

function updateBtnsChannels() {
    let sg = getSwitchsChannels();
    let sel = "";

    if ((sg.corrR && sg.corrS && sg.corrT && sg.corrN && sg.voltR && sg.voltS && sg.voltT) === true) sel = "todos";
    else if ((sg.corrR && sg.corrS && sg.corrT && sg.corrN) === true && (sg.voltR || sg.voltS || sg.voltT) === false) sel = "corriente";
    else if ((sg.corrR || sg.corrS || sg.corrT || sg.corrN) === false && (sg.voltR && sg.voltS && sg.voltT) === true) sel = "voltaje";
    else if ((sg.corrR && sg.voltR) === true && (sg.corrS || sg.voltS || sg.corrT || sg.voltT || sg.corrN) === false) sel = "fase-r";
    else if ((sg.corrS && sg.voltS) === true && (sg.corrR || sg.voltR || sg.corrT || sg.voltT || sg.corrN) === false) sel = "fase-s";
    else if ((sg.corrT && sg.voltT) === true && (sg.corrR || sg.voltR || sg.corrS || sg.voltS || sg.corrN) === false) sel = "fase-t";

    if (sel != "") $("input[name='radioChkCanal'][value='" + sel + "']").prop("checked", true);
    else $("input[name='radioChkCanal']").prop("checked", false);
}

function getSwitchsChannels() {
    let channels = {};
    channels.corrR = $("#switchCorrienteR").prop("checked");
    channels.corrS = $("#switchCorrienteS").prop("checked");
    channels.corrT = $("#switchCorrienteT").prop("checked");
    channels.corrN = $("#switchCorrienteN").prop("checked");
    channels.voltR = $("#switchVoltR").prop("checked");
    channels.voltS = $("#switchVoltS").prop("checked");
    channels.voltT = $("#switchVoltT").prop("checked");

    return channels;
}

function updateSwitchsChannels() {
    let channels = Scope.getChannelsInfo();
    let ids = {ir: "CorrienteR", is: "CorrienteS", it: "CorrienteT", in: "CorrienteN", vr: "VoltR", vs: "VoltS", vt: "VoltT"};

    channels.forEach((ch) => {
        let name = "#switch" + ids[ch.id];
        $(name).prop("checked", ch.visible);
    });
}

function updateScales() {
    let scales = Scope.getDivScales();
    $("#rangeVolt").val(scales.Voltage.step);
    $("#lblVoltDiv").text(scales.Voltage.value + " V/div");
    $("#rangeCorr").val(scales.Current.step);
    $("#lblCorrDiv").text(scales.Current.value + " A/div");
    $("#rangeTiempo").val(scales.Time.step);
    $("#lblTimeDiv").text(scales.Time.value + " ms/div");
}

function updateTrigger() {
    let info = Scope.getTriggerInfo();

    //Cargar los canales visibles y seleccionar el que se está usando como trigger
    $("#selectTriggerCh").empty();
    Scope.Data.waves.forEach((val, index) => {
        if (val.visible) $("#selectTriggerCh").append(`<option value="${index}">${val.name}</option>`);
    });
    $("#selectTriggerCh").val(info.Channel.toString());

    $("#rangeTrigger").val(info.Value);
    $("#rangeTrigger").prop("min", info.minValue);
    $("#rangeTrigger").prop("max", info.maxValue);
    $("#lblTrigger").text(`${info.Value.toFixed(3)}${info.unit}`);

    $("#radioTriggerEdge").val(info.Edge);
}
