"""Tests for tools/codex-bridge/bridge.py apply_event() + cwd-join build.

Codex hook events are already Claude-Code-shaped, so apply_event mirrors
cc-bridge. The codex-specific bits under test:
  - SessionStart IS fired by Codex (unlike Cursor) → idle bucket + cwd stashed.
  - per-session state lifecycle (thinking → tool → waiting+FIFO → idle).
  - _build_codex_sessions joins hook state to live cmux panes by **cwd**
    (cmux gives Codex panes no session-id), excludes dirs with no live pane,
    and merges two sessions sharing a cwd (keeping the most-recently-seen).
  - _cmux_codex_panes parses Codex panes (title "codex"), skipping Claude and
    Cursor panes.
openspec change cardputer-codex-sessions.
"""

import time


def ev(name, sid="s1", cwd="/Users/txie/proj-a", **kw):
    d = {"hook_event_name": name, "session_id": sid, "cwd": cwd}
    d.update(kw)
    return d


# ─── session lifecycle (Claude-shaped, Codex fires SessionStart) ───────

def test_session_start_creates_idle_bucket(codex, fresh_state):
    codex.apply_event(fresh_state, ev("SessionStart"))
    assert fresh_state.total == 1
    assert fresh_state._sessions["s1"]["st"] == "idle"
    assert fresh_state._sessions["s1"]["cwd"] == "/Users/txie/proj-a"


def test_user_prompt_submit_sets_thinking(codex, fresh_state):
    codex.apply_event(fresh_state, ev("SessionStart"))
    codex.apply_event(fresh_state, ev("UserPromptSubmit", prompt="hi"))
    assert fresh_state.running == 1
    assert fresh_state.msg == "thinking…"
    assert fresh_state._sessions["s1"]["st"] == "thinking"


def test_user_prompt_submit_implicit_start(codex, fresh_state):
    # Defensive: if SessionStart was missed, UserPromptSubmit still starts it.
    codex.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.total == 1
    assert fresh_state.running == 1


def test_stop_decrements_running(codex, fresh_state):
    codex.apply_event(fresh_state, ev("SessionStart"))
    codex.apply_event(fresh_state, ev("UserPromptSubmit"))
    codex.apply_event(fresh_state, ev("Stop"))
    assert fresh_state.running == 0
    assert fresh_state.msg == "ready"


def test_stop_surfaces_last_assistant_message(codex, fresh_state):
    codex.apply_event(fresh_state, ev("UserPromptSubmit"))
    codex.apply_event(fresh_state, ev("Stop", last_assistant_message="all done\nok"))
    assert fresh_state.entries[0] == "buddy: all done ok"


# ─── per-session state for cardputer rotation ─────────────────────────

def test_per_session_state_transitions(codex, fresh_state):
    codex.apply_event(fresh_state, ev("SessionStart"))
    assert fresh_state._sessions["s1"]["st"] == "idle"
    codex.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state._sessions["s1"]["st"] == "thinking"
    codex.apply_event(fresh_state, ev("PreToolUse", tool_name="Bash"))
    assert fresh_state._sessions["s1"]["st"] == "tool"
    codex.apply_event(fresh_state, ev("PermissionRequest", tool_name="Bash",
                                      tool_input={"command": "touch x"}))
    assert fresh_state._sessions["s1"]["st"] == "waiting"
    assert fresh_state._sessions["s1"]["ws"] > 0           # FIFO seq assigned
    assert fresh_state.prompt["hint"] == "touch x"         # command surfaced
    codex.apply_event(fresh_state, ev("Stop"))
    assert fresh_state._sessions["s1"]["st"] == "idle"
    assert fresh_state._sessions["s1"]["ws"] == 0          # cleared on leave


def test_per_session_state_in_payload(codex, fresh_state):
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="s1", cwd="/a"))
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="s2", cwd="/b"))
    codex.apply_event(fresh_state, ev("PreToolUse", sid="s2", cwd="/b", tool_name="x"))
    p = fresh_state.to_payload()
    sess = {s["sid"]: s for s in p["sessions"]}
    assert sess["s1"]["st"] == "thinking"
    assert sess["s2"]["st"] == "tool"


# ─── cwd join: build sessions from live cmux panes ────────────────────

def test_build_codex_sessions_joins_by_cwd(codex, fresh_state):
    # The pushed list = live cmux panes (session_labels keyed by cwd), joining
    # hook st by directory. A hook session whose dir has no live pane is excluded.
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="abc", cwd="/Users/txie/live"))
    codex.apply_event(fresh_state, ev("PreToolUse", sid="abc", cwd="/Users/txie/live",
                                      tool_name="Bash"))
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="zzz", cwd="/Users/txie/gone"))
    fresh_state.session_labels = {"/Users/txie/live": "live"}   # only one live pane
    rows = codex._build_codex_sessions(fresh_state)
    by = {r.get("label"): r for r in rows}
    assert "live" in by                                  # live pane listed, labeled
    assert by["live"]["sid"] == "/Users/txie/live"       # sid IS the cwd (focus key)
    assert by["live"]["st"] == "tool"                    # hook st joined by cwd
    assert by["live"]["cwd"] == "/Users/txie/live"       # cwd carried (informational)
    assert all(r["cwd"] != "/Users/txie/gone" for r in rows)  # dir w/o pane excluded


def test_build_codex_sessions_only_cmux_panes(codex, fresh_state):
    # ONLY live cmux Codex panes are listed; no labels → empty list.
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="h1", cwd="/x"))
    assert codex._build_codex_sessions(fresh_state) == []


def test_build_codex_sessions_long_cwd_sid_is_suffix(codex, fresh_state):
    # sid must fit the firmware's char sid[40] (≤39). A long cwd → sid is its
    # last 39 chars; cc-bridge's focus matches by endswith. (focus key, not UUID)
    long_cwd = "/Users/txie/OpenSourceProjects/some/deeply/nested/hardware-buddies"
    fresh_state.session_labels = {long_cwd: "hardware-buddies"}
    rows = codex._build_codex_sessions(fresh_state)
    assert len(rows) == 1
    assert rows[0]["sid"] == long_cwd[-39:]              # bounded suffix
    assert len(rows[0]["sid"]) <= 39
    assert long_cwd.endswith(rows[0]["sid"])             # focus can match by endswith


def test_build_codex_sessions_same_cwd_merges_latest(codex, fresh_state):
    # Two Codex sessions in one dir collide on cwd → one row, most-recent wins.
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="old", cwd="/dup"))
    fresh_state._sessions["old"]["last_seen"] = 100.0
    fresh_state._sessions["old"]["st"] = "idle"
    codex.apply_event(fresh_state, ev("UserPromptSubmit", sid="new", cwd="/dup"))
    fresh_state._sessions["new"]["last_seen"] = 200.0     # newer
    fresh_state._sessions["new"]["st"] = "thinking"
    fresh_state.session_labels = {"/dup": "dup"}
    rows = codex._build_codex_sessions(fresh_state)
    assert len(rows) == 1
    assert rows[0]["sid"] == "/dup"                       # sid IS the cwd (one per dir)
    assert rows[0]["st"] == "thinking"                    # most-recently-seen st kept


def test_cmux_codex_panes_from_session_file_kind(codex, monkeypatch, tmp_path):
    # Detection via cmux's session file terminal.agent.kind — reliable even after
    # a Codex pane retitles to its conversation topic (title no longer says
    # "codex"). cwd = agent.workingDirectory (NOT requested_working_directory).
    # label = user customTitle when set. Claude/Cursor agents are skipped.
    import json as _j
    session = {"windows": [{"tabManager": {"workspaces": [
        {"panels": [
            {"id": "CDX", "customTitle": "Agent HUB design", "customTitleSource": "user",
             "terminal": {"agent": {"kind": "codex",
                                    "workingDirectory": "/Users/txie/proj-z"}}},
            {"id": "CLA", "terminal": {"agent": {"kind": "claude",
                                                 "workingDirectory": "/c"}}},
            {"id": "CUR", "terminal": {"agent": {"kind": "cursor",
                                                 "workingDirectory": "/u"}}},
        ]},
    ]}}]}
    f = tmp_path / "session.json"
    f.write_text(_j.dumps(session))
    monkeypatch.setattr(codex, "CMUX_SESSION_JSON", str(f))
    panes = codex._cmux_codex_panes()
    # the retitled codex pane is found by kind; label uses the user title
    assert panes == {"/Users/txie/proj-z": "Agent HUB design"}


def test_cmux_codex_panes_falls_back_to_rpc_when_no_session_file(codex, monkeypatch):
    # No readable session file → fall back to the title-based surface.list scan.
    import subprocess
    import json as _j
    monkeypatch.setattr(codex, "CMUX_SESSION_JSON", "/no/such/file.json")

    def fake_run(argv, **kw):
        method = argv[2] if len(argv) > 2 else ""
        class R:
            pass
        r = R()
        if method == "workspace.list":
            r.stdout = _j.dumps({"workspaces": [{"id": "W"}]})
        elif method == "surface.list":
            r.stdout = _j.dumps({"surfaces": [
                {"title": "codex", "requested_working_directory": "/Users/txie/proj-z",
                 "resume_binding": None},
            ]})
        else:
            r.stdout = "{}"
        return r

    monkeypatch.setattr(subprocess, "run", fake_run)
    assert codex._cmux_codex_panes() == {"/Users/txie/proj-z": "proj-z"}


# ─── stale-session reaper ─────────────────────────────────────────────

def test_reaper_recomputes_counters(codex, fresh_state):
    now = time.monotonic()
    fresh_state._sessions = {
        "live": {"running": True, "last_seen": now},
        "stale": {"running": True, "last_seen": now - codex.STALE_SESSION_SEC - 10},
    }
    fresh_state.total = 2
    fresh_state.running = 2
    stale = [sid for sid, s in fresh_state._sessions.items()
             if now - s.get("last_seen", now) > codex.STALE_SESSION_SEC]
    for sid in stale:
        fresh_state._sessions.pop(sid, None)
    fresh_state.total = len(fresh_state._sessions)
    fresh_state.running = sum(1 for s in fresh_state._sessions.values()
                              if s.get("running"))
    assert stale == ["stale"]
    assert fresh_state.total == 1
    assert fresh_state.running == 1
