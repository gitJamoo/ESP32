#!/usr/bin/env python3
"""
Reads your Claude Code OAuth token, pings the Anthropic API, and extracts
the rate-limit utilization headers that reflect your real Claude.ai Pro usage.

Serves the result as JSON on a local HTTP endpoint so the ESP32 can poll it.

Usage:
    pip install requests
    python usage_server.py

Then set PC_HOST in src/config.h to your PC's local IP address.
Find it with:  ipconfig  (look for IPv4 Address under your WiFi adapter)
"""

import json
import re
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import requests

CREDENTIALS_PATH = Path.home() / ".claude" / ".credentials.json"
PORT = 8765
POLL_INTERVAL = 60  # seconds between API polls

API_URL = "https://api.anthropic.com/v1/messages"
API_HEADERS = {
    "anthropic-version": "2023-06-01",
    "anthropic-beta": "oauth-2025-04-20",
    "Content-Type": "application/json",
    "User-Agent": "claude-code/2.1.5",
}
API_BODY = {
    "model": "claude-haiku-4-5-20251001",
    "max_tokens": 1,
    "messages": [{"role": "user", "content": "hi"}],
}

latest = {"ok": False, "s": 0, "sr": 0, "w": 0, "wr": 0, "st": "unknown"}


def read_token():
    try:
        data = json.loads(CREDENTIALS_PATH.read_text())
    except Exception as e:
        print(f"  Could not read credentials: {e}")
        return None
    # direct: {"accessToken": "..."}
    if isinstance(data.get("accessToken"), str):
        return data["accessToken"]
    # nested: {"claudeAiOauth": {"accessToken": "..."}}
    for v in data.values():
        if isinstance(v, dict) and isinstance(v.get("accessToken"), str):
            return v["accessToken"]
    # regex fallback
    m = re.search(r'"accessToken"\s*:\s*"([^"]+)"', json.dumps(data))
    return m.group(1) if m else None


def poll():
    global latest
    token = read_token()
    if not token:
        print("  No token — is Claude Code installed and signed in?")
        return

    headers = {**API_HEADERS, "Authorization": f"Bearer {token}"}
    try:
        resp = requests.post(API_URL, headers=headers, json=API_BODY, timeout=20)
    except requests.RequestException as e:
        print(f"  API call failed: {e}")
        return

    if resp.status_code >= 400:
        print(f"  API error {resp.status_code}: {resp.text[:200]}")
        return

    now = time.time()

    def pct(name):
        try:
            return int(round(float(resp.headers.get(name, "0")) * 100))
        except (ValueError, TypeError):
            return 0

    def reset_min(name):
        try:
            r = float(resp.headers.get(name, "0"))
            return max(0, int(round((r - now) / 60)))
        except (ValueError, TypeError):
            return 0

    latest = {
        "ok": True,
        "s":  pct("anthropic-ratelimit-unified-5h-utilization"),
        "sr": reset_min("anthropic-ratelimit-unified-5h-reset"),
        "w":  pct("anthropic-ratelimit-unified-7d-utilization"),
        "wr": reset_min("anthropic-ratelimit-unified-7d-reset"),
        "st": resp.headers.get("anthropic-ratelimit-unified-5h-status", "ok"),
    }
    print(f"[{time.strftime('%H:%M:%S')}]  5h={latest['s']}% (resets {latest['sr']}m)  "
          f"7d={latest['w']}% (resets {latest['wr']}m)")


def poll_loop():
    while True:
        time.sleep(POLL_INTERVAL)
        poll()


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/usage":
            body = json.dumps(latest).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):
        pass  # suppress per-request logs


def main():
    print("=== Claude Usage Server ===")
    print(f"Credentials: {CREDENTIALS_PATH}")
    print(f"Poll interval: {POLL_INTERVAL}s")
    print()

    print("Initial poll...")
    poll()

    t = threading.Thread(target=poll_loop, daemon=True)
    t.start()

    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"\nServing on http://0.0.0.0:{PORT}/usage")
    print("Find your PC IP with: ipconfig")
    print("Then set PC_HOST in src/config.h to that IP.")
    print()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
