#!/usr/bin/env python3

"""Build web/digit_templates.json from reference tile captures.

Development-only tool: requires Pillow. Reads data/captures/tiles/<rank>.png
(rank 1..11), extracts the beige digit badge of each tile with the same
pipeline as web/recognize.py, and stores one normalized bitmap per digit.

Usage:
    python3 tools/build_digit_templates.py
"""

import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "web"))

import recognize

TILE_DIR = ROOT / "data" / "captures" / "tiles"
OUTPUT = ROOT / "web" / "digit_templates.json"


def tile_pixels(path):
    img = Image.open(path).convert("RGB")
    width, height = img.size
    return list(img.getdata()), width, height


def main():
    templates = {}
    for rank in range(1, 12):
        path = TILE_DIR / f"{rank}.png"
        if not path.exists():
            print(f"skip rank {rank}: {path} not found")
            continue
        pixels, width, height = tile_pixels(path)
        segments = recognize.extract_digit_segments(pixels, width, height, 0, 0, width, height)
        digits = str(rank)
        if len(segments) != len(digits):
            print(f"rank {rank}: expected {len(digits)} digit(s), found {len(segments)} segment(s)")
            continue
        for digit, segment in zip(digits, segments):
            bitmap = recognize.normalize_segment(segment)
            templates.setdefault(digit, []).append(bitmap)
            print(f"rank {rank}: digit {digit} ok")

    missing = [str(d) for d in range(10) if str(d) not in templates]
    if missing:
        print(f"WARNING: missing digits: {', '.join(missing)}")

    payload = {
        "width": recognize.TEMPLATE_WIDTH,
        "height": recognize.TEMPLATE_HEIGHT,
        "digits": {digit: bitmaps for digit, bitmaps in sorted(templates.items())},
    }
    OUTPUT.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)} ({sum(len(b) for b in templates.values())} template(s))")


if __name__ == "__main__":
    main()
