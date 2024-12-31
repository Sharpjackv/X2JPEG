const ws = new WebSocket(`wss://${window.location.host}/ws`);
ws.binaryType = 'arraybuffer';
const body = document.getElementById("body");
const pause = document.getElementById("pause");
const img = document.getElementById('stream');
const ctx = document.getElementById("canvastream").getContext('2d');
const canvimage = new Image();
const decodeworker = new Worker("worker.js");
const pausscreen = document.getElementById("pausescreen");
const menutab = document.getElementById("menutab")
const menu = document.getElementById("menu")
const FPSbutton = document.getElementById("FPSbutton")
const Qualitybutton = document.getElementById("Qualitybutton")
const FPSfield = document.getElementById("FPSfield")
const streamcontainer = document.getElementById("streamcontainer")

const altkeybind = 
{
    w : "up",
    a : "left",
    s : "down",
    d : "right",
    h : "z",
    u : "x",
    " " : "shiftleft",
    m : " ",
    q : "esc"
}

let drawbool = true
let menutoggle = false
let controlswap = false
let canvrender = false
var imagesReceived = 0;
var imagesdrawn = 0
var sizeofimagesrecived = 0;
let startTime = Date.now();
let domtime = 0
let domtimeavg = 0
let pasturl = ""

ws.onmessage = function(event) {
    if(drawbool == true){
        const arrayBuffer = event.data;
        decodeworker.postMessage(arrayBuffer);
    }
};

decodeworker.onmessage = function(event) {
    const arrayBuffer = event.data;
    const url = arrayBuffer[0];
    const size = arrayBuffer[1];

    if (canvrender){
        canvimage.onload = function() {
            ctx.drawImage(canvimage, 0, 0);
            URL.revokeObjectURL(url);
            imagesdrawn++;
        };
    
        canvimage.onerror = function() {
            URL.revokeObjectURL(url);
        };
    
        canvimage.src = url;
    }
    else {
        img.onload = function() {
            domtimeavg = domtimeavg + (Date.now() - domtime)
            imagesdrawn++;
        };
    
        img.onerror = function() {
            URL.revokeObjectURL(url);
        };
    
        requestAnimationFrame(() => {
            URL.revokeObjectURL(pasturl);
            img.src = url;
            pasturl = url
        });
        domtime = Date.now();
    }

    sizeofimagesrecived += size/1024
    imagesReceived++;

    // Update the images per second display
    const currentTime = Date.now();
    if (currentTime - startTime >= 1000) {
        document.getElementById('fps').innerText = `FPS: ${imagesReceived}`
        document.getElementById("bandwith").innerText = 'Bandwith: ' + Math.trunc(sizeofimagesrecived * 8) + " kbit/s"
        document.getElementById("avgimage").innerText = 'Average image size: ' + Math.trunc(size/1024) 
        document.getElementById("dfps").innerText = `Drawn FPS : ${imagesdrawn}`
        document.getElementById("domtime").innerText = `Average time to render to dom : ${Math.trunc(domtimeavg/imagesdrawn)}`
        sizeofimagesrecived = 0;
        imagesReceived = 0;
        imagesdrawn = 0;
        domtimeavg = 0;
        startTime = currentTime;
    }
}

ws.onopen = function() {
    console.log('WebSocket connection established');
};

ws.onclose = function() {
    console.log('WebSocket connection closed');
};

ws.onerror = function(error) {
    console.error('WebSocket error:', error);
};

Qualitybutton.onclick = function(){
    let quality = document.getElementById("Qualityfield").value
    ws.send("q " + quality)
}

document.getElementById("Controlbutton").onclick = function(){
    controlswap = !controlswap
}

document.getElementById("Canvasrendertoggle").onclick = function(){
    if(canvrender){
        canvrender = false
        document.getElementById("Canvasrendertoggle").innerText = "Canvas : off"
        document.getElementById("canvastream").style.display = "none"
    }
    else{
        canvrender = true
        document.getElementById("Canvasrendertoggle").innerText = "Canvas : on"
        document.getElementById("canvastream").style.display = "block"
    }
}

document.getElementById("Qualitytoggle").onclick = function(){
    if(document.getElementById("Qualitytoggle").innerText == "Compression : Off"){
        document.getElementById("Qualitytoggle").innerText = "Compression : On"
    }
    else{
        document.getElementById("Qualitytoggle").innerText = "Compression : Off"
    }
}

FPSbutton.onclick = function(){
    let fps = document.getElementById("FPSfield").value
    ws.send("f " + fps)
}

menutab.onclick = function(){
    if(!menutoggle){
        menutab.classList.remove("move-back")
        menutab.classList.add("move-right")

        menu.classList.remove("move-backbox")
        menu.classList.add("move-rightbox")

        menutoggle = true
    }
    else{
        menutoggle = false
        menutab.classList.remove("move-right")
        menutab.classList.add("move-back")

        menu.classList.add("move-backbox")
        menu.classList.remove("move-rightbox")
    }
}

pause.onclick = function(){
    if(pause.src.includes("pause.svg")){
        pause.src = "play.svg"
        drawbool = false
        pausscreen.style.visibility = "visible"
    }
    else{
        pause.src = "pause.svg"
        drawbool = true
        pausscreen.style.visibility = "hidden"
    }
}

pausscreen.onclick = function(){
    if(pausscreen.style.visibility == "visible"){
        pausscreen.style.visibility = "hidden"
        drawbool = true
        pause.src = "pause.svg"
    }
}

body.onkeydown = function(event){
    if(drawbool){
        if(pausscreen.style.visibility == "hidden"){
            if(document.fullscreenElement != null){
                event.preventDefault();
                if(!controlswap){
                    ws.send("2+1+" + event.code)
                }
                else {
                    ws.send("2+1+" + altkeybind[event.code])
                }
            }
        }
    }
}

body.onkeyup = function(event){
    if(drawbool){
        if(pausscreen.style.visibility == "hidden"){
            if(document.fullscreenElement != null){
                event.preventDefault();
                if(!controlswap){
                    ws.send("2+0+" + event.code)
                }
                else{
                    ws.send("2+0+" + altkeybind[event.code])
                }
            }
        }
    }
}

document.addEventListener("visibilitychange", () => {
    if (document.hidden) {
        if(drawbool){
            pause.src = "play.svg"
            drawbool = false
            pausscreen.style.visibility = "visible"
            document.title = "Paused"
        }
    }
    if (!document.hidden) {
        drawbool = true
        document.title = "hollowknightwebstream"
    }
});

document.addEventListener('fullscreenchange', async () => {
    try {
      if (document.fullscreenElement != null) {
        await navigator.keyboard.lock(["Escape","ControlLeft", "ControlRight", "Tab", "AltLeft", "AltRight"]);        
        console.log('Keyboard locked.');
      } else {
        pause.src = "play.svg"
        pausscreen.style.visibility = "visible"
      }
    } catch (error) {
      console.log(error);
    }
});

streamcontainer.onclick = function(){
    body.requestPointerLock();
    body.requestFullscreen();
}

document.addEventListener("mousemove", (event) => {
    if (document.pointerLockElement === body) {
        ws.send("1+" + event.movementX + "+" + event.movementY)
    }
});

document.addEventListener("mousedown", (event) => {
    if (document.pointerLockElement === body) {
        if(event.button == 2){
            ws.send("3+1+0")
        }
        if(event.button == 0){
            ws.send("3+1+1")
        }
    }
});

document.addEventListener("mouseup", (event) => {    
    if (document.pointerLockElement === body) {
        if(event.button == 2){
            ws.send("3+0+0")
        }
        if(event.button == 0){
            ws.send("3+0+1")
        }
    }
});
