# different ways to host index.html and client.js

## windows

- locally for development, you do not need to host it at all, just open index.hmtl in a browser and it will load client.js
- using a simple WSGI server, examples: waitress or gunicorn, both can run 'Flask' applications such as 'app.py'

- `pip install flask waitress`, `waitress-serve --host=0.0.0.0 --port=8000 app:app` `

# linux/macos

- `pip install flask gunicorn`, `gunicorn -w 1 -b 0.0.0.0:8000 app:app`

```
gunicorn app:app
        ^   ^
        |   ?? variable name
        ????? file name (app.py)
```

```
from flask import Flask, send_from_directory

app = Flask(__name__)

# Serve index.html at /


@app.route("/")
def index():
    return send_from_directory(".", "index.html")

# Serve client.js


@app.route("/client.js")
def client_js():
    return send_from_directory(".", "client.js")
```

- `app.py' is a minimal flash (WSGI) application that, flash is traditionally synchronous, so this is just a way to host the application/files, not to stream data.
- `server.py` is a dummy signal producer that listens for websocket connections on 0.0.0.0:8765
