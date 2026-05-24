"""Staging state machine for gesture-gated routing.

A voice command is STAGED (not sent) when the agent calls route_to_session.
The user then confirms with a thumbs-up gesture (→ confirm, fires the cmux
send) or thumbs-down (→ cancel). A pending command auto-expires after a TTL so
a stale stage never fires much later.

Holds at most one pending command (last-wins). The three mutators run on
different threads — stage() on the daemon socket coroutine (event loop),
cancel() on the frame-callback, confirm() on an executor thread (the cmux send
is blocking) — so all access to `_pending` is guarded by a lock. The actual
route action runs OUTSIDE the lock (it shells out to cmux; we must not hold the
lock across a subprocess).

Pure/host-testable: the route action and the clock are injected.
"""
from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional

# route_fn(number, text) performs the actual cmux send (e.g. CmuxClient.route).
RouteFn = Callable[[int, str], object]
Clock = Callable[[], float]


@dataclass
class Pending:
    number: int
    text: str
    staged_at: float


class RouteStager:
    def __init__(
        self,
        route_fn: RouteFn,
        ttl_s: float = 60.0,
        clock: Optional[Clock] = None,
    ):
        self._route = route_fn
        self._ttl = ttl_s
        self._clock = clock or time.monotonic
        self._pending: Optional[Pending] = None
        self._lock = threading.Lock()

    # Assumes the lock is held. Clears + returns nothing.
    def _drop_if_expired_locked(self) -> None:
        if (
            self._pending is not None
            and (self._clock() - self._pending.staged_at) > self._ttl
        ):
            self._pending = None

    def peek(self) -> Optional[Pending]:
        """Current pending command, or None (clears it first if expired)."""
        with self._lock:
            self._drop_if_expired_locked()
            return self._pending

    def stage(self, number: int, text: str) -> None:
        """Stage a command (last-wins), resetting the TTL."""
        with self._lock:
            self._pending = Pending(number=number, text=text, staged_at=self._clock())

    def confirm(self) -> bool:
        """Thumbs-up: fire the staged route. Returns True if something fired.

        Takes + clears the pending under the lock, then runs the (blocking)
        route action with the lock released so a concurrent stage/cancel is
        never blocked and the route_fn may safely re-enter the stager.
        """
        with self._lock:
            self._drop_if_expired_locked()
            p = self._pending
            self._pending = None
        if p is None:
            return False
        self._route(p.number, p.text)
        return True

    def cancel(self) -> bool:
        """Thumbs-down: drop any staged command. Returns True if one existed."""
        with self._lock:
            self._drop_if_expired_locked()
            had = self._pending is not None
            self._pending = None
        return had
