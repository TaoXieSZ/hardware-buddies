#!/usr/bin/env python3
"""R1 spike: can we enumerate (and later focus) the right Claude/Cursor window?

Read-only by default — lists windows + titles for Terminal / iTerm2 / VS Code /
Cursor so we can see whether each session's cwd or project name shows up in the
window title (the proposed number→window binding for the voice control plane).

Focus + type are gated behind --focus "<title substr>" and --type "<text>" so a
real session is never touched unless you ask.

Usage:
  python3 windows.py                      # list all windows (safe)
  python3 windows.py --focus backend      # raise first window whose title contains "backend"
  python3 windows.py --focus backend --type "echo hi"   # focus + paste text + Enter
"""
from __future__ import annotations

import argparse
import subprocess
import sys

TERMINAL_APPS = ["Terminal", "iTerm2"]
ELECTRON_APPS = ["Code", "Cursor"]  # Electron → use System Events accessibility


def osa(script: str) -> tuple[int, str, str]:
    p = subprocess.run(["osascript", "-e", script], capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()


def running_apps() -> set[str]:
    rc, out, _ = osa(
        'tell application "System Events" to get name of every process whose background only is false'
    )
    return {a.strip() for a in out.split(",")} if rc == 0 else set()


def list_terminal_windows(app: str) -> list[str]:
    # Terminal/iTerm expose window names directly via their own dictionary.
    rc, out, err = osa(f'tell application "{app}" to get name of windows')
    if rc != 0:
        return [f"<error: {err or 'n/a'}>"]
    return [w.strip() for w in out.split(",") if w.strip()]


def list_electron_windows(app: str) -> list[str]:
    # Electron apps have no useful AppleScript dictionary; read titles via the
    # accessibility tree (System Events). Needs Accessibility permission.
    rc, out, err = osa(
        f'tell application "System Events" to tell process "{app}" to get title of windows'
    )
    if rc != 0:
        return [f"<error (Accessibility perm?): {err or 'n/a'}>"]
    return [w.strip() for w in out.split(",") if w.strip()]


def focus_window(title_substr: str) -> bool:
    # Generic: find the first process window whose title contains the substring,
    # raise it, and bring its app frontmost. Works for both terminal + Electron.
    script = f'''
    tell application "System Events"
      repeat with proc in (every process whose background only is false)
        repeat with w in (every window of proc)
          if (title of w) contains "{title_substr}" then
            set frontmost of proc to true
            perform action "AXRaise" of w
            return (name of proc) & " | " & (title of w)
          end if
        end repeat
      end repeat
    end tell
    return "NOT FOUND"
    '''
    rc, out, err = osa(script)
    print(f"  focus → {out or err}")
    return rc == 0 and out != "NOT FOUND"


def type_text(text: str) -> None:
    # Clipboard paste (reliable for long/CJK) then Return. Targets the focused
    # window — call focus_window first.
    safe = text.replace('"', '\\"')
    osa(f'set the clipboard to "{safe}"')
    osa('tell application "System Events" to keystroke "v" using command down')
    osa('tell application "System Events" to key code 36')  # Return
    print(f"  typed + Enter: {text!r}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--focus", metavar="SUBSTR")
    ap.add_argument("--type", metavar="TEXT")
    args = ap.parse_args()

    if args.focus:
        ok = focus_window(args.focus)
        if ok and args.type:
            type_text(args.type)
        return 0 if ok else 1

    running = running_apps()
    print("=== window enumeration (read-only) ===\n")
    for app in TERMINAL_APPS:
        if app in running:
            print(f"[{app}]  (terminal dictionary)")
            for t in list_terminal_windows(app):
                print(f"    • {t}")
            print()
    for app in ELECTRON_APPS:
        if app in running:
            print(f"[{app}]  (accessibility)")
            for t in list_electron_windows(app):
                print(f"    • {t}")
            print()
    print("Question this answers: does each session's cwd/project appear in a")
    print("title above? If yes, number→window binding by title-match is viable.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
