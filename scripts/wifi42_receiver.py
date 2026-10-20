#!/usr/bin/env python3
"""Isolated HTTPS receiver for the synthetic wifi42 sketch, NOT an archive.

Off until explicitly launched. Defaults to loopback; a board test requires an
operator-selected private LAN bind/address and certificate valid for that name.
No file storage, production ingestion or credentials in logs. A 204 accepts only
this disposable test record and permits the test node to remove it from its queue.
"""
import argparse
from datetime import datetime, timezone
from html import escape
from threading import Lock
import hmac
import json
import os
import ssl
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAX_BODY = 16384

def is_wifi42(payload):
    if not isinstance(payload, dict) or payload.get("format") != "bardbox-delivery-candidate":
        return False
    channels = payload.get("channels")
    if not isinstance(channels, list) or len(channels) != 1 or not isinstance(channels[0], dict):
        return False
    channel = channels[0]
    value = channel.get("value")
    return (channel.get("sensor") == "demo" and channel.get("measurement") == "answer"
            and channel.get("unit") == "1" and channel.get("status") == "valid"
            and type(value) in (int, float) and value == 42)

class DemoState:
    """Only synthetic acceptance counters, never arbitrary payload data."""
    def __init__(self):
        self.lock = Lock()
        self.count = 0
        self.arrived = None

    def accept(self):
        with self.lock:
            self.count += 1
            self.arrived = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")

    def snapshot(self):
        with self.lock:
            return self.count, self.arrived


def viewing_page(count, arrived):
    value = "42" if count else "—"
    arrival = escape(arrived) if arrived else "No upload received yet"
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="3"><title>BardBox · 42 demo</title>
<style>
body{{margin:0;background:#f3f5f0;color:#173528;font:18px system-ui,sans-serif}}
main{{max-width:640px;margin:8vh auto;padding:32px}}
article{{background:white;border:1px solid #ccd8cb;border-radius:20px;padding:32px}}
h1{{font-size:28px;margin:0 0 8px}} .value{{font-size:112px;font-weight:650;margin:12px 0}}
dt{{color:#526357;margin-top:20px}}dd{{margin:4px 0;overflow-wrap:anywhere}}
small,p{{color:#526357;line-height:1.5}} .tag{{font-size:14px;letter-spacing:.08em}}
</style></head><body><main><p class="tag">BARDBOX / WI-FI DEMO</p>
<article><h1>The answer</h1><p>Latest accepted demo reading</p>
<div class="value">{value}</div><dl><dt>Last arrival</dt><dd>{arrival}</dd>
<dt>Accepted uploads this session</dt><dd>{count}</dd></dl></article>
<p>Refreshes every 3 seconds. Arrival time is when this Mac received the upload,
not when the sensor measured it.</p>
<small>Synthetic data · No archive · Count includes retries and queued records.
The display resets when the receiver restarts.</small></main></body></html>""".encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass  # never log Authorization, query strings, or arbitrary request text

    def reply(self, code):
        self.send_response(code)
        if code != 204:
            self.send_header("Content-Length", "0")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True

    def do_GET(self):
        if self.path not in ("/", "/demo/wifi42"):
            return self.reply(404)
        body = viewing_page(*self.server.demo_state.snapshot())
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def do_POST(self):
        self.connection.settimeout(3)
        if self.path != "/demo/wifi42":
            return self.reply(404)
        expected = "Bearer " + self.server.demo_token
        if not hmac.compare_digest(self.headers.get("Authorization", "").encode(), expected.encode()):
            return self.reply(403)
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get("Transfer-Encoding") or len(lengths) != 1:
            return self.reply(400)
        try:
            length = int(lengths[0])
            if not 0 < length <= MAX_BODY:
                return self.reply(413)
            if self.headers.get("Content-Type", "").split(";", 1)[0].strip() != "application/json":
                return self.reply(415)
            raw = self.rfile.read(length)
            if len(raw) != length:
                return self.reply(400)
            payload = json.loads(raw)
        except (ValueError, UnicodeError, OSError):
            return self.reply(400)
        if not is_wifi42(payload):
            return self.reply(422)
        self.server.demo_state.accept()
        print("Received synthetic demo.answer = 42 (volatile diagnostic acceptance only)", flush=True)
        self.reply(204)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8443)
    args = parser.parse_args()
    token = os.environ.get("BARDBOX_DEMO_TOKEN", "")
    if not 16 <= len(token) <= 256 or any(ord(c) < 33 or ord(c) > 126 for c in token):
        parser.error("Set BARDBOX_DEMO_TOKEN to a private 16..256 character visible-ASCII token")
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.set_ciphers("ECDHE+AESGCM:ECDHE+CHACHA20")
    context.load_cert_chain(args.cert, args.key)
    with ThreadingHTTPServer((args.bind, args.port), Handler) as server:
        server.demo_token = token
        server.demo_state = DemoState()
        server.socket = context.wrap_socket(server.socket, server_side=True)
        print("Synthetic wifi42 receiver ready; no archive storage. Stop with Ctrl-C.", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass

if __name__ == "__main__":
    main()
