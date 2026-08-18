#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import base64
import contextlib
import io
import json
import logging
import os
import subprocess
import tempfile
import time
import wave
from dataclasses import dataclass
from typing import Any, Protocol

from dashboard import DashboardState, start_dashboard
from multi_agent import CcBridgeClient, MultiAgentRouter, ProposalStore, RouterError, TaskTracker
from protocol_v2 import (
    BRIDGE_TO_DEVICE,
    DEVICE_TO_BRIDGE,
    NONCE_BYTES,
    PROTOCOL_V1,
    PROTOCOL_V2,
    AuthenticatedSession,
    ControlProtocolError,
    b64url_decode,
    fresh_token,
    make_proof,
    parse_secret,
    verify_proof,
)

try:
    import websockets
    from websockets.exceptions import ConnectionClosed
except ImportError:  # pragma: no cover - handled by main()
    websockets = None
    ConnectionClosed = ConnectionError


PROTOCOL_VERSION = PROTOCOL_V1
SAMPLE_RATE = 16000
BITS_PER_SAMPLE = 16
CHANNELS = 1
ENCODING = "pcm_s16le"
BYTES_PER_SECOND = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE // 8)
MAX_PCM_BYTES = BYTES_PER_SECOND * 60
MAX_TTS_PCM_BYTES = BYTES_PER_SECOND * 30
TTS_CHUNK_BYTES = 4096

LOG = logging.getLogger("walkie_bridge")


class ProtocolFailure(Exception):
    def __init__(self, code: str, message: str, utterance_id: str | None = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.utterance_id = utterance_id


class ASRFailure(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


class ASRClient(Protocol):
    async def transcribe_wav(self, wav_bytes: bytes) -> str:
        ...


class TTSClient(Protocol):
    async def synthesize_pcm(self, text: str) -> bytes:
        ...


@dataclass
class BridgeConfig:
    host: str = "127.0.0.1"
    port: int = 8765
    dashscope_api_key: str | None = None
    dashscope_base_url: str | None = None
    asr_timeout_seconds: float = 30.0
    tts_voice: str = "Tingting"
    tts_timeout_seconds: float = 30.0
    tts_provider: str = "auto"
    tts_model: str = "qwen3-tts-flash"
    tts_dashscope_voice: str = "Cherry"
    control_enabled: bool = False
    control_secret: bytes | None = None
    control_aliases: dict[str, dict[str, str]] | None = None
    proposal_ttl_seconds: float = 60.0
    cc_bridge_socket: str = "/tmp/cc-bridge.sock"
    dashboard_enabled: bool = True
    dashboard_port: int = 8766

    @classmethod
    def from_env(cls) -> "BridgeConfig":
        aliases_raw = os.getenv("WALKIE_CONTROL_ALIASES_JSON", "{}")
        try:
            aliases = json.loads(aliases_raw)
            if not isinstance(aliases, dict):
                raise ValueError
        except (json.JSONDecodeError, ValueError) as exc:
            raise ControlProtocolError("invalid_aliases", "WALKIE_CONTROL_ALIASES_JSON must be a JSON object") from exc
        dashboard_port = int(os.getenv("WALKIE_DASHBOARD_PORT", "8766"))
        if not 1 <= dashboard_port <= 65535:
            raise ControlProtocolError("invalid_dashboard_port", "WALKIE_DASHBOARD_PORT must be between 1 and 65535")
        return cls(
            host=os.getenv("WALKIE_BRIDGE_HOST", "127.0.0.1"),
            port=int(os.getenv("WALKIE_BRIDGE_PORT", "8765")),
            dashscope_api_key=os.getenv("DASHSCOPE_API_KEY"),
            dashscope_base_url=os.getenv("DASHSCOPE_BASE_URL"),
            asr_timeout_seconds=float(os.getenv("WALKIE_BRIDGE_ASR_TIMEOUT", "30")),
            tts_voice=os.getenv("WALKIE_TTS_VOICE", "Tingting"),
            tts_timeout_seconds=float(os.getenv("WALKIE_TTS_TIMEOUT", "30")),
            tts_provider=os.getenv("WALKIE_TTS_PROVIDER", "auto"),
            tts_model=os.getenv("WALKIE_TTS_MODEL", "qwen3-tts-flash"),
            tts_dashscope_voice=os.getenv("WALKIE_TTS_DASHSCOPE_VOICE", "Cherry"),
            control_enabled=os.getenv("WALKIE_CONTROL_ENABLED", "0").lower() in {"1", "true", "yes"},
            control_secret=parse_secret(os.getenv("WALKIE_CONTROL_SECRET")),
            control_aliases=aliases,
            proposal_ttl_seconds=float(os.getenv("WALKIE_PROPOSAL_TTL", "60")),
            cc_bridge_socket=os.path.expanduser(os.getenv("WALKIE_CC_BRIDGE_SOCKET", "/tmp/cc-bridge.sock")),
            dashboard_enabled=os.getenv("WALKIE_DASHBOARD_ENABLED", "1").lower() in {"1", "true", "yes"},
            dashboard_port=dashboard_port,
        )


@dataclass
class Utterance:
    utterance_id: str
    started_at: float
    pcm: bytearray

    @property
    def byte_count(self) -> int:
        return len(self.pcm)

    def append(self, chunk: bytes) -> None:
        if len(chunk) % 2:
            raise ProtocolFailure("audio_format", "binary PCM frame has a partial sample", self.utterance_id)
        if len(self.pcm) + len(chunk) > MAX_PCM_BYTES:
            raise ProtocolFailure("utterance_too_large", "utterance exceeds the 60 second PCM limit", self.utterance_id)
        self.pcm.extend(chunk)


class ConnectionHandler:
    def __init__(
        self,
        asr_client: ASRClient,
        asr_timeout_seconds: float = 30.0,
        tts_client: TTSClient | None = None,
        control_secret: bytes | None = None,
        router: MultiAgentRouter | None = None,
        control_client: CcBridgeClient | None = None,
        proposal_store: ProposalStore | None = None,
        task_tracker: TaskTracker | None = None,
        observation_interval: float = 0.5,
        dashboard_state: DashboardState | None = None,
    ):
        self._asr_client = asr_client
        self._asr_timeout_seconds = asr_timeout_seconds
        self._tts_client = tts_client
        self._active: Utterance | None = None
        self._control_secret = control_secret
        self._router = router
        self._control_client = control_client
        self._proposals = proposal_store or ProposalStore()
        self._tasks = task_tracker or TaskTracker()
        self._protocol = PROTOCOL_V1
        self._auth: AuthenticatedSession | None = None
        self._auth_pending: dict[str, str] | None = None
        self._observation_interval = observation_interval
        self._observer_tasks: dict[str, asyncio.Task] = {}
        self._dashboard = dashboard_state
        self._connection_id = fresh_token()[:16]

    def _dash(self, kind: str, **fields: Any) -> None:
        if self._dashboard is not None:
            self._dashboard.publish(kind, **fields)

    async def handle(self, websocket: Any) -> None:
        self._dash("watch.connected", connection_id=self._connection_id)
        try:
            try:
                async for message in websocket:
                    try:
                        if isinstance(message, bytes):
                            await self._handle_binary(message)
                        elif isinstance(message, str):
                            await self._handle_text(websocket, message)
                        else:
                            raise ProtocolFailure("invalid_frame", "unsupported WebSocket frame type")
                    except ProtocolFailure as exc:
                        self._active = None
                        LOG.warning("protocol_error code=%s id=%s", exc.code, exc.utterance_id)
                        self._dash("protocol.failed", code=exc.code, utterance_id=exc.utterance_id,
                                   hint="手表协议处理失败，设备会自动重试。")
                        if self._auth is not None:
                            await self._send_json(websocket, {
                                "type": "error", "id": exc.utterance_id,
                                "code": exc.code, "message": exc.message,
                                "retryable": True,
                            })
                        else:
                            await send_error(websocket, exc.code, exc.message, exc.utterance_id)
            except (ConnectionError, ConnectionClosed):
                LOG.info("connection_closed")
        finally:
            if self._active is not None:
                LOG.info("disconnect_cleanup id=%s bytes=%d", self._active.utterance_id, self._active.byte_count)
            self._active = None
            if self._auth is not None:
                self._proposals.invalidate(self._auth.session_id)
            self._auth = None
            self._auth_pending = None
            for task in self._observer_tasks.values():
                task.cancel()
            self._observer_tasks.clear()
            self._dash("watch.disconnected", connection_id=self._connection_id)

    async def _handle_binary(self, message: bytes) -> None:
        if self._active is None:
            raise ProtocolFailure("invalid_sequence", "binary audio received without an active utterance")
        self._active.append(message)

    async def _handle_text(self, websocket: Any, message: str) -> None:
        payload = parse_json_message(message)
        if self._auth is not None:
            if "type" in payload:
                raise ProtocolFailure("plaintext_control", "authenticated control messages require an envelope")
            try:
                payload = self._auth.decode(payload)
            except ControlProtocolError as exc:
                raise ProtocolFailure(exc.code, str(exc)) from exc
        msg_type = require_string(payload, "type")
        if msg_type == "hello":
            await self._handle_hello(websocket, payload)
            return
        if msg_type == "auth.proof":
            await self._handle_auth_proof(websocket, payload)
            return
        if self._protocol == PROTOCOL_V2 and self._auth is None:
            raise ProtocolFailure("authentication_required", "protocol-v2 control session is not authenticated")
        if msg_type == "utterance.start":
            self._start(payload)
            return
        if msg_type == "utterance.cancel":
            utterance_id = require_string(payload, "id")
            self._require_active_id(utterance_id)
            byte_count = self._active.byte_count if self._active else 0
            LOG.info("utterance_cancel id=%s bytes=%d", utterance_id, byte_count)
            self._dash("utterance.cancelled", utterance_id=utterance_id, bytes=byte_count)
            self._active = None
            return
        if msg_type == "utterance.end":
            utterance_id = require_string(payload, "id")
            utterance = self._require_active_id(utterance_id)
            self._active = None
            await self._submit(websocket, utterance)
            return
        if msg_type == "command.decision":
            await self._handle_command_decision(websocket, payload)
            return
        if msg_type == "permission.decision":
            await self._handle_permission_decision(websocket, payload)
            return
        if msg_type == "task.snapshot":
            task_id = require_string(payload, "task_id")
            event = self._tasks.snapshot(task_id)
            await self._send_json(websocket, event or {"type": "task.unknown", "task_id": task_id})
            if event and task_id in self._tasks.active and task_id not in self._observer_tasks:
                self._start_observer(websocket, task_id, str(event.get("session_key") or ""))
            return
        raise ProtocolFailure("unknown_message", f"unsupported message type: {msg_type}")

    async def _handle_hello(self, websocket: Any, payload: dict[str, Any]) -> None:
        protocol = payload.get("protocol")
        if protocol == PROTOCOL_V1:
            self._protocol = PROTOCOL_V1
            device_id = require_string(payload, "device_id")
            self._dash("watch.hello", protocol=protocol, device_id=device_id)
            LOG.info("device_hello protocol=%s", protocol)
            return
        if protocol != PROTOCOL_V2:
            raise ProtocolFailure("protocol_version", "unsupported protocol version")
        if self._control_secret is None or self._router is None or self._control_client is None:
            raise ProtocolFailure("control_disabled", "protocol-v2 control mode is not configured")
        device_id = require_string(payload, "device_id")[:64]
        device_nonce = require_string(payload, "device_nonce")
        try:
            if len(b64url_decode(device_nonce)) != NONCE_BYTES:
                raise ValueError
        except (ControlProtocolError, ValueError) as exc:
            raise ProtocolFailure("invalid_nonce", "device nonce must be 24 random bytes") from exc
        bridge_nonce = fresh_token()
        session_id = fresh_token()
        self._protocol = PROTOCOL_V2
        self._auth_pending = {
            "device_id": device_id, "device_nonce": device_nonce,
            "bridge_nonce": bridge_nonce, "session_id": session_id,
        }
        self._dash("watch.hello", protocol=protocol, device_id=device_id)
        await websocket.send(json.dumps({
            "type": "auth.challenge", "protocol": PROTOCOL_V2,
            "bridge_nonce": bridge_nonce, "session_id": session_id,
            "proof": make_proof(self._control_secret, "bridge", device_id, device_nonce, bridge_nonce, session_id),
        }))
        LOG.info("device_hello protocol=%s", protocol)

    async def _handle_auth_proof(self, websocket: Any, payload: dict[str, Any]) -> None:
        pending = self._auth_pending
        if pending is None or self._control_secret is None:
            raise ProtocolFailure("authentication_required", "no authentication challenge is pending")
        proof = require_string(payload, "proof")
        if not verify_proof(self._control_secret, proof, "device", pending["device_id"], pending["device_nonce"], pending["bridge_nonce"], pending["session_id"]):
            self._auth_pending = None
            raise ProtocolFailure("authentication_failed", "device authentication failed")
        self._auth = AuthenticatedSession(
            self._control_secret, pending["session_id"], DEVICE_TO_BRIDGE, BRIDGE_TO_DEVICE)
        self._auth_pending = None
        await self._send_json(websocket, {"type": "auth.ok"})
        self._dash("control.authenticated")
        LOG.info("control_authenticated session=%s", self._auth.session_id[:12])

    def _start(self, payload: dict[str, Any]) -> None:
        if self._active is not None:
            raise ProtocolFailure("invalid_sequence", "a second utterance started before the first ended", self._active.utterance_id)
        utterance_id = require_string(payload, "id")
        validate_audio_format(payload.get("audio"), utterance_id)
        self._active = Utterance(utterance_id=utterance_id, started_at=time.monotonic(), pcm=bytearray())
        self._dash("utterance.started", utterance_id=utterance_id)
        LOG.info("utterance_start id=%s", utterance_id)

    def _require_active_id(self, utterance_id: str) -> Utterance:
        if self._active is None:
            raise ProtocolFailure("invalid_sequence", "no active utterance", utterance_id)
        if self._active.utterance_id != utterance_id:
            raise ProtocolFailure("invalid_sequence", "utterance id does not match the active utterance", utterance_id)
        return self._active

    async def _submit(self, websocket: Any, utterance: Utterance) -> None:
        if utterance.byte_count == 0:
            self._dash("asr.failed", utterance_id=utterance.utterance_id, code="no_speech",
                       hint="没有收到音频，请按住 A 说完再松开。")
            await send_error(websocket, "no_speech", "no speech recognized", utterance.utterance_id)
            return
        wav_bytes = pcm_to_wav(bytes(utterance.pcm))
        started = time.monotonic()
        self._dash("asr.started", utterance_id=utterance.utterance_id, bytes=utterance.byte_count)
        try:
            text = await asyncio.wait_for(
                self._asr_client.transcribe_wav(wav_bytes),
                timeout=self._asr_timeout_seconds,
            )
        except TimeoutError:
            LOG.warning("asr_error code=asr_timeout id=%s bytes=%d", utterance.utterance_id, utterance.byte_count)
            self._dash("asr.failed", utterance_id=utterance.utterance_id, code="asr_timeout",
                       hint="百炼 ASR 超时，请稍后重试。")
            await send_error(websocket, "asr_timeout", "ASR request timed out", utterance.utterance_id)
            return
        except ASRFailure as exc:
            LOG.warning("asr_error code=%s id=%s bytes=%d", exc.code, utterance.utterance_id, utterance.byte_count)
            self._dash("asr.failed", utterance_id=utterance.utterance_id, code=exc.code,
                       hint="百炼 ASR 请求失败，请检查本地 bridge 配置。")
            await send_error(websocket, exc.code, exc.message, utterance.utterance_id)
            return
        text = text.strip()
        latency_ms = int((time.monotonic() - started) * 1000)
        LOG.info("asr_complete id=%s bytes=%d latency_ms=%d text_len=%d", utterance.utterance_id, utterance.byte_count, latency_ms, len(text))
        if not text:
            self._dash("asr.failed", utterance_id=utterance.utterance_id, code="no_speech",
                       hint="百炼没有识别出文字，请靠近手表重试。")
            await send_error(websocket, "no_speech", "no speech recognized", utterance.utterance_id)
            return
        self._dash("asr.completed", utterance_id=utterance.utterance_id, bytes=utterance.byte_count,
                   latency_ms=latency_ms, text=text)
        await self._send_json(websocket, {"type": "transcript", "id": utterance.utterance_id, "text": text})
        if self._auth is not None and self._router is not None and self._control_client is not None:
            await self._propose(websocket, utterance.utterance_id, text)
        elif self._tts_client is not None:
            await self._speak(websocket, utterance.utterance_id, text)

    async def _propose(self, websocket: Any, utterance_id: str, text: str) -> None:
        try:
            snapshot = await self._control_client.snapshot()
            self._dash("control.snapshot", healthy=True, revision=snapshot.get("revision", 0),
                       sessions=snapshot.get("sessions", []))
            proposal = self._router.propose(text, snapshot)
        except RouterError as exc:
            LOG.warning("route_error code=%s id=%s candidates=%d", exc.code, utterance_id, len(exc.candidates))
            self._dash("route.failed", utterance_id=utterance_id, code=exc.code, candidates=exc.candidates)
            await self._send_json(websocket, {
                "type": "command.error", "id": utterance_id, "code": exc.code,
                "retryable": True, "candidates": exc.candidates,
            })
            return
        self._proposals.put(self._auth.session_id, proposal)
        self._dash("proposal.created", command_id=proposal.command_id, agent=proposal.agent,
                   label=proposal.label, project_label=proposal.project_label, text=proposal.preview)
        await self._send_json(websocket, {
            "type": "command.proposal", "id": utterance_id,
            "command_id": proposal.command_id, "agent": proposal.agent,
            "session_label": proposal.label, "project_label": proposal.project_label,
            "preview": proposal.preview, "expires_in": int(self._router.ttl_seconds),
        })
        LOG.info("proposal_created command=%s agent=%s text_len=%d", proposal.command_id[:16], proposal.agent, len(proposal.text))

    async def _handle_command_decision(self, websocket: Any, payload: dict[str, Any]) -> None:
        command_id = require_string(payload, "command_id")
        decision = require_string(payload, "decision")
        if self._auth is None or self._control_client is None:
            raise ProtocolFailure("authentication_required", "control session is not authenticated")
        try:
            proposal = self._proposals.consume(self._auth.session_id, command_id)
        except RouterError as exc:
            raise ProtocolFailure(exc.code, "proposal is no longer actionable") from exc
        if decision == "reject":
            self._dash("proposal.rejected", command_id=command_id)
            await self._send_json(websocket, {"type": "task.cancelled", "command_id": command_id})
            return
        if decision != "approve":
            raise ProtocolFailure("invalid_decision", "decision must be approve or reject")
        self._dash("proposal.approved", command_id=command_id)
        if proposal.session_key in self._tasks.session_to_task:
            self._dash("task.failed", command_id=command_id, code="ambiguous_session_activity",
                       hint="目标会话已有手表任务在运行。")
            await self._send_json(websocket, {
                "type": "task.failed", "command_id": command_id,
                "code": "ambiguous_session_activity"})
            return
        staged = await self._control_client.stage(proposal)
        if not staged.get("ok"):
            self._dash("task.failed", command_id=command_id, code=staged.get("error", "route_failed"))
            await self._send_json(websocket, {"type": "task.failed", "command_id": command_id, "code": staged.get("error", "route_failed")})
            return
        confirmed = await self._control_client.confirm(command_id)
        if not confirmed.get("ok") or not confirmed.get("fired"):
            self._dash("task.failed", command_id=command_id, code=confirmed.get("error", "route_failed"))
            await self._send_json(websocket, {"type": "task.failed", "command_id": command_id, "code": confirmed.get("error", "route_failed")})
            return
        task_id = "task-" + os.urandom(16).hex()
        event = self._tasks.accepted(task_id, proposal)
        self._dash("task.accepted", task_id=task_id, command_id=command_id)
        await self._send_json(websocket, event)
        self._start_observer(websocket, task_id, proposal.session_key)
        LOG.info("task_accepted task=%s command=%s", task_id[:17], command_id[:16])

    def _start_observer(self, websocket: Any, task_id: str, session_key: str) -> None:
        if not session_key or not hasattr(self._control_client, "task_status"):
            return
        task = asyncio.create_task(self._observe_task(websocket, task_id, session_key))
        self._observer_tasks[task_id] = task
        task.add_done_callback(lambda _task: self._observer_tasks.pop(task_id, None))

    async def _observe_task(self, websocket: Any, task_id: str, session_key: str) -> None:
        saw_active = False
        try:
            while task_id in self._tasks.active:
                if hasattr(self._control_client, "events"):
                    polled = await self._control_client.events(self._tasks.cursor)
                    self._tasks.cursor = int(polled.get("next_cursor") or self._tasks.cursor)
                    if not polled.get("gap"):
                        for local_event in polled.get("events", []):
                            if (local_event.get("type") == "permission.request"
                                    and local_event.get("session_key") == session_key):
                                await self._send_json(websocket, {
                                    "type": "permission.request", "task_id": task_id,
                                    "request_id": str(local_event.get("request_id") or "")[:64],
                                    "agent": str(local_event.get("agent") or "")[:16],
                                    "tool": str(local_event.get("tool") or "")[:48],
                                    "hint": str(local_event.get("hint") or "")[:120],
                                    "actionable": bool(local_event.get("actionable")),
                                })
                                self._dash("permission.requested", task_id=task_id,
                                           request_id=local_event.get("request_id"), agent=local_event.get("agent"),
                                           hint=local_event.get("hint"), actionable=local_event.get("actionable"))
                status = await self._control_client.task_status(session_key)
                if not status.get("ok"):
                    event = self._tasks.observe({
                        "type": "task.failed", "task_id": task_id,
                        "code": status.get("error", "target_stale")})
                    if event:
                        self._dash("task.failed", task_id=task_id, code=event.get("code"))
                        await self._send_json(websocket, event)
                        if self._tts_client is not None:
                            await self._speak(websocket, task_id, str(event.get("summary") or "Completed"))
                    return
                state = str(status.get("state") or "idle")
                if state in {"running", "thinking", "tool"}:
                    if not saw_active:
                        saw_active = True
                        event = self._tasks.observe({"type": "task.running", "task_id": task_id})
                        if event:
                            self._dash("task.running", task_id=task_id)
                            await self._send_json(websocket, event)
                elif state == "waiting":
                    saw_active = True
                elif state == "idle" and saw_active:
                    event = self._tasks.observe({
                        "type": "task.completed", "task_id": task_id,
                        "summary": str(status.get("summary") or "Completed"),
                        "summary_source": str(status.get("summary_source") or "session_state"),
                    })
                    if event:
                        self._dash("task.completed", task_id=task_id, summary=event.get("summary"))
                        await self._send_json(websocket, event)
                    return
                await asyncio.sleep(self._observation_interval)
        except asyncio.CancelledError:
            raise
        except (RouterError, OSError):
            event = self._tasks.observe({
                "type": "task.failed", "task_id": task_id,
                "code": "control_plane_unavailable"})
            if event:
                self._dash("task.failed", task_id=task_id, code="control_plane_unavailable")
                await self._send_json(websocket, event)

    async def _handle_permission_decision(self, websocket: Any, payload: dict[str, Any]) -> None:
        request_id = require_string(payload, "request_id")
        decision = require_string(payload, "decision")
        if decision not in {"approve", "deny"}:
            raise ProtocolFailure("invalid_decision", "permission decision must be approve or deny")
        response = await self._control_client.resolve_permission(request_id, decision)
        self._dash("permission.resolved", request_id=request_id, code=response.get("error"))
        await self._send_json(websocket, {
            "type": "permission.resolved", "request_id": request_id,
            "applied": bool(response.get("ok")), "code": response.get("error"),
        })

    async def _send_json(self, websocket: Any, payload: dict[str, Any]) -> None:
        message = self._auth.encode(payload) if self._auth is not None else payload
        await websocket.send(json.dumps(message, ensure_ascii=False))

    async def _speak(self, websocket: Any, utterance_id: str, text: str) -> None:
        self._dash("tts.started", utterance_id=utterance_id)
        try:
            pcm = await self._tts_client.synthesize_pcm(text)
        except Exception:
            LOG.exception("tts_error id=%s", utterance_id)
            self._dash("tts.failed", utterance_id=utterance_id, code="tts_unavailable")
            return
        if not pcm or len(pcm) % 2 or len(pcm) > MAX_TTS_PCM_BYTES:
            LOG.warning("tts_error code=tts_audio_invalid id=%s bytes=%d", utterance_id, len(pcm))
            self._dash("tts.failed", utterance_id=utterance_id, code="tts_audio_invalid", bytes=len(pcm))
            return
        await self._send_json(
            websocket,
                {
                    "type": "audio.start",
                    "id": utterance_id,
                    "audio": {"rate": SAMPLE_RATE, "bits": BITS_PER_SAMPLE, "channels": CHANNELS, "encoding": ENCODING},
                }
        )
        for offset in range(0, len(pcm), TTS_CHUNK_BYTES):
            await websocket.send(pcm[offset : offset + TTS_CHUNK_BYTES])
        await self._send_json(websocket, {"type": "audio.end", "id": utterance_id})
        self._dash("tts.completed", utterance_id=utterance_id, bytes=len(pcm))
        LOG.info("tts_complete id=%s bytes=%d", utterance_id, len(pcm))


class DashScopeASRClient:
    def __init__(self, api_key: str | None, base_url: str | None, timeout_seconds: float = 30.0):
        if not api_key:
            raise ASRFailure("missing_credentials", "DASHSCOPE_API_KEY is not configured")
        if not base_url:
            raise ASRFailure("missing_configuration", "DASHSCOPE_BASE_URL is not configured")
        self._api_key = api_key
        self._base_url = base_url
        self._timeout_seconds = timeout_seconds

    async def transcribe_wav(self, wav_bytes: bytes) -> str:
        return await asyncio.to_thread(self._transcribe_sync, wav_bytes)

    def _transcribe_sync(self, wav_bytes: bytes) -> str:
        try:
            from openai import OpenAI
        except ImportError as exc:
            raise ASRFailure("missing_dependency", "openai package is not installed") from exc
        data_uri = "data:audio/wav;base64," + base64.b64encode(wav_bytes).decode("ascii")
        try:
            client = OpenAI(api_key=self._api_key, base_url=self._base_url, timeout=self._timeout_seconds)
            completion = client.chat.completions.create(
                model="qwen3-asr-flash",
                messages=[
                    {
                        "role": "user",
                        "content": [
                            {
                                "type": "input_audio",
                                "input_audio": {"data": data_uri},
                            }
                        ],
                    }
                ],
                stream=False,
                extra_body={"asr_options": {"enable_itn": False}},
            )
        except Exception as exc:
            raise ASRFailure("asr_unavailable", "ASR request failed") from exc
        try:
            content = completion.choices[0].message.content
        except Exception as exc:
            raise ASRFailure("asr_invalid_response", "ASR response did not include recognized text") from exc
        if content is None:
            return ""
        if not isinstance(content, str):
            raise ASRFailure("asr_invalid_response", "ASR response content was not text")
        return content


class MacSystemTTSClient:
    def __init__(self, voice: str = "Tingting", timeout_seconds: float = 30.0):
        self._voice = voice
        self._timeout_seconds = timeout_seconds

    async def synthesize_pcm(self, text: str) -> bytes:
        return await asyncio.to_thread(self._synthesize_sync, text)

    def _synthesize_sync(self, text: str) -> bytes:
        with tempfile.TemporaryDirectory(prefix="walkie-tts-") as temp_dir:
            source = os.path.join(temp_dir, "speech.aiff")
            output = os.path.join(temp_dir, "speech.wav")
            subprocess.run(
                ["say", "-v", self._voice, "-o", source, text],
                check=True,
                capture_output=True,
                timeout=self._timeout_seconds,
            )
            subprocess.run(
                ["afconvert", "-f", "WAVE", "-d", "LEI16@16000", source, output],
                check=True,
                capture_output=True,
                timeout=self._timeout_seconds,
            )
            with wave.open(output, "rb") as wav:
                if wav.getframerate() != SAMPLE_RATE or wav.getnchannels() != CHANNELS or wav.getsampwidth() != 2:
                    raise ValueError("system TTS returned an unsupported audio format")
                pcm = wav.readframes(wav.getnframes())
        if len(pcm) > MAX_TTS_PCM_BYTES:
            raise ValueError("system TTS exceeded the 30 second audio limit")
        return pcm


def _pcm_from_wav_bytes(wav_bytes: bytes, timeout_seconds: float = 30.0) -> bytes:
    """Normalize a WAV blob to 16 kHz LEI16 mono PCM.

    Fast path parses the WAV directly when it already matches the device
    format; anything else is resampled with afconvert (macOS, already a
    dependency of the system-TTS path).
    """
    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wav:
            if wav.getframerate() == SAMPLE_RATE and wav.getnchannels() == CHANNELS and wav.getsampwidth() == 2:
                pcm = wav.readframes(wav.getnframes())
                if len(pcm) > MAX_TTS_PCM_BYTES:
                    raise ValueError("TTS exceeded the 30 second audio limit")
                return pcm
    except wave.Error:
        pass
    with tempfile.TemporaryDirectory(prefix="walkie-tts-conv-") as temp_dir:
        source = os.path.join(temp_dir, "in.wav")
        output = os.path.join(temp_dir, "out.wav")
        with open(source, "wb") as handle:
            handle.write(wav_bytes)
        subprocess.run(
            ["afconvert", "-f", "WAVE", "-d", "LEI16@16000", source, output],
            check=True,
            capture_output=True,
            timeout=timeout_seconds,
        )
        with wave.open(output, "rb") as wav:
            if wav.getframerate() != SAMPLE_RATE or wav.getnchannels() != CHANNELS or wav.getsampwidth() != 2:
                raise ValueError("converted TTS audio has an unsupported format")
            pcm = wav.readframes(wav.getnframes())
    if len(pcm) > MAX_TTS_PCM_BYTES:
        raise ValueError("TTS exceeded the 30 second audio limit")
    return pcm


class DashScopeTTSClient:
    """DashScope native HTTP TTS (qwen3-tts-flash).

    The OpenAI-compatible endpoint does not serve speech synthesis (404), so
    this calls the native multimodal-generation API, downloads the resulting
    temporary WAV URL, and normalizes it to the device PCM format.
    """

    def __init__(
        self,
        api_key: str | None,
        base_url: str | None,
        model: str = "qwen3-tts-flash",
        voice: str = "Cherry",
        timeout_seconds: float = 30.0,
        post: Any | None = None,
        get: Any | None = None,
    ):
        if not api_key:
            raise ValueError("DASHSCOPE_API_KEY is not configured")
        if not base_url:
            raise ValueError("DASHSCOPE_BASE_URL is not configured")
        host = base_url.split("//", 1)[-1].split("/", 1)[0]
        self._endpoint = f"https://{host}/api/v1/services/aigc/multimodal-generation/generation"
        self._api_key = api_key
        self._model = model
        self._voice = voice
        self._timeout_seconds = timeout_seconds
        self._post = post or self._default_post
        self._get = get or self._default_get

    async def synthesize_pcm(self, text: str) -> bytes:
        return await asyncio.to_thread(self._synthesize_sync, text)

    def _default_post(self, payload: dict[str, Any]) -> dict[str, Any]:
        import httpx

        response = httpx.post(
            self._endpoint,
            json=payload,
            headers={"Authorization": f"Bearer {self._api_key}"},
            timeout=self._timeout_seconds,
        )
        response.raise_for_status()
        return response.json()

    def _default_get(self, url: str) -> bytes:
        import httpx

        response = httpx.get(url, timeout=self._timeout_seconds)
        response.raise_for_status()
        return response.content

    def _synthesize_sync(self, text: str) -> bytes:
        payload = {
            "model": self._model,
            "input": {"text": text, "voice": self._voice, "language_type": "Auto"},
        }
        body = self._post(payload)
        audio = body.get("output", {}).get("audio", {}) if isinstance(body, dict) else {}
        url = audio.get("url")
        if not url:
            raise ValueError("DashScope TTS response did not include an audio URL")
        return _pcm_from_wav_bytes(self._get(url), self._timeout_seconds)


class FallbackTTSClient:
    """Try the primary TTS client; on any failure fall back to the secondary."""

    def __init__(self, primary: TTSClient, fallback: TTSClient):
        self._primary = primary
        self._fallback = fallback

    async def synthesize_pcm(self, text: str) -> bytes:
        try:
            return await self._primary.synthesize_pcm(text)
        except Exception:
            LOG.warning("tts primary failed, using fallback", exc_info=True)
        return await self._fallback.synthesize_pcm(text)


def parse_json_message(message: str) -> dict[str, Any]:
    try:
        payload = json.loads(message)
    except json.JSONDecodeError as exc:
        raise ProtocolFailure("invalid_json", "control message is not valid JSON") from exc
    if not isinstance(payload, dict):
        raise ProtocolFailure("invalid_json", "control message must be a JSON object")
    return payload


def require_string(payload: dict[str, Any], key: str) -> str:
    value = payload.get(key)
    if not isinstance(value, str) or not value:
        raise ProtocolFailure("invalid_message", f"missing or invalid {key}")
    return value


def validate_audio_format(audio: Any, utterance_id: str) -> None:
    expected = {
        "rate": SAMPLE_RATE,
        "bits": BITS_PER_SAMPLE,
        "channels": CHANNELS,
        "encoding": ENCODING,
    }
    if not isinstance(audio, dict):
        raise ProtocolFailure("audio_format", "audio format must be declared", utterance_id)
    for key, expected_value in expected.items():
        if audio.get(key) != expected_value:
            raise ProtocolFailure("audio_format", f"unsupported audio {key}", utterance_id)


def pcm_to_wav(pcm: bytes) -> bytes:
    if len(pcm) % 2:
        raise ProtocolFailure("audio_format", "PCM input has a partial sample")
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(BITS_PER_SAMPLE // 8)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm)
    return output.getvalue()


async def send_error(websocket: Any, code: str, message: str, utterance_id: str | None) -> None:
    await websocket.send(
        json.dumps(
            {
                "type": "error",
                "id": utterance_id,
                "code": code,
                "message": message,
                "retryable": True,
            }
        )
    )


def _build_tts_client(config: BridgeConfig) -> TTSClient:
    """Select the TTS client: DashScope neural voices when configured, macOS
    `say` as offline fallback. WALKIE_TTS_PROVIDER: auto | dashscope | say."""
    provider = config.tts_provider.lower()
    mac_client = MacSystemTTSClient(config.tts_voice, config.tts_timeout_seconds)
    if provider == "say":
        return mac_client
    if provider not in {"auto", "dashscope"}:
        raise ControlProtocolError("invalid_tts_provider", "WALKIE_TTS_PROVIDER must be auto, dashscope, or say")
    if config.dashscope_api_key:
        dashscope_client = DashScopeTTSClient(
            config.dashscope_api_key,
            config.dashscope_base_url,
            model=config.tts_model,
            voice=config.tts_dashscope_voice,
            timeout_seconds=config.tts_timeout_seconds,
        )
        return FallbackTTSClient(dashscope_client, mac_client)
    if provider == "dashscope":
        raise ControlProtocolError("missing_credentials", "WALKIE_TTS_PROVIDER=dashscope requires DASHSCOPE_API_KEY")
    return mac_client


async def serve(config: BridgeConfig) -> None:
    if websockets is None:
        raise RuntimeError("websockets package is not installed")
    asr_client = DashScopeASRClient(
        config.dashscope_api_key,
        config.dashscope_base_url,
        timeout_seconds=config.asr_timeout_seconds,
    )
    tts_client = _build_tts_client(config)
    if config.control_enabled and config.control_secret is None:
        raise ControlProtocolError("missing_control_secret", "control mode requires WALKIE_CONTROL_SECRET")
    router = MultiAgentRouter(config.control_aliases, config.proposal_ttl_seconds) if config.control_enabled else None
    control_client = CcBridgeClient(config.cc_bridge_socket) if config.control_enabled else None
    proposal_store = ProposalStore()
    task_tracker = TaskTracker()
    dashboard_state = DashboardState()
    dashboard_server = start_dashboard(dashboard_state, config.dashboard_port) if config.dashboard_enabled else None
    if config.dashboard_enabled and dashboard_server is None:
        LOG.warning("dashboard_error code=bind_failed port=%d", config.dashboard_port)

    async def refresh_control_snapshot() -> None:
        if control_client is None:
            return
        last_projection: tuple | None = None
        while True:
            try:
                snapshot = await control_client.snapshot()
                sessions = snapshot.get("sessions", [])
                projection = (
                    True,
                    int(snapshot.get("revision", 0)),
                    tuple((row.get("agent"), row.get("label"), row.get("project_label"), row.get("state"),
                           bool((row.get("capabilities") or {}).get("steer")),
                           bool((row.get("capabilities") or {}).get("permission_reply")))
                          for row in sessions if isinstance(row, dict)),
                )
                if projection != last_projection:
                    dashboard_state.publish("control.snapshot", healthy=True,
                                            revision=snapshot.get("revision", 0), sessions=sessions)
                    last_projection = projection
            except (RouterError, OSError):
                projection = (False, 0, ())
                if projection != last_projection:
                    dashboard_state.publish("control.snapshot", healthy=False, revision=0, sessions=[])
                    last_projection = projection
            await asyncio.sleep(2)

    async def handler(websocket: Any) -> None:
        await ConnectionHandler(
            asr_client,
            asr_timeout_seconds=config.asr_timeout_seconds,
            tts_client=tts_client,
            control_secret=config.control_secret,
            router=router,
            control_client=control_client,
            proposal_store=proposal_store,
            task_tracker=task_tracker,
            dashboard_state=dashboard_state,
        ).handle(websocket)

    refresh_task = asyncio.create_task(refresh_control_snapshot())
    try:
        async with websockets.serve(handler, config.host, config.port):
            dashboard_state.publish("bridge.started", port=config.port)
            LOG.info("walkie_bridge_listening host=%s port=%d", config.host, config.port)
            if dashboard_server is not None:
                LOG.info("dashboard_listening host=127.0.0.1 port=%d", dashboard_server.port)
            await asyncio.Future()
    finally:
        refresh_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await refresh_task
        if dashboard_server is not None:
            dashboard_server.stop()


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="StopWatch walkie-talkie prototype bridge")
    parser.add_argument("--host", default=None)
    parser.add_argument("--port", type=int, default=None)
    return parser


def main() -> int:
    logging.basicConfig(level=os.getenv("WALKIE_BRIDGE_LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
    try:
        config = BridgeConfig.from_env()
        args = build_arg_parser().parse_args()
        if args.host is not None:
            config.host = args.host
        if args.port is not None:
            config.port = args.port
        asyncio.run(serve(config))
    except (ASRFailure, ControlProtocolError) as exc:
        LOG.error("configuration_error code=%s message=%s", exc.code, exc.message)
        return 2
    except KeyboardInterrupt:
        LOG.info("walkie_bridge_stopped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
