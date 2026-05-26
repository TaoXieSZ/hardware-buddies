"""Keyboard fleet driver — type what you would say, end-to-end test the
control-plane without voice/ASR/browser.

Same wire path as the voice-trigger Path B (stage_route socket action), so it
exercises RouteStager + cmux routing identically. Grammar accepts a NATO
phonetic nickname (alpha/bravo/…) as the addressee, the legacy session-number
markers (`2号` / `第二个` / `session 2`), or a `--then` follow-up that binds
the previous bare nickname/number to the next line.

  # one-shot (stage + commit — the fastest dev loop)
  python -m control_plane.say -y "alpha echo hi"
  python -m control_plane.say -y "bravo git status"

  # legacy number markers still work (back-compat with the voice path)
  python -m control_plane.say -y "二号 git status"

  # stage only (then 👍 / confirm.py to commit, mirrors the safety gate)
  python -m control_plane.say "alpha echo hi"

  # split across two inputs (two-step dictation)
  python -m control_plane.say -y --then "echo hi" "alpha"

  # REPL — type lines like the captain would speak them
  python -m control_plane.say --repl
"""
from __future__ import annotations

import argparse
import json
import os
import re
import socket
import sys
from typing import Optional, Union

SOCKET = os.environ.get("CONTROL_PLANE_SOCKET", "/tmp/cc-bridge.sock")

# Nickname pool (matches NATO in cmux_control.NATO). Listed here too so the
# parser is self-contained without importing cmux state — the daemon resolves
# the actual surface UUID at fire time.
NATO = (
    "alpha", "bravo", "charlie", "delta", "echo", "foxtrot", "golf", "hotel",
    "india", "juliet", "kilo", "lima", "mike", "november", "oscar", "papa",
    "quebec", "romeo", "sierra", "tango", "uniform", "victor", "whiskey",
    "xray", "yankee", "zulu",
)
_NATO_RE = "|".join(NATO)

# Legacy number markers (still accepted so existing voice/CLI lines keep
# working through the migration).
_CN = {"零":0,"一":1,"二":2,"两":2,"三":3,"四":4,"五":5,"六":6,"七":7,"八":8,"九":9,"十":10}
_EN = {"one":1,"two":2,"three":3,"four":4,"five":5,
       "six":6,"seven":7,"eight":8,"nine":9,"ten":10}
_NUM = r"(?:[0-9]+|[一二两三四五六七八九十]+|one|two|three|four|five|six|seven|eight|nine|ten)"

# Marker = a NATO nickname OR a legacy number marker. Capture groups:
#   1: bare nickname     2: 第N个    3: N号     4: session/会话/window N
_MARKER = (
    r"(?:秘书|助手|secretary|hey secretary|大副|first\s*mate)?[\s,，:：、]*"
    r"(?:"
    rf"({_NATO_RE})"                                          # 1: nickname
    rf"|第\s*({_NUM})\s*个"                                   # 2
    rf"|({_NUM})\s*号"                                        # 3
    rf"|(?:session|number|no\.?|窗口|会话)\s*({_NUM})"        # 4
    r")"
)
_TRAILER = r"[\s.,，、:：。!！?？]*"
_CMD_RE = re.compile(r"^" + _MARKER + r"[\s,，、:：]*(.+)$", re.IGNORECASE)
_SESS_ONLY_RE = re.compile(r"^" + _MARKER + _TRAILER + r"$", re.IGNORECASE)


def _tok_to_target(groups: tuple) -> Optional[str]:
    """Normalize whichever marker capture matched into a target STRING.

    Returns a NATO nickname lowercase, or a decimal string for legacy numeric
    markers. None if no marker captured.
    """
    nick, g2, g3, g4 = groups[:4]
    if nick:
        return nick.lower()
    for tok in (g2, g3, g4):
        if not tok:
            continue
        if tok.isdigit():
            return tok
        if len(tok) == 1 and tok in _CN:
            return str(_CN[tok])
        if tok.lower() in _EN:
            return str(_EN[tok.lower()])
    return None


def parse_command(raw: str) -> Optional[tuple[str, str]]:
    """`(target, text)` if the line has a marker AND a command; else None."""
    m = _CMD_RE.match(raw.strip())
    if not m:
        return None
    target = _tok_to_target(m.groups())
    text = (m.group(5) or "").strip().rstrip(" .。!！?？").strip()
    if not target or not text:
        return None
    return target, text


def parse_target_only(raw: str) -> Optional[str]:
    """The bare target ('alpha' / '二号') when the line carries no command."""
    m = _SESS_ONLY_RE.match(raw.strip())
    if not m:
        return None
    return _tok_to_target(m.groups())


# --- daemon socket I/O --------------------------------------------------

def _send(action: dict) -> dict:
    """Send one JSON action to the daemon socket; return its JSON reply."""
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(SOCKET)
        s.sendall((json.dumps(action) + "\n").encode())
        resp = s.recv(4096).decode().strip()
        s.close()
    except OSError as e:
        return {"ok": False, "error": f"can't reach daemon at {SOCKET}: {e}"}
    try:
        return json.loads(resp) if resp else {"ok": False, "error": "empty reply"}
    except json.JSONDecodeError:
        return {"ok": False, "error": f"non-JSON reply: {resp!r}"}


def stage(target: Union[str, int], text: str) -> dict:
    """Stage a command. `target` is a nickname; ints are accepted for back-compat."""
    return _send({"action": "stage_route", "target": str(target), "text": text})


def confirm() -> dict:
    return _send({"action": "confirm_route"})


def cancel() -> dict:
    return _send({"action": "cancel_route"})


# --- driver -------------------------------------------------------------

def _do_one(raw: str, pending: dict, auto: bool) -> Optional[bool]:
    """Process a single line through the same state machine the voice hook uses.

    Returns True if a command fired (auto mode), False on stage-only or target
    remembered, None if the line was ignored.
    """
    cmd = parse_command(raw)
    if cmd:
        target, text = cmd
        pending.pop("target", None)
        r = stage(target, text)
        print(f"  stage  -> {r}")
        if auto and r.get("ok"):
            cr = confirm()
            print(f"  commit -> {cr}")
            return bool(cr.get("fired"))
        return False
    target = parse_target_only(raw)
    if target is not None:
        pending["target"] = target
        print(f"  remembered target {target!r} (next line is its command)")
        return False
    if "target" in pending:
        text = raw.strip().rstrip(" .。!！?？").strip()
        if text:
            t = pending.pop("target")
            r = stage(t, text)
            print(f"  stage  -> {r}")
            if auto and r.get("ok"):
                cr = confirm()
                print(f"  commit -> {cr}")
                return bool(cr.get("fired"))
            return False
    print(f"  ignored (no marker, no pending target): {raw!r}")
    return None


def _repl(auto: bool) -> int:
    print("type orders like the captain would speak them ('alpha echo hi',")
    print("or 'alpha' then 'echo hi'). Ctrl-D to quit.")
    print(f"mode: {'AUTO-COMMIT (stage+confirm)' if auto else 'stage only'}")
    pending: dict = {}
    while True:
        try:
            raw = input("> ")
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not raw.strip():
            continue
        _do_one(raw, pending, auto)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("text", nargs="*", help='e.g. "alpha echo hi" or "二号 跑测试"')
    ap.add_argument("-y", "--commit", action="store_true",
                    help="auto-confirm immediately after staging (skip the gesture)")
    ap.add_argument("--then", action="append", default=[],
                    help="extra line(s), one --then per line (two-step dictation)")
    ap.add_argument("--repl", action="store_true", help="interactive prompt mode")
    args = ap.parse_args()

    if args.repl:
        return _repl(args.commit)

    if not args.text and not args.then:
        ap.print_help()
        return 2

    pending: dict = {}
    lines = ([" ".join(args.text)] if args.text else []) + list(args.then)
    fired_any = False
    for line in lines:
        result = _do_one(line, pending, args.commit)
        fired_any = fired_any or bool(result)

    return 0 if (not args.commit or fired_any or pending) else 1


if __name__ == "__main__":
    sys.exit(main())
