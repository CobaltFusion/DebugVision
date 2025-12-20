const WS_URL = "ws://localhost:8765";
const canvas = document.getElementById("scope");
const ctx = canvas.getContext("2d");

canvas.width = window.innerWidth;
canvas.height = window.innerHeight;

window.addEventListener("resize", () => {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
});


// ---- Configuration -------------------------------------------------

const CHANNEL_HEIGHT = 40;

let TIME_WINDOW_MS = 10_000;
const MIN_TIME_WINDOW_MS = 100;
const MAX_TIME_WINDOW_MS = 60_000;

const LEVELS = 16;           // fixed discrete steps
const LEVEL_PADDING = 4;     // padding inside channel band

const TIME_SMOOTHING = 0.2;


// ---- Data model ----------------------------------------------------

const channels = new Map();          // name -> index
const channelVisible = new Map();    // index -> bool
const channelLevel = new Map();      // index -> last level

let events = [];
const incoming = [];

function getChannelIndex(name) {
    if (!channels.has(name)) {
        const index = channels.size;
        channels.set(name, index);
        channelVisible.set(index, true);
        channelLevel.set(index, 0);
    }
    return channels.get(name);
}

function quantize(value) {
    if (typeof value !== "number") return 0;

    const clamped = Math.max(0, Math.min(1, value));
    return Math.min(
        LEVELS - 1,
        Math.floor(clamped * LEVELS)
    );
}


// ---- Discrete lane geometry ---------------------------------------

function levelToY(chIndex, level) {
    const bandTop =
        40 + chIndex * CHANNEL_HEIGHT + LEVEL_PADDING;

    const bandHeight =
        CHANNEL_HEIGHT - 2 * LEVEL_PADDING;

    const laneHeight = bandHeight / LEVELS;

    // center of the lane
    return (
        bandTop +
        bandHeight -
        (level + 0.5) * laneHeight
    );
}

function timeToX(ts, now) {
    return (
        canvas.width -
        ((now - ts) / TIME_WINDOW_MS) * canvas.width
    );
}


// ---- Controls ------------------------------------------------------

let paused = false;

window.addEventListener("keydown", (e) => {
    // Channel toggles
    if (e.key >= "1" && e.key <= "3") {
        const idx = Number(e.key) - 1;
        if (channelVisible.has(idx)) {
            channelVisible.set(idx, !channelVisible.get(idx));
        }
    }

    // Pause / resume
    if (e.key === "s" || e.key === "S") {
        paused = !paused;
    }
});

// Mouse wheel zoom
canvas.addEventListener(
    "wheel",
    (e) => {
        e.preventDefault();
        const ZOOM = 1.15;

        TIME_WINDOW_MS *= e.deltaY < 0 ? 1 / ZOOM : ZOOM;
        TIME_WINDOW_MS = Math.max(
            MIN_TIME_WINDOW_MS,
            Math.min(MAX_TIME_WINDOW_MS, TIME_WINDOW_MS)
        );
    },
    { passive: false }
);


// ---- WebSocket -----------------------------------------------------

let ws = null;
let reconnectDelayMs = 1000;
const MAX_RECONNECT_DELAY = 10_000;

function connect() {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => reconnectDelayMs = 1000;

    ws.onmessage = (e) => {
        if (paused) return;

        const msg = JSON.parse(e.data);
        if (msg.type === "event") {
            incoming.push(msg);
        }
    };

    ws.onerror = () => ws.close();

    ws.onclose = () => {
        setTimeout(connect, reconnectDelayMs);
        reconnectDelayMs = Math.min(
            reconnectDelayMs * 2,
            MAX_RECONNECT_DELAY
        );
    };
}


// ---- Rendering -----------------------------------------------------

let smoothedNow = Date.now();
let frozenNow = smoothedNow;

function render() {
    const realNow = Date.now();

    if (!paused) {
        smoothedNow +=
            (realNow - smoothedNow) * TIME_SMOOTHING;
        frozenNow = smoothedNow;
    }

    const now = paused ? frozenNow : smoothedNow;

    // Ingest events
    if (!paused && incoming.length) {
        events.push(...incoming);
        incoming.length = 0;
    }

    // Drop old events
    if (!paused) {
        events = events.filter(
            e => e.ts >= now - TIME_WINDOW_MS
        );
    }

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // ---- Draw stepped traces --------------------------------------

    // Track last level + time per channel
    const lastLevel = new Map(channelLevel);
    const lastTs = new Map();

    for (const [_, idx] of channels) {
        lastTs.set(idx, now - TIME_WINDOW_MS);
    }

    for (const e of events) {
        const ch = getChannelIndex(e.channel);
        if (!channelVisible.get(ch)) continue;

        const newLevel = quantize(e.value);
        const prevLevel = lastLevel.get(ch);

        const t0 = lastTs.get(ch);
        const t1 = e.ts;

        const x0 = timeToX(t0, now);
        const x1 = timeToX(t1, now);

        const yPrev = levelToY(ch, prevLevel);
        const yNew = levelToY(ch, newLevel);

        ctx.strokeStyle = "#0f0";
        ctx.lineWidth = 2;

        // horizontal hold
        ctx.beginPath();
        ctx.moveTo(x0, yPrev);
        ctx.lineTo(x1, yPrev);
        ctx.stroke();

        // vertical step
        if (prevLevel !== newLevel) {
            ctx.beginPath();
            ctx.moveTo(x1, yPrev);
            ctx.lineTo(x1, yNew);
            ctx.stroke();
        }

        lastLevel.set(ch, newLevel);
        lastTs.set(ch, t1);
    }

    // Extend to "now"
    for (const [name, idx] of channels) {
        if (!channelVisible.get(idx)) continue;

        const x0 = timeToX(lastTs.get(idx), now);
        const x1 = canvas.width;
        const y = levelToY(idx, lastLevel.get(idx));

        ctx.beginPath();
        ctx.moveTo(x0, y);
        ctx.lineTo(x1, y);
        ctx.stroke();
    }

    // ---- Labels ----------------------------------------------------

    for (const [name, idx] of channels) {
        ctx.fillStyle = channelVisible.get(idx) ? "#aaa" : "#444";
        ctx.fillText(
            `[${idx + 1}] ${name}`,
            5,
            40 + idx * CHANNEL_HEIGHT + 20
        );
    }

    ctx.fillStyle = paused ? "#f66" : "#aaa";
    ctx.fillText(
        `${paused ? "PAUSED" : "RUNNING"} — ${Math.round(TIME_WINDOW_MS)} ms`,
        5,
        20
    );

    requestAnimationFrame(render);
}

connect();
requestAnimationFrame(render);
