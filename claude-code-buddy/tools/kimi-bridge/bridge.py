#!/usr/bin/env python3
"""
kimi-bridge — Kimi Code CLI ↔ cardputer buddy daemon.

Fifth agent feed, after cc-bridge (Claude), cursor-bridge (Cursor),
codex-bridge (Codex) and opencode-bridge (OpenCode).

Why this follows the *codex* pattern (near-identity hook forwarder):
  - Kimi Code CLI's hooks are ALREADY Claude-Code-shaped: event names
    (SessionStart / UserPromptSubmit / PreToolUse / PostToolUse / Stop /
    PermissionRequest / SessionEnd / PostCompact) and the base fields
    (session_id, cwd, tool_name, tool_input, prompt) match what
    apply_event() reads. So kimi_hook.js is a near-identity forwarder and
    apply_event below mirrors codex-bridge almost verbatim.
  - Kimi additionally fires Interrupt / StopFailure / PostToolUseFailure.
    The hook maps PostToolUseFailure → PostToolUse+failure:true (reusing the
    codex PostToolUse failure branch); Interrupt / StopFailure are handled
    here as Stop-equivalents (turn aborted → session back to idle).
  - Aggregation is free: cc-bridge already keys ext_sessions by agent, so
    pushing {agent:"kimi", sessions:[...]} to its socket merges Kimi into
    the single-BLE-owner payload with no cc-bridge change. kimi-bridge owns
    no BLE device of its own (no_ble=True — scanning would contend with
    cc-bridge's cardputer link on the shared macOS BLE radio).

The ONE real difference from codex-bridge — pane discovery direction:
  cmux's session file only knows agent kinds claude/codex (no "kimi"), so we
  can't scan panes by kind. Instead we INVERT the join: the hook payload's
  `cwd` is the source of truth for which Kimi sessions exist; cmux is only
  consulted (rpc workspace.list/surface.list, plus customTitle from the
  session file) to find a human label for a pane whose
  requested_working_directory matches that cwd. Sessions whose directory has
  no discoverable cmux pane are STILL listed (label = cwd basename) —
  selecting them just no-ops on focus. sid = cwd (or its last 39 chars),
  same as codex, so cc-bridge focuses by cwd match.

Run manually:
  python3 tools/kimi-bridge/bridge.py
Run as launchd daemon:
  see tools/kimi-bridge/install.sh   (com.kimi-bridge)
"""

import os
import sys
import time
import json
import asyncio
import pathlib

# Allow `from buddy_core import ...` AND `from dashboard import ...`
# when launched as a standalone script.
sys.path.insert(0, str(pathlib.Path(__file__).parent.parent))
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from buddy_core import run, BuddyState
# Dashboard is optional for kimi-bridge (defaults OFF — see DASH_PORT). Same
# guarded-import pattern as codex-bridge: no dashboard.py ships in this dir.
try:
    from dashboard import start_dashboard, DEFAULT_PORT as DASH_DEFAULT_PORT
except Exception:  # pragma: no cover - dashboard not shipped with kimi-bridge
    start_dashboard = None
    DASH_DEFAULT_PORT = 0

# ─── config ────────────────────────────────────────────────────────────
SOCKET_PATH = os.environ.get("KIMI_BRIDGE_SOCKET", "/tmp/kimi-bridge.sock")
DEVICE_PREFIX = os.environ.get("KIMI_BRIDGE_DEVICE_PREFIX", "Kimi-")
LOG_PATH = os.environ.get(
    "KIMI_BRIDGE_LOG", str(pathlib.Path.home() / "Library/Logs/kimi-bridge.log")
)
PTT_KEYCODE = int(os.environ.get("KIMI_BRIDGE_PTT_KEYCODE", "61"))  # right Option
PTT_MODE = os.environ.get("KIMI_BRIDGE_PTT_MODE", "tap")
TAB5_SERIAL = os.environ.get("KIMI_BRIDGE_TAB5_SERIAL", "")

# Sessions older than this with no events are reaped on the next tick.
# Kimi fires SessionEnd, but defensively mirror codex: an agent killed
# mid-turn may not, and without this state.total only ever grows.
STALE_SESSION_SEC = 600  # 10 min


def _cwd_of(ev: dict) -> str:
    """The working directory carried by every Kimi hook event — our join key
    against live cmux panes (their requested_working_directory)."""
    return ev.get("cwd") or ev.get("workspace") or ""


# ─── hook event → state mutations ──────────────────────────────────────
def apply_event(state: BuddyState, ev: dict) -> bool:
    """Mutate state from a Kimi hook event. Returns True if the payload changed
    materially (= we should re-emit immediately).

    Kimi hook events are already Claude-Code-shaped, so this mirrors
    codex-bridge. Additions vs. codex: Interrupt / StopFailure (Kimi-specific
    turn-abort events) are treated as Stop-equivalents — the session is no
    longer generating, so it goes back to idle. (PostToolUseFailure arrives
    pre-mapped to PostToolUse+failure:true by kimi_hook.js.)
    """
    name = ev.get("hook_event_name") or ev.get("event") or ""
    sid = ev.get("session_id") or ev.get("sessionId") or "anon"
    cwd = _cwd_of(ev)
    changed = False

    now = time.monotonic()

    def _touch():
        s = state._sessions.get(sid)
        if s is not None:
            s["last_seen"] = now
            if cwd:
                s["cwd"] = cwd

    if name == "SessionStart":
        if sid not in state._sessions:
            state._sessions[sid] = {"running": False, "last_seen": now}
            state.total += 1
            changed = True
        if cwd:
            state._sessions[sid]["cwd"] = cwd
        state._sessions[sid]["last_seen"] = now
        state.set_session_state(sid, "idle")
        state.add_entry("session start")

    elif name == "SessionEnd":
        if state._sessions.pop(sid, None):
            state.total = max(0, state.total - 1)
            state.add_entry("session ended")
            changed = True

    elif name == "UserPromptSubmit":
        # User submitted → model about to think.
        if sid in state._sessions and not state._sessions[sid].get("running"):
            state._sessions[sid]["running"] = True
            state.running += 1
        elif sid not in state._sessions:
            # Defensive: if we somehow missed SessionStart, treat this as start.
            state._sessions[sid] = {"running": True, "last_seen": now}
            state.total += 1
            state.running += 1
        if cwd:
            state._sessions[sid]["cwd"] = cwd
        state._sessions[sid]["last_seen"] = now
        prompt = ev.get("prompt") or ev.get("user_prompt") or ""
        if prompt:
            state.add_entry(f"you: {prompt}")
        state.msg = "thinking…"
        state.set_session_state(sid, "thinking")
        changed = True

    elif name == "Stop":
        # Assistant done responding (this turn). Session still open.
        s = state._sessions.get(sid)
        if s:
            s["last_seen"] = now
            if cwd:
                s["cwd"] = cwd
            if s.get("running"):
                s["running"] = False
                state.running = max(0, state.running - 1)
                state.msg = "ready"
                state.set_session_state(sid, "idle")
                changed = True
        # Kimi may attach a reply head on Stop (not guaranteed by its docs) —
        # surface it when present so the transcript scroll has content.
        txt = ev.get("last_assistant_message") or ev.get("text") or ""
        if txt:
            state.add_entry(f"buddy: {str(txt).replace(chr(10), ' ').strip()}")
            changed = True

    elif name in ("Interrupt", "StopFailure"):
        # Kimi-specific: the turn was aborted (user interrupt / stop hook
        # failure). Same lifecycle effect as Stop — session no longer
        # generating — minus the reply-head entry (there is none).
        s = state._sessions.get(sid)
        if s:
            s["last_seen"] = now
            if cwd:
                s["cwd"] = cwd
            if s.get("running"):
                s["running"] = False
                state.running = max(0, state.running - 1)
            state.msg = "ready"
            state.set_session_state(sid, "idle")
            changed = True

    elif name == "PreToolUse":
        tool = ev.get("tool_name") or "tool"
        state.msg = f"running: {tool}"
        ti = ev.get("tool_input") or {}
        hint = ti.get("command") or ti.get("description") or ti.get("file_path") or ""
        state.add_entry(f"{tool} {hint}".strip())
        _touch()
        state.set_session_state(sid, "tool")
        changed = True

    elif name == "PostToolUse":
        tool = ev.get("tool_name") or "tool"
        if ev.get("failure"):
            err = (ev.get("error") or "").strip()
            state.msg = f"failed: {tool}"
            state.add_entry(f"!fail {tool} {err}".strip())
        else:
            state.msg = f"done: {tool}"
        _touch()
        state.set_session_state(sid, "tool")
        changed = True

    elif name in ("PermissionRequest", "Notification"):
        if name == "PermissionRequest" or "permission" in (ev.get("message") or "").lower():
            tool = ev.get("tool_name") or ev.get("tool") or "tool"
            pid = ev.get("request_id") or ev.get("id") or ev.get("tool_use_id") \
                or f"req_{int(time.time())}"
            ti = ev.get("tool_input") or {}
            hint = ti.get("command") or ti.get("description") \
                or ev.get("message") or ev.get("hint") or ""
            state.waiting = max(state.waiting, 1)
            state.prompt = {"id": pid, "tool": tool, "hint": str(hint)[:120]}
            state.msg = f"approve: {tool}"
            _touch()
            state.set_session_state(sid, "waiting")
            changed = True
        else:
            msg = ev.get("message") or ev.get("title") or ""
            if msg:
                state.msg = msg[:120]
                state.add_entry(msg)
                changed = True

    elif name == "PostCompact":
        state.add_entry("compacted")
        changed = True

    return changed


# ─── push Kimi sessions to cc-bridge (single-BLE-owner aggregation) ─────
# kimi-bridge has no BLE device of its own; it feeds its per-session snapshot
# to cc-bridge, which owns the cardputer's BLE link and merges all agents'
# sessions[] into one payload.
CC_BRIDGE_SOCKET = os.environ.get("CC_BRIDGE_SOCKET", "/tmp/cc-bridge.sock")
EXT_PUSH_INTERVAL_S = 2.0


def _build_kimi_sessions(state: BuddyState) -> list:
    """sessions[] snapshot — driven by HOOK-tracked sessions (inverted join vs
    codex). Every hook session with a cwd gets a row: cmux (state.session_labels
    {cwd: label}, populated by cmux_kimi_label_loop) only supplies a human label
    when a pane with a matching requested_working_directory exists; otherwise
    the label falls back to the cwd basename and the row is still listed —
    selecting it on the device just no-ops on focus.

    Known limitation (same as codex): two sessions sharing one cwd collide;
    we keep the most-recently-seen one (cmux exposes nothing finer).
    """
    labels = getattr(state, "session_labels", {})   # {cwd: label}
    # index hook-tracked sessions by cwd, keeping the most-recently-seen per dir
    by_cwd = {}
    for sid, s in state._sessions.items():
        cwd = s.get("cwd")
        if not cwd:
            continue
        prev = by_cwd.get(cwd)
        if prev is None or s.get("last_seen", 0) >= prev[1].get("last_seen", 0):
            by_cwd[cwd] = (sid, s)

    out = []
    for cwd, (_sid, s) in by_cwd.items():
        # sid must round-trip through the firmware's char sid[40] buffer AND let
        # cc-bridge focus the pane WITHOUT any daemon-side state (the select
        # callback can't see BuddyState). So the sid IS the cwd — or its last 39
        # chars when the path is longer — and cc-bridge's focus_by_kimi_cwd
        # matches a pane whose working directory == sid OR ends with it.
        sid = cwd if len(cwd) <= 39 else cwd[-39:]
        label = labels.get(cwd) or (cwd.rstrip("/").split("/")[-1] or cwd)
        row = {"sid": sid, "running": bool(s.get("running")),
               "cwd": cwd, "label": label[:24]}
        if s.get("st"):
            row["st"] = s["st"]
        if s.get("ws"):
            row["ws"] = s["ws"]
        out.append(row)
        if len(out) >= 16:
            break
    return out


# cmux binary — self-contained query (kimi-bridge has no control_plane module
# in every deployment, so we shell out directly instead of importing CmuxClient).
CMUX_BIN = os.environ.get(
    "CMUX_BIN", "/Applications/cmux.app/Contents/Resources/bin/cmux")

# cmux's session file carries each pane's user-set customTitle — a better label
# than the live pane title. cmux knows no "kimi" agent kind, so we read labels
# for ALL panes here (no kind filter) and match by working directory later.
CMUX_SESSION_JSON = os.environ.get(
    "CMUX_SESSION_JSON",
    str(pathlib.Path.home()
        / "Library/Application Support/cmux/session-com.cmuxterm.app.json"))


def _cmux_custom_titles() -> dict:
    """{cwd: customTitle} for every cmux pane with a USER-set title, from the
    session file. Kind-agnostic — used purely as a label source. Returns {} if
    the file is missing/unreadable."""
    try:
        with open(CMUX_SESSION_JSON, encoding="utf-8") as f:
            doc = json.load(f)
    except Exception:
        return {}
    titles, stack = {}, [doc]
    while stack:
        o = stack.pop()
        if isinstance(o, dict):
            if o.get("customTitleSource") == "user" and o.get("customTitle"):
                term = o.get("terminal") or {}
                ag = term.get("agent") or {}
                cwd = (ag.get("workingDirectory") or term.get("workingDirectory")
                       or o.get("directory") or "")
                if cwd:
                    titles[cwd] = str(o["customTitle"])[:24]
            stack.extend(o.values())
        elif isinstance(o, list):
            stack.extend(o)
    return titles


def _cmux_pane_dirs() -> dict:
    """{cwd: label} for ALL live cmux panes, via rpc workspace.list /
    surface.list (the codex-bridge fallback path). cwd = the pane's
    requested_working_directory; label = the pane title (first segment before
    the cmux "·" separator), falling back to the cwd basename. User customTitles
    from the session file take precedence. Returns {} on any rpc failure —
    callers then label by cwd basename only."""
    import subprocess

    def _rpc(method, params):
        try:
            out = subprocess.run(
                [CMUX_BIN, "rpc", method, json.dumps(params)],
                capture_output=True, text=True, timeout=5).stdout
            return json.loads(out)
        except Exception:
            return {}

    panes = {}
    wl = _rpc("workspace.list", {})
    for ws in (wl.get("workspaces") or wl.get("items") or []):
        wid = ws.get("id") or ""
        sl = _rpc("surface.list", {"workspace": wid})
        for s in (sl.get("surfaces") or sl.get("items") or []):
            cwd = s.get("requested_working_directory") or ""
            if not cwd:
                continue
            title = (s.get("title") or "").strip()
            # cmux titles are often "cmd · cwd" or "topic · cursor-<uuid>" —
            # keep the first segment as the human label.
            head = title.split("·")[0].strip() if title else ""
            label = (head or cwd.rstrip("/").split("/")[-1] or cwd)[:24]
            panes.setdefault(cwd, label)
    # User-set customTitles win over live pane titles.
    panes.update(_cmux_custom_titles())
    return panes


async def cmux_kimi_label_loop(state: BuddyState, dirty: asyncio.Event):
    """Poll cmux every 15s for pane labels by cwd → state.session_labels
    {cwd: label}. Unlike codex-bridge this is LABEL-ONLY: sessions come from
    hook state, cmux just makes the device list human-readable. Missing cmux →
    labels stay empty and _build_kimi_sessions falls back to cwd basenames."""
    import logging
    log = logging.getLogger("kimi-bridge")
    loop = asyncio.get_event_loop()
    while True:
        await asyncio.sleep(15)
        try:
            labels = await loop.run_in_executor(None, _cmux_pane_dirs)
            if labels != getattr(state, "session_labels", None):
                state.session_labels = labels
                dirty.set()
                log.info("cmux pane labels refreshed: %d pane(s)", len(labels))
        except Exception:
            log.exception("cmux_kimi_label_loop tick failed")


async def push_ext_sessions_loop(state: BuddyState, dirty: asyncio.Event):
    """Periodically push Kimi sessions to cc-bridge's socket. Best-effort:
    any failure (cc-bridge down, socket missing) is swallowed and retried."""
    import logging
    log = logging.getLogger("kimi-bridge")
    if not CC_BRIDGE_SOCKET:
        log.info("ext_sessions push disabled (CC_BRIDGE_SOCKET empty)")
        return
    while True:
        await asyncio.sleep(EXT_PUSH_INTERVAL_S)
        msg = json.dumps({
            "action": "ext_sessions",
            "agent": "kimi",
            "sessions": _build_kimi_sessions(state),
        }) + "\n"
        try:
            r, w = await asyncio.open_unix_connection(CC_BRIDGE_SOCKET)
            w.write(msg.encode())
            await w.drain()
            w.close()
        except Exception:
            pass   # cc-bridge not up / no socket — try again next tick


# ─── stale-session reaper ──────────────────────────────────────────────
async def reaper_loop(state: BuddyState, dirty: asyncio.Event):
    """Periodically drop sessions with no recent activity, recompute counters.
    Defensive (Kimi does fire SessionEnd, but a killed agent may not). Passed
    to run() via extra_tasks.
    """
    import logging
    log = logging.getLogger("kimi-bridge")
    while True:
        await asyncio.sleep(60)
        now = time.monotonic()
        stale = [
            sid for sid, s in state._sessions.items()
            if now - s.get("last_seen", now) > STALE_SESSION_SEC
        ]
        if not stale:
            continue
        for sid in stale:
            log.info("reaping stale session %s (idle %ds)",
                     sid[:8], int(now - state._sessions[sid].get("last_seen", now)))
            state._sessions.pop(sid, None)
        # Recompute counters from the post-reap session set so they can't drift.
        state.total = len(state._sessions)
        state.running = sum(1 for s in state._sessions.values() if s.get("running"))
        dirty.set()


def _kimi_log_fmt(payload: dict) -> str:
    return (
        f"running={payload.get('running', 0)} waiting={payload.get('waiting', 0)} "
        f"sessions={len(payload.get('sessions', []) or [])} "
        f"prompt={(payload.get('prompt', {}) or {}).get('id', '-')} "
        f"msg={payload.get('msg', '')[:40]} "
        f"entries[0]={(payload.get('entries', [''])[:1] or [''])[0][:50]}"
    )


# Dashboard defaults OFF for kimi-bridge to avoid colliding with cc-bridge /
# cursor-bridge dashboards on the same Mac. Set KIMI_BRIDGE_DASH_PORT>0 to enable.
DASH_PORT = int(os.environ.get("KIMI_BRIDGE_DASH_PORT", "0"))


def _on_loop_start(ble, loop, log, state: BuddyState):
    if DASH_PORT > 0 and start_dashboard is not None:
        start_dashboard(state, ble, loop, log=log, port=DASH_PORT)
    elif DASH_PORT > 0:
        log.info("dashboard requested but module unavailable — skipping")
    else:
        log.info("dashboard disabled (KIMI_BRIDGE_DASH_PORT=0)")


if __name__ == "__main__":
    run(
        name="kimi-bridge",
        socket_path=SOCKET_PATH,
        log_path=LOG_PATH,
        device_prefix=DEVICE_PREFIX,
        apply_event=apply_event,
        ptt_keycode=PTT_KEYCODE,
        ptt_mode=PTT_MODE,
        keepalive_s=2.0,
        rtc_sync_on_connect=True,
        extra_tasks=[reaper_loop, push_ext_sessions_loop, cmux_kimi_label_loop],
        log_fmt=_kimi_log_fmt,
        on_loop_start=_on_loop_start,
        serial_port=TAB5_SERIAL or None,
        app="kimi",
        # kimi-bridge owns no stick — it pushes ext_sessions to cc-bridge (the
        # single BLE owner). Scanning for a non-existent Kimi-* device would
        # contend with cc-bridge's cardputer link on the shared macOS BLE radio
        # and flap it (observed 2026-06-26). Push-only: no BLE scan.
        no_ble=True,
    )
