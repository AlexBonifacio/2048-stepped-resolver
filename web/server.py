#!/usr/bin/env python3

import argparse
import json
import mimetypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse


ROOT = Path(__file__).resolve().parents[1]
WEB_ROOT = Path(__file__).resolve().parent
SESSION_DIR = ROOT / "data" / "sessions"


def session_path(name):
    safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in name)
    if not safe:
        safe = "default"
    return SESSION_DIR / f"{safe}.json"


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/session":
            self.handle_get_session(parsed)
            return
        self.serve_static(parsed.path)

    def do_POST(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/session":
            self.handle_save_session(parsed)
            return
        if parsed.path == "/api/new-session":
            self.handle_new_session(parsed)
            return
        self.send_error(404)

    def handle_get_session(self, parsed):
        name = parse_qs(parsed.query).get("name", ["try2"])[0]
        path = session_path(name)
        if not path.exists():
            self.write_json({"error": f"Session introuvable: {path.relative_to(ROOT)}"}, status=404)
            return
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            self.write_json({"error": f"JSON invalide: {exc}"}, status=500)
            return
        self.write_json({"name": name, "path": str(path.relative_to(ROOT)), "data": data})

    def handle_save_session(self, parsed):
        name = parse_qs(parsed.query).get("name", ["try2"])[0]
        length = int(self.headers.get("Content-Length", "0"))
        try:
            data = json.loads(self.rfile.read(length).decode("utf-8"))
        except json.JSONDecodeError as exc:
            self.write_json({"error": f"JSON invalide: {exc}"}, status=400)
            return

        SESSION_DIR.mkdir(parents=True, exist_ok=True)
        path = session_path(name)
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        self.write_json({"ok": True, "path": str(path.relative_to(ROOT))})

    def handle_new_session(self, parsed):
        name = parse_qs(parsed.query).get("name", [""])[0]
        if not name.strip():
            self.write_json({"error": "Nom de session requis."}, status=400)
            return

        path = session_path(name)
        if path.exists():
            self.write_json({"error": f"La session existe deja: {path.relative_to(ROOT)}"}, status=409)
            return

        data = {
            "moves": 0,
            "score": 0,
            "cells": [0] * 16,
            "map": [[0, 0, 0, 0] for _ in range(4)],
            "observations": [],
            "spawns": {},
        }
        SESSION_DIR.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        self.write_json({"ok": True, "path": str(path.relative_to(ROOT)), "data": data})

    def serve_static(self, request_path):
        relative = "index.html" if request_path in ("", "/") else request_path.lstrip("/")
        path = (WEB_ROOT / relative).resolve()
        if WEB_ROOT not in path.parents and path != WEB_ROOT:
            self.send_error(403)
            return
        if not path.exists() or not path.is_file():
            self.send_error(404)
            return

        body = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def write_json(self, payload, status=200):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        print(f"{self.address_string()} - {fmt % args}")


def main():
    parser = argparse.ArgumentParser(description="Mini serveur web pour jouer une session 2048 ranks.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"2048 ranks web: http://{args.host}:{args.port}/?session=try2")
    server.serve_forever()


if __name__ == "__main__":
    main()
