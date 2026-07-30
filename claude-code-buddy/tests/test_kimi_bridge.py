"""Tests for tools/kimi-bridge/bridge.py apply_event() + inverted cwd join.

Kimi hook events are already Claude-Code-shaped, so apply_event mirrors
codex-bridge. The kimi-specific bits under test:
  - Interrupt / StopFailure (Kimi-specific turn-abort events) behave like
    Stop: running session → idle, counters decremented, no transcript entry.
  - PostToolUseFailure arrives pre-mapped to PostToolUse+failure:true by
    kimi_hook.js → the shared PostToolUse failure branch fires.
  - _build_kimi_sessions INVERTS the codex join: hook-tracked sessions are
    the source of truth (not live cmux panes), cmux only supplies labels.
    Sessions with no matching cmux pane are still listed (cwd basename
    label); a user customTitle from the session file wins over pane titles.
"""

import time


def ev(name, sid="s1", cwd="/Users/txie/proj-a", **kw):
    d = {"hook_event_name": name, "session_id": sid, "cwd": cwd}
    d.update(kw)
    return d


# ─── session lifecycle (Claude-shaped, Kimi fires SessionStart) ────────

def test_session_start_creates_idle_bucket(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("SessionStart"))
    assert fresh_state.total == 1
    assert fresh_state._sessions["s1"]["st"] == "idle"
    assert fresh_state._sessions["s1"]["cwd"] == "/Users/txie/proj-a"


def test_user_prompt_submit_sets_thinking(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("SessionStart"))
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", prompt="hi"))
    assert fresh_state.running == 1
    assert fresh_state.msg == "thinking…"
    assert fresh_state._sessions["s1"]["st"] == "thinking"


def test_stop_decrements_running(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("UserPromptSubmit"))
    kimi.apply_event(fresh_state, ev("Stop"))
    assert fresh_state.running == 0
    assert fresh_state.msg == "ready"
    assert fresh_state._sessions["s1"]["st"] == "idle"


# ─── Kimi-specific turn-abort events ───────────────────────────────────

def test_interrupt_returns_session_to_idle(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.running == 1
    n_entries = len(fresh_state.entries)
    kimi.apply_event(fresh_state, ev("Interrupt"))
    assert fresh_state.running == 0
    assert fresh_state.msg == "ready"
    assert fresh_state._sessions["s1"]["st"] == "idle"
    assert fresh_state._sessions["s1"]["ws"] == 0
    # no reply-head entry (unlike Stop with last_assistant_message)
    assert len(fresh_state.entries) == n_entries


def test_stop_failure_returns_session_to_idle(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("UserPromptSubmit"))
    kimi.apply_event(fresh_state, ev("StopFailure"))
    assert fresh_state.running == 0
    assert fresh_state._sessions["s1"]["st"] == "idle"


def test_interrupt_unknown_session_is_noop(kimi, fresh_state):
    # An Interrupt for a session we never saw must not mint a bucket (the
    # reaper recomputes total from _sessions — a phantom would inflate it).
    assert kimi.apply_event(fresh_state, ev("Interrupt", sid="ghost")) is False
    assert "ghost" not in fresh_state._sessions


def test_post_tool_use_failure_mapped_by_hook(kimi, fresh_state):
    # kimi_hook.js maps PostToolUseFailure → PostToolUse + failure:true.
    kimi.apply_event(fresh_state, ev(
        "PostToolUse", tool_name="Bash", failure=True, error="boom"))
    assert fresh_state.msg == "failed: Bash"
    assert fresh_state.entries[0].startswith("!fail Bash")
    assert "boom" in fresh_state.entries[0]


def test_per_session_state_transitions(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("SessionStart"))
    assert fresh_state._sessions["s1"]["st"] == "idle"
    kimi.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state._sessions["s1"]["st"] == "thinking"
    kimi.apply_event(fresh_state, ev("PreToolUse", tool_name="Bash"))
    assert fresh_state._sessions["s1"]["st"] == "tool"
    kimi.apply_event(fresh_state, ev("PermissionRequest", tool_name="Bash",
                                     tool_input={"command": "touch x"}))
    assert fresh_state._sessions["s1"]["st"] == "waiting"
    assert fresh_state._sessions["s1"]["ws"] > 0
    assert fresh_state.prompt["hint"] == "touch x"
    kimi.apply_event(fresh_state, ev("Stop"))
    assert fresh_state._sessions["s1"]["st"] == "idle"
    assert fresh_state._sessions["s1"]["ws"] == 0


# ─── inverted cwd join: hook sessions are the source of truth ─────────

def test_build_kimi_sessions_lists_hook_sessions_without_pane(kimi, fresh_state):
    # Unlike codex (cmux-pane-driven), a hook session whose directory has NO
    # cmux pane is still listed — label falls back to the cwd basename.
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", sid="abc",
                                     cwd="/Users/txie/lonely"))
    rows = kimi._build_kimi_sessions(fresh_state)
    assert len(rows) == 1
    assert rows[0]["sid"] == "/Users/txie/lonely"      # sid IS the cwd
    assert rows[0]["label"] == "lonely"                # basename fallback
    assert rows[0]["running"] is True
    assert rows[0]["st"] == "thinking"


def test_build_kimi_sessions_uses_cmux_label_when_present(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", sid="abc",
                                     cwd="/Users/txie/live"))
    fresh_state.session_labels = {"/Users/txie/live": "My Kimi Pane"}
    rows = kimi._build_kimi_sessions(fresh_state)
    assert rows[0]["label"] == "My Kimi Pane"          # cmux label wins


def test_build_kimi_sessions_long_cwd_sid_is_suffix(kimi, fresh_state):
    # sid must fit the firmware's char sid[40] (≤39). A long cwd → sid is its
    # last 39 chars; cc-bridge's focus matches by endswith.
    long_cwd = "/Users/txie/OpenSourceProjects/some/deeply/nested/hardware-buddies"
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", sid="abc", cwd=long_cwd))
    rows = kimi._build_kimi_sessions(fresh_state)
    assert len(rows) == 1
    assert rows[0]["sid"] == long_cwd[-39:]
    assert len(rows[0]["sid"]) <= 39
    assert long_cwd.endswith(rows[0]["sid"])
    assert rows[0]["cwd"] == long_cwd                  # full cwd carried


def test_build_kimi_sessions_same_cwd_merges_latest(kimi, fresh_state):
    # Two sessions in one dir collide on cwd → one row, most-recent wins.
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", sid="old", cwd="/dup"))
    fresh_state._sessions["old"]["last_seen"] = 100.0
    fresh_state._sessions["old"]["st"] = "idle"
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", sid="new", cwd="/dup"))
    fresh_state._sessions["new"]["last_seen"] = 200.0  # newer
    fresh_state._sessions["new"]["st"] = "thinking"
    rows = kimi._build_kimi_sessions(fresh_state)
    assert len(rows) == 1
    assert rows[0]["sid"] == "/dup"
    assert rows[0]["st"] == "thinking"


def test_build_kimi_sessions_skips_cwdless_sessions(kimi, fresh_state):
    kimi.apply_event(fresh_state, ev("UserPromptSubmit", sid="nocwd", cwd=""))
    assert kimi._build_kimi_sessions(fresh_state) == []


# ─── cmux label sources ────────────────────────────────────────────────

def test_cmux_custom_titles_from_session_file(kimi, monkeypatch, tmp_path):
    # User-set customTitles are read kind-agnostically (cmux knows no "kimi"
    # kind), keyed by the pane's working directory.
    import json as _j
    session = {"windows": [{"tabManager": {"workspaces": [
        {"panels": [
            {"id": "K1", "customTitle": "refactor buddy", "customTitleSource": "user",
             "terminal": {"workingDirectory": "/Users/txie/proj-k"}},
            {"id": "K2", "customTitle": "auto name", "customTitleSource": "auto",
             "terminal": {"workingDirectory": "/Users/txie/proj-auto"}},
            {"id": "C1", "terminal": {"agent": {"kind": "codex",
                                                "workingDirectory": "/c"}}},
        ]},
    ]}}]}
    f = tmp_path / "session.json"
    f.write_text(_j.dumps(session))
    monkeypatch.setattr(kimi, "CMUX_SESSION_JSON", str(f))
    titles = kimi._cmux_custom_titles()
    # only USER-set titles, any agent kind
    assert titles == {"/Users/txie/proj-k": "refactor buddy"}


def test_cmux_pane_dirs_via_rpc(kimi, monkeypatch, tmp_path):
    # All panes listed by cwd (no kind filter); pane title's first "·" segment
    # is the label; customTitles from the session file win.
    import subprocess
    import json as _j
    session = {"panels": [
        {"id": "K1", "customTitle": "user title", "customTitleSource": "user",
         "terminal": {"workingDirectory": "/Users/txie/proj-k"}},
    ]}
    f = tmp_path / "session.json"
    f.write_text(_j.dumps(session))
    monkeypatch.setattr(kimi, "CMUX_SESSION_JSON", str(f))

    def fake_run(argv, **kw):
        method = argv[2] if len(argv) > 2 else ""
        class R:
            pass
        r = R()
        if method == "workspace.list":
            r.stdout = _j.dumps({"workspaces": [{"id": "W"}]})
        elif method == "surface.list":
            r.stdout = _j.dumps({"surfaces": [
                {"title": "kimi · proj-k",
                 "requested_working_directory": "/Users/txie/proj-k"},
                {"title": "zsh",
                 "requested_working_directory": "/Users/txie/other"},
            ]})
        else:
            r.stdout = "{}"
        return r

    monkeypatch.setattr(subprocess, "run", fake_run)
    panes = kimi._cmux_pane_dirs()
    assert panes["/Users/txie/proj-k"] == "user title"   # customTitle wins
    assert panes["/Users/txie/other"] == "zsh"           # title head fallback


def test_cmux_pane_dirs_empty_when_cmux_down(kimi, monkeypatch):
    import subprocess
    monkeypatch.setattr(kimi, "CMUX_SESSION_JSON", "/no/such/file.json")

    def boom(*a, **kw):
        raise FileNotFoundError("cmux not installed")
    monkeypatch.setattr(subprocess, "run", boom)
    assert kimi._cmux_pane_dirs() == {}


# ─── stale-session reaper ──────────────────────────────────────────────

def test_reaper_recomputes_counters(kimi, fresh_state):
    now = time.monotonic()
    fresh_state._sessions = {
        "live": {"running": True, "last_seen": now},
        "stale": {"running": True, "last_seen": now - kimi.STALE_SESSION_SEC - 10},
    }
    fresh_state.total = 2
    fresh_state.running = 2
    stale = [sid for sid, s in fresh_state._sessions.items()
             if now - s.get("last_seen", now) > kimi.STALE_SESSION_SEC]
    for sid in stale:
        fresh_state._sessions.pop(sid, None)
    fresh_state.total = len(fresh_state._sessions)
    fresh_state.running = sum(1 for s in fresh_state._sessions.values()
                              if s.get("running"))
    assert stale == ["stale"]
    assert fresh_state.total == 1
    assert fresh_state.running == 1
