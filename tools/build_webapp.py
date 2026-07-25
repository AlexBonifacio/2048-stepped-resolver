#!/usr/bin/env python3

"""Assemble the static web version into web-dist/.

Copies the browser-side files from web/ (leaving out the local-server
Python pieces), the WASM solver build, and bundles data/sessions/*.json
into seed-sessions.json so a first-time visitor starts with the project's
learned spawn data.

Run tools/build_wasm.sh first (the CI workflow does both).
"""

import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
DIST = ROOT / "web-dist"

PAGE_FILES = [
    "index.html",
    "styles.css",
    "app.js",
    "backend.js",
    "recognize.js",
    "digit_templates.json",
]

SOLVER_FILES = [
    "2048-ranks.mjs",
    "2048-ranks.wasm",
    "solver-api.mjs",
    "solver-worker.mjs",
]


def main():
    if DIST.exists():
        shutil.rmtree(DIST)
    (DIST / "solver").mkdir(parents=True)

    for name in PAGE_FILES:
        shutil.copy2(WEB / name, DIST / name)

    for name in SOLVER_FILES:
        source = WEB / "solver" / name
        if not source.exists():
            raise SystemExit(f"Missing {source}: run tools/build_wasm.sh first.")
        shutil.copy2(source, DIST / "solver" / name)

    seed = {}
    for path in sorted((ROOT / "data" / "sessions").glob("*.json")):
        try:
            seed[path.stem] = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            continue
    (DIST / "seed-sessions.json").write_text(json.dumps(seed) + "\n", encoding="utf-8")

    total = sum(f.stat().st_size for f in DIST.rglob("*") if f.is_file())
    print(f"web-dist ready: {len(seed)} seed session(s), {total / (1024 * 1024):.2f} MiB total")


if __name__ == "__main__":
    main()
