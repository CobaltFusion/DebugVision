const WS_URL = "ws://localhost:8765";
const canvas = document.getElementById("scope");
const ctx = canvas.getContext("2d");


canvas.width = window.innerWidth;
canvas.height = window.innerHeight;


window.addEventListener("resize", () => {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
});


// ---- Data model ----------------------------------------------------


const CHANNEL_HEIGHT = 30;
const TIME_WINDOW_MS = 10_000; // 10 seconds


const channels = new Map();
let events = [];


function getChannelIndex(name) {
    if (!channels.has(name)) {
        channels.set(name, channels.size);
    }
    return channels.get(name);
}


// ---- WebSocket -----------------------------------------------------


const ws = new WebSocket(WS_URL);


ws.onopen = () => console.log("Connected to server");


ws.onmessage = (e) => {
    const msg = JSON.parse(e.data);


    if (msg.type === "event") {
        events.push(msg);
    }
};


// ---- Rendering -----------------------------------------------------


function render() {
    const now = Date.now();


    // Drop old events
    events = events.filter(e => e.ts >= now - TIME_WINDOW_MS);


    ctx.clearRect(0, 0, canvas.width, canvas.height);


    for (const e of events) {
        const chIndex = getChannelIndex(e.channel);


        const x = canvas.width - ((now - e.ts) / TIME_WINDOW_MS) * canvas.width;
        const y = chIndex * CHANNEL_HEIGHT;


        ctx.fillStyle = typeof e.value === "number" && e.value === 0
            ? "#444"
            : "#0f0";


        ctx.fillRect(x, y + 5, 4, CHANNEL_HEIGHT - 10);


        ctx.fillStyle = "#aaa";
        ctx.fillText(e.channel, 5, y + 20);
    }


    requestAnimationFrame(render);
}


render();
