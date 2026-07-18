"""Shared bits for the voice-prototype scripts (key loading, URL candidates)."""

import json
import os
import sys
from pathlib import Path

MODEL = "qwen-audio-3.0-realtime-flash"
# 管道自检备胎：若主模型连不通，用已发布较久的 omni realtime 模型排除
# “我们的代码错了”还是“模型 ID/权限问题”。
FALLBACK_MODEL = "qwen3-omni-flash-realtime"

# 文档同时出现过两种 URL 形态（2026-07，模型刚发布，文档在漂移）：
#   经典 DashScope 形态（Realtime API 老文档、OpenAI 兼容）
#   新 maas 形态（需要百炼业务空间 ID，qwen3.5-omni 文档用的这种）
CLASSIC_URL = "wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model={model}"
MAAS_URL = "wss://{ws_id}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model={model}"


def read_api_key() -> str:
    key = os.environ.get("DASHSCOPE_API_KEY", "").strip()
    if not key:
        p = Path.home() / ".dashscope-key"
        if p.exists():
            key = p.read_text().strip()
    if not key.startswith("sk-"):
        sys.exit("未找到 API key：请 export DASHSCOPE_API_KEY=sk-xxx 或写入 ~/.dashscope-key")
    return key


def candidate_urls(model: str) -> list[str]:
    urls = [CLASSIC_URL.format(model=model)]
    ws_id = os.environ.get("DASHSCOPE_WORKSPACE_ID", "").strip()
    if ws_id:
        urls.append(MAAS_URL.format(ws_id=ws_id, model=model))
    return urls


def auth_headers(key: str) -> list[str]:
    return [f"Authorization: Bearer {key}"]


def jdump(obj) -> str:
    return json.dumps(obj, ensure_ascii=False)
