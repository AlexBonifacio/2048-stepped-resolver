#!/usr/bin/env python3

"""Screen capture helpers for the web server.

Uses the optional pure-Python ``mss`` package. When it is missing, the server
keeps working and the web page shows how to enable capture.
"""

import json

try:
    import mss
    import mss.tools
    MSS_IMPORT_ERROR = ""
except ImportError as exc:
    mss = None
    MSS_IMPORT_ERROR = str(exc)


def capture_available():
    return mss is not None


def capture_error_hint():
    if capture_available():
        return ""
    return (
        f"Screen capture is disabled: the mss package is not installed ({MSS_IMPORT_ERROR}). "
        "Install it with: pip install mss"
    )


def list_monitors():
    with mss.mss() as sct:
        monitors = []
        for index, monitor in enumerate(sct.monitors):
            monitors.append({
                "index": index,
                "left": monitor["left"],
                "top": monitor["top"],
                "width": monitor["width"],
                "height": monitor["height"],
                "all": index == 0,
            })
        return monitors


def grab_png(region):
    with mss.mss() as sct:
        shot = sct.grab(region)
        return mss.tools.to_png(shot.rgb, shot.size)


def grab_raw(region):
    """Grab a region and return its raw BGRA buffer with dimensions."""
    with mss.mss() as sct:
        shot = sct.grab(region)
        return shot.raw, shot.width, shot.height


def grab_monitor_png(index):
    with mss.mss() as sct:
        if index < 0 or index >= len(sct.monitors):
            raise ValueError(f"Monitor {index} does not exist.")
        shot = sct.grab(sct.monitors[index])
        return mss.tools.to_png(shot.rgb, shot.size)


def normalize_region(value):
    if not isinstance(value, dict):
        return None
    try:
        region = {key: int(value[key]) for key in ("left", "top", "width", "height")}
    except (KeyError, TypeError, ValueError):
        return None
    if region["width"] < 8 or region["height"] < 8:
        return None
    return region


def normalize_monitor(value):
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 1


def build_config(data):
    if not isinstance(data, dict):
        return None
    board = normalize_region(data.get("board"))
    if board is None:
        return None
    return {
        "monitor": normalize_monitor(data.get("monitor", 1)),
        "board": board,
        "score": normalize_region(data.get("score")),
    }


def load_config(path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        data = {}
    if not isinstance(data, dict):
        data = {}
    return {
        "monitor": normalize_monitor(data.get("monitor", 1)),
        "board": normalize_region(data.get("board")),
        "score": normalize_region(data.get("score")),
    }
