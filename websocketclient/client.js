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
const CHANNEL_HEIGHT = 50;
let TIME_WINDOW_MS = 10000;
const MIN_TIME_WINDOW_MS = 100;
const MAX_TIME_WINDOW_MS = 60000;
const LEVEL_PADDING = 4;
const TIME_SMOOTHING = 0.2;

// ---- Data model ----------------------------------------------------
const channels = new Map();          // name -> index
const channelVisible = new Map();    // index -> bool
const channelLevel = new Map();      // index -> last value
const channelRange = new Map();      // index -> { values: [] }

let events = [];
const incoming = [];

function getChannelIndex(name) {
    if (!channels.has(name)) {
        const index = channels.size;
        channels.set(name, index);
        channelVisible.set(index, true);
        channelLevel.set(index, null);
        channelRange.set(index, { values: [] });
    }
    return channels.get(name);
}

// ---- Compute per-channel unique value lanes -----------------------
function computeLanes(chIndex) {
    const now = Date.now();
    const values = Array.from(
        new Set(
            events
                .filter(e => getChannelIndex(e.channel) === chIndex && e.ts >= now - TIME_WINDOW_MS)
                .map(e => e.value)
        )
    ).sort((a, b) => a - b);
    channelRange.set(chIndex, { values });
}

function valueToY(chIndex, value) {
    const bandTop = 40 + chIndex * CHANNEL_HEIGHT + LEVEL_PADDING;
    const bandHeight = CHANNEL_HEIGHT - 2 * LEVEL_PADDING;
    const { values } = channelRange.get(chIndex);

    if (!values.length) return bandTop + bandHeight;

    const index = values.indexOf(value);
    const laneHeight = bandHeight / values.length;

    return bandTop + bandHeight - (index + 0.5) * laneHeight;
}

// ---- Convert timestamp to X ---------------------------------------
function timeToX(ts, now) {
    return canvas.width - ((now - ts) / TIME_WINDOW_MS) * canvas.width;
}

// ---- Controls ------------------------------------------------------
let paused = false;

window.addEventListener("keydown", (e) => {
    if (e.key >= "1" && e.key <= "3") {
        const idx = Number(e.key) - 1;
        if (channelVisible.has(idx)) channelVisible.set(idx, !channelVisible.get(idx));
    }

    if (e.key === "s" || e.key === "S") paused = !paused;
});

canvas.addEventListener(
    "wheel",
    (e) => {
        e.preventDefault();
        const ZOOM = 1.15;
        TIME_WINDOW_MS *= e.deltaY < 0 ? 1 / ZOOM : ZOOM;
        TIME_WINDOW_MS = Math.max(MIN_TIME_WINDOW_MS, Math.min(MAX_TIME_WINDOW_MS, TIME_WINDOW_MS));
    },
    { passive: false }
);

// ---- WebSocket -----------------------------------------------------
let ws = null;
let reconnectDelayMs = 1000;
const MAX_RECONNECT_DELAY = 10000;

function connect() {
    ws = new WebSocket(WS_URL);

    ws.onopen = () => reconnectDelayMs = 1000;

    ws.onmessage = (e) => {
        if (paused) return;
        const msg = JSON.parse(e.data);
        if (msg.type === "event") incoming.push(msg);
    };

    ws.onerror = () => ws.close();

    ws.onclose = () => {
        setTimeout(connect, reconnectDelayMs);
        reconnectDelayMs = Math.min(reconnectDelayMs * 2, MAX_RECONNECT_DELAY);
    };
}

// ---- Raster with absolute time -----------------------------------
const scriptStartTime = Date.now(); // absolute start timestamp

function drawRaster(now) {
    ctx.strokeStyle = "#222";
    ctx.lineWidth = 1;
    ctx.font = "10px monospace";
    ctx.fillStyle = "#555";

    // Vertical time grid (absolute times)
    const interval = 1000; // 1 second
    const firstTick = now - TIME_WINDOW_MS - ((now - TIME_WINDOW_MS - scriptStartTime) % interval);
    for (let t = firstTick; t < now; t += interval) {
        const x = timeToX(t, now);
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, canvas.height);
        ctx.stroke();

        const absSec = Math.floor((t - scriptStartTime) / 1000);
        ctx.fillText(`${absSec}s`, x + 2, 10);
    }

    // Horizontal lines per channel
    for (const [_, idx] of channels) {
        const yTop = 40 + idx * CHANNEL_HEIGHT;
        const yBottom = yTop + CHANNEL_HEIGHT;
        const step = 5; // px steps inside channel
        for (let y = yTop; y <= yBottom; y += step) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(canvas.width, y);
            ctx.stroke();
        }
    }
}

// ---- Rendering -----------------------------------------------------
let smoothedNow = Date.now();
let frozenNow = smoothedNow;

function render() {
    const realNow = Date.now();

    if (!paused) {
        smoothedNow += (realNow - smoothedNow) * TIME_SMOOTHING;
        frozenNow = smoothedNow;
    }
    const now = paused ? frozenNow : smoothedNow;

    // Ingest new events
    if (!paused && incoming.length) {
        events.push(...incoming);
        incoming.length = 0;
    }

    if (!paused) {
        events = events.filter(e => e.ts >= now - TIME_WINDOW_MS);
    }

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    // ---- Draw raster first ----
    drawRaster(now);

    // ---- Compute lanes per channel ----
    for (const [name, idx] of channels) {
        computeLanes(idx);
    }

    // ---- Draw stepped traces ----
    const lastValue = new Map(channelLevel);
    const lastTs = new Map();
    for (const [_, idx] of channels) lastTs.set(idx, now - TIME_WINDOW_MS);

    for (const e of events) {
        const ch = getChannelIndex(e.channel);
        if (!channelVisible.get(ch)) continue;

        const prevValue = lastValue.get(ch);
        const t0 = lastTs.get(ch);
        const t1 = e.ts;

        const x0 = timeToX(t0, now);
        const x1 = timeToX(t1, now);

        const yPrev = prevValue !== null ? valueToY(ch, prevValue) : valueToY(ch, e.value);
        const yNew = valueToY(ch, e.value);

        ctx.strokeStyle = "#0f0";
        ctx.lineWidth = 2;

        // horizontal hold
        ctx.beginPath();
        ctx.moveTo(x0, yPrev);
        ctx.lineTo(x1, yPrev);
        ctx.stroke();

        // vertical step
        if (prevValue !== null && prevValue !== e.value) {
            ctx.beginPath();
            ctx.moveTo(x1, yPrev);
            ctx.lineTo(x1, yNew);
            ctx.stroke();
        }

        lastValue.set(ch, e.value);
        lastTs.set(ch, t1);
    }

    // extend to now
    for (const [_, idx] of channels) {
        if (!channelVisible.get(idx)) continue;
        const x0 = timeToX(lastTs.get(idx), now);
        const x1 = canvas.width;
        const y = valueToY(idx, lastValue.get(idx));
        ctx.beginPath();
        ctx.moveTo(x0, y);
        ctx.lineTo(x1, y);
        ctx.stroke();
    }

    // ---- Channel labels ----
    for (const [name, idx] of channels) {
        ctx.fillStyle = channelVisible.get(idx) ? "#aaa" : "#444";
        ctx.fillText(`[${idx + 1}] ${name}`, 5, 40 + idx * CHANNEL_HEIGHT + 20);
    }

    ctx.fillStyle = paused ? "#f66" : "#aaa";
    ctx.fillText(`${paused ? "PAUSED" : "RUNNING"} — ${Math.round(TIME_WINDOW_MS)} ms`, 5, 20);

    requestAnimationFrame(render);
}

connect();
requestAnimationFrame(render);
