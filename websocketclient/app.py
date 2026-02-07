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
