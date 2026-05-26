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

from control_plane.cmux_control import (  # noqa: E402
    BOARD_REGISTRY_DIR,
    CmuxClient,
    DEFAULT_CMUX,
)


def build_board(client) -> list[dict]:
    """Return [{nickname,number,title,cwd,selected,status}] for each cmux session.

    Pure of argv/printing — takes any object exposing list_sessions() +
    read_surface(surface), so it is unit-testable with a fake client. Reads
    each pane directly by surface UUID (one list_sessions(), not one per row).
    The user-facing identifier is `nickname` (stable across pane open/close);
    `number` is retained for layout and legacy callers.
    """
    rows = []
    for s in client.list_sessions():
        try:
            status = client.read_surface(s.surface)
        except Exception:
            status = ""
        rows.append({
            "nickname": s.nickname,
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
        nick = (r.get("nickname") or "").ljust(8)
        lines.append(f"{mark} {nick}{r['title'][:38]:38}  {r['status'][:48]}")
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
        nick = (r.get("nickname") or "").ljust(8)
        head = f'{"*" if sel else " "} {nick}{r["title"][:24]}'
        line = f"{head}  ➜ {_short_cwd(r['cwd'])}"
        out.append(f"{_SELECTED}{line}{_RESET}" if color and sel else line)
        status = r["status"][: width - 6]
        if status:
            out.append(f"        {status}")
    out.append(bar)
    out.append('say "alpha <cmd>" / "bravo …"  →  👍  /  confirm.py')
    return "\n".join(out)


def register_self_as_board(surface_id: str) -> None:
    """Touch a marker file so the enumerator skips this pane.

    cmux overwrites surface titles based on rendered content (and ignores OSC
    escape sequences), so the title-marker fallback alone misses the live board
    pane once the watcher starts drawing frames. The registry under
    BOARD_REGISTRY_DIR is the durable signal — one empty file per live board
    pane, keyed by surface UUID. Removed via atexit on clean shutdown.
    """
    import atexit
    sid = (surface_id or "").strip()
    if not sid:
        return
    BOARD_REGISTRY_DIR.mkdir(parents=True, exist_ok=True)
    marker = BOARD_REGISTRY_DIR / sid
    try:
        marker.touch(exist_ok=True)
    except OSError:
        return
    atexit.register(lambda: marker.unlink(missing_ok=True))


def watch(client, interval: float = 2.0, color: bool = True,
          self_surface: str = "") -> None:
    """Re-render the board every `interval` seconds until Ctrl-C.

    Pass `self_surface` (this pane's cmux surface UUID, supplied by the
    launcher) so the enumerator excludes this pane from the session list.
    """
    register_self_as_board(self_surface)
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
    ap.add_argument("--self-surface", default=os.environ.get("CONTROL_PLANE_BOARD_SURFACE", ""),
                    help="this pane's cmux surface UUID (so the board excludes itself)")
    args = ap.parse_args()

    cmux = shutil.which("cmux") or DEFAULT_CMUX
    if not os.path.exists(cmux):
        print("cmux not installed")
        return 0

    client = CmuxClient(binary=cmux)
    if args.watch:
        watch(client, interval=args.interval, color=sys.stdout.isatty(),
              self_surface=args.self_surface)
        return 0

    rows = build_board(client)
    print(json.dumps(rows, indent=2) if args.json else render_text(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
