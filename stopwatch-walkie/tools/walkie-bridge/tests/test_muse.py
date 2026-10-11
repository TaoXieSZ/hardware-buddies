from __future__ import annotations

import asyncio
import io
import json
import os
import urllib.error
from email.message import Message

import pytest

from bridge import ConnectionHandler
from muse import MuseClient, MuseConfig, MuseFailure, display_line, render
from tests.test_bridge import FakeASR, FakeTTS, FakeWebSocket, VALID_AUDIO, end, start

QUESTION = "明天上午有什么安排"


class FakeResponse:
    def __init__(self, status, body, headers=None):
        self.status = status
        self._body = json.dumps(body, ensure_ascii=False).encode()
        self.headers = Message()
        for key, value in (headers or {}).items():
            self.headers[key] = value

    def read(self):
        return self._body

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False


class FakeOpener:
    """Replays scripted (status, body, headers) replies per path and records requests."""

    def __init__(self, script):
        self.script = {path: list(replies) for path, replies in script.items()}
        self.requests = []

    def open(self, request, timeout):
        path = request.full_url.split("/device", 1)[1]
        self.requests.append((path, dict(request.header_items()), request.data))
        status, body, *rest = self.script[path].pop(0)
        if status >= 400:
            headers = Message()
            raise urllib.error.HTTPError(request.full_url, status, "error", headers, io.BytesIO(json.dumps(body).encode()))
        return FakeResponse(status, body, rest[0] if rest else None)


ANSWER = {
    "status": "done",
    "brief": "明天上午九点有组会，十点前要交周报。",
    "points": ["组会在三楼", "周报发给导师"],
    "cited": True,
    "general": False,
}


def client(script, **kwargs):
    opener = FakeOpener(script)

    async def no_sleep(_seconds):
        return None

    return MuseClient(MuseConfig("https://muse.example", "t" * 43), opener=opener, sleep=no_sleep, **kwargs), opener


def test_display_line_fits_one_watch_line():
    assert display_line("明天上午九点有组会。十点前要交周报。") == "明天上午九点有组会。"
    assert display_line("一" * 30) == "一" * 17 + "…"
    assert len(display_line("没有标点但是非常非常非常长的一句话会被截断")) == 18


def test_render_speaks_brief_and_points_and_labels_general_answers():
    answer = render(ANSWER)
    assert answer.display == "明天上午九点有组会，十点前要交周报。"  # exactly 18 chars: fits
    assert answer.spoken == "明天上午九点有组会，十点前要交周报。组会在三楼。周报发给导师"
    general = render({**ANSWER, "general": True, "points": []})
    assert general.spoken.startswith("这不是来自你的资料。")


def test_device_file_must_be_private(tmp_path):
    path = tmp_path / "muse.json"
    path.write_text(json.dumps({"server": "https://muse.example", "token": "t" * 43}))
    os.chmod(path, 0o644)
    with pytest.raises(MuseFailure) as error:
        MuseConfig.from_file(path)
    assert error.value.code == "muse_config_insecure"
    os.chmod(path, 0o600)
    assert MuseConfig.from_file(path).server == "https://muse.example"


def test_transcribe_posts_wav_with_the_device_token():
    muse, opener = client({"/v1/transcribe": [(200, {"text": QUESTION})]})
    assert asyncio.run(muse.transcribe_wav(b"RIFF....WAVE")) == QUESTION
    path, headers, body = opener.requests[0]
    assert path == "/v1/transcribe"
    assert headers["Content-type"] == "audio/wav"
    assert headers["Authorization"] == "Bearer " + "t" * 43
    assert body == b"RIFF....WAVE"


def test_ask_uses_protocol_2_and_retries_202_with_the_same_request_id():
    muse, opener = client({"/v1/chat": [(202, {"status": "processing"}, {"Retry-After": "3"}), (200, ANSWER)]})
    answer = asyncio.run(muse.ask("conn:utt-1", QUESTION))
    assert answer.cited is True
    first, second = (json.loads(request[2]) for request in opener.requests)
    assert first["requestId"] == second["requestId"]
    assert first["briefChars"] == 60
    assert opener.requests[0][1]["X-openmuse-protocol"] == "2"


@pytest.mark.parametrize(
    ("status", "code"),
    [(401, "muse_device_revoked"), (429, "chat_limit"), (503, "chat_unavailable"), (500, "chat_failed")],
)
def test_ask_maps_http_failures_to_watch_error_codes(status, code):
    muse, _ = client({"/v1/chat": [(status, {"error": "nope"})]})
    with pytest.raises(MuseFailure) as error:
        asyncio.run(muse.ask("conn:utt-1", QUESTION))
    assert error.value.code == code


def test_protocol_v1_with_muse_sends_one_answer_line_then_speech():
    muse, _ = client({"/v1/transcribe": [(200, {"text": QUESTION})], "/v1/chat": [(200, ANSWER)]})
    websocket = FakeWebSocket([start("utt-1"), b"\x01\x00\x02\x00", end("utt-1")])
    dashscope, tts = FakeASR(), FakeTTS()
    asyncio.run(ConnectionHandler(dashscope, tts_client=tts, muse=muse).handle(websocket))
    assert websocket.sent[0] == {"type": "transcript", "id": "utt-1", "text": render(ANSWER).display}
    assert websocket.sent[1] == {"type": "audio.start", "id": "utt-1", "audio": VALID_AUDIO}
    assert tts.calls == [render(ANSWER).spoken]
    assert dashscope.calls == []  # Muse questions never go to DashScope


def test_muse_failure_becomes_a_correlated_watch_error():
    muse, _ = client({"/v1/transcribe": [(503, {"error": "语音转写尚未配置"})]})
    websocket = FakeWebSocket([start("utt-1"), b"\x01\x00\x02\x00", end("utt-1")])
    asyncio.run(ConnectionHandler(FakeASR(), muse=muse).handle(websocket))
    assert websocket.sent[0]["type"] == "error"
    assert websocket.sent[0]["id"] == "utt-1"
    assert websocket.sent[0]["code"] == "transcribe_unavailable"
