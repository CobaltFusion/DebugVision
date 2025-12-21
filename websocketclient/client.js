// TIME_SMOOTHING smooths how the how fast the window moves, not what happened
// 0.0 never smooth, this can cause the window to lag or run fast over time
// 1.0 jump instantly, (no smoothing)
// 0.2 move 20% of the remaining difference per frame

const WS_URL = "ws://localhost:8765";
const canvas = document.getElementById("scope");
const ctx = canvas.getContext("2d");

// ---- Canvas --------------------------------------------------------
function resize() {
    canvas.width = window.innerWidth;
    canvas.height = window.innerHeight;
}
window.addEventListener("resize", resize);
resize();

// ---- Stroke discipline --------------------------------------------
ctx.lineCap = "butt";
ctx.lineJoin = "miter";

// Align only at draw time
function align(v) {
    return Math.round(v);
}

// ---- Configuration -------------------------------------------------
const CHANNEL_HEIGHT = 50;
let TIME_WINDOW_MS = 10000;
const MIN_TIME_WINDOW_MS = 100;
const MAX_TIME_WINDOW_MS = 60000;
const LEVEL_PADDING = 4;
const TIME_SMOOTHING = 0.2;

// ---- Data model ----------------------------------------------------
const channels = new Map();
const channelVisible = new Map();
const channelLevel = new Map();
const channelRange = new Map();

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

// ---- Lane computation ---------------------------------------------------------
function computeLanes(chIndex, now) {
    const values = Array.from(
        new Set(
            events
                .filter(e =>
                    getChannelIndex(e.channel) === chIndex &&
                    e.ts >= now - TIME_WINDOW_MS
                )
                .map(e => e.value)
        )
    ).sort((a, b) => a - b);

    channelRange.set(chIndex, { values });
}

function valueToY(chIndex, value) {
    const bandTop = 40 + chIndex * CHANNEL_HEIGHT + LEVEL_PADDING;
    const bandHeight = CHANNEL_HEIGHT - 2 * LEVEL_PADDING;
    const { values } = channelRange.get(chIndex);

    if (!values.length)
        return bandTop + bandHeight;

    const i = values.indexOf(value);
    const laneHeight = bandHeight / values.length;

    return bandTop + bandHeight - (i + 0.5) * laneHeight;
}

// ---- Time mapping --------------------------------------------------
function timeToX(ts, now) {
    return canvas.width -
        ((now - ts) / TIME_WINDOW_MS) * canvas.width;
}

// ---- Controls ------------------------------------------------------
let paused = false;

window.addEventListener("keydown", e => {
    if (e.key >= "1" && e.key <= "3") {
        const idx = Number(e.key) - 1;
        if (channelVisible.has(idx))
            channelVisible.set(idx, !channelVisible.get(idx));
    }
    if (e.key === "s" || e.key === "S")
        paused = !paused;
});

canvas.addEventListener("wheel", e => {
    e.preventDefault();
    const ZOOM = 1.15;
    TIME_WINDOW_MS *= e.deltaY < 0 ? 1 / ZOOM : ZOOM;
    TIME_WINDOW_MS = Math.max(
        MIN_TIME_WINDOW_MS,
        Math.min(MAX_TIME_WINDOW_MS, TIME_WINDOW_MS)
    );
}, { passive: false });

// ---- WebSocket -----------------------------------------------------
let ws = null;
let reconnectDelayMs = 1000;

function connect() {
    ws = new WebSocket(WS_URL);

    ws.onmessage = e => {
        if (!paused) {
            const msg = JSON.parse(e.data);
            if (msg.type === "event")
                incoming.push(msg);
        }
    };

    ws.onclose = () => {
        setTimeout(connect, reconnectDelayMs);
        reconnectDelayMs = Math.min(reconnectDelayMs * 2, 10000);
    };
}
connect();

// ---- Raster --------------------------------------------------------
const scriptStartTime = Date.now();

function drawRaster(now) {
    ctx.strokeStyle = "#222";
    ctx.lineWidth = 1;
    ctx.font = "10px monospace";
    ctx.fillStyle = "#555";

    const interval = 1000;
    const firstTick =
        now - TIME_WINDOW_MS - ((now - TIME_WINDOW_MS - scriptStartTime) % interval);

    for (let t = firstTick; t < now; t += interval) {
        const x = align(timeToX(t, now));
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, canvas.height);
        ctx.stroke();

        const absSec = Math.floor((t - scriptStartTime) / 1000);
        ctx.fillText(`${absSec}s`, x + 2, 10);
    }

    for (const [, idx] of channels) {
        const yTop = align(40 + idx * CHANNEL_HEIGHT);
        const yBottom = align(yTop + CHANNEL_HEIGHT);

        for (let y = yTop; y <= yBottom; y += 5) {
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

    if (!paused && incoming.length) {
        events.push(...incoming);
        incoming.length = 0;
    }

    if (!paused) {
        events = events.filter(e => e.ts >= now - TIME_WINDOW_MS);
    }

    ctx.clearRect(0, 0, canvas.width, canvas.height);

    drawRaster(now);

    for (const [, idx] of channels)
        computeLanes(idx, now);

    const lastValue = new Map(channelLevel);
    const lastTs = new Map();
    for (const [, idx] of channels)
        lastTs.set(idx, now - TIME_WINDOW_MS);

    ctx.fillStyle = "#0f0";
    ctx.strokeStyle = "#0f0";
    ctx.lineWidth = 2;

    for (const e of events) {
        const ch = getChannelIndex(e.channel);
        if (!channelVisible.get(ch)) continue;

        const prev = lastValue.get(ch);
        const t0 = lastTs.get(ch);
        const t1 = e.ts;

        let x0 = align(timeToX(t0, now));
        let x1 = align(timeToX(t1, now));
        if (x1 <= x0) x1 = x0 + 1;

        const yPrev = align(valueToY(ch, prev ?? e.value));
        const yNew = align(valueToY(ch, e.value));

        // ---- Horizontal hold as filled rectangle (spot-free)
        const yTop = yPrev - 1;
        const height = 2;
        ctx.fillRect(x0, yTop, x1 - x0, height);

        // ---- Vertical step
        if (prev !== null && prev !== e.value) {
            ctx.beginPath();
            ctx.moveTo(x1, yPrev);
            ctx.lineTo(x1, yNew);
            ctx.stroke();
        }

        lastValue.set(ch, e.value);
        lastTs.set(ch, t1);
    }

    // ---- Extend to now
    for (const [, idx] of channels) {
        if (!channelVisible.get(idx)) continue;

        const x0 = align(timeToX(lastTs.get(idx), now));
        const y = align(valueToY(idx, lastValue.get(idx)));

        ctx.fillRect(x0, y - 1, canvas.width - x0, 2);
    }

    // ---- Channel labels
    ctx.fillStyle = "#aaa";
    for (const [name, idx] of channels) {
        ctx.fillStyle = channelVisible.get(idx) ? "#aaa" : "#444";
        ctx.fillText(`[${idx + 1}] ${name}`, 5, 40 + idx * CHANNEL_HEIGHT + 20);
    }

    // ---- Status
    ctx.fillStyle = paused ? "#f66" : "#aaa";
    ctx.fillText(`${paused ? "PAUSED" : "RUNNING"} — ${Math.round(TIME_WINDOW_MS)} ms`, 5, 20);

    requestAnimationFrame(render);
}

requestAnimationFrame(render);
