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

# Default board width when no terminal is attached (JSON output, pipes, tests).
# The watch loop overrides this from the live terminal size each frame so the
# board fills the pane and status lines don't get clipped at 50 chars.
_WIDTH = 80
_MIN_WIDTH = 56
_MAX_WIDTH = 140

# ANSI sequences. Truncation is always computed on the plain visible text so
# fixed-width columns line up; colors are wrapped around the already-sized
# segments.
_CLEAR    = "\033[2J\033[H"   # clear screen + home cursor
_HIDE     = "\033[?25l"
_SHOW     = "\033[?25h"
_RESET    = "\033[0m"
_BOLD     = "\033[1m"
_DIM      = "\033[2m"
_REVERSE  = "\033[7m"
_FG_CYAN     = "\033[36m"
_FG_GREEN    = "\033[32m"
_FG_YELLOW   = "\033[33m"
_FG_BLUE     = "\033[34m"
_FG_MAGENTA  = "\033[35m"

# Per-status-glyph color (what each Claude Code signal "means" at a glance).
_STATUS_COLOR = {
    "✻": _FG_YELLOW,   # actively thinking
    "⏺": _FG_GREEN,    # just spoke
    "❯": _FG_BLUE,     # user prompt
    ">": _FG_BLUE,
    "※": _DIM,         # idle recap (most muted on purpose)
}


def _short_cwd(cwd: str) -> str:
    home = os.path.expanduser("~")
    return "~" + cwd[len(home):] if cwd.startswith(home) else cwd


def _wrap(s: str, *codes: str) -> str:
    """Wrap `s` in the given ANSI codes (no-op if `s` empty)."""
    if not s:
        return s
    return "".join(codes) + s + _RESET


def render_board(rows: list[dict], width: int = _WIDTH, color: bool = True) -> str:
    """Two lines per session (header + status), header bar with a clock.

    `color=False` drops ANSI so the output is plain (tests, pipes, non-TTY).
    Colour hierarchy: nicknames stand out (bold cyan), focus marker is bright
    green, status glyphs are colored by meaning (✻ yellow, ⏺ green, ❯ blue,
    ※ dim), cwd/separators/footer are dim grey so they recede.
    """
    bar_plain = "─" * width
    clock = datetime.now().strftime("%H:%M:%S")
    title = "FLEET BOARD"
    header_plain = title + clock.rjust(width - len(title))

    out: list[str] = []
    if color:
        out.append(f"{_BOLD}{title}{_RESET}{_DIM}{clock.rjust(width - len(title))}{_RESET}")
        out.append(_wrap(bar_plain, _DIM))
    else:
        out.append(header_plain)
        out.append(bar_plain)

    if not rows:
        out.append(_wrap("  (no cmux sessions)", _DIM) if color else "  (no cmux sessions)")
    for r in rows:
        sel = bool(r["selected"])
        nick = (r.get("nickname") or "?").ljust(8)
        title_trunc = (r["title"] or "")[:24]
        cwd = _short_cwd(r["cwd"] or "")
        focus_mark = "▶" if sel else " "

        if color:
            # Same visible layout for every row — mark(1) + ␣(1) + nick(8) —
            # so titles and arrows line up regardless of focus. Focused row
            # gets a bright-green ▶ and reverse-video on its bold-cyan
            # nickname; the inverse block reads as a chip without disturbing
            # the column grid.
            mark = (f"{_BOLD}{_FG_GREEN}{focus_mark}{_RESET}"
                    if sel else focus_mark)
            nick_disp = (f"{_REVERSE}{_BOLD}{_FG_CYAN}{nick}{_RESET}"
                         if sel else f"{_BOLD}{_FG_CYAN}{nick}{_RESET}")
            line = f"{mark} {nick_disp}{title_trunc}  {_DIM}➜ {cwd}{_RESET}"
        else:
            line = f'{focus_mark} {nick}{title_trunc}  ➜ {cwd}'
        out.append(line)

        # Status line — truncated on visible width, then colored by glyph.
        status_plain = (r["status"] or "")[: max(0, width - 10)]
        if status_plain:
            head = status_plain[:1]
            if color:
                status_disp = _wrap(status_plain, _STATUS_COLOR.get(head, _DIM))
            else:
                status_disp = status_plain
            out.append(f"        {status_disp}")

    out.append(_wrap(bar_plain, _DIM) if color else bar_plain)
    footer = 'say "alpha <cmd>" / "bravo …"  →  👍  /  confirm.py'
    out.append(_wrap(footer, _DIM) if color else footer)
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


def _term_width() -> int:
    """Live terminal width, clamped to a sensible range for the board."""
    cols = shutil.get_terminal_size((_WIDTH, 24)).columns
    return max(_MIN_WIDTH, min(_MAX_WIDTH, cols - 2))


def watch(client, interval: float = 2.0, color: bool = True,
          self_surface: str = "") -> None:
    """Re-render the board every `interval` seconds until Ctrl-C.

    Pass `self_surface` (this pane's cmux surface UUID, supplied by the
    launcher) so the enumerator excludes this pane from the session list.
    Board width is sampled from the live terminal each frame so the layout
    follows pane resizes.
    """
    register_self_as_board(self_surface)
    try:
        if color:
            sys.stdout.write(_HIDE)
        while True:
            board = render_board(build_board(client), width=_term_width(), color=color)
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
