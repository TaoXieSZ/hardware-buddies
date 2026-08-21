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
