const canvas = document.getElementById("scope");
const ctx = canvas.getContext("2d");

function resize() {
    canvas.width = canvas.clientWidth;
    canvas.height = canvas.clientHeight;
}
window.addEventListener("resize", resize);
resize();

const channels = {};
const colors = {};
const colorList = ["#0f0", "#0ff", "#ff0", "#f0f", "#f80", "#08f"];

let colorIndex = 0;
function getColor(ch) {
    if (!colors[ch]) {
        colors[ch] = colorList[colorIndex % colorList.length];
        colorIndex++;
    }
    return colors[ch];
}

// Rolling time window in milliseconds
const TIME_WINDOW = 5000; // 5 seconds
const MAX_SAMPLES = 5000; // safety cap

// WebSocket connection
const ws = new WebSocket(`ws://${location.host}/ws`);

ws.onmessage = (event) => {
    const sample = JSON.parse(event.data);

    if (sample.type === "event") {
        const { ts, channel, value } = sample;

        if (!channels[channel]) {
            channels[channel] = [];
        }

        channels[channel].push({ ts, value });

        // Safety: limit total samples
        if (channels[channel].length > MAX_SAMPLES) {
            channels[channel].shift();
        }
    }
};

// Drawing loop
function draw() {
    ctx.fillStyle = "#111";
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    const now = Date.now();
    const startTime = now - TIME_WINDOW;

    const channelNames = Object.keys(channels);
    const rowHeight = canvas.height / Math.max(channelNames.length, 1);

    channelNames.forEach((ch, i) => {
        const data = channels[ch];

        // Remove old samples
        while (data.length > 0 && data[0].ts < startTime) {
            data.shift();
        }

        const yBase = i * rowHeight + rowHeight / 2;

        ctx.strokeStyle = getColor(ch);
        ctx.lineWidth = 2;
        ctx.beginPath();

        let lastX = null;
        let lastY = null;

        for (const s of data) {
            const x =
                ((s.ts - startTime) / TIME_WINDOW) * canvas.width;
            const y =
                yBase - (s.value ? rowHeight / 4 : -rowHeight / 4);

            if (lastX !== null) {
                ctx.lineTo(x, lastY);
                ctx.lineTo(x, y);
            } else {
                ctx.moveTo(x, y);
            }

            lastX = x;
            lastY = y;
        }

        ctx.stroke();

        // Channel label
        ctx.fillStyle = "#aaa";
        ctx.fillText(ch, 10, i * rowHeight + 12);
    });

    requestAnimationFrame(draw);
}

draw();
