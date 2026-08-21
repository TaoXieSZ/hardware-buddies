"""Loopback-only runtime dashboard for the StopWatch walkie bridge."""
from __future__ import annotations

import copy
import json
import threading
import time
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit


MAX_EVENTS = 200
MAX_PREVIEW = 160
MAX_LABEL = 48
MAX_HINT = 180
MAX_CANDIDATES = 4
ASSET_DIR = Path(__file__).with_name("dashboard_assets")

_TEXT_LIMITS = {
    "device_id": 64,
    "utterance_id": 64,
    "command_id": 64,
    "task_id": 64,
    "request_id": 64,
    "agent": 16,
    "label": MAX_LABEL,
    "project_label": MAX_LABEL,
    "text": MAX_PREVIEW,
    "summary": MAX_PREVIEW,
    "code": 64,
    "hint": MAX_HINT,
    "state": 32,
}
_NUMBER_FIELDS = {"protocol", "bytes", "latency_ms", "revision", "port"}
_BOOL_FIELDS = {"authenticated", "actionable", "healthy", "direct"}

ROUTE_HINTS = {
    "target_required": "请以 Claude、Codex、OpenCode、Kimi 或已配置别名开头。",
    "target_not_found": "没有找到唯一可控制的目标；请检查 Agent 和会话标签。",
    "target_ambiguous": "目标不唯一；请补充看板中显示的会话或项目标签。",
    "spawn_not_supported": "当前只能控制已存在的会话，不能从手表新建 Agent。",
    "control_plane_unavailable": "cc-bridge 控制面不可用；语音已识别，但没有发送命令。",
}


def bounded_utf8(value: object, limit: int) -> str:
    text = str(value or "")
    raw = text.encode("utf-8")
    if len(raw) <= limit:
        return text
    cut = raw[: max(0, limit - 3)]
    while cut:
        try:
            return cut.decode("utf-8") + "…"
        except UnicodeDecodeError:
            cut = cut[:-1]
    return "…"


def _safe_sessions(value: object) -> list[dict]:
    rows = []
    for item in value if isinstance(value, list) else []:
        if not isinstance(item, dict):
            continue
        capabilities = item.get("capabilities") if isinstance(item.get("capabilities"), dict) else {}
        rows.append({
            "agent": bounded_utf8(item.get("agent"), 16),
            "label": bounded_utf8(item.get("label"), MAX_LABEL),
            "project_label": bounded_utf8(item.get("project_label"), MAX_LABEL),
            "state": bounded_utf8(item.get("state"), 32),
            "capabilities": {
                "steer": bool(capabilities.get("steer")),
                "permission_reply": bool(capabilities.get("permission_reply")),
            },
        })
        if len(rows) >= 32:
            break
    return rows


def _safe_fields(fields: dict) -> dict:
    safe = {}
    for key, limit in _TEXT_LIMITS.items():
        if key in fields:
            safe[key] = bounded_utf8(fields[key], limit)
    for key in _NUMBER_FIELDS:
        if key in fields:
            try:
                safe[key] = max(0, int(fields[key]))
            except (TypeError, ValueError):
                safe[key] = 0
    for key in _BOOL_FIELDS:
        if key in fields:
            safe[key] = bool(fields[key])
    if "candidates" in fields:
        safe["candidates"] = [bounded_utf8(v, MAX_LABEL) for v in fields["candidates"][:MAX_CANDIDATES]] \
            if isinstance(fields["candidates"], list) else []
    if "sessions" in fields:
        safe["sessions"] = _safe_sessions(fields["sessions"])
    return safe


class DashboardState:
    """Thread-safe, allowlisted, memory-only dashboard projection."""

    def __init__(self, capacity: int = MAX_EVENTS, clock=None):
        self._events = deque(maxlen=max(1, min(int(capacity), MAX_EVENTS)))
        self._next_sequence = 1
        self._clock = clock or time.time
        self._lock = threading.RLock()
        self._active_connection_id = ""
        self._snapshot = {
            "bridge": {"status": "starting", "dashboard": "starting"},
            "watch": {"connected": False, "authenticated": False, "protocol": 0, "device_id": ""},
            "pipeline": {
                "stage": "disconnected", "utterance_id": "", "bytes": 0,
                "latency_ms": 0, "transcript": "", "error_code": "",
                "hint": "等待 StopWatch 连接。", "candidates": [],
            },
            "control_plane": {"healthy": False, "revision": 0, "sessions": []},
            "proposal": None,
            "task": None,
            "permission": None,
            "tts": {"state": "idle", "bytes": 0},
            "updated_at_ms": int(self._clock() * 1000),
            "latest_sequence": 0,
        }

    def publish(self, kind: str, **fields) -> int:
        event_kind = bounded_utf8(kind, 48)
        safe = _safe_fields(fields)
        connection_id = bounded_utf8(fields.get("connection_id"), 32)
        with self._lock:
            sequence = self._next_sequence
            self._next_sequence += 1
            timestamp_ms = int(self._clock() * 1000)
            event = {"sequence": sequence, "timestamp_ms": timestamp_ms, "type": event_kind, **safe}
            self._events.append(event)
            self._apply(event_kind, safe, connection_id)
            self._snapshot["updated_at_ms"] = timestamp_ms
            self._snapshot["latest_sequence"] = sequence
            return sequence

    def _apply(self, kind: str, fields: dict, connection_id: str = "") -> None:
        pipeline = self._snapshot["pipeline"]
        watch = self._snapshot["watch"]
        if kind == "bridge.started":
            self._snapshot["bridge"].update(status="listening", port=fields.get("port", 0))
        elif kind == "dashboard.started":
            self._snapshot["bridge"].update(dashboard="listening", dashboard_port=fields.get("port", 0))
        elif kind == "dashboard.failed":
            self._snapshot["bridge"].update(dashboard="failed", dashboard_error=fields.get("code", "bind_failed"))
        elif kind == "watch.connected":
            self._active_connection_id = connection_id
            watch.update(connected=True)
            pipeline.update(stage="connected", error_code="", hint="等待协议握手。", candidates=[])
        elif kind == "watch.disconnected":
            if not connection_id or connection_id == self._active_connection_id:
                watch.update(connected=False, authenticated=False)
                self._active_connection_id = ""
                pipeline.update(stage="disconnected", hint="StopWatch 已断开，等待自动重连。")
                self._snapshot["proposal"] = None
                self._snapshot["permission"] = None
        elif kind == "watch.hello":
            watch.update(protocol=fields.get("protocol", 0), device_id=fields.get("device_id", ""))
            pipeline.update(stage="authenticating" if fields.get("protocol") == 2 else "ready")
        elif kind == "control.authenticated":
            watch.update(authenticated=True)
            pipeline.update(stage="ready", error_code="", hint="按住 A 说话；命令需以 Agent 或别名开头。")
        elif kind == "utterance.started":
            pipeline.update(stage="recording", utterance_id=fields.get("utterance_id", ""), bytes=0,
                            latency_ms=0, transcript="", error_code="", candidates=[], hint="正在录音…")
        elif kind == "utterance.cancelled":
            pipeline.update(stage="ready", bytes=fields.get("bytes", 0), hint="录音已取消，没有发送内容。")
        elif kind == "asr.started":
            pipeline.update(stage="transcribing", bytes=fields.get("bytes", 0), hint="百炼 ASR 正在识别。")
        elif kind == "asr.completed":
            pipeline.update(stage="routing", bytes=fields.get("bytes", 0), latency_ms=fields.get("latency_ms", 0),
                            transcript=fields.get("text", ""), error_code="", candidates=[], hint="识别完成，正在解析目标。")
        elif kind in {"asr.failed", "protocol.failed"}:
            pipeline.update(stage="failed", error_code=fields.get("code", "error"), hint=fields.get("hint", "处理失败。"))
        elif kind == "route.failed":
            code = fields.get("code", "target_required")
            pipeline.update(stage="route_error", error_code=code, candidates=fields.get("candidates", []),
                            hint=fields.get("hint") or ROUTE_HINTS.get(code, "没有发送命令，请重试。"))
            if code == "control_plane_unavailable":
                self._snapshot["control_plane"]["healthy"] = False
        elif kind == "control.snapshot":
            self._snapshot["control_plane"].update(
                healthy=fields.get("healthy", True), revision=fields.get("revision", 0),
                sessions=fields.get("sessions", []))
        elif kind == "proposal.created":
            pipeline.update(stage="proposal", error_code="", hint="请在手表上按 A 批准或按 B 拒绝。")
            self._snapshot["proposal"] = {
                key: fields.get(key, "") for key in ("command_id", "agent", "label", "project_label", "text")
            }
        elif kind == "proposal.rejected":
            pipeline.update(stage="cancelled", hint="提案已拒绝，没有发送命令。")
            self._snapshot["proposal"] = None
        elif kind == "proposal.approved":
            pipeline.update(stage="dispatching", hint="已确认，正在向唯一目标发送。")
        elif kind in {"task.accepted", "task.running", "task.completed", "task.failed", "task.cancelled"}:
            state = kind.split(".", 1)[1]
            pipeline.update(stage=state, error_code=fields.get("code", ""),
                            hint=fields.get("summary") or fields.get("hint") or state)
            self._snapshot["task"] = {
                "task_id": fields.get("task_id", ""), "command_id": fields.get("command_id", ""),
                "state": state, "summary": fields.get("summary", ""), "code": fields.get("code", ""),
            }
            if state in {"completed", "failed", "cancelled"}:
                self._snapshot["proposal"] = None
                self._snapshot["permission"] = None
        elif kind == "permission.requested":
            pipeline.update(stage="permission", hint=fields.get("hint", "等待权限确认。"))
            self._snapshot["permission"] = {
                "request_id": fields.get("request_id", ""), "agent": fields.get("agent", ""),
                "hint": fields.get("hint", ""), "actionable": fields.get("actionable", False),
            }
        elif kind == "permission.resolved":
            self._snapshot["permission"] = None
            pipeline.update(stage="running", hint="权限请求已处理。")
        elif kind in {"tts.started", "tts.completed", "tts.failed"}:
            state = kind.split(".", 1)[1]
            self._snapshot["tts"] = {"state": state, "bytes": fields.get("bytes", 0)}

    def snapshot(self) -> dict:
        with self._lock:
            return copy.deepcopy(self._snapshot)

    def events(self, after: int, limit: int = 50) -> dict:
        after = max(0, int(after))
        limit = max(1, min(int(limit), 100))
        with self._lock:
            oldest = self._events[0]["sequence"] if self._events else self._next_sequence
            gap = after < oldest - 1
            events = [] if gap else [copy.deepcopy(e) for e in self._events if e["sequence"] > after][:limit]
            return {"gap": gap, "events": events, "next_sequence": self._next_sequence - 1}


_SECURITY_HEADERS = {
    "Cache-Control": "no-store",
    "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
}


def _handler_factory(state: DashboardState, asset_dir: Path):
    class DashboardHandler(BaseHTTPRequestHandler):
        server_version = "StopWatchDashboard/1"

        def log_message(self, _format, *_args):
            return

        def _send(self, status: int, body: bytes, content_type: str) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            for key, value in _SECURITY_HEADERS.items():
                self.send_header(key, value)
            self.end_headers()
            self.wfile.write(body)

        def _json(self, status: int, payload: dict) -> None:
            self._send(status, json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8"),
                       "application/json; charset=utf-8")

        def do_GET(self):
            parsed = urlsplit(self.path)
            if parsed.path == "/api/status":
                self._json(200, state.snapshot())
                return
            if parsed.path == "/api/events":
                try:
                    query = parse_qs(parsed.query, keep_blank_values=True)
                    after = int(query.get("after", ["0"])[0])
                    limit = int(query.get("limit", ["50"])[0])
                    if after < 0:
                        raise ValueError
                except (TypeError, ValueError):
                    self._json(400, {"error": "invalid_cursor"})
                    return
                self._json(200, state.events(after, limit))
                return
            assets = {"/": ("index.html", "text/html; charset=utf-8"),
                      "/dashboard.css": ("dashboard.css", "text/css; charset=utf-8"),
                      "/dashboard.js": ("dashboard.js", "text/javascript; charset=utf-8")}
            asset = assets.get(parsed.path)
            if asset:
                try:
                    self._send(200, (asset_dir / asset[0]).read_bytes(), asset[1])
                except OSError:
                    self._json(500, {"error": "asset_unavailable"})
                return
            self._json(404, {"error": "not_found"})

        def _reject_mutation(self):
            self._json(405, {"error": "read_only"})

        do_POST = do_PUT = do_PATCH = do_DELETE = _reject_mutation

    return DashboardHandler


class DashboardServer:
    def __init__(self, state: DashboardState, port: int = 8766, asset_dir: Path = ASSET_DIR):
        self.state = state
        self._server = ThreadingHTTPServer(("127.0.0.1", int(port)), _handler_factory(state, asset_dir))
        self._server.daemon_threads = True
        self._thread: threading.Thread | None = None

    @property
    def port(self) -> int:
        return int(self._server.server_address[1])

    def start(self) -> "DashboardServer":
        self._thread = threading.Thread(target=self._server.serve_forever, name="stopwatch-dashboard", daemon=True)
        self._thread.start()
        self.state.publish("dashboard.started", port=self.port)
        return self

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        if self._thread:
            self._thread.join(timeout=2)


def start_dashboard(state: DashboardState, port: int = 8766) -> DashboardServer | None:
    try:
        return DashboardServer(state, port=port).start()
    except OSError:
        state.publish("dashboard.failed", code="bind_failed", port=port)
        return None
