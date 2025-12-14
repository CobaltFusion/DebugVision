import asyncio
import json
import time
import random
import websockets

HOST = "0.0.0.0"
PORT = 8765

CHANNELS = [
    ("FSM.State", ["IDLE", "RUN", "ERROR"]),
    ("Motor.Enable", [0, 1]),
    ("Sensor.Ready", [0, 1]),
]


async def sample_generator(queue: asyncio.Queue):
    """
    Produces state-change samples and pushes them into a queue.
    """
    last_values = {}

    while True:
        ts = int(time.time() * 1000)

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

        await asyncio.sleep(0.1)  # 10 Hz base rate


async def client_handler(websocket):
    print("Client connected")

    try:
        while True:
            sample = await event_queue.get()
            await websocket.send(json.dumps(sample))

    except websockets.ConnectionClosed:
        print("Client disconnected")


async def main():
    global event_queue
    event_queue = asyncio.Queue(maxsize=1000)

    # Background producer
    asyncio.create_task(sample_generator(event_queue))

    async with websockets.serve(client_handler, HOST, PORT):
        print(f"WebSocket server running on ws://{HOST}:{PORT}")
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    asyncio.run(main())
