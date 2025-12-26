// ================================================================
// Scope Viewer with Falling Flags + Rising Lollipops
// ================================================================

// ---- WebSocket --------------------------------------------------
const WS_URL = "ws://localhost:8765";

// ---- Canvas -----------------------------------------------------
const canvas = document.getElementById("scope");

// ---- Configuration ---------------------------------------------
const config = {
    CHANNEL_HEIGHT: 50,
    LEVEL_PADDING: 4,

    TIME_WINDOW_MS: 10000,
    MIN_TIME_WINDOW_MS: 100,
    MAX_TIME_WINDOW_MS: 60000,

    TIME_SMOOTHING: 0.2,

    // Falling edges
    SHOW_FALLING_FLAGS: true,
    FALLING_FLAG_SIZE: 4,

    // Rising edges
    SHOW_RISING_LOLLIPOPS: true,
    RISING_DELAY_MS: 150,   // fixed time after edge
    LOLLIPOP_HEIGHT: 10,
    LOLLIPOP_RADIUS: 3
};

// ================================================================
// Renderer
// ================================================================

class ScopeRenderer {
    constructor(canvas, config) {
        this.canvas = canvas;
        this.ctx = canvas.getContext("2d");
        this.config = config;

        this.channels = new Map();
        this.channelVisible = new Map();
        this.channelLevel = new Map();
        this.channelRange = new Map();

        this.events = [];

        this.smoothedNow = Date.now();
        this.frozenNow = this.smoothedNow;
        this.scriptStartTime = Date.now();

        this.ctx.lineCap = "butt";
        this.ctx.lineJoin = "miter";

        this.resize();
        window.addEventListener("resize", () => this.resize());
    }

    // ---- Infrastructure ----------------------------------------

    resize() {
        this.canvas.width = window.innerWidth;
        this.canvas.height = window.innerHeight;
    }

    align(v) {
        return Math.round(v);
    }

    getChannelIndex(name) {
        if (!this.channels.has(name)) {
            const idx = this.channels.size;
            this.channels.set(name, idx);
            this.channelVisible.set(idx, true);
            this.channelLevel.set(idx, null);
            this.channelRange.set(idx, { values: [] });
        }
        return this.channels.get(name);
    }

    setChannelVisible(idx, visible) {
        if (this.channelVisible.has(idx))
            this.channelVisible.set(idx, visible);
    }

    // ---- Data ---------------------------------------------------

    pushEvents(events) {
        this.events.push(...events);
    }

    pruneEvents(now) {
        this.events = this.events.filter(
            e => e.ts >= now - this.config.TIME_WINDOW_MS
        );
    }

    // ---- Lanes --------------------------------------------------

    computeLanes(chIndex, now) {
        const values = Array.from(
            new Set(
                this.events
                    .filter(e =>
                        this.getChannelIndex(e.channel) === chIndex &&
                        e.ts >= now - this.config.TIME_WINDOW_MS
                    )
                    .map(e => e.value)
            )
        ).sort((a, b) => a - b);

        this.channelRange.set(chIndex, { values });
    }

    valueToY(chIndex, value) {
        const { CHANNEL_HEIGHT, LEVEL_PADDING } = this.config;

        const bandTop = 40 + chIndex * CHANNEL_HEIGHT + LEVEL_PADDING;
        const bandHeight = CHANNEL_HEIGHT - 2 * LEVEL_PADDING;
        const { values } = this.channelRange.get(chIndex);

        if (!values.length)
            return bandTop + bandHeight;

        const i = values.indexOf(value);
        const laneHeight = bandHeight / values.length;

        return bandTop + bandHeight - (i + 0.5) * laneHeight;
    }

    timeToX(ts, now) {
        return this.canvas.width -
            ((now - ts) / this.config.TIME_WINDOW_MS) * this.canvas.width;
    }

    // ---- Raster -------------------------------------------------

    drawRaster(now) {
        const ctx = this.ctx;

        ctx.strokeStyle = "#555";
        ctx.lineWidth = 1;

        // scale labels
        ctx.font = "12px monospace";
        ctx.fillStyle = "#fff";

        const interval = 1000;
        const firstTick =
            now - this.config.TIME_WINDOW_MS -
            ((now - this.config.TIME_WINDOW_MS - this.scriptStartTime) % interval);

        for (let t = firstTick; t < now; t += interval) {
            const x = this.align(this.timeToX(t, now));
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, this.canvas.height);
            ctx.stroke();

            const absSec = Math.floor((t - this.scriptStartTime) / 1000);
            ctx.fillText(`${absSec}s`, x + 2, 10);
        }

        for (const [, idx] of this.channels) {
            const yTop = this.align(40 + idx * this.config.CHANNEL_HEIGHT);
            const yBottom = yTop + this.config.CHANNEL_HEIGHT;

            for (let y = yTop; y <= yBottom; y += 5) {
                ctx.beginPath();
                ctx.moveTo(0, y);
                ctx.lineTo(this.canvas.width, y);
                ctx.stroke();
            }
        }
    }

    // ---- Edge decorations --------------------------------------

    drawFallingFlag(x, y, size) {
        const ctx = this.ctx;
        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x + size, y - size);
        ctx.lineTo(x + size, y + size);
        ctx.closePath();
        ctx.fill();
    }

    drawRisingLollipop(x, y) {
        const ctx = this.ctx;
        const { LOLLIPOP_HEIGHT, LOLLIPOP_RADIUS } = this.config;

        const stemTop = y - LOLLIPOP_HEIGHT;

        ctx.beginPath();
        ctx.moveTo(x, y);
        ctx.lineTo(x, stemTop);
        ctx.stroke();

        ctx.beginPath();
        ctx.arc(x, stemTop, LOLLIPOP_RADIUS, 0, Math.PI * 2);
        ctx.fill();
    }

    // ---- Render -------------------------------------------------

    render(paused) {
        const realNow = Date.now();

        if (!paused) {
            this.smoothedNow +=
                (realNow - this.smoothedNow) * this.config.TIME_SMOOTHING;
            this.frozenNow = this.smoothedNow;
        }

        const now = paused ? this.frozenNow : this.smoothedNow;

        if (!paused)
            this.pruneEvents(now);

        const ctx = this.ctx;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        this.drawRaster(now);

        for (const [, idx] of this.channels)
            this.computeLanes(idx, now);

        const lastValue = new Map(this.channelLevel);
        const lastTs = new Map();
        for (const [, idx] of this.channels)
            lastTs.set(idx, now - this.config.TIME_WINDOW_MS);

        // Base signal color
        ctx.strokeStyle = "#0f0";
        ctx.fillStyle = "#0f0";
        ctx.lineWidth = 2;

        for (const e of this.events) {
            const ch = this.getChannelIndex(e.channel);
            if (!this.channelVisible.get(ch)) continue;

            const prev = lastValue.get(ch);
            const t0 = lastTs.get(ch);
            const t1 = e.ts;

            let x0 = this.align(this.timeToX(t0, now));
            let x1 = this.align(this.timeToX(t1, now));
            if (x1 <= x0) x1 = x0 + 1;

            const yPrev = this.align(this.valueToY(ch, prev ?? e.value));
            const yNew = this.align(this.valueToY(ch, e.value));

            // Horizontal hold
            ctx.fillRect(x0, yPrev - 1, x1 - x0, 2);

            if (prev !== null && prev !== e.value) {
                // Vertical step
                ctx.beginPath();
                ctx.moveTo(x1, yPrev);
                ctx.lineTo(x1, yNew);
                ctx.stroke();

                // Falling edge
                if (
                    this.config.SHOW_FALLING_FLAGS &&
                    e.value < prev
                ) {
                    this.drawFallingFlag(
                        x1 - 1,
                        yPrev,
                        this.config.FALLING_FLAG_SIZE
                    );
                }

                // Rising edge lollipop (fixed time offset)
                if (
                    this.config.SHOW_RISING_LOLLIPOPS &&
                    e.value > prev
                ) {
                    const lx = this.align(
                        this.timeToX(
                            e.ts + this.config.RISING_DELAY_MS,
                            now
                        )
                    );

                    ctx.save();
                    ctx.strokeStyle = "#f44";
                    ctx.fillStyle = "#f44";
                    this.drawRisingLollipop(lx, yNew);
                    ctx.restore();
                }
            }

            lastValue.set(ch, e.value);
            lastTs.set(ch, t1);
        }

        // Extend to now
        for (const [, idx] of this.channels) {
            if (!this.channelVisible.get(idx)) continue;
            const x0 = this.align(this.timeToX(lastTs.get(idx), now));
            const y = this.align(this.valueToY(idx, lastValue.get(idx)));
            ctx.fillRect(x0, y - 1, this.canvas.width - x0, 2);
        }

        // Labels
        ctx.fillStyle = "#aaa";
        for (const [name, idx] of this.channels) {
            ctx.fillStyle = this.channelVisible.get(idx) ? "#aaa" : "#444";
            ctx.fillText(
                `[${idx + 1}] ${name}`,
                5,
                40 + idx * this.config.CHANNEL_HEIGHT + 20
            );
        }

        // Status
        ctx.fillStyle = paused ? "#f66" : "#aaa";
        ctx.fillText(
            `${paused ? "PAUSED" : "RUNNING"} — ${Math.round(this.config.TIME_WINDOW_MS)} ms`,
            5,
            20
        );
    }
}

// ================================================================
// Application
// ================================================================

const renderer = new ScopeRenderer(canvas, config);

let paused = false;
const incoming = [];

// ---- Controls ---------------------------------------------------

window.addEventListener("keydown", e => {
    if (e.key >= "1" && e.key <= "9") {
        const idx = Number(e.key) - 1;
        renderer.setChannelVisible(
            idx,
            !renderer.channelVisible.get(idx)
        );
    }
    if (e.key === "s" || e.key === "S")
        paused = !paused;
});

canvas.addEventListener("wheel", e => {
    e.preventDefault();
    const ZOOM = 1.15;
    config.TIME_WINDOW_MS *= e.deltaY < 0 ? 1 / ZOOM : ZOOM;
    config.TIME_WINDOW_MS = Math.max(
        config.MIN_TIME_WINDOW_MS,
        Math.min(config.MAX_TIME_WINDOW_MS, config.TIME_WINDOW_MS)
    );
}, { passive: false });

// ---- WebSocket --------------------------------------------------

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

// ---- Main loop --------------------------------------------------

function frame() {
    if (!paused && incoming.length) {
        renderer.pushEvents(incoming);
        incoming.length = 0;
    }
    renderer.render(paused);
    requestAnimationFrame(frame);
}

requestAnimationFrame(frame);
