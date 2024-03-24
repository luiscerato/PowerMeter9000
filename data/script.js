$(document).ready(function () {
    //Cargar nav y mostrar el cuadro de carga de IP si es necesario
    $("#navbar").load("nav.html #codeNavBar", function (status) {
        if (status == "error") {
            $("#navbar").text("Fallo al cargar la barra de navegación!");
        } else {
            if (editBoardURL) $("#navServerIP").removeClass("d-none");

            $("#inputBoardURL").val(boardIP);
            $("#btnReloadIP").click(function () {
                if (saveBoardURL($("#inputBoardURL").val())) location.reload();
            });

            let path = location.pathname.split("/");
            path = path[path.length-1].split(".")[0];
            path = "#nav" + path;
            $(path).addClass("active");
        }
    });

    $("#divFooter").load("nav.html #codeFooter", function (status) {
        if (status == "error") {
            $("#divFooter").text("Fallo al cargar la barar de estado!");
        }
        else {
            updateFooter("loading");
            board.getStateRepeat(
                (data) => updateFooter(data),
                () => updateFooter("fail"),
                500
            );
        }
    });

    function updateFooter(status) {
        const spinner = '<span class="spinner-border spinner-border-sm ml-2" role="status" aria-hidden="true"></span>';
        if (status == "loading" || status == "fail") {
            $("#statusWifi").html("Conectado a " + spinner + " | señal: " + spinner);
            $("#statusIP").html("IP: " + spinner);
            $("#statusHeap").html("Heap total: " + spinner + " libre: " + spinner);
        } else {
            let icon = iconWifiFull;
            if (status.rssi < -80) icon = iconWifi0;
            else if (status.rssi < -65) icon = iconWifi1;
            else if (status.rssi < -50) icon = iconWifi2;

            $("#statusWifi").html("Conectado a " + status.ssid + " " + icon);
            $("#statusIP").text("IP: " + status.ip);
            $("#statusHeap").text("Heap total: " + status.heap + " libre: " + status.free);
        }
    }

    initUI();
});


//Iconos SVG
const iconWifiOff =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#ff2825" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M12 18l.01 0" /><path d="M9.172 15.172a4 4 0 0 1 5.656 0" /><path d="M6.343 12.343a7.963 7.963 0 0 1 3.864 -2.14m4.163 .155a7.965 7.965 0 0 1 3.287 2" /><path d="M3.515 9.515a12 12 0 0 1 3.544 -2.455m3.101 -.92a12 12 0 0 1 10.325 3.374" /><path d="M3 3l18 18" /></svg>';
const iconWifi0 =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#ff2825" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M12 18l.01 0" /></svg>';
const iconWifi1 =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#ffbf00" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M12 18l.01 0" /><path d="M9.172 15.172a4 4 0 0 1 5.656 0" /></svg>';
const iconWifi2 =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#c9de00" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M12 18l.01 0" /><path d="M9.172 15.172a4 4 0 0 1 5.656 0" /><path d="M6.343 12.343a8 8 0 0 1 11.314 0" /></svg>';
const iconWifiFull =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#00e341" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M12 18l.01 0" /><path d="M9.172 15.172a4 4 0 0 1 5.656 0" /><path d="M6.343 12.343a8 8 0 0 1 11.314 0" /><path d="M3.515 9.515c4.686 -4.687 12.284 -4.687 17 0" /></svg>';

const iconReload =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1em" viewBox="0 0 512 512"><!--! Font Awesome Free 6.4.2 by @fontawesome - https://fontawesome.com License - https://fontawesome.com/license (Commercial License) Copyright 2023 Fonticons, Inc. --><path d="M105.1 202.6c7.7-21.8 20.2-42.3 37.8-59.8c62.5-62.5 163.8-62.5 226.3 0L386.3 160H336c-17.7 0-32 14.3-32 32s14.3 32 32 32H463.5c0 0 0 0 0 0h.4c17.7 0 32-14.3 32-32V64c0-17.7-14.3-32-32-32s-32 14.3-32 32v51.2L414.4 97.6c-87.5-87.5-229.3-87.5-316.8 0C73.2 122 55.6 150.7 44.8 181.4c-5.9 16.7 2.9 34.9 19.5 40.8s34.9-2.9 40.8-19.5zM39 289.3c-5 1.5-9.8 4.2-13.7 8.2c-4 4-6.7 8.8-8.1 14c-.3 1.2-.6 2.5-.8 3.8c-.3 1.7-.4 3.4-.4 5.1V448c0 17.7 14.3 32 32 32s32-14.3 32-32V396.9l17.6 17.5 0 0c87.5 87.4 229.3 87.4 316.7 0c24.4-24.4 42.1-53.1 52.9-83.7c5.9-16.7-2.9-34.9-19.5-40.8s-34.9 2.9-40.8 19.5c-7.7 21.8-20.2 42.3-37.8 59.8c-62.5 62.5-163.8 62.5-226.3 0l-.1-.1L125.6 352H176c17.7 0 32-14.3 32-32s-14.3-32-32-32H48.4c-1.6 0-3.2 .1-4.8 .3s-3.1 .5-4.6 1z"/></svg>';

const iconInfo =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#000000" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M10.425 1.414a3.33 3.33 0 0 1 3.026 -.097l.19 .097l6.775 3.995l.096 .063l.092 .077l.107 .075a3.224 3.224 0 0 1 1.266 2.188l.018 .202l.005 .204v7.284c0 1.106 -.57 2.129 -1.454 2.693l-.17 .1l-6.803 4.302c-.918 .504 -2.019 .535 -3.004 .068l-.196 -.1l-6.695 -4.237a3.225 3.225 0 0 1 -1.671 -2.619l-.007 -.207v-7.285c0 -1.106 .57 -2.128 1.476 -2.705l6.95 -4.098zm1.575 9.586h-1l-.117 .007a1 1 0 0 0 0 1.986l.117 .007v3l.007 .117a1 1 0 0 0 .876 .876l.117 .007h1l.117 -.007a1 1 0 0 0 .876 -.876l.007 -.117l-.007 -.117a1 1 0 0 0 -.764 -.857l-.112 -.02l-.117 -.006v-3l-.007 -.117a1 1 0 0 0 -.876 -.876l-.117 -.007zm.01 -3l-.127 .007a1 1 0 0 0 0 1.986l.117 .007l.127 -.007a1 1 0 0 0 0 -1.986l-.117 -.007z" stroke-width="0" fill="currentColor" /></svg>';

const iconAlert =
    '<svg xmlns="http://www.w3.org/2000/svg"  height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#000000" fill="none" stroke-linecap="round" stroke-linejoin="round">< path stroke = "none" d = "M0 0h24v24H0z" fill = "none" /> <path d="M14.897 1a4 4 0 0 1 2.664 1.016l.165 .156l4.1 4.1a4 4 0 0 1 1.168 2.605l.006 .227v5.794a4 4 0 0 1 -1.016 2.664l-.156 .165l-4.1 4.1a4 4 0 0 1 -2.603 1.168l-.227 .006h-5.795a3.999 3.999 0 0 1 -2.664 -1.017l-.165 -.156l-4.1 -4.1a4 4 0 0 1 -1.168 -2.604l-.006 -.227v-5.794a4 4 0 0 1 1.016 -2.664l.156 -.165l4.1 -4.1a4 4 0 0 1 2.605 -1.168l.227 -.006h5.793zm-2.887 14l-.127 .007a1 1 0 0 0 0 1.986l.117 .007l.127 -.007a1 1 0 0 0 0 -1.986l-.117 -.007zm-.01 -8a1 1 0 0 0 -.993 .883l-.007 .117v4l.007 .117a1 1 0 0 0 1.986 0l.007 -.117v-4l-.007 -.117a1 1 0 0 0 -.993 -.883z" stroke-width="0" fill="currentColor" /></svg> ';

const iconDone =
    '<svg xmlns="http://www.w3.org/2000/svg"  height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#000000" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/>  <path d="M17 3.34a10 10 0 1 1 -14.995 8.984l-.005 -.324l.005 -.324a10 10 0 0 1 14.995 -8.336zm-1.293 5.953a1 1 0 0 0 -1.32 -.083l-.094 .083l-3.293 3.292l-1.293 -1.292l-.094 -.083a1 1 0 0 0 -1.403 1.403l.083 .094l2 2l.094 .083a1 1 0 0 0 1.226 0l.094 -.083l4 -4l.083 -.094a1 1 0 0 0 -.083 -1.32z" stroke-width="0" fill="currentColor" /></svg>';

const iconX =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#000000" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/>  <path d="M17 3.34a10 10 0 1 1 -14.995 8.984l-.005 -.324l.005 -.324a10 10 0 0 1 14.995 -8.336zm-6.489 5.8a1 1 0 0 0 -1.218 1.567l1.292 1.293l-1.292 1.293l-.083 .094a1 1 0 0 0 1.497 1.32l1.293 -1.292l1.293 1.292l.094 .083a1 1 0 0 0 1.32 -1.497l-1.292 -1.293l1.292 -1.293l.083 -.094a1 1 0 0 0 -1.497 -1.32l-1.293 1.292l-1.293 -1.292l-.094 -.083z" stroke-width="0" fill="currentColor" /></svg>';

const iconMessage =
    '<svg xmlns="http://www.w3.org/2000/svg" height="1.5em" viewBox="0 0 24 24" stroke-width="2" stroke="#000000" fill="none" stroke-linecap="round" stroke-linejoin="round"><path stroke="none" d="M0 0h24v24H0z" fill="none"/><path d="M8 9h8" /><path d="M8 13h6" /><path d="M10.99 19.206l-2.99 1.794v-3h-2a3 3 0 0 1 -3 -3v-8a3 3 0 0 1 3 -3h12a3 3 0 0 1 3 3v6" /><path d="M15 19l2 2l4 -4" /></svg>';

function showToast(title, content, options) {
    if (typeof options !== "object") options = {};
    let body = $("<div></div>").addClass("toast-body");
    body.append(content);

    let header = $("<div></div>").addClass("toast-header");

    let icon = iconMessage;
    if (options.type == "ok") {
        header.addClass("bg-success text-white");
        body.addClass("text-success");
        icon = iconDone;
    } else if (options.type == "warning") {
        header.addClass("bg-warning text-white");
        body.addClass("text-warning");
        icon = iconAlert;
    } else if (options.type == "error") {
        header.addClass("bg-danger text-white");
        body.addClass("text-danger");
        icon = iconX;
    } else if (options.type == "info") {
        header.addClass("bg-info text-white");
        body.addClass("text-info");
        icon = iconInfo;
    }

    header.append(icon);
    header.append('<strong class="ms-2 me-auto">' + title + "</strong>");
    header.append('<small class="text-body-secondary">' + new Date().toLocaleTimeString() + "</small>");
    header.append('<button type="button" class="btn-close" data-bs-dismiss="toast" aria-label="Close"></button>');

    let toast = $('<div class="toast" role="alert" aria-live="assertive" aria-atomic="true"></div>');
    toast.append(header);
    toast.append(body);

    $("#toastContainer").append(toast);
    toast.toast("show");
    toast.on("hidden.bs.toast", () => {
        toast.remove();
    });

    return toast;
}
