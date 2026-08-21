from __future__ import annotations

import asyncio
import contextlib
import time
import uuid
from dataclasses import dataclass
from typing import Any


LOG_NAME = "walkie_brain"


@dataclass
class BrainItem:
    item_id: str
    kind: str  # "transcript" | "permission"
    payload: dict[str, Any]
    future: asyncio.Future
    created_at: float


class BrainQueue:
    """Single-asyncio-loop work queue for the brain control API.

    `submit` returns a BrainItem whose future is resolved by `resolve` (a
    brain decision) or cancelled by `forget` (timeout/fallback). `pop` is a
    long-poll VIEW — items stay until resolved/forgotten, so a second poller
    may see the same item; the first decision wins.
    """

    def __init__(self, max_pending: int = 4, clock=time.monotonic):
        self.max_pending = max(1, int(max_pending))
        self._clock = clock
        self._items: dict[str, BrainItem] = {}
        self._order: list[str] = []
        self._cond = asyncio.Condition()
        self._loop: asyncio.AbstractEventLoop | None = None

    def _ensure_loop(self) -> asyncio.AbstractEventLoop:
        if self._loop is None or self._loop.is_closed():
            self._loop = asyncio.get_running_loop()
        return self._loop

    async def submit(self, kind: str, payload: dict[str, Any]) -> BrainItem | None:
        loop = self._ensure_loop()
        if len(self._items) >= self.max_pending:
            return None
        item = BrainItem(
            item_id="brain-" + uuid.uuid4().hex[:12],
            kind=kind,
            payload=payload,
            future=loop.create_future(),
            created_at=self._clock(),
        )
        self._items[item.item_id] = item
        self._order.append(item.item_id)
        async with self._cond:
            self._cond.notify_all()
        return item

    async def pop(self, wait_ms: int = 25000) -> dict[str, Any] | None:
        wait_ms = max(0, min(int(wait_ms), 60000))
        async with self._cond:
            if not self._items:
                try:
                    await asyncio.wait_for(self._cond.wait(), wait_ms / 1000)
                except TimeoutError:
                    return None
            for item_id in self._order:
                item = self._items.get(item_id)
                if item is not None:
                    return {"item_id": item.item_id, "kind": item.kind, **item.payload}
            return None

    def resolve(self, item_id: str, decision: Any) -> bool:
        item = self._items.get(item_id)
        if item is None or item.future.done():
            return False
        item.future.set_result(decision)
        self._drop(item_id)
        return True

    def forget(self, item_id: str) -> None:
        item = self._items.get(item_id)
        if item is None:
            return
        if not item.future.done():
            item.future.cancel()
        self._drop(item_id)

    def _drop(self, item_id: str) -> None:
        self._items.pop(item_id, None)
        with contextlib.suppress(ValueError):
            self._order.remove(item_id)
