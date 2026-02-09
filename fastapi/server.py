#!/usr/bin/env python3
import asyncio
import contextlib
from contextlib import asynccontextmanager
import json
import time
import random
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse

CHANNELS = [
    ("State", ["IDLE", "RUN", "ERROR"]),
    ("Enable", [0, 1]),
    ("Voltage", [0, 1, 2, 4, 5, 6, 7]),
]

event_queue = asyncio.Queue(maxsize=1000)


async def sample_generator(queue: asyncio.Queue):
    last_values = {}
    clock = 0
    count = 0

    while True:
        ts = int(time.time() * 1000)

        count += 1
        if count == 30:
            count = 0
            clock = int(not clock)

        ch, values = random.choice(CHANNELS)
        new_val = random.choice(values)

        if last_values.get(ch) != new_val:
            last_values[ch] = new_val
            sample = {
                "type": "event",
                "ts": ts,
                "channel": ch,
                "value": new_val,
            }
            await queue.put(sample)

        # Clock signal
        await queue.put({
            "type": "event",
            "ts": ts,
            "channel": "Clock",
            "value": clock,
        })

        await asyncio.sleep(0.1)


@asynccontextmanager
async def lifespan(app: FastAPI):
    task = asyncio.create_task(sample_generator(event_queue))
    try:
        yield
    finally:
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task

app = FastAPI(lifespan=lifespan)

app.mount("/static", StaticFiles(directory="static"), name="static")


@app.get("/")
def index():
    return FileResponse("static/index.html")


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            sample = await event_queue.get()
            await websocket.send_text(json.dumps(sample))
    except WebSocketDisconnect:
        pass
