#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import base64
import io
import json
import logging
import os
import time
import wave
from dataclasses import dataclass
from typing import Any, Protocol

try:
    import websockets
except ImportError:  # pragma: no cover - handled by main()
    websockets = None


PROTOCOL_VERSION = 1
SAMPLE_RATE = 16000
BITS_PER_SAMPLE = 16
CHANNELS = 1
ENCODING = "pcm_s16le"
BYTES_PER_SECOND = SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE // 8)
MAX_PCM_BYTES = BYTES_PER_SECOND * 60

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


@dataclass
class BridgeConfig:
    host: str = "127.0.0.1"
    port: int = 8765
    dashscope_api_key: str | None = None
    dashscope_base_url: str | None = None
    asr_timeout_seconds: float = 30.0

    @classmethod
    def from_env(cls) -> "BridgeConfig":
        return cls(
            host=os.getenv("WALKIE_BRIDGE_HOST", "127.0.0.1"),
            port=int(os.getenv("WALKIE_BRIDGE_PORT", "8765")),
            dashscope_api_key=os.getenv("DASHSCOPE_API_KEY"),
            dashscope_base_url=os.getenv("DASHSCOPE_BASE_URL"),
            asr_timeout_seconds=float(os.getenv("WALKIE_BRIDGE_ASR_TIMEOUT", "30")),
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
    def __init__(self, asr_client: ASRClient, asr_timeout_seconds: float = 30.0):
        self._asr_client = asr_client
        self._asr_timeout_seconds = asr_timeout_seconds
        self._active: Utterance | None = None

    async def handle(self, websocket: Any) -> None:
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
                        await send_error(websocket, exc.code, exc.message, exc.utterance_id)
            except ConnectionError:
                LOG.info("connection_closed")
        finally:
            if self._active is not None:
                LOG.info("disconnect_cleanup id=%s bytes=%d", self._active.utterance_id, self._active.byte_count)
            self._active = None

    async def _handle_binary(self, message: bytes) -> None:
        if self._active is None:
            raise ProtocolFailure("invalid_sequence", "binary audio received without an active utterance")
        self._active.append(message)

    async def _handle_text(self, websocket: Any, message: str) -> None:
        payload = parse_json_message(message)
        msg_type = require_string(payload, "type")
        if msg_type == "hello":
            self._handle_hello(payload)
            return
        if msg_type == "utterance.start":
            self._start(payload)
            return
        if msg_type == "utterance.cancel":
            utterance_id = require_string(payload, "id")
            self._require_active_id(utterance_id)
            LOG.info("utterance_cancel id=%s bytes=%d", utterance_id, self._active.byte_count if self._active else 0)
            self._active = None
            return
        if msg_type == "utterance.end":
            utterance_id = require_string(payload, "id")
            utterance = self._require_active_id(utterance_id)
            self._active = None
            await self._submit(websocket, utterance)
            return
        raise ProtocolFailure("unknown_message", f"unsupported message type: {msg_type}")

    def _handle_hello(self, payload: dict[str, Any]) -> None:
        protocol = payload.get("protocol")
        if protocol != PROTOCOL_VERSION:
            raise ProtocolFailure("protocol_version", "unsupported protocol version")
        require_string(payload, "device_id")
        LOG.info("device_hello protocol=%s", protocol)

    def _start(self, payload: dict[str, Any]) -> None:
        if self._active is not None:
            raise ProtocolFailure("invalid_sequence", "a second utterance started before the first ended", self._active.utterance_id)
        utterance_id = require_string(payload, "id")
        validate_audio_format(payload.get("audio"), utterance_id)
        self._active = Utterance(utterance_id=utterance_id, started_at=time.monotonic(), pcm=bytearray())
        LOG.info("utterance_start id=%s", utterance_id)

    def _require_active_id(self, utterance_id: str) -> Utterance:
        if self._active is None:
            raise ProtocolFailure("invalid_sequence", "no active utterance", utterance_id)
        if self._active.utterance_id != utterance_id:
            raise ProtocolFailure("invalid_sequence", "utterance id does not match the active utterance", utterance_id)
        return self._active

    async def _submit(self, websocket: Any, utterance: Utterance) -> None:
        if utterance.byte_count == 0:
            await send_error(websocket, "no_speech", "no speech recognized", utterance.utterance_id)
            return
        wav_bytes = pcm_to_wav(bytes(utterance.pcm))
        started = time.monotonic()
        try:
            text = await asyncio.wait_for(
                self._asr_client.transcribe_wav(wav_bytes),
                timeout=self._asr_timeout_seconds,
            )
        except TimeoutError:
            LOG.warning("asr_error code=asr_timeout id=%s bytes=%d", utterance.utterance_id, utterance.byte_count)
            await send_error(websocket, "asr_timeout", "ASR request timed out", utterance.utterance_id)
            return
        except ASRFailure as exc:
            LOG.warning("asr_error code=%s id=%s bytes=%d", exc.code, utterance.utterance_id, utterance.byte_count)
            await send_error(websocket, exc.code, exc.message, utterance.utterance_id)
            return
        text = text.strip()
        latency_ms = int((time.monotonic() - started) * 1000)
        LOG.info("asr_complete id=%s bytes=%d latency_ms=%d text_len=%d", utterance.utterance_id, utterance.byte_count, latency_ms, len(text))
        if not text:
            await send_error(websocket, "no_speech", "no speech recognized", utterance.utterance_id)
            return
        await websocket.send(json.dumps({"type": "transcript", "id": utterance.utterance_id, "text": text}, ensure_ascii=False))


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


async def serve(config: BridgeConfig) -> None:
    if websockets is None:
        raise RuntimeError("websockets package is not installed")
    asr_client = DashScopeASRClient(
        config.dashscope_api_key,
        config.dashscope_base_url,
        timeout_seconds=config.asr_timeout_seconds,
    )

    async def handler(websocket: Any) -> None:
        await ConnectionHandler(asr_client, asr_timeout_seconds=config.asr_timeout_seconds).handle(websocket)

    async with websockets.serve(handler, config.host, config.port):
        LOG.info("walkie_bridge_listening host=%s port=%d", config.host, config.port)
        await asyncio.Future()


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="StopWatch walkie-talkie prototype bridge")
    parser.add_argument("--host", default=None)
    parser.add_argument("--port", type=int, default=None)
    return parser


def main() -> int:
    logging.basicConfig(level=os.getenv("WALKIE_BRIDGE_LOG_LEVEL", "INFO"), format="%(asctime)s %(levelname)s %(message)s")
    config = BridgeConfig.from_env()
    args = build_arg_parser().parse_args()
    if args.host is not None:
        config.host = args.host
    if args.port is not None:
        config.port = args.port
    try:
        asyncio.run(serve(config))
    except ASRFailure as exc:
        LOG.error("configuration_error code=%s message=%s", exc.code, exc.message)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
