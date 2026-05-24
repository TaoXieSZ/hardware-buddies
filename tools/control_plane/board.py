"""Mac-side session board: numbered cmux sessions + a status line each.

  python -m control_plane.board          # one-shot text board
  python -m control_plane.board --watch  # live board, auto-refresh (glanceable)
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
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from control_plane.cmux_control import CmuxClient, DEFAULT_CMUX  # noqa: E402


def build_board(client) -> list[dict]:
    """Return [{number,title,cwd,selected,status}] for each cmux session.

    Pure of argv/printing — takes any object exposing list_sessions() +
    read_surface(surface), so it is unit-testable with a fake client. Reads each
    pane directly by surface UUID (one list_sessions(), not one per row).
    """
    rows = []
    for s in client.list_sessions():
        try:
            status = client.read_surface(s.surface)
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


# --- live watch board ------------------------------------------------------

_WIDTH = 56
_CLEAR = "\033[2J\033[H"   # clear screen + home cursor
_HIDE = "\033[?25l"        # hide cursor
_SHOW = "\033[?25h"        # restore cursor
_SELECTED = "\033[7m"      # reverse video for the focused session
_RESET = "\033[0m"


def _short_cwd(cwd: str) -> str:
    home = os.path.expanduser("~")
    return "~" + cwd[len(home):] if cwd.startswith(home) else cwd


def render_board(rows: list[dict], width: int = _WIDTH, color: bool = True) -> str:
    """Two lines per session (header + status), header bar with a clock.

    `color=False` drops ANSI so the output is plain (tests, pipes, non-TTY).
    """
    bar = "─" * width
    clock = datetime.now().strftime("%H:%M:%S")
    out = ["FLEET BOARD" + clock.rjust(width - len("FLEET BOARD")), bar]
    if not rows:
        out.append("  (no cmux sessions)")
    for r in rows:
        sel = r["selected"]
        head = f'{"*" if sel else " "} [{r["number"]}] {r["title"][:24]}'
        line = f"{head}  ➜ {_short_cwd(r['cwd'])}"
        out.append(f"{_SELECTED}{line}{_RESET}" if color and sel else line)
        status = r["status"][: width - 6]
        if status:
            out.append(f"      {status}")
    out.append(bar)
    out.append('say "2 <cmd>" / "二号 <命令>"  →  👍  /  confirm.py')
    return "\n".join(out)


def watch(client, interval: float = 2.0, color: bool = True) -> None:
    """Re-render the board every `interval` seconds until Ctrl-C."""
    try:
        if color:
            sys.stdout.write(_HIDE)
        while True:
            board = render_board(build_board(client), color=color)
            sys.stdout.write(_CLEAR + board + "\n")
            sys.stdout.flush()
            time.sleep(interval)
    except KeyboardInterrupt:
        pass
    finally:
        if color:
            sys.stdout.write(_SHOW)
            sys.stdout.flush()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--watch", action="store_true", help="live auto-refreshing board")
    ap.add_argument("--interval", type=float, default=2.0, help="--watch refresh seconds")
    args = ap.parse_args()

    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("cmux not installed")
        return 0

    client = CmuxClient(binary=cmux)
    if args.watch:
        watch(client, interval=args.interval, color=sys.stdout.isatty())
        return 0

    rows = build_board(client)
    print(json.dumps(rows, indent=2) if args.json else render_text(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
