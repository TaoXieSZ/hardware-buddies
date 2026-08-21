from __future__ import annotations

import asyncio

from brain_api import BrainQueue


def test_brain_queue_submit_pop_resolve():
    async def scenario():
        queue = BrainQueue(max_pending=2)
        item = await queue.submit("transcript", {"text": "hi"})
        assert item is not None and not item.future.done()
        popped = await asyncio.wait_for(queue.pop(50), 1)
        assert popped is not None and popped["item_id"] == item.item_id
        assert popped["kind"] == "transcript" and popped["text"] == "hi"
        assert queue.resolve(item.item_id, {"kind": "route", "command": "hi"}) is True
        assert (await asyncio.wait_for(item.future, 1))["command"] == "hi"
        assert queue.resolve(item.item_id, {"kind": "route"}) is False  # already resolved
        assert await asyncio.wait_for(queue.pop(50), 1) is None
    asyncio.run(scenario())


def test_brain_queue_pop_returns_none_on_timeout():
    async def scenario():
        queue = BrainQueue(max_pending=2)
        assert await asyncio.wait_for(queue.pop(50), 1) is None
    asyncio.run(scenario())


def test_brain_queue_cap_and_forget():
    async def scenario():
        queue = BrainQueue(max_pending=1)
        first = await queue.submit("transcript", {})
        assert first is not None
        assert await queue.submit("transcript", {}) is None  # full
        popped = await asyncio.wait_for(queue.pop(50), 1)
        queue.forget(popped["item_id"])
        assert await asyncio.wait_for(queue.pop(50), 1) is None
        assert first.future.cancelled() or first.future.done()
        assert await queue.submit("transcript", {}) is not None  # slot freed
    asyncio.run(scenario())


def test_brain_queue_resolve_unknown_item():
    async def scenario():
        queue = BrainQueue(max_pending=2)
        assert queue.resolve("brain-unknown", {}) is False
        queue.forget("brain-unknown")  # no-op, must not raise
    asyncio.run(scenario())


import json

import httpx

from brain_api import BrainServer, BrainServices


class FakeServices(BrainServices):
    def __init__(self):
        self.spoken = []
        self.proposals = []
        self.decisions = []
        self.popped = []

    def status(self) -> dict:
        return {"ok": True, "fake": 1}

    def events(self, after: int, limit: int) -> dict:
        return {"events": [], "next_sequence": 0, "after": after, "limit": limit}

    def pop_queue(self, wait_ms: int) -> dict | None:
        self.popped.append(wait_ms)
        return None

    def submit_decision(self, item_id: str, decision: dict) -> dict:
        self.decisions.append((item_id, decision))
        return {"ok": True, "accepted": True}

    def submit_proposal(self, text: str, selector: dict) -> dict:
        self.proposals.append((text, selector))
        return {"ok": True, "command_id": "cmd-test", "gated": True}

    def speak(self, text: str) -> dict:
        self.spoken.append(text)
        return {"ok": True}


def _server() -> BrainServer:
    server = BrainServer(services=FakeServices(), auth_token="secret-token", port=0)
    server.start()
    return server


AUTH = {"Authorization": "Bearer secret-token"}


def test_brain_api_requires_bearer_token():
    server = _server()
    try:
        with httpx.Client(base_url=server.url) as client:
            assert client.get("/api/v1/status").status_code == 401
            assert client.get("/api/v1/status", headers={"Authorization": "Bearer wrong"}).status_code == 401
            assert client.get("/api/v1/status", headers=AUTH).status_code == 200
            assert client.get("/api/v1/brain/queue", headers=AUTH).status_code == 200
    finally:
        server.stop()


def test_brain_api_events_and_queue_routing():
    server = _server()
    try:
        with httpx.Client(base_url=server.url, headers=AUTH) as client:
            events = client.get("/api/v1/events?after=7&limit=3")
            assert events.status_code == 200
            assert events.json()["after"] == 7 and events.json()["limit"] == 3
            queue = client.get("/api/v1/brain/queue?wait_ms=50")
            assert queue.status_code == 200 and queue.json() == {"item": None}
            assert server.services.popped == [50]
    finally:
        server.stop()


def test_brain_api_proposals_and_tts():
    server = _server()
    try:
        with httpx.Client(base_url=server.url, headers=AUTH) as client:
            response = client.post("/api/v1/proposals", json={"text": "run tests", "agent": "codex"})
            assert response.status_code == 200 and response.json()["gated"] is True
            assert server.services.proposals == [("run tests", {"agent": "codex"})]
            long = client.post("/api/v1/tts", json={"text": "x" * 1000})
            assert long.status_code == 400 and long.json()["error"] == "text_too_large"
            assert client.post("/api/v1/tts", json={"text": "hello"}).json() == {"ok": True}
            assert server.services.spoken == ["hello"]
            missing = client.post("/api/v1/tts", json={})
            assert missing.status_code == 400 and missing.json()["error"] == "invalid_json"
    finally:
        server.stop()


def test_brain_api_decision_validation_and_unknown_routes():
    server = _server()
    try:
        with httpx.Client(base_url=server.url, headers=AUTH) as client:
            decision = client.post("/api/v1/brain/decision", json={
                "item_id": "brain-1", "decision": {"kind": "route", "command": "hi"}})
            assert decision.status_code == 200 and decision.json() == {"ok": True, "accepted": True}
            assert server.services.decisions == [("brain-1", {"kind": "route", "command": "hi"})]
            bad = client.post("/api/v1/brain/decision", json={"item_id": "brain-1"})
            assert bad.status_code == 400 and bad.json()["error"] == "invalid_json"
            assert client.get("/api/v1/nope", headers=AUTH).status_code == 404
            assert client.post("/api/v1/status", headers=AUTH).status_code == 405
    finally:
        server.stop()


from bridge import ConnectionHandler
from dashboard import DashboardState
from multi_agent import MultiAgentRouter, compile_whitelist
from test_bridge import VALID_AUDIO, FakeASR, FakeWebSocket, msg
from protocol_v2 import BRIDGE_TO_DEVICE, DEVICE_TO_BRIDGE, AuthenticatedSession
from test_control_integration import SECRET, DEVICE_NONCE, FakeControlClient, authenticate


def _handler(transcript="帮我看看日志", **kwargs):
    return ConnectionHandler(
        FakeASR(transcript), control_secret=SECRET,
        router=MultiAgentRouter(), control_client=FakeControlClient(),
        dashboard_state=DashboardState(), **kwargs,
    )


async def _utter(handler, websocket, device, asr_side_effects=None):
    """Drive one utterance through authenticate → start → binary → end."""
    await handler._handle_text(websocket, json.dumps(device.encode(
        {"type": "utterance.start", "id": "u1", "audio": VALID_AUDIO})))
    await handler._handle_binary(b"\x00\x00")
    await handler._handle_text(websocket, json.dumps(device.encode({"type": "utterance.end", "id": "u1"})))


def test_brain_whitelist_direct_dispatch_skips_watch_gate():
    async def scenario():
        websocket = FakeWebSocket([])
        queue = BrainQueue(max_pending=4)
        handler = _handler(
            brain_queue=queue,
            brain_whitelist=compile_whitelist([r"^git\s+(status|diff|log)\b"]),
            brain_timeout_seconds=2.0,
        )
        device = await authenticate(handler, websocket)

        async def fake_brain():
            item = await queue.pop(5000)
            queue.resolve(item["item_id"], {"kind": "route", "agent": "codex",
                                            "label": "beta", "command": "git status"})
        waiter = asyncio.create_task(fake_brain())
        await _utter(handler, websocket, device)
        await waiter
        types = []
        for m in websocket.sent:
            if not isinstance(m, dict):
                continue
            if m.get("direction"):
                fresh = AuthenticatedSession(SECRET, device.session_id, BRIDGE_TO_DEVICE, DEVICE_TO_BRIDGE)
                types.append(fresh.decode(m).get("type"))
            else:
                types.append(m.get("type"))
        assert "command.proposal" not in types
        assert "task.accepted" in types
        assert [call[0] for call in handler._control_client.calls] == ["stage", "confirm"]
        assert handler._control_client.calls[0][3] == "git status"
    asyncio.run(scenario())


def test_brain_timeout_falls_back_to_router():
    async def scenario():
        websocket = FakeWebSocket([])
        handler = _handler("codex explain '$HOME'", brain_queue=BrainQueue(max_pending=4),
                           brain_timeout_seconds=0.2)
        device = await authenticate(handler, websocket)
        await _utter(handler, websocket, device)
        last = device.decode(websocket.sent[-1])
        assert last["type"] == "command.proposal" and last["preview"] == "explain '$HOME'"
        assert handler._control_client.calls == []
    asyncio.run(scenario())


def test_brain_route_without_whitelist_goes_to_watch_gate():
    async def scenario():
        websocket = FakeWebSocket([])
        queue = BrainQueue(max_pending=4)
        handler = _handler(
            brain_queue=queue,
            brain_whitelist=compile_whitelist([r"^git\s+(status|diff|log)\b"]),
            brain_timeout_seconds=2.0,
        )
        device = await authenticate(handler, websocket)

        async def fake_brain():
            item = await queue.pop(5000)
            queue.resolve(item["item_id"], {"kind": "route", "agent": "codex",
                                            "label": "beta", "command": "run tests"})
        waiter = asyncio.create_task(fake_brain())
        await _utter(handler, websocket, device)
        await waiter
        proposal = device.decode(websocket.sent[-1])
        assert proposal["type"] == "command.proposal" and proposal["preview"] == "run tests"
        assert handler._control_client.calls == []
        decision = device.encode({"type": "command.decision",
                                  "command_id": proposal["command_id"], "decision": "approve"})
        await handler._handle_text(websocket, json.dumps(decision))
        assert [call[0] for call in handler._control_client.calls] == ["stage", "confirm"]
    asyncio.run(scenario())


from bridge import BrainServiceBridge, ConnectionHandler, WatchRegistry
from test_bridge import FakeTTS


def test_brain_server_ops_proposal_and_speak_through_watch():
    async def scenario():
        websocket = FakeWebSocket([])
        registry = WatchRegistry()
        handler = _handler(brain_queue=None, watch_registry=registry, tts_client=FakeTTS())
        handler._websocket = websocket  # handle() sets this; unit tests drive _handle_text directly
        services = BrainServiceBridge(
            asyncio.get_running_loop(), DashboardState(), BrainQueue(4),
            FakeControlClient(), MultiAgentRouter(), None, registry)
        server = BrainServer(services, "secret-token", port=0)
        server.start()
        try:
            client = httpx.Client(base_url=server.url, headers=AUTH)
            offline = await asyncio.to_thread(
                client.post, "/api/v1/proposals",
                json={"text": "run tests", "agent": "codex"})
            assert offline.status_code == 200
            assert offline.json() == {"ok": False, "error": "watch_offline"}
            device = await authenticate(handler, websocket)
            response = await asyncio.to_thread(
                client.post, "/api/v1/proposals",
                json={"text": "run tests", "agent": "codex", "label": "beta"})
            assert response.status_code == 200
            body = response.json()
            assert body["ok"] is True and body["gated"] is True
            proposal = device.decode(websocket.sent[-1])
            assert proposal["type"] == "command.proposal" and proposal["preview"] == "run tests"
            spoke = await asyncio.to_thread(client.post, "/api/v1/tts", json={"text": "你好"})
            assert spoke.status_code == 200 and spoke.json() == {"ok": True}
            assert any(isinstance(m, bytes) for m in websocket.sent)  # TTS PCM frames
        finally:
            server.stop()
    asyncio.run(scenario())
