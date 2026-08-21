from __future__ import annotations

import asyncio
from dataclasses import replace

import pytest

from multi_agent import (
    MultiAgentRouter,
    ProposalStore,
    RouterError,
    TaskTracker,
    bounded_utf8,
    compile_whitelist,
    match_whitelist,
)


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


def test_fullwidth_punctuation_after_alias_still_routes():
    router = MultiAgentRouter({"测试绘画": {"agent": "codex", "project_label": "hardware"}})
    proposal = router.propose("测试绘画，介绍一下你自己。", SNAPSHOT)
    assert proposal.session_key == "s-codex"
    assert proposal.text == "介绍一下你自己。"

    # Built-in agent aliases accept fullwidth boundaries too.
    assert MultiAgentRouter().propose("codex，run tests", SNAPSHOT).text == "run tests"
    # No boundary char → not an alias match (prefix of a longer word).
    with pytest.raises(RouterError) as excinfo:
        MultiAgentRouter({"测试": {"agent": "codex"}}).propose("测试员开会", SNAPSHOT)
    assert excinfo.value.code == "target_required"


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


def test_propose_target_selects_unique_session():
    router = MultiAgentRouter()
    proposal = router.propose_target("run tests", SNAPSHOT, {"agent": "codex", "label": "beta"})
    assert proposal.session_key == "s-codex"
    assert proposal.text == "run tests"
    assert proposal.command_id.startswith("cmd-")


def test_propose_target_rejects_ambiguous_target():
    router = MultiAgentRouter()
    with pytest.raises(RouterError) as excinfo:
        router.propose_target("run tests", SNAPSHOT, {"project_label": "hardware"})
    assert excinfo.value.code == "target_ambiguous"
    duplicate = {**SNAPSHOT, "sessions": SNAPSHOT["sessions"] +
                 [{**SNAPSHOT["sessions"][1], "session_key": "s-codex-2", "label": "other"}]}
    with pytest.raises(RouterError) as excinfo:
        router.propose_target("run tests", duplicate, {"agent": "codex"})
    assert excinfo.value.code == "target_ambiguous"


def test_propose_target_rejects_bad_selector_and_empty_command():
    router = MultiAgentRouter()
    with pytest.raises(RouterError) as excinfo:
        router.propose_target("run tests", SNAPSHOT, {"nope": "x"})
    assert excinfo.value.code == "invalid_selector"
    with pytest.raises(RouterError) as excinfo:
        router.propose_target("run tests", SNAPSHOT, {})
    assert excinfo.value.code == "invalid_selector"
    with pytest.raises(RouterError) as excinfo:
        router.propose_target("   ", SNAPSHOT, {"agent": "codex"})
    assert excinfo.value.code == "empty_command"


def test_propose_target_rejects_unsteerable_target():
    stale = {**SNAPSHOT, "sessions": [{**SNAPSHOT["sessions"][1], "capabilities": {"steer": False}}]}
    with pytest.raises(RouterError) as excinfo:
        MultiAgentRouter().propose_target("run tests", stale, {"agent": "codex", "label": "beta"})
    assert excinfo.value.code == "target_not_found"


def test_match_whitelist_search_semantics():
    patterns = compile_whitelist([r"^git\s+(status|diff|log)\b"])
    assert match_whitelist("git status", patterns) is True
    assert match_whitelist("git diff HEAD", patterns) is True
    assert match_whitelist("git push origin main", patterns) is False
    assert match_whitelist("", patterns) is False
    patterns = compile_whitelist([r"^(查看|看看|查一下)", r"^tail\s", r"^cat\s"])
    assert match_whitelist("看看日志", patterns) is True
    assert match_whitelist("帮我看看日志", patterns) is False


def test_compile_whitelist_fails_loud_on_bad_pattern():
    with pytest.raises(RouterError) as excinfo:
        compile_whitelist([r"(unclosed"])
    assert excinfo.value.code == "invalid_whitelist_pattern"
    assert compile_whitelist([]) == []
