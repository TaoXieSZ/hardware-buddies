"""Tests for tools/cc-bridge/bridge.py apply_event().

These encode the scenarios in openspec/specs/daemon-event-mapping/spec.md.
"""


def ev(name, **kw):
    d = {"hook_event_name": name, "session_id": "s1"}
    d.update(kw)
    return d


# ─── session counting ─────────────────────────────────────────────────

def test_session_start_increments_total(cc, fresh_state):
    assert cc.apply_event(fresh_state, ev("SessionStart")) is True
    assert fresh_state.total == 1
    assert "s1" in fresh_state._sessions


def test_session_end_decrements_total(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("SessionEnd"))
    assert fresh_state.total == 0
    assert "s1" not in fresh_state._sessions


def test_user_prompt_without_session_start_is_implicit_start(cc, fresh_state):
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.total == 1
    assert fresh_state.running == 1


# ─── running counter ──────────────────────────────────────────────────

def test_user_prompt_submit_sets_running_and_msg(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.running == 1
    assert fresh_state.msg == "thinking…"


def test_stop_decrements_running_and_sets_ready(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    cc.apply_event(fresh_state, ev("Stop"))
    assert fresh_state.running == 0
    assert fresh_state.msg == "ready"


def test_running_does_not_double_count_same_session(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.running == 1


# ─── tool activity message ────────────────────────────────────────────

def test_pre_tool_use_sets_running_message(cc, fresh_state):
    cc.apply_event(fresh_state, ev("PreToolUse", tool_name="Bash"))
    assert fresh_state.msg == "running: Bash"


def test_post_tool_use_sets_done_message(cc, fresh_state):
    cc.apply_event(fresh_state, ev("PostToolUse", tool_name="Read"))
    assert fresh_state.msg == "done: Read"


# ─── permission waiting state ─────────────────────────────────────────

def test_permission_request_sets_waiting_and_prompt(cc, fresh_state):
    cc.apply_event(fresh_state, ev("PermissionRequest", tool_name="Bash",
                                   request_id="r1", message="rm -rf /tmp/x"))
    assert fresh_state.waiting == 1
    assert fresh_state.prompt["tool"] == "Bash"
    assert fresh_state.prompt["id"] == "r1"
    assert fresh_state.msg == "approve: Bash"


def test_safe_tool_permission_does_not_block(cc, fresh_state):
    # AskUserQuestion is in SAFE_TOOLS — gating it is a logic loop.
    cc.apply_event(fresh_state, ev("PermissionRequest", tool_name="AskUserQuestion",
                                   request_id="r2"))
    assert fresh_state.waiting == 0
    assert fresh_state.prompt is None


def test_safe_tools_set_contents(cc):
    # The gate must cover the interactive + planning/state-only tools.
    for t in ("AskUserQuestion", "ExitPlanMode", "TodoWrite", "TaskCreate"):
        assert t in cc.SAFE_TOOLS


# ─── "waiting for your input" notification (the merged BUSY-msg fix) ──

def test_waiting_for_input_notification_clears_running(cc, fresh_state):
    # A turn is in flight, then Claude Code fires the idle Notification.
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.running == 1
    cc.apply_event(fresh_state, ev("Notification",
                                   message="Claude is waiting for your input"))
    # Semantically a turn-end — running must drop so the firmware maps IDLE,
    # not BUSY, behind the "waiting for your input" message.
    assert fresh_state.running == 0
    assert fresh_state.msg == "Claude is waiting for your input"


# ─── waiting clears when the turn progresses (Change 0001) ───────────

def _block_on_permission(cc, state):
    cc.apply_event(state, ev("PermissionRequest", tool_name="Bash", request_id="r1"))
    assert state.waiting == 1 and state.prompt is not None


def test_waiting_clears_on_stop(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    _block_on_permission(cc, fresh_state)
    cc.apply_event(fresh_state, ev("Stop"))
    assert fresh_state.waiting == 0
    assert fresh_state.prompt is None


def test_waiting_clears_on_pre_tool_use(cc, fresh_state):
    # A tool starting means the pending permission was granted.
    _block_on_permission(cc, fresh_state)
    cc.apply_event(fresh_state, ev("PreToolUse", tool_name="Bash"))
    assert fresh_state.waiting == 0
    assert fresh_state.prompt is None


def test_waiting_clears_on_new_user_prompt(cc, fresh_state):
    _block_on_permission(cc, fresh_state)
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.waiting == 0
    assert fresh_state.prompt is None


# ─── compaction / subagents / tool failures (Change 0005) ────────────

def test_post_compact_adds_entry(cc, fresh_state):
    cc.apply_event(fresh_state, ev("PostCompact"))
    assert fresh_state.entries[0] == "compacted"
    assert fresh_state.msg == "compacted"


def test_pre_compact_sets_msg_and_entry(cc, fresh_state):
    assert cc.apply_event(fresh_state, ev("PreCompact")) is True
    assert fresh_state.msg == "compacting…"
    assert "compact" in fresh_state.entries[0]


def test_subagent_start_names_agent_type(cc, fresh_state):
    assert cc.apply_event(fresh_state, ev("SubagentStart", agent_type="Explore")) is True
    assert "Explore" in fresh_state.entries[0]


def test_subagent_start_without_type_still_logs(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SubagentStart"))
    assert "subagent" in fresh_state.entries[0]


def test_subagent_stop_adds_entry(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SubagentStop"))
    assert fresh_state.entries[0] == "subagent done"


def test_post_tool_use_failure_sets_msg_and_entry(cc, fresh_state):
    assert cc.apply_event(
        fresh_state, ev("PostToolUseFailure", tool_name="Bash", error="exit 1")
    ) is True
    assert fresh_state.msg == "failed: Bash"
    assert fresh_state.entries[0].startswith("✗ Bash")


# ─── hud metrics event (Change 0002) ─────────────────────────────────

def test_hud_event_populates_metric_fields(cc, fresh_state):
    changed = cc.apply_event(fresh_state, {
        "hook_event_name": "hud",
        "context_pct": 62, "tokens": 48000,
        "limit_5h": 38, "limit_7d": 13,
        "model": "Opus 4.7", "session_ms": 1234567,
    })
    assert changed is True
    assert fresh_state.context_pct == 62
    assert fresh_state.tokens == 48000
    assert fresh_state.limit_5h == 38
    assert fresh_state.limit_7d == 13
    assert fresh_state.model == "Opus 4.7"
    assert fresh_state.session_ms == 1234567


def test_hud_partial_event_leaves_missing_fields_untouched(cc, fresh_state):
    fresh_state.model = "Opus 4.7"
    cc.apply_event(fresh_state, {"hook_event_name": "hud", "context_pct": 70})
    assert fresh_state.context_pct == 70
    assert fresh_state.model == "Opus 4.7"   # not clobbered by an absent field


def test_hud_event_does_not_disturb_lifecycle(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    cc.apply_event(fresh_state, ev("PermissionRequest", tool_name="Bash",
                                   request_id="r1"))
    assert fresh_state.running == 1 and fresh_state.waiting == 1
    cc.apply_event(fresh_state, {"hook_event_name": "hud", "context_pct": 50})
    # Pure telemetry — must not touch running/waiting.
    assert fresh_state.running == 1
    assert fresh_state.waiting == 1


def test_hud_event_fires_no_sound_cue(cc, fresh_state):
    cc.apply_event(fresh_state, {"hook_event_name": "hud", "context_pct": 50})
    # `hud` is telemetry — no /sounds/hud.wav blip on every statusline render.
    assert fresh_state.pending_play is None


# ─── session staleness + reaper (openspec change 0004) ─────────────────

import time
import pytest


@pytest.mark.parametrize(
    "name,extra",
    [
        ("SessionStart", {}),
        ("UserPromptSubmit", {}),
        ("Stop", {}),
        ("PreToolUse",        {"tool_name": "Bash"}),
        ("PostToolUse",       {"tool_name": "Bash"}),
        ("PermissionRequest", {"tool_name": "Bash", "request_id": "r1"}),
        ("Notification",      {"message": "ping"}),
    ],
)
def test_apply_event_stamps_last_seen_for_session_events(cc, fresh_state, name, extra):
    """Every per-session event MUST refresh last_seen so the reaper has a
    single source of truth for whether a session is alive."""
    if name != "SessionStart":
        cc.apply_event(fresh_state, ev("SessionStart"))
        # Backdate so we can prove it gets refreshed by the next event.
        fresh_state._sessions["s1"]["last_seen"] = time.monotonic() - 999

    before = fresh_state._sessions.get("s1", {}).get("last_seen", 0.0)
    cc.apply_event(fresh_state, ev(name, **extra))
    after = fresh_state._sessions.get("s1", {}).get("last_seen")
    # SessionStart creates the record at the current time; all other events
    # refresh from the backdated -999.
    assert after is not None
    assert after > before


def test_hud_event_does_not_stamp_anon_session(cc, fresh_state):
    """`hud` events use the anon/unknown sid and MUST NOT create or touch
    a real-looking session record. (apply_event's last_seen stamp guards
    on `if sid in state._sessions`.)"""
    cc.apply_event(fresh_state, {"hook_event_name": "hud", "context_pct": 50})
    assert "anon" not in fresh_state._sessions


def test_reaper_drops_stale_session(cc, fresh_state):
    """A session idle > STALE_SESSION_SEC is removed; counters recompute
    to zero — this is the exact symptom that previously needed a daemon
    kickstart."""
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    assert fresh_state.running == 1
    # Simulate "Stop never came" by jumping last_seen 1000s into the past.
    fresh_state._sessions["s1"]["last_seen"] = time.monotonic() - 1000
    changed = cc._reap_stale_sessions(fresh_state)
    assert changed is True
    assert "s1" not in fresh_state._sessions
    assert fresh_state.running == 0
    assert fresh_state.total == 0


def test_reaper_corrects_drifted_counter(cc, fresh_state):
    """If state.running ever desyncs from the session map, recompute fixes
    it on the next reap — even when nothing is stale."""
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    # Simulate drift: a phantom increment that the session map doesn't back.
    fresh_state.running = 5
    changed = cc._reap_stale_sessions(fresh_state)
    assert changed is True
    # One real session with running=True → recomputed running=1.
    assert fresh_state.running == 1


def test_reaper_leaves_fresh_sessions_alone(cc, fresh_state):
    """Don't reap mid-conversation."""
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    # last_seen is "right now" — well within the stale window.
    changed = cc._reap_stale_sessions(fresh_state)
    assert changed is False
    assert "s1" in fresh_state._sessions
    assert fresh_state.running == 1


def test_reaper_partial_stale_partial_fresh(cc, fresh_state):
    """Mixed stale + fresh — only stales drop; counters reflect survivors."""
    cc.apply_event(fresh_state, ev("SessionStart", session_id="s1"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit", session_id="s1"))
    cc.apply_event(fresh_state, ev("SessionStart", session_id="s2"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit", session_id="s2"))
    assert fresh_state.running == 2 and fresh_state.total == 2
    fresh_state._sessions["s1"]["last_seen"] = time.monotonic() - 1000
    changed = cc._reap_stale_sessions(fresh_state)
    assert changed is True
    assert "s1" not in fresh_state._sessions
    assert "s2" in fresh_state._sessions
    assert fresh_state.running == 1
    assert fresh_state.total == 1


# ─── q_done: tool-completion clock for answered-question detection ─────
# (cardputer question panel dismisses when answered outside the device)

def test_q_done_advances_only_on_post_tool_use_and_stop(cc, fresh_state):
    cc.apply_event(fresh_state, ev("SessionStart"))
    cc.apply_event(fresh_state, ev("UserPromptSubmit"))
    # AskUserQuestion ask phase: PreToolUse + PermissionRequest + Notification —
    # none of these are a tool COMPLETION, so q_done must NOT be set yet.
    cc.apply_event(fresh_state, ev("PreToolUse", tool_name="AskUserQuestion"))
    cc.apply_event(fresh_state, ev("PermissionRequest", tool_name="AskUserQuestion"))
    cc.apply_event(fresh_state, ev("Notification", message="waiting for your input"))
    assert "q_done" not in fresh_state._sessions["s1"]   # still blocked → no completion
    # Answer arrives → the AskUserQuestion tool completes → PostToolUse → q_done set.
    cc.apply_event(fresh_state, ev("PostToolUse", tool_name="AskUserQuestion"))
    first = fresh_state._sessions["s1"].get("q_done")
    assert isinstance(first, float) and first > 0
    # Stop also advances it (turn end = no longer blocked on the user).
    cc.apply_event(fresh_state, ev("Stop"))
    assert fresh_state._sessions["s1"]["q_done"] >= first
