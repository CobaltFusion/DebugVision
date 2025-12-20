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

// Time window (now adjustable)
let TIME_WINDOW_MS = 10_000;

// Limits for zoom
const MIN_TIME_WINDOW_MS = 100;      // 100 ms
const MAX_TIME_WINDOW_MS = 60_000;   // 60 s

const channels = new Map();          // name -> index
const channelVisible = new Map();    // index -> bool

let events = [];
const incoming = [];

function getChannelIndex(name) {
    if (!channels.has(name)) {
        const index = channels.size;
        channels.set(name, index);
        channelVisible.set(index, true);
    }
    return channels.get(name);
}


// ---- Controls ------------------------------------------------------

let paused = false;

// Keyboard controls
window.addEventListener("keydown", (e) => {
    // Channel toggles
    if (e.key >= "1" && e.key <= "3") {
        const index = Number(e.key) - 1;
        if (channelVisible.has(index)) {
            channelVisible.set(index, !channelVisible.get(index));
        }
    }

    // Pause / resume
    if (e.key === "s" || e.key === "S") {
        paused = !paused;
        console.log(paused ? "ACQUISITION PAUSED" : "ACQUISITION RUNNING");
    }
});

// Mouse wheel = time zoom
canvas.addEventListener(
    "wheel",
    (e) => {
        e.preventDefault();

        // Zoom factor (tweakable)
        const ZOOM_FACTOR = 1.15;

        if (e.deltaY < 0) {
            // zoom in
            TIME_WINDOW_MS /= ZOOM_FACTOR;
        } else {
            // zoom out
            TIME_WINDOW_MS *= ZOOM_FACTOR;
        }

        TIME_WINDOW_MS = Math.max(
            MIN_TIME_WINDOW_MS,
            Math.min(MAX_TIME_WINDOW_MS, TIME_WINDOW_MS)
        );

        console.log(`Time window: ${Math.round(TIME_WINDOW_MS)} ms`);
    },
    { passive: false }
);


// ---- WebSocket -----------------------------------------------------

let ws = null;
let reconnectDelayMs = 1000;
const MAX_RECONNECT_DELAY = 10_000;

function connect() {
    console.log("Connecting to WebSocket…");

    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
        console.log("Connected to server");
        reconnectDelayMs = 1000;
    };

    ws.onmessage = (e) => {
        if (paused) return;

        const msg = JSON.parse(e.data);
        if (msg.type === "event") {
            incoming.push(msg);
        }
    };

    ws.onerror = (err) => {
        console.warn("WebSocket error", err);
        ws.close();
    };

    ws.onclose = () => {
        console.log(`Disconnected. Reconnecting in ${reconnectDelayMs} ms`);
        setTimeout(connect, reconnectDelayMs);
        reconnectDelayMs = Math.min(
            reconnectDelayMs * 2,
            MAX_RECONNECT_DELAY
        );
    };
}


// ---- Rendering -----------------------------------------------------

const start = performance.now();
let frameCounter = 0;

let smoothedNow = Date.now();
let frozenNow = smoothedNow;
const TIME_SMOOTHING = 0.2;

function render(time) {
    const realNow = Date.now();

    if (!paused) {
        smoothedNow += (realNow - smoothedNow) * TIME_SMOOTHING;
        frozenNow = smoothedNow;
    }

    const now = paused ? frozenNow : smoothedNow;

    frameCounter++;
    const elapsedMs = time - start;

    // Ingest queued events
    if (!paused && incoming.length > 0) {
        events.push(...incoming);
        incoming.length = 0;
    }

    // Drop old events
    if (!paused) {
        events = events.filter(e => e.ts >= now - TIME_WINDOW_MS);
    }

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // Draw events
    for (const e of events) {
        const chIndex = getChannelIndex(e.channel);
        if (!channelVisible.get(chIndex)) continue;

        const x = canvas.width
            - ((now - e.ts) / TIME_WINDOW_MS) * canvas.width;
        const y = 40 + chIndex * CHANNEL_HEIGHT;

        ctx.fillStyle =
            typeof e.value === "number" && e.value === 0
                ? "#444"
                : "#0f0";

        ctx.fillRect(x, y + 5, 4, CHANNEL_HEIGHT - 10);
    }

    // Channel labels
    for (const [name, index] of channels) {
        ctx.fillStyle = channelVisible.get(index) ? "#aaa" : "#444";
        ctx.fillText(
            `[${index + 1}] ${name}`,
            5,
            40 + index * CHANNEL_HEIGHT + 20
        );
    }

    // Status / FPS / time window
    const fps = (frameCounter / elapsedMs) * 1000;
    ctx.fillStyle = paused ? "#f66" : "#aaa";
    ctx.fillText(
        `${paused ? "PAUSED" : "RUNNING"} — ${Math.trunc(fps)} fps — ${Math.round(TIME_WINDOW_MS)} ms`,
        5,
        20
    );

    requestAnimationFrame(render);
}

connect();
requestAnimationFrame(render);
