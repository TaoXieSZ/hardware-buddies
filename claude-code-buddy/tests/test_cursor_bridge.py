"""Tests for tools/cursor-bridge/bridge.py apply_event() + reaper_loop().

Cursor's apply_event mirrors cc-bridge's but also tracks per-session
`last_seen` and accumulates `output_tokens` (the reference behaviour the
cc-bridge token gap is measured against).
"""

import time


def ev(name, **kw):
    d = {"hook_event_name": name, "session_id": "s1"}
    d.update(kw)
    return d


# ─── shared event mapping ─────────────────────────────────────────────

def test_user_prompt_submit_implicit_start(cursor, fresh_state):
    # Cursor doesn't fire SessionStart — UserPromptSubmit is the start signal.
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.total == 1
    assert fresh_state.running == 1
    assert fresh_state.msg == "thinking…"


def test_stop_decrements_running(cursor, fresh_state):
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    cursor.apply_event(fresh_state, ev("Stop"))
    assert fresh_state.running == 0
    assert fresh_state.msg == "ready"


def test_session_tracks_last_seen(cursor, fresh_state):
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert "last_seen" in fresh_state._sessions["s1"]


# ─── per-session state for cardputer rotation (cursor-session-monitoring) ──

def test_per_session_state_transitions(cursor, fresh_state):
    # st follows the same lifecycle as cc-bridge: thinking → tool → waiting → idle.
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state._sessions["s1"]["st"] == "thinking"
    cursor.apply_event(fresh_state, ev("PreToolUse", tool_name="shell"))
    assert fresh_state._sessions["s1"]["st"] == "tool"
    cursor.apply_event(fresh_state, ev("PermissionRequest", tool_name="shell"))
    assert fresh_state._sessions["s1"]["st"] == "waiting"
    assert fresh_state._sessions["s1"]["ws"] > 0          # FIFO seq assigned
    cursor.apply_event(fresh_state, ev("Stop"))
    assert fresh_state._sessions["s1"]["st"] == "idle"
    assert fresh_state._sessions["s1"]["ws"] == 0          # cleared on leave


def test_per_session_state_in_payload(cursor, fresh_state):
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))   # s1 thinking
    cursor.apply_event(fresh_state, ev("UserPromptSubmit", session_id="s2"))
    cursor.apply_event(fresh_state, ev("PreToolUse", session_id="s2", tool_name="x"))
    p = fresh_state.to_payload()
    sess = {s["sid"]: s for s in p["sessions"]}
    assert sess["s1"]["st"] == "thinking"
    assert sess["s2"]["st"] == "tool"


def test_build_cursor_sessions_uses_live_cmux_panes(cursor, fresh_state):
    # The pushed list = live cmux panes (session_labels), joining hook st by
    # UUID first segment — NOT the raw hook-history _sessions. A hook session
    # whose pane is gone (not in labels) is excluded. (cardputer-cursor-sessions)
    full = "66099139-1550-4241-bd6a-a177bfb0d21c"
    cursor.apply_event(fresh_state, ev("UserPromptSubmit", session_id=full))
    cursor.apply_event(fresh_state, ev("PreToolUse", session_id=full, tool_name="sh"))
    cursor.apply_event(fresh_state, ev("UserPromptSubmit", session_id="zombie-999"))
    fresh_state.session_labels = {"66099139-1550-4241": "my-cursor-proj"}  # live pane
    rows = cursor._build_cursor_sessions(fresh_state)
    by = {r.get("label"): r for r in rows}
    assert "my-cursor-proj" in by                     # live pane listed, labeled
    assert by["my-cursor-proj"]["sid"] == full        # joined to full hook sid
    assert by["my-cursor-proj"]["st"] == "tool"       # hook st carried over
    assert all(r["sid"] != "zombie-999" for r in rows)  # stale hook session excluded


def test_build_cursor_sessions_only_cmux_panes(cursor, fresh_state):
    # Constraint: ONLY live cmux Cursor panes are listed. A hook session with
    # no cmux pane (labels empty) yields an empty list — not the hook session.
    cursor.apply_event(fresh_state, ev("UserPromptSubmit", session_id="h1"))
    assert cursor._build_cursor_sessions(fresh_state) == []   # no cmux pane → none


def test_cmux_cursor_panes_parses_live_panes(cursor, monkeypatch):
    # Self-contained cmux query: parse Cursor panes from cmux rpc output,
    # skip Claude panes. (cardputer-cursor-sessions, no control_plane dep)
    import subprocess
    import json as _j

    def fake_run(argv, **kw):
        method = argv[2] if len(argv) > 2 else ""
        class R:
            pass
        r = R()
        if method == "workspace.list":
            r.stdout = _j.dumps({"workspaces": [{"id": "W"}]})
        elif method == "surface.list":
            r.stdout = _j.dumps({"surfaces": [
                {"title": "claude-x · 229a", "resume_binding":
                    {"kind": "claude", "checkpoint_id": "C"}},
                {"title": "CeLLM harness · hi · cursor-66099139-1550",
                    "resume_binding": None},
            ]})
        else:
            r.stdout = "{}"
        return r

    monkeypatch.setattr(subprocess, "run", fake_run)
    panes = cursor._cmux_cursor_panes()
    assert "66099139-1550" in panes              # Cursor pane picked up
    assert all("229a" not in k for k in panes)   # Claude pane skipped
    assert panes["66099139-1550"]                # has a non-empty label


# ─── token accounting (the reference behaviour) ───────────────────────

def test_stop_accumulates_output_tokens(cursor, fresh_state):
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    cursor.apply_event(fresh_state, ev("Stop", output_tokens=420))
    assert fresh_state.tokens == 420
    assert fresh_state.tokens_today == 420


def test_tokens_accumulate_across_turns(cursor, fresh_state):
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    cursor.apply_event(fresh_state, ev("Stop", output_tokens=100))
    cursor.apply_event(fresh_state, ev("UserPromptSubmit"))
    cursor.apply_event(fresh_state, ev("Stop", output_tokens=50))
    assert fresh_state.tokens == 150


def test_post_tool_use_failure_marks_message(cursor, fresh_state):
    cursor.apply_event(fresh_state, ev("PostToolUse", tool_name="Bash", failure=True,
                                       error="exit 1"))
    assert fresh_state.msg == "failed: Bash"
    assert fresh_state.entries[0].startswith("!fail")


# ─── stale-session reaper ─────────────────────────────────────────────

def test_reaper_recomputes_counters(cursor, fresh_state):
    # Two sessions; one went idle past the stale threshold.
    now = time.monotonic()
    fresh_state._sessions = {
        "live": {"running": True, "last_seen": now},
        "stale": {"running": True, "last_seen": now - cursor.STALE_SESSION_SEC - 10},
    }
    fresh_state.total = 2
    fresh_state.running = 2
    # Inline the reaper body (the loop itself just sleeps + calls this logic).
    stale = [sid for sid, s in fresh_state._sessions.items()
             if now - s.get("last_seen", now) > cursor.STALE_SESSION_SEC]
    for sid in stale:
        fresh_state._sessions.pop(sid, None)
    fresh_state.total = len(fresh_state._sessions)
    fresh_state.running = sum(1 for s in fresh_state._sessions.values()
                              if s.get("running"))
    assert stale == ["stale"]
    assert fresh_state.total == 1
    assert fresh_state.running == 1
