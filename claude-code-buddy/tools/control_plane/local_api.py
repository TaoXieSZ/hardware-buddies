"""Bounded owner-local API state for external control-plane peers."""
from __future__ import annotations

import hashlib
import os
import threading
from collections import deque
from pathlib import Path
from typing import Callable

from .cmux_control import label_from_title


SUPPORTED_AGENTS = {"claude", "codex", "opencode", "kimi"}
MAX_SESSIONS = 32
MAX_LABEL = 48
MAX_EVENTS = 256
MAX_EVENT_TEXT = 512


def _bounded(value, limit=MAX_LABEL) -> str:
    text = str(value or "")
    return text if len(text) <= limit else text[: limit - 1] + "…"


class EventRing:
    def __init__(self, capacity: int = MAX_EVENTS):
        self._events = deque(maxlen=capacity)
        self._next = 1
        self._lock = threading.Lock()

    def publish(self, kind: str, **fields) -> int:
        with self._lock:
            cursor = self._next
            self._next += 1
            event = {"cursor": cursor, "type": _bounded(kind, 48)}
            for key, value in fields.items():
                if isinstance(value, str):
                    value = _bounded(value, MAX_EVENT_TEXT)
                event[key] = value
            self._events.append(event)
            return cursor

    def poll(self, after: int, limit: int = 32) -> dict:
        limit = max(1, min(int(limit), 64))
        with self._lock:
            oldest = self._events[0]["cursor"] if self._events else self._next
            gap = after < oldest - 1
            events = [] if gap else [dict(e) for e in self._events if e["cursor"] > after][:limit]
            return {"gap": gap, "events": events, "next_cursor": self._next - 1}


class LocalControlAPI:
    """Normalize cmux sessions and expose stage/confirm/event primitives."""

    def __init__(self, cmux, route_stager, pending_permissions: dict | None = None, state=None):
        self._cmux = cmux
        self._stager = route_stager
        self._pending_permissions = pending_permissions if pending_permissions is not None else {}
        self._state = state
        self.events = EventRing()
        self._revision = 0
        self._fingerprint = None
        self._surface_by_key: dict[str, str] = {}
        self._native_to_key: dict[tuple[str, str], str] = {}

    @staticmethod
    def socket_is_owner_only(path: str) -> bool:
        try:
            stat = os.stat(path)
            return stat.st_uid == os.getuid() and (stat.st_mode & 0o077) == 0
        except OSError:
            return False

    def snapshot(self) -> dict:
        sessions = self._cmux.list_sessions()
        metadata = self._cmux.agent_surface_metadata()
        identity_counts: dict[tuple[str, str], int] = {}
        identities: dict[str, tuple[str, str]] = {}
        for session in sessions:
            meta = metadata.get(session.surface, {})
            agent = meta.get("agent")
            if agent not in SUPPORTED_AGENTS:
                continue
            native_id = meta.get("session_id") or meta.get("working_directory") or ""
            identity = (agent, native_id)
            identities[session.surface] = identity
            identity_counts[identity] = identity_counts.get(identity, 0) + 1

        rows = []
        mapping = {}
        for session in sessions:
            identity = identities.get(session.surface)
            if not identity:
                continue
            agent, native_id = identity
            unique = bool(native_id) and identity_counts[identity] == 1
            key = hashlib.sha256(("cc-control-v1:" + session.surface).encode()).hexdigest()[:32]
            mapping[key] = session.surface
            tracked = self._tracked_state(agent, native_id)
            state = str(tracked.get("st") or ("running" if tracked.get("running") else "idle"))
            rows.append({
                "session_key": key,
                "agent": agent,
                "label": _bounded(label_from_title(session.title) or session.nickname),
                "project_label": _bounded(Path(session.cwd).name),
                "state": state,
                "capabilities": {
                    "steer": unique,
                    "permission_reply": unique and agent in {"claude", "opencode"},
                },
            })
            if len(rows) >= MAX_SESSIONS:
                break
        fingerprint = tuple((r["session_key"], r["agent"], r["label"], r["project_label"], r["state"], tuple(r["capabilities"].items())) for r in rows)
        if fingerprint != self._fingerprint:
            self._revision += 1
            self._fingerprint = fingerprint
            self.events.publish("session.snapshot", revision=self._revision)
        self._surface_by_key = mapping
        self._native_to_key = {
            identity: row["session_key"]
            for row in rows
            for identity in [identities.get(mapping.get(row["session_key"], ""))]
            if identity and identity_counts.get(identity) == 1
        }
        for row in rows:
            row["revision"] = self._revision
        return {"revision": self._revision, "sessions": rows}

    def _tracked_state(self, agent: str, native_id: str) -> dict:
        if self._state is None:
            return {}
        direct = self._state._sessions.get(native_id)
        if isinstance(direct, dict):
            return direct
        snapshot = self._state.ext_sessions.get(agent, {})
        rows = snapshot.get("sessions", [])
        cwd_matches = [row for row in rows if str(row.get("cwd") or "") == native_id]
        if cwd_matches:
            return cwd_matches[0] if len(cwd_matches) == 1 else {}
        matches = []
        for row in rows:
            sid = str(row.get("sid") or "")
            if sid and (sid == native_id or native_id.endswith(sid)):
                matches.append(row)
        return matches[0] if len(matches) == 1 else {}

    def task_status(self, session_key: str) -> dict:
        snapshot = self.snapshot()
        row = next((item for item in snapshot["sessions"] if item["session_key"] == session_key), None)
        if row is None:
            return {"ok": False, "error": "target_stale"}
        surface = self._surface_by_key.get(session_key)
        summary = ""
        if surface:
            try:
                summary = _bounded(self._cmux.read_surface(surface), MAX_EVENT_TEXT)
            except Exception:
                summary = ""
        return {
            "ok": True, "session_key": session_key, "state": row["state"],
            "revision": snapshot["revision"], "summary": summary,
            "summary_source": "pane_snapshot",
        }

    def publish_permission(self, request_id: str, agent: str, native_session_id: str,
                           tool: str, hint: str) -> None:
        session_key = self._native_to_key.get((agent, native_session_id), "")
        actionable = bool(session_key) and agent in {"claude", "opencode"}
        self.events.publish(
            "permission.request", request_id=request_id, session_key=session_key,
            agent=agent, tool=tool, hint=hint, actionable=actionable,
        )

    def stage(self, command_id: str, session_key: str, target_revision: int, text: str) -> dict:
        if (not command_id or not session_key or not text or
                len(command_id) > 64 or len(session_key) > 64 or len(text.encode("utf-8")) > 1024):
            return {"ok": False, "error": "invalid_request"}
        snapshot = self.snapshot()
        row = next((r for r in snapshot["sessions"] if r["session_key"] == session_key), None)
        if row is None or not row["capabilities"]["steer"] or target_revision != snapshot["revision"]:
            return {"ok": False, "error": "target_stale"}
        surface = self._surface_by_key[session_key]
        self._stager.stage(surface, text, command_id=command_id, target_revision=target_revision)
        self.events.publish("command.staged", command_id=command_id, session_key=session_key)
        return {"ok": True}

    def confirm(self, command_id: str) -> dict:
        pending = self._stager.peek()
        if pending is None or pending.command_id != command_id:
            return {"ok": False, "error": "stale_command", "fired": False}
        snapshot = self.snapshot()
        if pending.target_revision != snapshot["revision"] or pending.target not in self._surface_by_key.values():
            self._stager.cancel(command_id)
            return {"ok": False, "error": "target_stale", "fired": False}
        try:
            fired = self._stager.confirm(command_id)
        except (KeyError, RuntimeError):
            return {"ok": False, "error": "route_failed", "fired": False}
        if fired:
            self.events.publish("task.accepted", command_id=command_id)
        return {"ok": True, "fired": bool(fired)}

    def cancel(self, command_id: str) -> dict:
        fired = self._stager.cancel(command_id)
        if fired:
            self.events.publish("command.cancelled", command_id=command_id)
        return {"ok": True, "cancelled": bool(fired)}

    def resolve_permission(self, request_id: str, decision: str) -> dict:
        future = self._pending_permissions.get(request_id)
        if future is None or future.done():
            return {"ok": False, "error": "already_resolved"}
        if decision not in {"approve", "deny"}:
            return {"ok": False, "error": "invalid_decision"}
        future.set_result(decision)
        self.events.publish("permission.resolved", request_id=request_id, decision=decision)
        return {"ok": True}
