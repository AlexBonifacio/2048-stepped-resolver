#!/usr/bin/env python3

"""Board recognition from raw screen pixels, in pure Python.

Each game tile shows a beige digit badge in its bottom-right corner whose
value is directly the rank. Recognition thresholds the warm beige pixels
(adaptively, because the game can be dimmed by overlays), segments the
digits, and matches them against templates built from reference tiles by
tools/build_digit_templates.py.
"""

import json

SIZE = 4
TEMPLATE_WIDTH = 12
TEMPLATE_HEIGHT = 20

# Digit search window inside a cell, as fractions of the cell box.
WINDOW_LEFT = 0.40
WINDOW_TOP = 0.45

# Adaptive brightness: keep pixels at least this fraction of the brightest
# warm pixel of the window, with an absolute floor to reject empty cells.
# Reading a cell starts strict (digits are brighter than warm icon parts
# such as cardboard boxes) and only relaxes when nothing readable comes out.
BRIGHTNESS_RATIO = 0.62
BRIGHTNESS_RATIOS = (0.85, 0.75, BRIGHTNESS_RATIO)
BRIGHTNESS_FLOOR = 110

MIN_COMPONENT_PIXELS = 12
MIN_MATCH_SCORE = 0.72
GOOD_MATCH_SCORE = 0.8
MAX_RANK = 16


def warm_shape(r, g, b):
    """True when the hue matches the beige digit font (low-saturation warm)."""
    if r < g or g + 6 < b:
        return False
    spread = (r - b) / r if r else 0.0
    tilt = (r - g) / r if r else 0.0
    return 0.04 <= spread <= 0.36 and tilt <= 0.16


def extract_digit_segments(pixels, width, height, x0, y0, x1, y1, ratio=BRIGHTNESS_RATIO):
    """Return digit pixel groups (left to right) found in a cell box."""
    wx0 = x0 + int((x1 - x0) * WINDOW_LEFT)
    wy0 = y0 + int((y1 - y0) * WINDOW_TOP)
    wx1, wy1 = x1, y1

    brightest = 0
    for y in range(wy0, wy1):
        row = y * width
        for x in range(wx0, wx1):
            r, g, b = pixels[row + x][:3]
            if r > brightest and warm_shape(r, g, b):
                brightest = r
    floor = max(BRIGHTNESS_FLOOR, int(brightest * ratio))
    if brightest < BRIGHTNESS_FLOOR:
        return []

    window_width = wx1 - wx0
    window_height = wy1 - wy0
    mask = [[False] * window_width for _ in range(window_height)]
    for y in range(window_height):
        row = (wy0 + y) * width
        for x in range(window_width):
            r, g, b = pixels[row + wx0 + x][:3]
            mask[y][x] = r >= floor and warm_shape(r, g, b)

    components = connected_components(mask)
    components = [c for c in components if len(c) >= MIN_COMPONENT_PIXELS]
    if not components:
        return []

    tallest = max(component_height(c) for c in components)
    if tallest < window_height * 0.12:
        return []
    kept = [c for c in components if component_height(c) >= tallest * 0.5]

    # The digit badge always sits at the bottom of the corner window, while
    # icon parts (box faces, glints) float higher. Anchor on the bottom-most
    # tall component and keep only components sharing its vertical band.
    reference = max(kept, key=lambda c: max(p[1] for p in c))
    ref_top = min(p[1] for p in reference)
    ref_bottom = max(p[1] for p in reference)
    margin = (ref_bottom - ref_top) * 0.6
    kept = [
        c for c in kept
        if min(p[1] for p in c) >= ref_top - margin and max(p[1] for p in c) <= ref_bottom + margin
    ]

    return merge_overlapping_columns(kept)


def connected_components(mask):
    height, width = len(mask), len(mask[0]) if mask else 0
    seen = [[False] * width for _ in range(height)]
    components = []
    for y in range(height):
        for x in range(width):
            if not mask[y][x] or seen[y][x]:
                continue
            stack = [(x, y)]
            seen[y][x] = True
            pixels = []
            while stack:
                cx, cy = stack.pop()
                pixels.append((cx, cy))
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < width and 0 <= ny < height and mask[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            stack.append((nx, ny))
            components.append(pixels)
    return components


def component_height(component):
    ys = [p[1] for p in component]
    return max(ys) - min(ys) + 1


def merge_overlapping_columns(components):
    """Merge components whose column ranges overlap (a digit split by
    anti-aliasing, like the bar and stroke of a 7), then sort left to right."""
    intervals = []
    for component in components:
        xs = [p[0] for p in component]
        intervals.append([min(xs), max(xs), list(component)])
    intervals.sort(key=lambda item: item[0])

    merged = []
    for left, right, pixels in intervals:
        if merged and left <= merged[-1][1] + 1:
            merged[-1][1] = max(merged[-1][1], right)
            merged[-1][2].extend(pixels)
        else:
            merged.append([left, right, pixels])
    return [pixels for _, _, pixels in merged]


def normalize_segment(segment):
    """Resample a digit pixel group to a TEMPLATE_WIDTH x TEMPLATE_HEIGHT
    bitmap, returned as a list of '01' strings."""
    xs = [p[0] for p in segment]
    ys = [p[1] for p in segment]
    left, top = min(xs), min(ys)
    box_width = max(xs) - left + 1
    box_height = max(ys) - top + 1
    filled = set(segment)

    rows = []
    for ty in range(TEMPLATE_HEIGHT):
        y0 = top + ty * box_height // TEMPLATE_HEIGHT
        y1 = max(y0 + 1, top + (ty + 1) * box_height // TEMPLATE_HEIGHT)
        row = []
        for tx in range(TEMPLATE_WIDTH):
            x0 = left + tx * box_width // TEMPLATE_WIDTH
            x1 = max(x0 + 1, left + (tx + 1) * box_width // TEMPLATE_WIDTH)
            hit = any((x, y) in filled for y in range(y0, y1) for x in range(x0, x1))
            row.append("1" if hit else "0")
        rows.append("".join(row))
    return rows


def match_digit(bitmap, templates):
    """Return (digit, score) of the best template for a normalized bitmap."""
    best_digit, best_score = None, -1.0
    total = TEMPLATE_WIDTH * TEMPLATE_HEIGHT
    for digit, candidates in templates.items():
        for candidate in candidates:
            same = sum(
                1
                for row, other in zip(bitmap, candidate)
                for a, b in zip(row, other)
                if a == b
            )
            score = same / total
            if score > best_score:
                best_digit, best_score = digit, score
    return best_digit, best_score


def load_templates(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    return {digit: bitmaps for digit, bitmaps in data.get("digits", {}).items()}


def pixels_from_bgra(raw, width, height):
    """Convert an mss BGRA buffer to a flat list of (r, g, b) tuples."""
    view = memoryview(raw)
    return [
        (view[offset + 2], view[offset + 1], view[offset])
        for offset in range(0, width * height * 4, 4)
    ]


def read_cell(pixels, width, height, box, templates):
    """Read one cell, trying strict brightness thresholds first.

    Returns (rank, confidence): rank 0 for an empty cell, None when the cell
    holds something unreadable.
    """
    x0, y0, x1, y1 = box
    found_anything = False
    best_rank, best_score = None, 0.0
    for ratio in BRIGHTNESS_RATIOS:
        segments = extract_digit_segments(pixels, width, height, x0, y0, x1, y1, ratio)
        if not segments:
            continue
        found_anything = True
        digits = []
        worst = 1.0
        for segment in segments:
            digit, score = match_digit(normalize_segment(segment), templates)
            worst = min(worst, score)
            digits.append(digit if digit is not None else "?")
        if "?" in digits:
            continue
        rank = int("".join(digits))
        if rank < 1 or rank > MAX_RANK:
            continue
        if worst >= GOOD_MATCH_SCORE:
            return rank, worst
        if worst > best_score:
            best_rank, best_score = rank, worst
    if best_rank is not None and best_score >= MIN_MATCH_SCORE:
        return best_rank, best_score
    if found_anything:
        return None, best_score
    return 0, 1.0


def recognize_board(pixels, width, height, templates):
    """Split the calibrated board area into 4x4 cells and read each rank.

    Returns {cells, confidences}: rank 0 with confidence 1.0 for empty cells,
    rank None for unreadable ones.
    """
    cells = []
    confidences = []
    for row in range(SIZE):
        for column in range(SIZE):
            box = (
                column * width // SIZE,
                row * height // SIZE,
                (column + 1) * width // SIZE,
                (row + 1) * height // SIZE,
            )
            rank, confidence = read_cell(pixels, width, height, box, templates)
            cells.append(rank)
            confidences.append(round(confidence, 4))
    return {"cells": cells, "confidences": confidences}
