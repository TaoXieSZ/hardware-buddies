from __future__ import annotations

import asyncio

from control_plane.cmux_control import Session
from control_plane.local_api import EventRing, LocalControlAPI
from control_plane.stager import RouteStager


class FakeCmux:
    def __init__(self):
        self.sessions = [
            Session(1, "alpha", "surface-claude", "w1", "repo · Claude task", "/work/repo", True, "claude-1"),
            Session(2, "bravo", "surface-codex-1", "w2", "Codex one", "/work/shared", False),
            Session(3, "charlie", "surface-codex-2", "w3", "Codex two", "/work/shared", False),
            Session(4, "delta", "surface-opencode", "w4", "OpenCode task", "/work/open", False),
            Session(5, "echo", "surface-kimi", "w5", "Kimi task", "/work/kimi", False),
        ]
        self.metadata = {
            "surface-claude": {"agent": "claude", "session_id": "claude-1", "working_directory": "/work/repo"},
            "surface-codex-1": {"agent": "codex", "session_id": "", "working_directory": "/work/shared"},
            "surface-codex-2": {"agent": "codex", "session_id": "", "working_directory": "/work/shared"},
            "surface-opencode": {"agent": "opencode", "session_id": "ses-open", "working_directory": "/work/open"},
            "surface-kimi": {"agent": "kimi", "session_id": "kimi-1", "working_directory": "/work/kimi"},
        }
        self.routes = []

    def list_sessions(self):
        return list(self.sessions)

    def agent_surface_metadata(self):
        return dict(self.metadata)

    def route_surface(self, surface, text):
        if surface not in {session.surface for session in self.sessions}:
            raise KeyError(surface)
        self.routes.append((surface, text))
        return surface

    def read_surface(self, surface):
        return "last pane status with secret-looking text" * 30


def make_api(pending=None):
    cmux = FakeCmux()
    stager = RouteStager(cmux.route_surface)
    return LocalControlAPI(cmux, stager, pending), cmux


def test_snapshot_is_normalized_bounded_monotonic_and_fails_closed_on_collision():
    api, cmux = make_api()
    first = api.snapshot()
    second = api.snapshot()

    assert first["revision"] == second["revision"] == 1
    assert {row["agent"] for row in first["sessions"]} == {"claude", "codex", "opencode", "kimi"}
    assert all("surface" not in row and "cwd" not in row for row in first["sessions"])
    assert all(len(row["session_key"]) == 32 for row in first["sessions"])
    codex = [row for row in first["sessions"] if row["agent"] == "codex"]
    assert len(codex) == 2 and not any(row["capabilities"]["steer"] for row in codex)
    capabilities = {row["agent"]: row["capabilities"] for row in first["sessions"] if row["agent"] != "codex"}
    assert capabilities["claude"]["permission_reply"] is True
    assert capabilities["opencode"]["permission_reply"] is True
    assert capabilities["kimi"]["permission_reply"] is False

    cmux.sessions[0] = Session(1, "alpha", "surface-claude", "w1", "renamed", "/work/repo", True, "claude-1")
    assert api.snapshot()["revision"] == 2


def test_stage_confirm_routes_exact_reviewed_text_once_and_revalidates_revision():
    api, cmux = make_api()
    snapshot = api.snapshot()
    claude = next(row for row in snapshot["sessions"] if row["agent"] == "claude")
    text = "explain '$HOME' and `pwd`; do not expand\nsecond line"

    assert api.stage("cmd-1", claude["session_key"], snapshot["revision"], text) == {"ok": True}
    assert api.confirm("cmd-old")["fired"] is False
    assert api.confirm("cmd-1") == {"ok": True, "fired": True}
    assert api.confirm("cmd-1")["fired"] is False
    assert cmux.routes == [("surface-claude", text)]


def test_target_disappearance_fails_closed():
    api, cmux = make_api()
    snapshot = api.snapshot()
    target = next(row for row in snapshot["sessions"] if row["agent"] == "kimi")
    assert api.stage("cmd-k", target["session_key"], snapshot["revision"], "status") == {"ok": True}
    cmux.sessions = [s for s in cmux.sessions if s.surface != "surface-kimi"]

    response = api.confirm("cmd-k")
    assert response == {"ok": False, "error": "target_stale", "fired": False}
    assert cmux.routes == []


def test_event_ring_is_bounded_and_reports_cursor_gap():
    ring = EventRing(capacity=2)
    ring.publish("one")
    ring.publish("two")
    ring.publish("three")
    assert ring.poll(0)["gap"] is True
    assert [event["type"] for event in ring.poll(1)["events"]] == ["two", "three"]


def test_permission_resolution_is_first_response_wins():
    async def scenario():
        future = asyncio.get_running_loop().create_future()
        api, _ = make_api({"rid-1": future})
        assert api.resolve_permission("rid-1", "approve") == {"ok": True}
        assert future.result() == "approve"
        assert api.resolve_permission("rid-1", "deny") == {"ok": False, "error": "already_resolved"}

    asyncio.run(scenario())


def test_task_status_is_bounded_and_permission_event_uses_opaque_session_key():
    api, _ = make_api()
    snapshot = api.snapshot()
    claude = next(row for row in snapshot["sessions"] if row["agent"] == "claude")
    status = api.task_status(claude["session_key"])
    assert status["ok"] is True
    assert status["summary_source"] == "pane_snapshot"
    assert len(status["summary"]) <= 512

    api.publish_permission("rid", "claude", "claude-1", "shell", "bounded hint")
    event = api.events.poll(0)["events"][-1]
    assert event["session_key"] == claude["session_key"]
    assert event["actionable"] is True
    assert "surface" not in event and "cwd" not in event


def test_codex_tracked_state_joins_by_full_cwd_and_fails_closed_on_ambiguity():
    class State:
        _sessions = {}
        ext_sessions = {
            "codex": {
                "sessions": [
                    {"sid": "shared", "cwd": "/work/shared", "running": True, "st": "thinking"},
                    {"sid": "shared", "cwd": "/other/shared", "running": False, "st": "idle"},
                ]
            }
        }

    api, _ = make_api()
    api._state = State()
    assert api._tracked_state("codex", "/work/shared")["st"] == "thinking"
    assert api._tracked_state("codex", "/missing/shared") == {}
