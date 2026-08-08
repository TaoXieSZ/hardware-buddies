from __future__ import annotations

import asyncio
import json
import os
import struct
import wave

import pytest
import websockets

from bridge import (
    ASRFailure,
    BridgeConfig,
    ConnectionHandler,
    DashScopeASRClient,
    MAX_PCM_BYTES,
    pcm_to_wav,
)


VALID_AUDIO = {"rate": 16000, "bits": 16, "channels": 1, "encoding": "pcm_s16le"}


class FakeWebSocket:
    def __init__(self, incoming):
        self._incoming = list(incoming)
        self.sent = []

    def __aiter__(self):
        return self

    async def __anext__(self):
        if not self._incoming:
            raise StopAsyncIteration
        item = self._incoming.pop(0)
        if isinstance(item, BaseException):
            raise item
        return item

    async def send(self, message):
        self.sent.append(json.loads(message))


class FakeASR:
    def __init__(self, text="hello stopwatch", failure=None, delay_seconds=0):
        self.text = text
        self.failure = failure
        self.delay_seconds = delay_seconds
        self.calls = []

    async def transcribe_wav(self, wav_bytes):
        self.calls.append(wav_bytes)
        if self.delay_seconds:
            await asyncio.sleep(self.delay_seconds)
        if self.failure:
            raise self.failure
        return self.text


def msg(payload):
    return json.dumps(payload)


def start(utterance_id="utt-1", audio=VALID_AUDIO):
    return msg({"type": "utterance.start", "id": utterance_id, "audio": audio})


def end(utterance_id="utt-1"):
    return msg({"type": "utterance.end", "id": utterance_id})


def cancel(utterance_id="utt-1"):
    return msg({"type": "utterance.cancel", "id": utterance_id})


def run_handler(incoming, asr=None):
    websocket = FakeWebSocket(incoming)
    asr = asr or FakeASR()
    asyncio.run(ConnectionHandler(asr).handle(websocket))
    return websocket, asr


def run_handler_with_timeout(incoming, asr, timeout_seconds):
    websocket = FakeWebSocket(incoming)
    asyncio.run(ConnectionHandler(asr, asr_timeout_seconds=timeout_seconds).handle(websocket))
    return websocket, asr


def test_complete_framed_utterance_returns_correlated_transcript():
    websocket, asr = run_handler([start("utt-1"), b"\x01\x00\x02\x00", end("utt-1")])

    assert websocket.sent == [{"type": "transcript", "id": "utt-1", "text": "hello stopwatch"}]
    assert len(asr.calls) == 1


def test_invalid_json_control_message_is_rejected():
    websocket, asr = run_handler(["{"])

    assert websocket.sent[0]["code"] == "invalid_json"
    assert asr.calls == []


def test_missing_control_message_type_is_rejected():
    websocket, asr = run_handler([msg({"id": "utt-1"})])

    assert websocket.sent[0]["code"] == "invalid_message"
    assert asr.calls == []


def test_unsupported_protocol_version_is_rejected():
    websocket, asr = run_handler([msg({"type": "hello", "protocol": 2, "device_id": "watch-1"})])

    assert websocket.sent[0]["code"] == "protocol_version"
    assert asr.calls == []


def test_binary_without_active_utterance_is_rejected():
    websocket, asr = run_handler([b"\x01\x00"])

    assert websocket.sent[0]["type"] == "error"
    assert websocket.sent[0]["code"] == "invalid_sequence"
    assert websocket.sent[0]["id"] is None
    assert asr.calls == []


def test_second_start_discards_partial_and_reports_active_id():
    websocket, asr = run_handler([start("utt-1"), b"\x01\x00", start("utt-2")])

    assert websocket.sent[0]["code"] == "invalid_sequence"
    assert websocket.sent[0]["id"] == "utt-1"
    assert asr.calls == []


def test_mismatched_end_rejects_and_does_not_call_asr():
    websocket, asr = run_handler([start("utt-1"), b"\x01\x00", end("utt-2")])

    assert websocket.sent[0]["code"] == "invalid_sequence"
    assert websocket.sent[0]["id"] == "utt-2"
    assert asr.calls == []


def test_cancel_discards_buffer_without_asr_call():
    websocket, asr = run_handler([start("utt-1"), b"\x01\x00", cancel("utt-1")])

    assert websocket.sent == []
    assert asr.calls == []


def test_disconnection_discards_partial_without_asr_call():
    websocket, asr = run_handler([start("utt-1"), b"\x01\x00", ConnectionError("closed")])

    assert websocket.sent == []
    assert asr.calls == []


def test_unsupported_audio_format_is_rejected():
    bad_audio = {"rate": 8000, "bits": 16, "channels": 1, "encoding": "pcm_s16le"}
    websocket, asr = run_handler([start("utt-1", bad_audio), b"\x01\x00", end("utt-1")])

    assert websocket.sent[0]["code"] == "audio_format"
    assert asr.calls == []


def test_partial_sample_frame_is_rejected():
    websocket, asr = run_handler([start("utt-1"), b"\x01"])

    assert websocket.sent[0]["code"] == "audio_format"
    assert websocket.sent[0]["id"] == "utt-1"
    assert asr.calls == []


def test_sixty_second_pcm_limit_is_enforced():
    websocket, asr = run_handler([start("utt-1"), b"\x00" * MAX_PCM_BYTES, b"\x00\x00"])

    assert websocket.sent[0]["code"] == "utterance_too_large"
    assert websocket.sent[0]["id"] == "utt-1"
    assert asr.calls == []


def test_pcm_to_wav_wraps_16khz_mono_s16le():
    pcm = struct.pack("<hhhh", 0, 100, -100, 0)
    wav_bytes = pcm_to_wav(pcm)

    with wave.open(PathBytes(wav_bytes), "rb") as wav:
        assert wav.getframerate() == 16000
        assert wav.getnchannels() == 1
        assert wav.getsampwidth() == 2
        assert wav.readframes(4) == pcm


class PathBytes:
    def __init__(self, data):
        from io import BytesIO

        self._file = BytesIO(data)

    def read(self, size=-1):
        return self._file.read(size)

    def seek(self, offset, whence=0):
        return self._file.seek(offset, whence)

    def tell(self):
        return self._file.tell()

    def close(self):
        return None


def test_no_speech_response_is_structured_retryable_error():
    websocket, asr = run_handler([start("utt-1"), b"\x00\x00", end("utt-1")], FakeASR(text=""))

    assert websocket.sent[0]["type"] == "error"
    assert websocket.sent[0]["code"] == "no_speech"
    assert websocket.sent[0]["id"] == "utt-1"
    assert websocket.sent[0]["retryable"] is True
    assert len(asr.calls) == 1


def test_asr_failure_is_structured_and_correlated():
    failure = ASRFailure("asr_unavailable", "ASR request failed")
    websocket, asr = run_handler([start("utt-1"), b"\x00\x00", end("utt-1")], FakeASR(failure=failure))

    assert websocket.sent[0]["type"] == "error"
    assert websocket.sent[0]["code"] == "asr_unavailable"
    assert websocket.sent[0]["id"] == "utt-1"


def test_asr_timeout_is_structured_and_correlated():
    websocket, asr = run_handler_with_timeout(
        [start("utt-1"), b"\x00\x00", end("utt-1")],
        FakeASR(delay_seconds=0.05),
        timeout_seconds=0.001,
    )

    assert websocket.sent[0]["type"] == "error"
    assert websocket.sent[0]["code"] == "asr_timeout"
    assert websocket.sent[0]["id"] == "utt-1"
    assert len(asr.calls) == 1


def test_real_websocket_server_exchanges_protocol_frames():
    async def scenario():
        asr = FakeASR(text="network hello")

        async def handler(websocket):
            await ConnectionHandler(asr).handle(websocket)

        async with websockets.serve(handler, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            async with websockets.connect(f"ws://127.0.0.1:{port}/audio") as websocket:
                await websocket.send(start("utt-1"))
                await websocket.send(b"\x00\x00")
                await websocket.send(end("utt-1"))
                response = json.loads(await websocket.recv())

        assert response == {"type": "transcript", "id": "utt-1", "text": "network hello"}
        assert len(asr.calls) == 1

    asyncio.run(scenario())


def test_missing_dashscope_credentials_are_local_configuration_errors():
    with pytest.raises(ASRFailure) as excinfo:
        DashScopeASRClient(api_key=None, base_url="https://example.invalid")

    assert excinfo.value.code == "missing_credentials"
    assert "sk-" not in excinfo.value.message


def test_missing_dashscope_base_url_is_local_configuration_error():
    with pytest.raises(ASRFailure) as excinfo:
        DashScopeASRClient(api_key="sk-test-redacted", base_url=None)

    assert excinfo.value.code == "missing_configuration"
    assert "sk-test-redacted" not in excinfo.value.message


def test_logs_do_not_include_audio_text_or_credentials(caplog):
    caplog.set_level("INFO", logger="walkie_bridge")
    secret = "sk-test-redacted"
    websocket, _ = run_handler([start("utt-1"), b"\x00\x00", end("utt-1")], FakeASR(text="secret transcript"))

    assert websocket.sent[0]["text"] == "secret transcript"
    logs = caplog.text
    assert "secret transcript" not in logs
    assert secret not in logs
    assert "AA==" not in logs


def test_bridge_config_reads_environment_without_printing_secret(monkeypatch):
    monkeypatch.setenv("DASHSCOPE_API_KEY", "sk-test-redacted")
    monkeypatch.setenv("DASHSCOPE_BASE_URL", "https://example.invalid/v1")

    config = BridgeConfig.from_env()

    assert config.dashscope_api_key == "sk-test-redacted"
    assert config.dashscope_base_url == "https://example.invalid/v1"


@pytest.mark.live
def test_live_dashscope_transcribes_non_personal_fixture():
    if not os.getenv("WALKIE_BRIDGE_LIVE_ASR"):
        pytest.skip("set WALKIE_BRIDGE_LIVE_ASR=1 to enable live DashScope ASR")
    api_key = os.getenv("DASHSCOPE_API_KEY")
    base_url = os.getenv("DASHSCOPE_BASE_URL")
    if not api_key or not base_url:
        pytest.skip("DASHSCOPE_API_KEY and DASHSCOPE_BASE_URL are required for live DashScope ASR")

    pcm = b"\x00\x00" * 16000
    client = DashScopeASRClient(api_key=api_key, base_url=base_url, timeout_seconds=30)
    text = asyncio.run(client.transcribe_wav(pcm_to_wav(pcm)))

    assert isinstance(text, str)
