"""Commit / cancel the staged voice-control-plane command by hand.

The keyboard/CLI fallback for the thumbs-up gesture — and what makes the
voice->stage->commit loop testable without the camera. Talks to the daemon's
unix socket (same `confirm_route`/`cancel_route` actions the gesture path uses).

  python -m control_plane.confirm           # 👍 commit the staged command
  python -m control_plane.confirm cancel     # 👎 cancel it
"""
from __future__ import annotations

import json
import os
import socket
import sys

SOCKET = os.environ.get("CONTROL_PLANE_SOCKET", "/tmp/cc-bridge.sock")


def main() -> int:
    cancel = len(sys.argv) > 1 and sys.argv[1].lower().startswith("c")
    action = "cancel_route" if cancel else "confirm_route"
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(SOCKET)
        s.sendall((json.dumps({"action": action}) + "\n").encode())
        resp = s.recv(4096).decode().strip()
        s.close()
    except OSError as e:
        print(f"error: can't reach daemon at {SOCKET}: {e}")
        print("(is the cc-bridge daemon running?)")
        return 1
    print(f"{action} -> {resp}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
