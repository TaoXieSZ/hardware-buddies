"""Ask the personal OpenMuse cloud from the walkie bridge (protocol v1 only).

Flow: watch PCM → bridge WAV → OpenMuse `/device/v1/transcribe` (the server forwards it to
Tencent one-sentence recognition and keeps nothing) → OpenMuse `/device/v1/chat` with
protocol 2 → one short display line for the watch plus a spoken answer.

The device token comes from a private JSON file (`{"server": ..., "token": ...}`, mode 0600),
paired as its own device in the OpenMuse web UI. Nothing here logs audio, questions or answers.
"""

from __future__ import annotations

import asyncio
import json
import os
import stat
import time
import urllib.error
import urllib.request
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any

DISPLAY_CHARS = 18  # one efontCN_16 line in the watch result band (326 px)
BRIEF_CHARS = 60
ANSWER_LIMIT_SECONDS = 60.0
REQUEST_NAMESPACE = uuid.UUID("4d3b1c7e-5a1f-4c55-9a43-6f2e0e5b7a10")


class MuseFailure(Exception):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code
        self.message = message


@dataclass(frozen=True)
class MuseConfig:
    server: str
    token: str
    brief_chars: int = BRIEF_CHARS

    @classmethod
    def from_file(cls, path: str | os.PathLike[str]) -> "MuseConfig":
        file = Path(path).expanduser()
        info = file.lstat()
        if stat.S_ISLNK(info.st_mode) or info.st_mode & 0o077:
            raise MuseFailure("muse_config_insecure", "Muse device file must be a regular 0600 file")
        data = json.loads(file.read_text(encoding="utf-8"))
        server, token = str(data.get("server", "")).rstrip("/"), str(data.get("token", ""))
        if not server.startswith("https://") or len(token) < 16:
            raise MuseFailure("muse_config_invalid", "Muse device file needs an https server and a token")
        return cls(server=server, token=token)


@dataclass(frozen=True)
class MuseAnswer:
    display: str
    spoken: str
    cited: bool
    general: bool


def display_line(text: str, limit: int = DISPLAY_CHARS) -> str:
    """First sentence, clipped to one watch line."""
    text = " ".join(text.split())
    for mark in "。！？!?":
        index = text.find(mark)
        if 0 <= index < limit:
            return text[: index + 1]
    return text if len(text) <= limit else text[: limit - 1] + "…"


def render(body: dict[str, Any]) -> MuseAnswer:
    brief = str(body.get("brief") or body.get("answer") or "").strip()
    points = [str(point).strip() for point in body.get("points") or [] if str(point).strip()]
    if not brief:
        raise MuseFailure("muse_invalid_response", "Muse returned no answer")
    general = bool(body.get("general"))
    spoken = "。".join([brief.rstrip("。"), *points])
    return MuseAnswer(
        display=display_line(brief),
        spoken=("这不是来自你的资料。" if general else "") + spoken,
        cited=bool(body.get("cited")),
        general=general,
    )


class MuseClient:
    def __init__(self, config: MuseConfig, opener: Any = None, sleep: Any = asyncio.sleep):
        self._config = config
        # Never follow ambient proxies with the device token.
        self._opener = opener or urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self._sleep = sleep

    def _request(self, path: str, body: bytes, content_type: str, extra: dict[str, str] | None = None):
        request = urllib.request.Request(
            f"{self._config.server}/device{path}",
            data=body,
            method="POST",
            headers={
                "Authorization": f"Bearer {self._config.token}",
                "Content-Type": content_type,
                "Accept": "application/json",
                "X-OpenMuse-Firmware": "stopwatch-walkie-bridge",
                **(extra or {}),
            },
        )
        try:
            with self._opener.open(request, timeout=ANSWER_LIMIT_SECONDS) as response:
                return response.status, json.loads(response.read() or b"{}"), response.headers
        except urllib.error.HTTPError as error:
            try:
                payload = json.loads(error.read() or b"{}")
            except json.JSONDecodeError:
                payload = {}
            return error.code, payload, error.headers
        except (urllib.error.URLError, TimeoutError, OSError) as error:
            raise MuseFailure("muse_unreachable", "personal cloud is unreachable") from error

    async def transcribe_wav(self, wav_bytes: bytes) -> str:
        status, body, _ = await asyncio.to_thread(self._request, "/v1/transcribe", wav_bytes, "audio/wav")
        if status != 200:
            raise MuseFailure(_code("transcribe", status), str(body.get("error") or "transcription failed"))
        return str(body.get("text") or "")

    async def ask(self, utterance_key: str, question: str) -> MuseAnswer:
        # Same utterance → same request ID, so a retried upload replays instead of paying twice.
        request_id = str(uuid.uuid5(REQUEST_NAMESPACE, utterance_key))
        payload = json.dumps(
            {"requestId": request_id, "question": question, "scope": "personal", "briefChars": self._config.brief_chars},
            ensure_ascii=False,
        ).encode("utf-8")
        deadline = time.monotonic() + ANSWER_LIMIT_SECONDS
        retried = False
        while True:
            try:
                status, body, headers = await asyncio.to_thread(
                    self._request, "/v1/chat", payload, "application/json", {"X-OpenMuse-Protocol": "2"}
                )
            except MuseFailure:
                if retried or time.monotonic() > deadline:
                    raise
                retried = True
                continue
            if status == 202 and time.monotonic() < deadline:
                await self._sleep(min(5.0, float(headers.get("Retry-After") or 3)))
                continue
            if status != 200:
                raise MuseFailure(_code("chat", status), str(body.get("error") or "Muse request failed"))
            return render(body)


def _code(stage: str, status: int) -> str:
    if status in (401, 403):
        return "muse_device_revoked"
    if status == 429:
        return f"{stage}_limit"
    if status == 503:
        return f"{stage}_unavailable"
    if status in (413, 422):
        return f"{stage}_rejected"
    return f"{stage}_failed"
