#!/usr/bin/env python3
""" File server for index.html / client.js and listening for websocket

server - refers to the filename 'server.py'
app    - refers to the variable that is called as the main program

pip install fastapi uvicorn
uvicorn server:app --reload
"""

import asyncio
import json
import time
import random
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse

app = FastAPI()

CHANNELS = [
    ("State", ["IDLE", "RUN", "ERROR"]),
    ("Enable", [0, 1]),
    ("Voltage", [0, 1, 2, 4, 5, 6, 7]),
]

event_queue = asyncio.Queue(maxsize=1000)

async def sample_generator(queue: asyncio.Queue):
    """
    Produces state-change samples and pushes them into a queue.
    """
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

        # Only emit on change (oscilloscope-style)
        if last_values.get(ch) != new_val:
            last_values[ch] = new_val

            sample = {
                "type": "event",
                "ts": ts,
                "channel": ch,
                "value": new_val,
            }
            await queue.put(sample)

        # Clock signal every cycle
        sample = {
            "type": "event",
            "ts": ts,
            "channel": "Clock",
            "value": clock,
        }
        await queue.put(sample)

        await asyncio.sleep(0.1)  # 10 Hz base rate


# Serve static files (client.js)
app.mount("/static", StaticFiles(directory="static"), name="static")

@app.on_event("startup")
async def startup_event():
    """Start background generator."""
    asyncio.create_task(sample_generator(event_queue))

@app.get("/")
def index():
    return FileResponse("static/index.html")

@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    print("Client connected")

    try:
        while True:
            sample = await event_queue.get()
            await websocket.send_text(json.dumps(sample))

    except WebSocketDisconnect:
        print("Client disconnected")
