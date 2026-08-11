from __future__ import annotations

import asyncio
import json

import pytest

from bridge import ConnectionHandler
from dashboard import DashboardState
from multi_agent import MultiAgentRouter, RouterError
from protocol_v2 import (
    BRIDGE_TO_DEVICE,
    DEVICE_TO_BRIDGE,
    AuthenticatedSession,
    b64url_encode,
    make_proof,
)
from test_bridge import FakeASR, FakeWebSocket, VALID_AUDIO, msg


SECRET = bytes(range(32))
DEVICE_NONCE = b64url_encode(b"d" * 24)


class FakeControlClient:
    def __init__(self):
        self.calls = []
        self.live = True

    async def snapshot(self):
        sessions = [] if not self.live else [{
            "session_key": "opaque-codex", "agent": "codex", "label": "beta",
            "project_label": "hardware", "state": "idle",
            "capabilities": {"steer": True, "permission_reply": False},
        }]
        return {"ok": True, "revision": 9, "sessions": sessions}

    async def stage(self, proposal):
        self.calls.append(("stage", proposal.command_id, proposal.session_key, proposal.text))
        return {"ok": self.live, "error": None if self.live else "target_stale"}

    async def confirm(self, command_id):
        self.calls.append(("confirm", command_id))
        return {"ok": self.live, "fired": self.live, "error": None if self.live else "target_stale"}

    async def resolve_permission(self, request_id, decision):
        self.calls.append(("permission", request_id, decision))
        return {"ok": True}


async def authenticate(handler, websocket):
    await handler._handle_text(websocket, msg({
        "type": "hello", "protocol": 2, "device_id": "watch-test",
        "device_nonce": DEVICE_NONCE,
    }))
    challenge = websocket.sent[-1]
    proof = make_proof(
        SECRET, "device", "watch-test", DEVICE_NONCE,
        challenge["bridge_nonce"], challenge["session_id"],
    )
    await handler._handle_text(websocket, msg({"type": "auth.proof", "proof": proof}))
    device = AuthenticatedSession(
        SECRET, challenge["session_id"], BRIDGE_TO_DEVICE, DEVICE_TO_BRIDGE)
    assert device.decode(websocket.sent[-1]) == {"type": "auth.ok"}
    return device


def test_authenticated_transcript_proposes_then_approve_dispatches_exactly_once():
    async def scenario():
        websocket = FakeWebSocket([])
        control = FakeControlClient()
        dashboard = DashboardState()
        handler = ConnectionHandler(
            FakeASR("小表 codex explain '$HOME' and `pwd`"), control_secret=SECRET,
            router=MultiAgentRouter({"小表 codex": {"agent": "codex", "project_label": "hardware"}}),
            control_client=control,
            dashboard_state=dashboard,
        )
        device = await authenticate(handler, websocket)
        await handler._handle_text(websocket, json.dumps(device.encode({"type": "utterance.start", "id": "u1", "audio": VALID_AUDIO})))
        await handler._handle_binary(b"\x00\x00")
        await handler._handle_text(websocket, json.dumps(device.encode({"type": "utterance.end", "id": "u1"})))
        transcript = device.decode(websocket.sent[-2])
        proposal = device.decode(websocket.sent[-1])
        assert transcript["type"] == "transcript"
        assert proposal["type"] == "command.proposal"
        assert proposal["preview"] == "explain '$HOME' and `pwd`"
        assert control.calls == []

        decision = device.encode({
            "type": "command.decision", "command_id": proposal["command_id"], "decision": "approve"})
        await handler._handle_text(websocket, json.dumps(decision))
        accepted = device.decode(websocket.sent[-1])
        assert accepted["type"] == "task.accepted"
        assert control.calls == [
            ("stage", proposal["command_id"], "opaque-codex", "explain '$HOME' and `pwd`"),
            ("confirm", proposal["command_id"]),
        ]
        assert dashboard.snapshot()["pipeline"]["stage"] == "accepted"
        assert dashboard.snapshot()["proposal"]["text"] == "explain '$HOME' and `pwd`"

        with pytest.raises(Exception) as replay:
            await handler._handle_text(websocket, json.dumps(decision))
        assert getattr(replay.value, "code", None) == "replay"
        assert len(control.calls) == 2

    asyncio.run(scenario())


def test_reject_and_disconnect_never_dispatch():
    async def scenario():
        websocket = FakeWebSocket([])
        control = FakeControlClient()
        handler = ConnectionHandler(
            FakeASR("codex harmless"), control_secret=SECRET,
            router=MultiAgentRouter(), control_client=control,
        )
        device = await authenticate(handler, websocket)
        await handler._handle_text(websocket, json.dumps(device.encode({"type": "utterance.start", "id": "u2", "audio": VALID_AUDIO})))
        await handler._handle_binary(b"\x00\x00")
        await handler._handle_text(websocket, json.dumps(device.encode({"type": "utterance.end", "id": "u2"})))
        device.decode(websocket.sent[-2])
        proposal = device.decode(websocket.sent[-1])
        await handler._handle_text(websocket, json.dumps(device.encode({
            "type": "command.decision", "command_id": proposal["command_id"], "decision": "reject"})))
        assert device.decode(websocket.sent[-1])["type"] == "task.cancelled"
        assert control.calls == []

    asyncio.run(scenario())


def test_invalid_device_proof_does_not_authenticate_or_route():
    async def scenario():
        websocket = FakeWebSocket([])
        control = FakeControlClient()
        handler = ConnectionHandler(
            FakeASR(), control_secret=SECRET, router=MultiAgentRouter(), control_client=control)
        await handler._handle_text(websocket, msg({
            "type": "hello", "protocol": 2, "device_id": "watch-test", "device_nonce": DEVICE_NONCE}))
        with pytest.raises(Exception) as excinfo:
            await handler._handle_text(websocket, msg({"type": "auth.proof", "proof": b64url_encode(b"x" * 32)}))
        assert getattr(excinfo.value, "code", None) == "authentication_failed"
        assert handler._auth is None and control.calls == []

    asyncio.run(scenario())


def test_authenticated_session_rejects_plaintext_side_effecting_message():
    async def scenario():
        websocket = FakeWebSocket([])
        control = FakeControlClient()
        handler = ConnectionHandler(
            FakeASR(), control_secret=SECRET, router=MultiAgentRouter(), control_client=control)
        await authenticate(handler, websocket)
        with pytest.raises(Exception) as excinfo:
            await handler._handle_text(websocket, msg({
                "type": "command.decision", "command_id": "cmd", "decision": "approve"}))
        assert getattr(excinfo.value, "code", None) == "plaintext_control"
        assert control.calls == []

    asyncio.run(scenario())


def test_task_observation_forwards_permission_running_and_bounded_completion_without_redispatch():
    class ObservingControl(FakeControlClient):
        def __init__(self):
            super().__init__()
            self.statuses = [
                {"ok": True, "state": "running", "summary": ""},
                {"ok": True, "state": "idle", "summary": "完成" * 1000, "summary_source": "pane_snapshot"},
            ]
            self.poll_count = 0

        async def events(self, after):
            self.poll_count += 1
            events = [] if self.poll_count > 1 else [{
                "cursor": 1, "type": "permission.request", "session_key": "opaque-codex",
                "request_id": "rid-1", "agent": "codex", "tool": "shell",
                "hint": "answer in terminal", "actionable": False,
            }]
            return {"ok": True, "gap": False, "events": events, "next_cursor": self.poll_count}

        async def task_status(self, session_key):
            assert session_key == "opaque-codex"
            return self.statuses.pop(0)

    async def scenario():
        websocket = FakeWebSocket([])
        control = ObservingControl()
        router = MultiAgentRouter()
        dashboard = DashboardState()
        handler = ConnectionHandler(
            FakeASR(), router=router, control_client=control,
            observation_interval=0, dashboard_state=dashboard)
        proposal = router.propose("codex observe", await control.snapshot())
        handler._tasks.accepted("task-observe", proposal)
        await handler._observe_task(websocket, "task-observe", proposal.session_key)

        assert [item["type"] for item in websocket.sent] == [
            "permission.request", "task.running", "task.completed"]
        assert websocket.sent[0]["actionable"] is False
        assert len(websocket.sent[-1]["summary"].encode()) <= 512
        assert websocket.sent[-1]["summary_source"] == "pane_snapshot"
        assert control.calls == []
        assert handler._tasks.snapshot("task-observe")["type"] == "task.completed"
        snapshot = dashboard.snapshot()
        assert snapshot["pipeline"]["stage"] == "completed"
        assert snapshot["permission"] is None

    asyncio.run(scenario())


@pytest.mark.parametrize(
    ("transcript", "sessions", "expected"),
    [
        ("please run tests", None, "target_required"),
        ("claude run tests", None, "target_not_found"),
        ("new codex task", None, "spawn_not_supported"),
        ("codex run tests", [
            {"session_key": "one", "agent": "codex", "label": "one", "project_label": "p",
             "state": "idle", "capabilities": {"steer": True}},
            {"session_key": "two", "agent": "codex", "label": "two", "project_label": "p",
             "state": "idle", "capabilities": {"steer": True}},
        ], "target_ambiguous"),
    ],
)
def test_successful_asr_surfaces_actionable_route_errors_without_dispatch(transcript, sessions, expected):
    async def scenario():
        websocket = FakeWebSocket([])
        control = FakeControlClient()
        if sessions is not None:
            async def snapshot():
                return {"ok": True, "revision": 10, "sessions": sessions}
            control.snapshot = snapshot
        dashboard = DashboardState()
        handler = ConnectionHandler(
            FakeASR(transcript), control_secret=SECRET, router=MultiAgentRouter(),
            control_client=control, dashboard_state=dashboard)
        device = await authenticate(handler, websocket)
        await handler._handle_text(websocket, json.dumps(device.encode({
            "type": "utterance.start", "id": "route-case", "audio": VALID_AUDIO})))
        await handler._handle_binary(b"\x00\x00")
        await handler._handle_text(websocket, json.dumps(device.encode({
            "type": "utterance.end", "id": "route-case"})))

        response = device.decode(websocket.sent[-1])
        assert response["type"] == "command.error" and response["code"] == expected
        snapshot = dashboard.snapshot()
        assert snapshot["pipeline"]["stage"] == "route_error"
        assert snapshot["pipeline"]["error_code"] == expected
        assert snapshot["pipeline"]["hint"]
        assert control.calls == []

    asyncio.run(scenario())


def test_control_plane_failure_after_asr_is_visible_and_never_dispatches():
    class OfflineControl(FakeControlClient):
        async def snapshot(self):
            raise RouterError("control_plane_unavailable")

    async def scenario():
        websocket = FakeWebSocket([])
        control = OfflineControl()
        dashboard = DashboardState()
        handler = ConnectionHandler(
            FakeASR("codex run tests"), control_secret=SECRET, router=MultiAgentRouter(),
            control_client=control, dashboard_state=dashboard)
        device = await authenticate(handler, websocket)
        await handler._handle_text(websocket, json.dumps(device.encode({
            "type": "utterance.start", "id": "offline", "audio": VALID_AUDIO})))
        await handler._handle_binary(b"\x00\x00")
        await handler._handle_text(websocket, json.dumps(device.encode({
            "type": "utterance.end", "id": "offline"})))

        assert device.decode(websocket.sent[-1])["code"] == "control_plane_unavailable"
        snapshot = dashboard.snapshot()
        assert snapshot["pipeline"]["stage"] == "route_error"
        assert snapshot["control_plane"]["healthy"] is False
        assert control.calls == []

    asyncio.run(scenario())


def test_connection_reconnect_ignores_stale_disconnect_and_tracks_protocol_failure():
    async def scenario():
        dashboard = DashboardState()
        first = ConnectionHandler(FakeASR(), dashboard_state=dashboard)
        second = ConnectionHandler(FakeASR(), dashboard_state=dashboard)
        first._dash("watch.connected", connection_id=first._connection_id)
        second._dash("watch.connected", connection_id=second._connection_id)
        first._dash("watch.disconnected", connection_id=first._connection_id)
        assert dashboard.snapshot()["watch"]["connected"] is True
        second._dash("protocol.failed", code="invalid_sequence", hint="retry")
        assert dashboard.snapshot()["pipeline"]["error_code"] == "invalid_sequence"

    asyncio.run(scenario())
