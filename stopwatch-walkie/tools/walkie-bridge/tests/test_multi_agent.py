from __future__ import annotations

import asyncio
from dataclasses import replace

import pytest

from multi_agent import MultiAgentRouter, ProposalStore, RouterError, TaskTracker, bounded_utf8


SNAPSHOT = {
    "revision": 4,
    "sessions": [
        {"session_key": "s-claude", "agent": "claude", "label": "alpha", "project_label": "hardware", "state": "idle", "capabilities": {"steer": True, "permission_reply": True}},
        {"session_key": "s-codex", "agent": "codex", "label": "beta", "project_label": "hardware", "state": "idle", "capabilities": {"steer": True, "permission_reply": False}},
        {"session_key": "s-open", "agent": "opencode", "label": "gamma", "project_label": "website", "state": "running", "capabilities": {"steer": True, "permission_reply": True}},
        {"session_key": "s-kimi", "agent": "kimi", "label": "delta", "project_label": "notes", "state": "idle", "capabilities": {"steer": True, "permission_reply": False}},
    ],
}


def test_explicit_agent_project_and_session_aliases_resolve_exact_text():
    router = MultiAgentRouter({"小表 codex": {"agent": "codex", "project_label": "hardware"}})
    command = "解释 '$HOME' 和 `pwd`\n不要展开"
    proposal = router.propose("小表 codex " + command, SNAPSHOT)
    assert proposal.session_key == "s-codex"
    assert proposal.text == command
    assert proposal.target_revision == 4

    assert MultiAgentRouter().propose("claude alpha run tests", SNAPSHOT).session_key == "s-claude"
    assert MultiAgentRouter().propose("opencode website fix css", SNAPSHOT).session_key == "s-open"


@pytest.mark.parametrize("text,code", [
    ("run tests", "target_required"),
    ("new codex task", "spawn_not_supported"),
    ("codex", "empty_command"),
])
def test_missing_target_spawn_and_empty_command_fail_closed(text, code):
    with pytest.raises(RouterError) as excinfo:
        MultiAgentRouter().propose(text, SNAPSHOT)
    assert excinfo.value.code == code


def test_ambiguous_and_unsteerable_targets_fail_closed():
    duplicate = {**SNAPSHOT, "sessions": SNAPSHOT["sessions"] + [{**SNAPSHOT["sessions"][1], "session_key": "s-codex-2", "label": "other"}]}
    with pytest.raises(RouterError) as ambiguous:
        MultiAgentRouter().propose("codex run tests", duplicate)
    assert ambiguous.value.code == "target_ambiguous"
    stale = {**SNAPSHOT, "sessions": [{**SNAPSHOT["sessions"][1], "capabilities": {"steer": False, "permission_reply": False}}]}
    with pytest.raises(RouterError) as missing:
        MultiAgentRouter().propose("codex run tests", stale)
    assert missing.value.code == "target_not_found"


def test_proposal_store_is_one_pending_atomic_expiring_and_disconnect_safe():
    now = [10.0]
    router = MultiAgentRouter(ttl_seconds=60, clock=lambda: now[0])
    proposal = router.propose("kimi summarize", SNAPSHOT)
    store = ProposalStore(clock=lambda: now[0])
    store.put("watch-1", proposal)
    assert store.consume("watch-1", proposal.command_id) == proposal
    with pytest.raises(RouterError):
        store.consume("watch-1", proposal.command_id)
    expired = replace(proposal, command_id="expired", expires_at=11.0)
    store.put("watch-1", expired)
    now[0] = 12.0
    with pytest.raises(RouterError) as excinfo:
        store.consume("watch-1", "expired")
    assert excinfo.value.code == "proposal_expired"
    store.put("watch-1", proposal)
    store.invalidate("watch-1")
    with pytest.raises(RouterError):
        store.consume("watch-1", proposal.command_id)


def test_task_tracker_orders_bounds_and_restores_without_dispatch():
    proposal = MultiAgentRouter().propose("claude alpha run", SNAPSHOT)
    tracker = TaskTracker(max_terminal=2)
    tracker.accepted("task-1", proposal)
    assert tracker.observe({"type": "task.running", "task_id": "task-1", "event_seq": 2})["type"] == "task.running"
    assert tracker.observe({"type": "task.failed", "task_id": "task-1", "event_seq": 1}) is None
    done = tracker.observe({"type": "task.completed", "task_id": "task-1", "event_seq": 3, "summary": "你" * 1000, "summary_source": "pane_snapshot"})
    assert len(done["summary"].encode()) <= 512
    assert tracker.snapshot("task-1") == done
    assert tracker.observe({"type": "task.running", "task_id": "old", "event_seq": 1}) is None


def test_utf8_bounding_never_splits_codepoint():
    value = bounded_utf8("你" * 100, 31)
    assert len(value.encode()) <= 31 and value.endswith("…")
