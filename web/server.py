#!/usr/bin/env python3

import argparse
import json
import mimetypes
import os
import subprocess
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, quote, urlparse


ROOT = Path(__file__).resolve().parents[1]
WEB_ROOT = Path(__file__).resolve().parent
SESSION_DIR = ROOT / "data" / "sessions"
SIMULATION_REPORT_DIR = ROOT / "data" / "simulation_reports"


def solver_binary():
    candidates = [ROOT / "2048-ranks.exe", ROOT / "2048-ranks"] if os.name == "nt" else [ROOT / "2048-ranks", ROOT / "2048-ranks.exe"]
    for path in candidates:
        if path.exists():
            return path
    return candidates[0]


SOLVER_BIN = solver_binary()


def session_path(name):
    safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in name)
    if not safe:
        safe = "default"
    return SESSION_DIR / f"{safe}.json"


def empty_session_data():
    return {
        "moves": 0,
        "score": 0,
        "cells": [0] * 16,
        "map": [[0, 0, 0, 0] for _ in range(4)],
        "observations": [],
        "spawns": {},
        "solved": [],
        "context_ready": False,
        "outcome": {"status": "in_progress", "target": 12},
    }


def ensure_session(name):
    path = session_path(name)
    if path.exists():
        return path
    SESSION_DIR.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(empty_session_data(), indent=2) + "\n", encoding="utf-8")
    return path


def session_summaries():
    sessions = []
    for path in sorted(SESSION_DIR.glob("*.json"), key=lambda item: item.stem.lower()):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            data = {}
        sessions.append({
            "name": path.stem,
            "score": int_value(data.get("score")),
            "moves": int_value(data.get("moves")),
            "highest": max((int_value(value) for value in data.get("cells", [])), default=0) if isinstance(data.get("cells"), list) else 0,
            "context_ready": bool(data.get("context_ready")),
        })
    return sessions


def int_value(value, fallback=0):
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return fallback


def score_move_references():
    refs = []
    for path in SESSION_DIR.glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        score = int_value(data.get("score"))
        moves = int_value(data.get("moves"))
        if score > 0 and moves > 0:
            refs.append({"score": score, "moves": moves, "source": path.stem})

    for path in SIMULATION_REPORT_DIR.glob("*.jsonl"):
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except OSError:
            continue
        for line in lines:
            if not line.strip():
                continue
            try:
                data = json.loads(line)
            except json.JSONDecodeError:
                continue
            for result in data.get("results", []):
                score = int_value(result.get("score"))
                moves = int_value(result.get("moves"))
                if score > 0 and moves > 0:
                    refs.append({"score": score, "moves": moves, "source": path.stem})
    return refs


def estimate_moves_from_score(score):
    score = int_value(score)
    if score <= 0:
        return {"score": score, "moves": 0, "confidence": "exact", "references": 0}

    refs = score_move_references()
    if not refs:
        return {
            "score": score,
            "moves": round(score / 20),
            "confidence": "fallback",
            "references": 0,
        }

    weighted_total = 0.0
    weight_sum = 0.0
    nearest = sorted(refs, key=lambda ref: abs(ref["score"] - score))[:5]
    for ref in refs:
        ratio_based_moves = ref["moves"] * (score / ref["score"])
        relative_distance = abs(ref["score"] - score) / max(score, ref["score"])
        weight = 1.0 / ((0.18 + relative_distance) ** 2)
        weighted_total += ratio_based_moves * weight
        weight_sum += weight

    estimate = round(weighted_total / weight_sum)
    close_count = sum(1 for ref in refs if abs(ref["score"] - score) / max(score, ref["score"]) <= 0.35)
    confidence = "high" if close_count >= 8 else "medium" if close_count >= 3 else "low"
    return {
        "score": score,
        "moves": max(0, estimate),
        "confidence": confidence,
        "references": len(refs),
        "nearest": nearest,
    }


def compact_solved_arrays(text):
    lines = text.splitlines()
    compacted = []
    index = 0
    while index < len(lines):
        stripped = lines[index].lstrip()
        if stripped.startswith('"before": [') or stripped.startswith('"after": ['):
            indent = lines[index][:len(lines[index]) - len(stripped)]
            key = stripped.split(":", 1)[0]
            values = []
            index += 1
            while index < len(lines):
                value_line = lines[index].strip()
                if value_line.startswith("]"):
                    suffix = "," if value_line.endswith(",") else ""
                    compacted.append(f"{indent}{key}: [{', '.join(values)}]{suffix}")
                    break
                values.append(value_line.rstrip(","))
                index += 1
        else:
            compacted.append(lines[index])
        index += 1
    return "\n".join(compacted) + "\n"


def outcome_summary():
    sessions = []
    direction_stats = {}
    source_stats = {}
    for path in SESSION_DIR.glob("*.json"):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
        outcome = data.get("outcome") if isinstance(data.get("outcome"), dict) else {}
        if outcome.get("status") != "ended":
            continue

        solved = data.get("solved") if isinstance(data.get("solved"), list) else []
        success = bool(outcome.get("success"))
        final_highest = int_value(outcome.get("final_highest"))
        target = int_value(outcome.get("target"), 12)
        sessions.append({
            "session": path.stem,
            "success": success,
            "target": target,
            "final_highest": final_highest,
            "final_score": int_value(outcome.get("final_score")),
            "final_moves": int_value(outcome.get("final_moves")),
            "solved_moves": len(solved),
        })

        for move in solved:
            direction = move.get("direction")
            if not isinstance(direction, str):
                continue
            source = move.get("source") if isinstance(move.get("source"), str) else "human"
            source_stats[source] = source_stats.get(source, 0) + 1
            stats = direction_stats.setdefault(direction, {
                "count": 0,
                "successes": 0,
                "avg_final_highest": 0.0,
                "avg_distance_to_target": 0.0,
            })
            stats["count"] += 1
            stats["successes"] += 1 if success else 0
            stats["avg_final_highest"] += final_highest
            stats["avg_distance_to_target"] += max(0, target - final_highest)

    for stats in direction_stats.values():
        if stats["count"] > 0:
            stats["avg_final_highest"] = round(stats["avg_final_highest"] / stats["count"], 3)
            stats["avg_distance_to_target"] = round(stats["avg_distance_to_target"] / stats["count"], 3)
            stats["success_rate"] = round(stats["successes"] / stats["count"], 4)

    return {
        "sessions": sessions,
        "ended_sessions": len(sessions),
        "successful_sessions": sum(1 for session in sessions if session["success"]),
        "source_stats": source_stats,
        "direction_stats": direction_stats,
    }


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/session":
            self.handle_get_session(parsed)
            return
        if parsed.path == "/api/sessions":
            self.handle_get_sessions(parsed)
            return
        if parsed.path == "/api/suggestion":
            self.handle_get_suggestion(parsed)
            return
        if parsed.path == "/api/estimate-moves":
            self.handle_estimate_moves(parsed)
            return
        if parsed.path == "/api/outcome-summary":
            self.handle_outcome_summary(parsed)
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
            self.write_json({"error": f"Session not found: {path.relative_to(ROOT)}"}, status=404)
            return
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            self.write_json({"error": f"Invalid JSON: {exc}"}, status=500)
            return
        self.write_json({"name": name, "path": str(path.relative_to(ROOT)), "data": data})

    def handle_get_sessions(self, parsed):
        self.write_json({"ok": True, "sessions": session_summaries()})

    def handle_get_suggestion(self, parsed):
        query = parse_qs(parsed.query)
        name = query.get("name", ["try2"])[0]
        solver = query.get("solver", ["hybrid"])[0]
        quality = query.get("quality", ["godlike"])[0]
        model_session = query.get("model_session", [""])[0]
        model_stats = query.get("model_stats", ["sessions"])[0]
        target = query.get("target", ["12"])[0]
        try:
            timeout = int(query.get("timeout", ["45"])[0])
        except ValueError:
            timeout = 45

        if solver not in ("expectimax", "optimistic", "human", "hybrid", "target"):
            self.write_json({"ok": False, "error": "Invalid solver."}, status=400)
            return
        if quality not in ("fast", "balanced", "strong", "godlike"):
            self.write_json({"ok": False, "error": "Invalid quality."}, status=400)
            return
        if model_stats not in ("global", "session", "sessions", "merged"):
            self.write_json({"ok": False, "error": "Invalid stats source."}, status=400)
            return
        if not SOLVER_BIN.exists():
            self.write_json({"ok": False, "error": "2048-ranks executable not found. Build it first."}, status=500)
            return

        command = [
            str(SOLVER_BIN),
            "--suggest-only",
            "--session", name,
            "--solver", solver,
            "--quality", quality,
            "--model-stats", model_stats,
            "--target", target,
        ]
        if model_session:
            command.extend(["--model-session", model_session])

        try:
            completed = subprocess.run(
                command,
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            self.write_json({"ok": False, "error": f"AI calculation timed out after {timeout}s."}, status=504)
            return
        except OSError as exc:
            self.write_json({
                "ok": False,
                "error": f"Could not start the AI executable: {exc}. On Windows, make sure 2048-ranks.exe matches your PC architecture and is not blocked by antivirus.",
            }, status=500)
            return

        try:
            payload = json.loads(completed.stdout)
        except json.JSONDecodeError:
            self.write_json({
                "ok": False,
                "error": "Invalid AI response.",
                "stdout": completed.stdout[-500:],
                "stderr": completed.stderr[-500:],
            }, status=500)
            return

        status = 200 if payload.get("ok") else 500
        self.write_json(payload, status=status)

    def handle_estimate_moves(self, parsed):
        query = parse_qs(parsed.query)
        score = query.get("score", ["0"])[0]
        self.write_json({"ok": True, **estimate_moves_from_score(score)})

    def handle_outcome_summary(self, parsed):
        self.write_json({"ok": True, **outcome_summary()})

    def handle_save_session(self, parsed):
        name = parse_qs(parsed.query).get("name", ["try2"])[0]
        length = int(self.headers.get("Content-Length", "0"))
        try:
            data = json.loads(self.rfile.read(length).decode("utf-8"))
        except json.JSONDecodeError as exc:
            self.write_json({"error": f"Invalid JSON: {exc}"}, status=400)
            return

        SESSION_DIR.mkdir(parents=True, exist_ok=True)
        path = session_path(name)
        path.write_text(compact_solved_arrays(json.dumps(data, indent=2)), encoding="utf-8")
        self.write_json({"ok": True, "path": str(path.relative_to(ROOT))})

    def handle_new_session(self, parsed):
        name = parse_qs(parsed.query).get("name", [""])[0]
        if not name.strip():
            self.write_json({"error": "Session name is required."}, status=400)
            return

        path = session_path(name)
        if path.exists():
            self.write_json({"error": f"Session already exists: {path.relative_to(ROOT)}"}, status=409)
            return

        data = empty_session_data()
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
    parser = argparse.ArgumentParser(description="Small web server for playing a 2048 ranks session.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--session", default="try2")
    parser.add_argument("--open", action="store_true", help="Ouvre automatiquement le navigateur.")
    args = parser.parse_args()

    ensure_session(args.session)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = (
        f"http://{args.host}:{args.port}/?session={quote(args.session)}"
        "&solver=hybrid&quality=godlike"
        "&model_stats=sessions&target=12&timeout=90"
    )
    print(f"2048 ranks web: {url}")
    if args.open:
        webbrowser.open(url)
    server.serve_forever()


if __name__ == "__main__":
    main()
