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
