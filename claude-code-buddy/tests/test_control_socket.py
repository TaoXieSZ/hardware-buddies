from __future__ import annotations

import asyncio
import json
import logging

from buddy_core.core import BuddyState, handle_client


class FakeWriter:
    def __init__(self):
        self.buf = bytearray()

    def write(self, data):
        self.buf.extend(data)

    async def drain(self):
        pass

    def close(self):
        pass

    async def wait_closed(self):
        pass


class FakeEvents:
    def poll(self, after, limit):
        return {"gap": False, "events": [{"cursor": 4, "type": "task.completed"}], "next_cursor": 4}


class FakeControlAPI:
    def __init__(self):
        self.events = FakeEvents()
        self.calls = []

    def snapshot(self):
        return {"revision": 7, "sessions": []}

    def stage(self, *args):
        self.calls.append(("stage", args))
        return {"ok": True}

    def confirm(self, command_id):
        self.calls.append(("confirm", command_id))
        return {"ok": True, "fired": True}

    def cancel(self, command_id):
        self.calls.append(("cancel", command_id))
        return {"ok": True, "cancelled": True}

    def resolve_permission(self, request_id, decision):
        self.calls.append(("permission", request_id, decision))
        return {"ok": True}


def request(payload, api=None):
    writer = FakeWriter()

    async def scenario():
        reader = asyncio.StreamReader()
        reader.feed_data(json.dumps(payload).encode() + b"\n")
        reader.feed_eof()
        await handle_client(
            reader, writer, BuddyState(), None, asyncio.Event(), lambda *_: False,
            {}, logging.getLogger("test"), control_api=api,
        )

    asyncio.run(scenario())
    return json.loads(writer.buf.decode())


def test_control_snapshot_and_events_are_bounded_actions():
    api = FakeControlAPI()
    assert request({"action": "control.snapshot"}, api) == {"ok": True, "revision": 7, "sessions": []}
    response = request({"action": "control.events", "after": 2, "limit": 500}, api)
    assert response["ok"] is True and response["next_cursor"] == 4


def test_control_stage_confirm_cancel_and_permission_actions_are_correlated():
    api = FakeControlAPI()
    assert request({"action": "control.stage", "command_id": "c1", "session_key": "s1", "target_revision": 7, "text": "go"}, api)["ok"]
    assert request({"action": "control.confirm", "command_id": "c1"}, api)["fired"]
    assert request({"action": "control.cancel", "command_id": "c2"}, api)["cancelled"]
    assert request({"action": "control.permission.resolve", "request_id": "r1", "decision": "deny"}, api)["ok"]
    assert api.calls == [
        ("stage", ("c1", "s1", 7, "go")),
        ("confirm", "c1"),
        ("cancel", "c2"),
        ("permission", "r1", "deny"),
    ]


def test_control_action_fails_closed_without_api():
    assert request({"action": "control.snapshot"}) == {"ok": False, "error": "control_unavailable"}
