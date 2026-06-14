#!/usr/bin/env python3
"""Capture the Tab5 screen to a PNG by talking to the device serial port DIRECTLY.

Use this ONLY when no bridge daemon is running (i.e. the USB-CDC port is free —
e.g. you stopped cc-bridge/cursor-bridge to flash firmware). When a daemon IS
running it owns the port exclusively, so use tools/tab5-shot/shot.py instead
(it asks the daemon over its unix socket).

Protocol (same as the daemon uses): send {"cmd":"shot"} on the serial line; the
firmware replies with `SHOT <w> <h> <len>`, then base64 chunks, then `ENDSHOT`.
The frame is the framebuffer downsampled x2 (1280x720 -> 640x360), RGB565
little-endian. Decode + PNG via the shared buddy_core helpers.

Usage:
  python3 tools/tab5-shot/shot_direct.py [--port /dev/cu.usbmodemXXXX] [--out PATH]

Requires pyserial (present in the cc-bridge / cursor-bridge venvs):
  ~/.cc-bridge/venv/bin/python3 tools/tab5-shot/shot_direct.py
"""
import argparse
import base64
import glob
import os
import sys
import time

# reuse the daemon's RGB565->RGB888 + stdlib PNG writer (tools/ on path)
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from buddy_core.core import _rgb565_to_rgb888, _write_png  # noqa: E402


def _default_port() -> str:
    env = os.environ.get("TAB5_SERIAL")
    if env:
        return env
    hits = sorted(glob.glob("/dev/cu.usbmodem*"))
    return hits[0] if hits else "/dev/cu.usbmodem2101"


def main() -> int:
    ap = argparse.ArgumentParser(description="Capture the Tab5 screen directly over serial.")
    ap.add_argument("--port", default=_default_port(), help="USB-CDC serial port")
    ap.add_argument("--out", default="/tmp/tab5-shot.png", help="output PNG path")
    ap.add_argument("--timeout", type=float, default=15.0, help="seconds to wait for the frame")
    a = ap.parse_args()

    try:
        import serial  # pyserial
    except ImportError:
        print("pyserial missing — run with a venv that has it, e.g.\n"
              "  ~/.cc-bridge/venv/bin/python3 tools/tab5-shot/shot_direct.py",
              file=sys.stderr)
        return 2

    try:
        ser = serial.Serial(a.port, 115200, timeout=2)
    except Exception as e:  # noqa: BLE001
        print(f"cannot open {a.port}: {e}\n"
              "(is a bridge daemon holding it? then use shot.py instead)",
              file=sys.stderr)
        return 2

    w = h = 0
    b64: list[str] = []
    capturing = False
    try:
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.write(b'{"cmd":"shot"}\n')
        ser.flush()
        deadline = time.time() + a.timeout
        while time.time() < deadline:
            line = ser.readline()
            if not line:
                continue
            text = line.decode("utf-8", "ignore").strip()
            if capturing:
                if text == "ENDSHOT":
                    break
                b64.append(text)
                continue
            if text.startswith("SHOT "):
                parts = text.split()
                w, h = int(parts[1]), int(parts[2])
                b64 = []
                capturing = True
    except serial.SerialException as e:
        print(f"serial read failed: {e}\n"
              "A bridge daemon is probably sharing this port — use shot.py instead "
              "(it routes through the running daemon).", file=sys.stderr)
        return 2
    finally:
        ser.close()

    raw = base64.b64decode("".join(b64)) if b64 else b""
    if not (w and h and len(raw) >= w * h * 2):
        print(f"incomplete frame: got {len(raw)} bytes, want {w * h * 2} ({w}x{h})",
              file=sys.stderr)
        return 1
    _write_png(a.out, w, h, _rgb565_to_rgb888(raw, w, h))
    print(a.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
