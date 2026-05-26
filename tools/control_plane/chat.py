"""Codex-style Fleet Secretary REPL backed by `claude --print` as the LLM.

Skips Agora's audio pipeline so you can drive the fleet by typing — same
persona, same daemon backend, same cmux routing as the voice path. Each user
turn shells out to `claude --print --bare --append-system-prompt ...
--json-schema ...` which returns a strict envelope:

  {"reply": "...", "actions": [{"type": "stage", "session": N, "text": "..."},
                                {"type": "confirm"}]}

Actions are executed against the cc-bridge daemon's Unix socket (same
stage_route / confirm_route / cancel_route actions the voice and gesture paths
use, see tools/buddy_core/core.py). The `reply` is rendered as the agent's
natural-language response.

  ~/.cache/buddy-venv/bin/python -m control_plane.chat            # REPL
  ~/.cache/buddy-venv/bin/python -m control_plane.chat -q "二号 跑 ls"  # one-shot

Deps (in buddy-venv): rich · prompt_toolkit.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

# Add tools/ to path so `control_plane.*` imports work when invoked directly.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from control_plane.board import build_board  # noqa: E402
from control_plane.cmux_control import CmuxClient, DEFAULT_CMUX  # noqa: E402
from control_plane.say import cancel as daemon_cancel  # noqa: E402
from control_plane.say import confirm as daemon_confirm  # noqa: E402
from control_plane.say import stage as daemon_stage  # noqa: E402

try:
    from prompt_toolkit import PromptSession
    from prompt_toolkit.formatted_text import HTML
    from prompt_toolkit.history import FileHistory
    from rich.console import Console
    from rich.live import Live
    from rich.panel import Panel
    from rich.spinner import Spinner
except ImportError:
    sys.stderr.write(
        "Missing deps. Install:\n"
        "  ~/.cache/buddy-venv/bin/pip install rich prompt_toolkit\n"
        "Then run with that interpreter:\n"
        "  ~/.cache/buddy-venv/bin/python -m control_plane.chat\n"
    )
    raise SystemExit(1) from None

CLAUDE_BIN = shutil.which("claude") or "/Users/txie/.local/bin/claude"
HISTORY_PATH = Path.home() / ".cache" / "fleet-chat" / "history"

PERSONA = """You are 大副 (First Mate), the captain's number-two on a fleet of coding-agent
ships (Claude Code / Cursor sessions running in cmux terminal panes). The user
is 舰长 (Captain). Every ship has a stable NATO-phonetic nickname (alpha,
bravo, charlie, …) that the captain uses to address it. You take the
captain's orders and route them — by typing the verbatim command into the
chosen ship's pane — via the routing actions below. You do NOT execute
commands yourself; you stage them, and the daemon types them into the chosen
pane on the captain's mark.

Tone: a crisp, calm naval first mate. One short sentence per `reply`. No
emoji, no laughter, no filler. Address the user as 舰长 (or "Captain" in
English). Match the user's language exactly (Chinese in → Chinese out;
English in → English out). Never invent ship state, command output, or
nicknames that aren't on the board.

How you choose a ship:
- Captain names a nickname ("alpha" / "bravo" — case-insensitive) AND an
  order → emit `stage` then `confirm` in the same turn; read back what you
  did (e.g. "alpha：pytest，舰长。" / "alpha: pytest, Captain.").
- Captain gives an order WITHOUT a nickname → pick a sensible default in
  this order: (1) the currently FOCUSED ship on the board (marked `*`),
  (2) the ship the captain last addressed in this conversation, (3) ask
  "哪艘船，舰长？" / "Which ship, Captain?" and emit no actions. If you pick
  a default, name the ship in the read-back ("默认走当前焦点 alpha：pytest，舰长。").
- Captain names a nickname with no order → "什么命令，舰长？" / "What
  order, Captain?" — emit no actions.
- Cancel a staged command with the `cancel` action on request.
- Use ONLY nicknames that appear on the board below. Unknown name → ask the
  captain to pick one that exists, no actions.
- Pure conversation with no routing intent: just `reply`, no actions.

Output a single JSON object matching the schema. No prose, no markdown fences.
The `target` field is the ship's nickname (string).
"""

ENVELOPE_SCHEMA: dict = {
    "type": "object",
    "additionalProperties": False,
    "properties": {
        "reply": {"type": "string"},
        "actions": {
            "type": "array",
            "items": {
                "oneOf": [
                    {
                        "type": "object",
                        "additionalProperties": False,
                        "properties": {
                            "type": {"const": "stage"},
                            "target": {"type": "string", "minLength": 1},
                            "text": {"type": "string", "minLength": 1},
                        },
                        "required": ["type", "target", "text"],
                    },
                    {
                        "type": "object",
                        "additionalProperties": False,
                        "properties": {"type": {"const": "confirm"}},
                        "required": ["type"],
                    },
                    {
                        "type": "object",
                        "additionalProperties": False,
                        "properties": {"type": {"const": "cancel"}},
                        "required": ["type"],
                    },
                ]
            },
        },
    },
    "required": ["reply", "actions"],
}


# ─── state ──────────────────────────────────────────────────────────────

@dataclass
class ChatState:
    history: list[tuple[str, str]] = field(default_factory=list)  # (user, agent)
    last_envelope: dict | None = None
    last_action_results: list[dict] = field(default_factory=list)


# ─── live board snapshot for the LLM ───────────────────────────────────

def _board_snapshot(cmux: str) -> str:
    """Return a compact text snapshot of the current fleet board."""
    if not os.path.exists(cmux):
        return "(cmux not installed — no fleet visible)"
    try:
        rows = build_board(CmuxClient(binary=cmux))
    except Exception as e:  # noqa: BLE001 — log + soldier on
        return f"(board unavailable: {e})"
    if not rows:
        return "(no cmux sessions)"
    lines = []
    for r in rows:
        mark = "*" if r["selected"] else " "
        nick = (r.get("nickname") or "?").ljust(8)
        title = (r["title"] or "")[:50]
        status = (r["status"] or "")[:60]
        lines.append(f"  {mark} {nick}{title}  status: {status}")
    return "\n".join(lines)


# ─── LLM call (subprocess to `claude --print`) ─────────────────────────

def _compose_user_message(state: ChatState, board: str, user_text: str) -> str:
    parts = [f"Current fleet board:\n{board}\n"]
    if state.history:
        parts.append("Recent turns:")
        for u, a in state.history[-6:]:
            parts.append(f"  user: {u}")
            parts.append(f"  agent: {a[:300]}")
        parts.append("")
    parts.append(f"User: {user_text}")
    return "\n".join(parts)


def _call_claude(prompt: str, *, timeout: int) -> dict:
    """Run the LLM and return the parsed envelope `{reply, actions[]}`.

    Uses `--output-format json --json-schema ...` so the result is a schema-
    validated object delivered in `meta.result.structured_output` (the bare
    stdout would otherwise just hold the assistant's natural-language reply,
    which Claude returns alongside the structured value). Skips `--bare` so
    your Claude Code OAuth carries through (otherwise needs ANTHROPIC_API_KEY).
    """
    proc = subprocess.run(
        [
            CLAUDE_BIN,
            "--print",
            "--output-format", "json",
            "--append-system-prompt", PERSONA,
            "--json-schema", json.dumps(ENVELOPE_SCHEMA),
        ],
        input=prompt,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"claude --print exit {proc.returncode}: {proc.stderr.strip()[:400]}")
    try:
        meta = json.loads(proc.stdout)
    except json.JSONDecodeError as e:
        raise RuntimeError(f"non-JSON meta envelope: {e}; head={proc.stdout[:200]!r}") from None
    env = meta.get("structured_output")
    if not isinstance(env, dict):
        # Fallback: maybe schema wasn't applied; try to parse `result` itself.
        try:
            env = json.loads(meta.get("result", "") or "")
        except json.JSONDecodeError:
            raise RuntimeError(
                f"no structured_output; result={meta.get('result','')[:200]!r}"
            ) from None
    env.setdefault("reply", "")
    env.setdefault("actions", [])
    return env


# ─── action execution ──────────────────────────────────────────────────

def _exec_actions(actions: list[dict], board_rows: list[dict]) -> list[dict]:
    valid_nicks = {r.get("nickname") for r in board_rows if r.get("nickname")}
    results: list[dict] = []
    for a in actions:
        kind = a.get("type")
        if kind == "stage":
            target, text = (a.get("target") or "").strip().lower(), a.get("text", "")
            if not target or target not in valid_nicks:
                results.append({"action": a, "ok": False,
                                "error": f"nickname {target!r} not on board"})
                continue
            results.append({"action": a, "result": daemon_stage(target, str(text))})
        elif kind == "confirm":
            results.append({"action": a, "result": daemon_confirm()})
        elif kind == "cancel":
            results.append({"action": a, "result": daemon_cancel()})
        else:
            results.append({"action": a, "ok": False, "error": f"unknown action {kind!r}"})
    return results


# ─── REPL UI ───────────────────────────────────────────────────────────

def _render_turn(console: Console, env: dict, results: list[dict]) -> None:
    reply = env.get("reply", "")
    actions = env.get("actions", [])
    subtitle_parts = []
    for r in results:
        a = r["action"]
        kind = a.get("type")
        if "result" in r:
            res = r["result"]
            ok = res.get("ok")
            fired = res.get("fired")
            tag = "✓" if ok and fired is not False else ("✗" if not ok else "·")
            if kind == "stage":
                subtitle_parts.append(f"{tag} stage[{a.get('target')}]")
            else:
                subtitle_parts.append(f"{tag} {kind}{'·fired' if fired else ''}")
        else:
            subtitle_parts.append(f"✗ {kind}({r.get('error','?')})")
    if not actions and not subtitle_parts:
        subtitle_parts.append("no actions")
    console.print(
        Panel(
            reply or "[dim](empty reply)[/dim]",
            title="[bold]大副 · first mate[/bold]",
            title_align="left",
            subtitle="[dim]" + " · ".join(subtitle_parts) + "[/dim]",
            subtitle_align="left",
            border_style="grey50",
            padding=(0, 1),
        )
    )


def _spinner(console: Console, fn, label: str = "Thinking"):
    start = time.perf_counter()
    import threading
    box: dict = {}
    def worker():
        try:
            box["ok"] = fn()
        except BaseException as e:  # noqa: BLE001
            box["err"] = e
    t = threading.Thread(target=worker, daemon=True)
    t.start()
    try:
        with Live(console=console, refresh_per_second=10, transient=True) as live:
            while t.is_alive():
                el = time.perf_counter() - start
                live.update(Spinner("dots", text=f"[dim]{label}… {el:0.1f}s[/dim]"))
                time.sleep(0.08)
        t.join()
    except KeyboardInterrupt:
        console.print("[dim](canceled — but the subprocess may still finish)[/dim]")
        raise
    if "err" in box:
        raise box["err"]
    return box.get("ok")


def _dispatch_slash(cmd: str, state: ChatState, console: Console, cmux: str) -> str:
    """Return one of: 'continue', 'exit', 'not_slash'."""
    verb = cmd.strip().split()[0].lower() if cmd.strip() else ""
    if verb in ("/quit", "/exit"):
        return "exit"
    if verb == "/clear":
        console.clear()
        return "continue"
    if verb == "/help":
        console.print("[dim]/board /last /raw /clear /quit · Enter sends · Ctrl-D quits[/dim]")
        return "continue"
    if verb == "/board":
        console.print(Panel(_board_snapshot(cmux), title="board", border_style="dim"))
        return "continue"
    if verb == "/last":
        if state.last_envelope is None:
            console.print("[dim]no envelope yet[/dim]")
        else:
            console.print(Panel(json.dumps(state.last_envelope, indent=2, ensure_ascii=False),
                                title="last envelope", border_style="dim"))
        return "continue"
    if verb == "/raw":
        if not state.last_action_results:
            console.print("[dim]no actions yet[/dim]")
        else:
            console.print(Panel(json.dumps(state.last_action_results, indent=2, ensure_ascii=False),
                                title="last action results", border_style="dim"))
        return "continue"
    if verb.startswith("/"):
        console.print(f"[red]unknown command:[/red] {verb} (/help)")
        return "continue"
    return "not_slash"


def _run_turn(console: Console, state: ChatState, cmux: str, user_text: str, timeout: int) -> None:
    board_text = _board_snapshot(cmux)
    prompt = _compose_user_message(state, board_text, user_text)
    try:
        env = _spinner(console, lambda: _call_claude(prompt, timeout=timeout))
    except RuntimeError as e:
        console.print(f"[red]LLM error:[/red] {e}")
        return
    except subprocess.TimeoutExpired:
        console.print(f"[red]LLM timed out after {timeout}s[/red]")
        return
    state.last_envelope = env
    rows = []
    try:
        rows = build_board(CmuxClient(binary=cmux))
    except Exception:  # noqa: BLE001 — render will still show LLM intent
        pass
    state.last_action_results = _exec_actions(env.get("actions", []) or [], rows)
    state.history.append((user_text, env.get("reply", "")))
    _render_turn(console, env, state.last_action_results)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-q", "--question", help="one-shot question then exit")
    ap.add_argument("--timeout", type=int, default=60, help="LLM timeout seconds")
    ap.add_argument("--cmux", default=DEFAULT_CMUX, help="cmux CLI binary")
    args = ap.parse_args()

    console = Console(highlight=False)
    state = ChatState()

    if not os.path.exists(CLAUDE_BIN):
        console.print(f"[red]claude CLI not found at {CLAUDE_BIN}[/red]")
        return 1

    console.print(Panel.fit(
        "[bold]大副 (First Mate)[/bold]  [dim]│  舰长 ⚓  │  via claude --print  │  board live from cmux[/dim]\n"
        "[dim]Give an order. Number optional (defaults to the focused ship). "
        "/help for slash commands. Ctrl-D to quit.[/dim]",
        border_style="grey50",
    ))

    if args.question:
        _run_turn(console, state, args.cmux, args.question, args.timeout)
        return 0

    HISTORY_PATH.parent.mkdir(parents=True, exist_ok=True)
    session = PromptSession(history=FileHistory(str(HISTORY_PATH)))
    prompt_msg = HTML('<style fg="#b0b0b0"><b>舰长</b> ❯ </style>')
    while True:
        try:
            raw = session.prompt(prompt_msg)
        except (EOFError, KeyboardInterrupt):
            console.print("[dim]bye.[/dim]")
            return 0
        text = (raw or "").strip()
        if not text:
            continue
        outcome = _dispatch_slash(text, state, console, args.cmux)
        if outcome == "exit":
            return 0
        if outcome == "continue":
            continue
        _run_turn(console, state, args.cmux, text, args.timeout)


if __name__ == "__main__":
    sys.exit(main())
