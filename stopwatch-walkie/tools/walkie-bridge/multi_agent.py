"""Deterministic existing-session routing for StopWatch control mode."""
from __future__ import annotations

import asyncio
import json
import time
import uuid
from dataclasses import dataclass
from typing import Any, Callable

from protocol_v2 import MAX_CANDIDATES, MAX_COMMAND_TEXT, MAX_PREVIEW, MAX_SUMMARY


AGENT_ALIASES = {
    "claude code": "claude", "claude": "claude",
    "codex": "codex",
    "open code": "opencode", "opencode": "opencode",
    "kimi code": "kimi", "kimi": "kimi",
}
SPAWN_WORDS = {"new", "start", "create", "spawn", "新建", "启动", "创建"}


class RouterError(ValueError):
    def __init__(self, code: str, candidates: list[str] | None = None):
        super().__init__(code)
        self.code = code
        self.candidates = (candidates or [])[:MAX_CANDIDATES]


def bounded_utf8(text: str, limit: int) -> str:
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


class CcBridgeClient:
    def __init__(self, socket_path: str, timeout_seconds: float = 3.0):
        self.socket_path = socket_path
        self.timeout_seconds = timeout_seconds

    async def request(self, action: str, **fields) -> dict[str, Any]:
        async def exchange():
            reader, writer = await asyncio.open_unix_connection(self.socket_path)
            try:
                writer.write((json.dumps({"action": action, **fields}, ensure_ascii=False) + "\n").encode())
                await writer.drain()
                line = await reader.readline()
                if not line:
                    raise RouterError("control_plane_unavailable")
                response = json.loads(line)
                if not isinstance(response, dict):
                    raise RouterError("control_plane_invalid_response")
                return response
            finally:
                writer.close()
                await writer.wait_closed()

        try:
            return await asyncio.wait_for(exchange(), self.timeout_seconds)
        except (OSError, TimeoutError, json.JSONDecodeError) as exc:
            raise RouterError("control_plane_unavailable") from exc

    async def snapshot(self):
        response = await self.request("control.snapshot")
        if not response.get("ok"):
            raise RouterError(str(response.get("error") or "control_plane_unavailable"))
        return response

    async def stage(self, proposal):
        return await self.request(
            "control.stage", command_id=proposal.command_id,
            session_key=proposal.session_key, target_revision=proposal.target_revision,
            text=proposal.text,
        )

    async def confirm(self, command_id: str):
        return await self.request("control.confirm", command_id=command_id)

    async def cancel(self, command_id: str):
        return await self.request("control.cancel", command_id=command_id)

    async def events(self, after: int, limit: int = 32):
        return await self.request("control.events", after=after, limit=limit)

    async def resolve_permission(self, request_id: str, decision: str):
        return await self.request("control.permission.resolve", request_id=request_id, decision=decision)

    async def task_status(self, session_key: str):
        return await self.request("control.task.status", session_key=session_key)


@dataclass(frozen=True)
class Proposal:
    command_id: str
    session_key: str
    target_revision: int
    text: str
    agent: str
    label: str
    project_label: str
    preview: str
    expires_at: float


class MultiAgentRouter:
    def __init__(self, aliases: dict[str, dict[str, str]] | None = None, ttl_seconds: float = 60.0, clock=None):
        self.aliases = {str(k).strip().casefold(): dict(v) for k, v in (aliases or {}).items() if str(k).strip() and isinstance(v, dict)}
        self.ttl_seconds = ttl_seconds
        self.clock = clock or time.monotonic

    def propose(self, transcript: str, snapshot: dict[str, Any]) -> Proposal:
        source = transcript.strip()
        if not source:
            raise RouterError("empty_command")
        folded = source.casefold()
        if any(folded == word or folded.startswith(word + " ") for word in SPAWN_WORDS):
            raise RouterError("spawn_not_supported")

        selector: dict[str, str] = {}
        command = ""
        for alias in sorted(self.aliases, key=len, reverse=True):
            if folded == alias or folded.startswith(alias + " "):
                selector = self.aliases[alias]
                command = source[len(alias):].lstrip()
                break

        sessions = [row for row in snapshot.get("sessions", []) if isinstance(row, dict)]
        if not selector:
            for alias in sorted(AGENT_ALIASES, key=len, reverse=True):
                if folded == alias or folded.startswith(alias + " "):
                    selector = {"agent": AGENT_ALIASES[alias]}
                    remainder = source[len(alias):].lstrip()
                    candidates = [row for row in sessions if row.get("agent") == selector["agent"]]
                    labels = []
                    for row in candidates:
                        for key in ("label", "project_label"):
                            label = str(row.get(key) or "")
                            if label and (remainder.casefold() == label.casefold() or remainder.casefold().startswith(label.casefold() + " ")):
                                labels.append((len(label), key, label))
                    if labels:
                        _, key, label = max(labels)
                        selector[key] = label
                        remainder = remainder[len(label):].lstrip()
                    command = remainder
                    break
        if not selector:
            raise RouterError("target_required", self._labels(sessions))
        if not command:
            raise RouterError("empty_command")
        if len(command.encode("utf-8")) > MAX_COMMAND_TEXT:
            raise RouterError("command_too_large")

        matches = []
        for row in sessions:
            caps = row.get("capabilities") or {}
            if not caps.get("steer"):
                continue
            if selector.get("agent") and row.get("agent") != selector["agent"]:
                continue
            if selector.get("label") and str(row.get("label") or "").casefold() != selector["label"].casefold():
                continue
            if selector.get("project_label") and str(row.get("project_label") or "").casefold() != selector["project_label"].casefold():
                continue
            matches.append(row)
        if len(matches) != 1:
            raise RouterError("target_not_found" if not matches else "target_ambiguous", self._labels(matches or sessions))
        target = matches[0]
        now = self.clock()
        return Proposal(
            command_id="cmd-" + uuid.uuid4().hex,
            session_key=str(target["session_key"]),
            target_revision=int(snapshot.get("revision") or target.get("revision") or 0),
            text=command,
            agent=str(target.get("agent") or "")[:16],
            label=str(target.get("label") or "")[:48],
            project_label=str(target.get("project_label") or "")[:48],
            preview=bounded_utf8(command, MAX_PREVIEW),
            expires_at=now + self.ttl_seconds,
        )

    @staticmethod
    def _labels(rows):
        return [str(row.get("label") or row.get("project_label") or "")[:48] for row in rows if row.get("label") or row.get("project_label")][:MAX_CANDIDATES]


class ProposalStore:
    def __init__(self, clock=None):
        self._clock = clock or time.monotonic
        self._items: dict[str, Proposal] = {}

    def put(self, watch_session_id: str, proposal: Proposal) -> None:
        self._items[watch_session_id] = proposal

    def consume(self, watch_session_id: str, command_id: str) -> Proposal:
        proposal = self._items.get(watch_session_id)
        if proposal is None or proposal.command_id != command_id:
            raise RouterError("stale_command")
        del self._items[watch_session_id]
        if self._clock() > proposal.expires_at:
            raise RouterError("proposal_expired")
        return proposal

    def invalidate(self, watch_session_id: str) -> None:
        self._items.pop(watch_session_id, None)


class TaskTracker:
    TERMINAL = {"task.completed", "task.failed", "task.cancelled"}

    def __init__(self, max_terminal: int = 32):
        self.active: dict[str, dict] = {}
        self.terminal: dict[str, dict] = {}
        self.max_terminal = max_terminal
        self.cursor = 0
        self.session_to_task: dict[str, str] = {}

    def accepted(self, task_id: str, proposal: Proposal) -> dict:
        existing = self.session_to_task.get(proposal.session_key)
        if existing and existing in self.active:
            raise RouterError("ambiguous_session_activity")
        event = {"type": "task.accepted", "task_id": task_id, "command_id": proposal.command_id,
                 "session_key": proposal.session_key, "event_seq": 1}
        self.active[task_id] = event
        self.session_to_task[proposal.session_key] = task_id
        return dict(event)

    def observe(self, event: dict) -> dict | None:
        task_id = str(event.get("task_id") or "")
        current = self.active.get(task_id)
        if not current:
            return None
        sequence = int(event.get("event_seq") or current["event_seq"] + 1)
        if sequence <= current["event_seq"]:
            return None
        bounded = {**current, **event}
        bounded["event_seq"] = sequence
        if "summary" in bounded:
            bounded["summary"] = bounded_utf8(str(bounded["summary"]), MAX_SUMMARY)
        self.active[task_id] = bounded
        if bounded.get("type") in self.TERMINAL:
            self.active.pop(task_id, None)
            self.session_to_task.pop(str(current.get("session_key") or ""), None)
            self.terminal[task_id] = bounded
            while len(self.terminal) > self.max_terminal:
                self.terminal.pop(next(iter(self.terminal)))
        return dict(bounded)

    def snapshot(self, task_id: str) -> dict | None:
        value = self.active.get(task_id) or self.terminal.get(task_id)
        return dict(value) if value else None
