//Funciones generales
let boardURL = location; //IP de la placa a la que conectar
let editBoardURL = false; //Indica que se puede editar la IP porque está en modo localhost

if (location.hostname.startsWith("127.0") || location.hostname.startsWith("localhost")) {
    try {
        boardURL = new URL(localStorage.getItem("boardURL"));
    } catch (error) {
        console.log("Error al cargar la URL desde localStorage", error);
    }
    editBoardURL = true;
}

const boardIP = boardURL.protocol + "//" + boardURL.hostname; //IP del servidor
const boardTimeOut = isServerRemote(boardIP) ? 5000 : 500; //Timeout dependiendo del tipo de conexión (remota o directa)
const boardUpdateTime = 2500; //Tiempo en ms de actualización de lecturas de la placa

let board = {}; //Objeto con las funciones
board.IP = boardIP;
board.timeout = boardTimeOut;
board.updateTime = boardUpdateTime;

/*
	Guarda la dirección de la placa en el almacenamiento local
*/
function saveBoardURL(url) {
    if (!url.startsWith("http")) url = "http://" + url;
    try {
        let x = new URL(url);
    } catch (error) {
        alert("Error en la dirección introducida: " + error);
        return false;
    }
    localStorage.setItem("boardURL", url);
    return true;
}

//Devolver un websocket según el protocolo web de la url base
//Server es la direccion del socket
board.getWebSocketURL = function (server) {
    let socket = "ws://",
        protocol = "http://";
    try {
        server = new URL(server);
    } catch (error) {
        alert("Se ha producido un error al cargar la URL: " + error);
        return;
    }
    if (server.protocol === "https:" || server.protocol === "wss:") {
        socket = "wss://";
        protocol = "https://";
    }

    let url = socket + server.hostname;
    if (server.pathname !== "") url += server.pathname;
    else if (server.port !== "") url += ":" + server.port;
    return url;
};

board.ping = function (done, fail, timeout = boardTimeOut) {
    if (board.ping.running) {
        console.log("Ping en curso...");
        return;
    }
    $.ajax({
        url: board.IP + "/ping",
        timeout: board.timeout, // sets timeout to 3 seconds
        success: function (result) {
            board.ping.running = false;
            console.log("ping ok");
            if (done) done();
        },
        error: function (result) {
            board.ping.running = false;
            console.log("ping error");
            if (fail) fail();
        },
    });
    board.ping.running = true;
};

//Cargar la configuracion desde la placa. Si no se pasan funciones done y fail se ejecuta de manera síncrona
board.readConf = function (done = undefined, fail = undefined, async = true) {
    if (done == undefined && fail == undefined) async = false;

    let response;
    $.ajax({type: "GET", url: board.IP + "/conf", timeout: board.timeout, async: async})
        .done((result) => {
            board.conf = response = result;
            if (typeof done == "function") done(result);
        })
        .fail((result) => {
            response = result;
            if (typeof fail == "function") fail(result);
        });
    return response;
};

board.writeConf = function (conf, done = undefined, fail = undefined, async = true) {
    if (done == undefined && fail == undefined) async = false;

    let response;
    $.ajax({type: "POST", url: board.IP + "/conf", data: JSON.stringify(conf), timeout: board.timeout, async: async})
        .done((result) => {
            response = result;
            if (typeof done == "function") done(result);
        })
        .fail((result) => {
            response = result;
            if (typeof fail == "function") fail(result);
        });
    return response;
};

board.getState = function (done = undefined, fail = undefined, async = true) {
    if (done == undefined && fail == undefined) async = false;

    let response;
    $.ajax({type: "GET", url: board.IP + "/status", timeout: board.timeout, async: async})
        .done((result) => {
            response = result;
            if (typeof done == "function") done(result);
        })
        .fail((result) => {
            response = result;
            if (typeof fail == "function") fail(result);
        });
    return response;
};

board.getStateRepeat = function (done, fail, period) {
    if (period == undefined) period = board.updateTime;
    board.getState(done, fail);

    board.getStateRepeat.timer = setInterval(() => {
        board.getState(done, fail);
    }, period);
};


board.getRespone = function (request, done = undefined, fail = undefined, async = true) {
    if (done == undefined && fail == undefined) async = false;

    let response;
    $.ajax({type: "GET", url: board.IP + "/" + request, timeout: board.timeout, async: async})
        .done((result) => {
            response = result;
            if (typeof done == "function") done(result);
        })
        .fail((result) => {
            response = result;
            if (typeof fail == "function") fail(result);
        });
    return response;
};

board.getResponseRepeat = function (request, done, fail, period) {
    if (period == undefined) period = board.updateTime;
    board.getRespone(request, done, fail);

    timer = setInterval(() => {
        board.getRespone(request, done, fail);
    }, period);
    return timer;
};


board.getWifiList = function (done, fail = () => {}) {
    if (typeof done != "function" || typeof fail != "function") return;

    $.ajax({type: "GET", url: board.IP + "/wifi", timeout: board.timeout})
        .done((data) => {
            if (typeof data == "object" && data.scan != "completed") {
                board.getWifiList.timer = setTimeout(() => board.getWifiList(done, fail), board.updateTime);
            } else if (typeof data == "object" && data.scan == "completed") {
                done(data);
                board.wifiList = data.list;
            } else fail({status: 0, statusText: "Los datos recibidos no son los esperados\n" + data});
        })
        .fail((data) => {
            clearTimeout(board.getWifiList.timer);
            fail(data);
        });
};

board.getTime = function (done = undefined, fail = undefined, async = true) {
    if (done == undefined && fail == undefined) async = false;

    let response;
    $.ajax({type: "GET", url: board.IP + "/time", timeout: board.timeout, async: async})
        .done((result) => {
            response = result;
            if (typeof done == "function") done(result);
        })
        .fail((result) => {
            response = result;
            if (typeof fail == "function") fail(result);
        });
    return new Date(Number(response) * 1000);
};

board.setTime = function (time = Date.now(), done = undefined, fail = undefined, async = true) {
    if (done == undefined && fail == undefined) async = false;
    time = (new Date(time).getTime() / 1000).toString();

    let response;
    $.ajax({type: "POST", url: board.IP + "/time", timeout: board.timeout, async: async, data: time})
        .done((result) => {
            response = result;
            if (typeof done == "function") done(result);
        })
        .fail((result) => {
            response = result;
            if (typeof fail == "function") fail(result);
        });
    return new Date(Number(response) * 1000);
};

/*
	Indica si la dirección ingresada es local o remota.
	Se dice que es remota si no coincide con el formato de una IP (power.luiscerato.com.ar)
*/
function isServerRemote(server) {
    let url = new URL(server);
    if (url.hostname == "localhost") return true;
    if (url.protocol == "https:") return true;          // si es https es porque está detrás de algún
    return !ValidateIPaddress(url.hostname);
}

function dateToLocalString(date) {
    date = new Date(date);
    let local = new Date(date);
    local.setMinutes(date.getMinutes() - date.getTimezoneOffset());
    console.log(local.toISOString().slice(0, 19));
    return local.toISOString().slice(0, 19);
}

function dateHTMLToLocalEpoch(txtdate) {
    let date = new Date(txtdate.replace("T", " "));
    let local = new Date(date);
    local.setMinutes(date.getMinutes() - date.getTimezoneOffset());
    return Math.round(local.getTime() / 1000);
}

function ValidateIPaddress(ipaddress) {
    if (/^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$/.test(ipaddress)) {
        return true;
    }
    return false;
}
