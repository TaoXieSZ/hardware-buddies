"""Mac-side session board: numbered cmux sessions + a status line each.

  python -m control_plane.board          # text board
  python -m control_plane.board --json   # machine-readable

The numbers shown here are what you say to the voice secretary ("two, run the
tests"). Status is each session's last non-empty screen line (via read-screen).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from control_plane.cmux_control import CmuxClient, DEFAULT_CMUX  # noqa: E402


def build_board(client) -> list[dict]:
    """Return [{number,title,cwd,selected,status}] for each cmux session.

    Pure of argv/printing — takes any object exposing list_sessions() +
    read_status(number), so it is unit-testable with a fake client.
    """
    rows = []
    for s in client.list_sessions():
        try:
            status = client.read_status(s.number)
        except Exception:
            status = ""
        rows.append({
            "number": s.number,
            "title": s.title,
            "cwd": s.cwd,
            "selected": s.selected,
            "status": status,
        })
    return rows


def render_text(rows: list[dict]) -> str:
    if not rows:
        return "(no cmux sessions)"
    lines = []
    for r in rows:
        mark = "*" if r["selected"] else " "
        lines.append(f"{mark} [{r['number']}] {r['title'][:38]:38}  {r['status'][:48]}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()

    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("cmux not installed")
        return 0

    rows = build_board(CmuxClient(binary=cmux))
    print(json.dumps(rows, indent=2) if args.json else render_text(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
