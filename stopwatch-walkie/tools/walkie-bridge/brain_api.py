from __future__ import annotations

import asyncio
import contextlib
import hmac
import json
import threading
import time
import uuid
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Protocol
from urllib.parse import parse_qs, urlsplit

from protocol_v2 import MAX_COMMAND_TEXT

MAX_TTS_TEXT = 400
MAX_BODY_BYTES = 65536
MAX_QUEUE_WAIT_MS = 60000

_SECURITY_HEADERS = {
    "Cache-Control": "no-store",
    "Content-Security-Policy": "default-src 'none'; frame-ancestors 'none'",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
}


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


class BrainServices(Protocol):
    """Thread-safe business surface. Implementations bridge to the event loop."""

    def status(self) -> dict: ...
    def events(self, after: int, limit: int) -> dict: ...
    def pop_queue(self, wait_ms: int) -> dict | None: ...
    def submit_decision(self, item_id: str, decision: dict) -> dict: ...
    def submit_proposal(self, text: str, selector: dict[str, str]) -> dict: ...
    def speak(self, text: str) -> dict: ...


_SELECTOR_KEYS = {"agent", "label", "project_label"}
_GET_PATHS = {"/api/v1/status", "/api/v1/events", "/api/v1/brain/queue"}


def _bounded_str(value: Any, limit: int) -> str | None:
    if not isinstance(value, str) or not value.strip():
        return None
    return value[:limit]


def _parse_body(handler: BaseHTTPRequestHandler) -> dict | None:
    try:
        length = int(handler.headers.get("Content-Length", "0"))
    except ValueError:
        length = 0
    if length > MAX_BODY_BYTES:
        handler._send_json(413, {"error": "payload_too_large"})
        return None
    raw = handler.rfile.read(length)
    try:
        payload = json.loads(raw or b"{}")
    except json.JSONDecodeError:
        handler._send_json(400, {"error": "invalid_json"})
        return None
    if not isinstance(payload, dict):
        handler._send_json(400, {"error": "invalid_json"})
        return None
    return payload


def _handler_factory(services: BrainServices, auth_token: str | None):
    class BrainHandler(BaseHTTPRequestHandler):
        server_version = "WalkieBrain/1"

        def log_message(self, _format, *_args):
            return

        def _send_json(self, status: int, payload: dict) -> None:
            body = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            for key, value in _SECURITY_HEADERS.items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)

        def _authorized(self) -> bool:
            if not auth_token:
                self._send_json(503, {"error": "brain_disabled"})
                return False
            provided = self.headers.get("Authorization", "")
            expected = "Bearer " + auth_token
            if not hmac.compare_digest(provided, expected):
                self._send_json(401, {"error": "unauthorized"})
                return False
            return True

        def do_GET(self):
            if not self._authorized():
                return
            parsed = urlsplit(self.path)
            if parsed.path == "/api/v1/status":
                self._send_json(200, services.status())
                return
            if parsed.path == "/api/v1/events":
                query = parse_qs(parsed.query, keep_blank_values=True)
                try:
                    after = max(0, int(query.get("after", ["0"])[0]))
                    limit = max(1, min(int(query.get("limit", ["50"])[0]), 100))
                except (TypeError, ValueError):
                    self._send_json(400, {"error": "invalid_cursor"})
                    return
                self._send_json(200, services.events(after, limit))
                return
            if parsed.path == "/api/v1/brain/queue":
                query = parse_qs(parsed.query, keep_blank_values=True)
                try:
                    wait_ms = int(query.get("wait_ms", ["25000"])[0])
                except (TypeError, ValueError):
                    self._send_json(400, {"error": "invalid_wait"})
                    return
                wait_ms = max(0, min(wait_ms, MAX_QUEUE_WAIT_MS))
                self._send_json(200, {"item": services.pop_queue(wait_ms)})
                return
            self._send_json(404, {"error": "not_found"})

        def do_POST(self):
            if not self._authorized():
                return
            parsed = urlsplit(self.path)
            if parsed.path in _GET_PATHS:
                self._send_json(405, {"error": "method_not_allowed"})
                return
            if parsed.path == "/api/v1/brain/decision":
                payload = _parse_body(self)
                if payload is None:
                    return
                item_id = _bounded_str(payload.get("item_id"), 64)
                decision = payload.get("decision")
                if item_id is None or not isinstance(decision, dict) or not decision:
                    self._send_json(400, {"error": "invalid_json"})
                    return
                self._send_json(200, services.submit_decision(item_id, decision))
                return
            if parsed.path == "/api/v1/proposals":
                payload = _parse_body(self)
                if payload is None:
                    return
                text = _bounded_str(payload.get("text"), MAX_COMMAND_TEXT)
                raw_selector = payload.get("selector") if isinstance(payload.get("selector"), dict) else payload
                selector = {}
                for key in _SELECTOR_KEYS:
                    value = _bounded_str(raw_selector.get(key), 48)
                    if value is not None:
                        selector[key] = value
                if text is None or not selector:
                    self._send_json(400, {"error": "invalid_json"})
                    return
                self._send_json(200, services.submit_proposal(text, selector))
                return
            if parsed.path == "/api/v1/tts":
                payload = _parse_body(self)
                if payload is None:
                    return
                text = payload.get("text")
                if not isinstance(text, str) or not text.strip():
                    self._send_json(400, {"error": "invalid_json"})
                    return
                if len(text) > MAX_TTS_TEXT:
                    self._send_json(400, {"error": "text_too_large"})
                    return
                self._send_json(200, services.speak(text))
                return
            self._send_json(404, {"error": "not_found"})

        def _reject_mutation(self):
            if self._authorized():
                self._send_json(405, {"error": "method_not_allowed"})

        do_PUT = do_PATCH = do_DELETE = _reject_mutation

    return BrainHandler


class BrainServer:
    """Loopback-only HTTP control plane for the DSH brain plugin."""

    def __init__(self, services: BrainServices, auth_token: str | None, port: int = 8767):
        self.services = services
        self.auth_token = auth_token
        self._server = ThreadingHTTPServer(("127.0.0.1", int(port)), _handler_factory(services, auth_token))
        self._server.daemon_threads = True
        self._thread: threading.Thread | None = None

    @property
    def port(self) -> int:
        return int(self._server.server_address[1])

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.port}"

    def start(self) -> "BrainServer":
        self._thread = threading.Thread(target=self._server.serve_forever, name="walkie-brain-api", daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        if self._thread:
            self._thread.join(timeout=2)
