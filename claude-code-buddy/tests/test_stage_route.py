"""Tests for the daemon's `stage_route` socket action (voice control plane).

Drives buddy_core.core.handle_client with a fake reader/writer and a stub
stager — asserts stage_route stages + acks, errors without a stager, and that
ordinary hook events are unaffected (no behaviour change to existing paths).
"""

import asyncio
import json
import logging

from buddy_core.core import BuddyState, handle_client


class FakeWriter:
    def __init__(self):
        self.buf = bytearray()
        self.closed = False

    def write(self, b):
        self.buf.extend(b)

    async def drain(self):
        pass

    def close(self):
        self.closed = True

    async def wait_closed(self):
        pass


class StubStager:
    def __init__(self, pending=True):
        self.staged = []
        self.confirmed = 0
        self.cancelled = 0
        self._pending = pending

    def stage(self, number, text):
        self.staged.append((number, text))

    def confirm(self):
        self.confirmed += 1
        return self._pending

    def cancel(self):
        self.cancelled += 1
        return self._pending


def _run(line: bytes, *, apply_event, stager) -> FakeWriter:
    w = FakeWriter()

    async def _go():
        # StreamReader must be built inside a running loop (py3.14).
        r = asyncio.StreamReader()
        r.feed_data(line)
        r.feed_eof()
        await handle_client(
            r, w, BuddyState(), None, asyncio.Event(),
            apply_event, {}, logging.getLogger("test"), route_stager=stager,
        )

    asyncio.run(_go())
    return w


def test_stage_route_with_nickname_target():
    stager = StubStager()
    msg = json.dumps({"action": "stage_route", "target": "alpha",
                      "text": "run the tests"}).encode() + b"\n"
    w = _run(msg, apply_event=lambda s, e: False, stager=stager)
    assert stager.staged == [("alpha", "run the tests")]
    assert json.loads(w.buf.decode().strip()) == {"ok": True}


def test_stage_route_legacy_session_field_still_accepted():
    # Pre-nickname clients (older say.py, the original voice hook) send
    # `session: int`. Daemon forwards it untouched so cmux_control.route's
    # resolver can handle the legacy positional number.
    stager = StubStager()
    msg = json.dumps({"action": "stage_route", "session": 2,
                      "text": "run the tests"}).encode() + b"\n"
    w = _run(msg, apply_event=lambda s, e: False, stager=stager)
    assert stager.staged == [(2, "run the tests")]
    assert json.loads(w.buf.decode().strip()) == {"ok": True}


def test_stage_route_without_stager_acks_error():
    msg = json.dumps({"action": "stage_route", "session": 1,
                      "text": "x"}).encode() + b"\n"
    w = _run(msg, apply_event=lambda s, e: False, stager=None)
    ack = json.loads(w.buf.decode().strip())
    assert ack["ok"] is False


def test_stage_route_does_not_call_apply_event():
    calls = []
    msg = json.dumps({"action": "stage_route", "session": 1, "text": "x"}).encode() + b"\n"
    _run(msg, apply_event=lambda s, e: calls.append(e), stager=StubStager())
    assert calls == []  # stage_route is not a hook event


def test_ordinary_hook_event_still_applies():
    calls = []
    msg = json.dumps({"hook_event_name": "PreToolUse", "session_id": "abc"}).encode() + b"\n"
    _run(msg, apply_event=lambda s, e: calls.append(e) or False, stager=StubStager())
    assert len(calls) == 1 and calls[0]["hook_event_name"] == "PreToolUse"


def test_confirm_route_commits():
    stager = StubStager(pending=True)
    msg = json.dumps({"action": "confirm_route"}).encode() + b"\n"
    w = _run(msg, apply_event=lambda s, e: False, stager=stager)
    assert stager.confirmed == 1 and stager.cancelled == 0
    assert json.loads(w.buf.decode().strip()) == {"ok": True, "fired": True}


def test_cancel_route_drops():
    stager = StubStager(pending=True)
    msg = json.dumps({"action": "cancel_route"}).encode() + b"\n"
    w = _run(msg, apply_event=lambda s, e: False, stager=stager)
    assert stager.cancelled == 1 and stager.confirmed == 0
    assert json.loads(w.buf.decode().strip()) == {"ok": True, "fired": True}


def test_confirm_route_nothing_staged_reports_not_fired():
    stager = StubStager(pending=False)
    msg = json.dumps({"action": "confirm_route"}).encode() + b"\n"
    w = _run(msg, apply_event=lambda s, e: False, stager=stager)
    assert json.loads(w.buf.decode().strip()) == {"ok": True, "fired": False}
