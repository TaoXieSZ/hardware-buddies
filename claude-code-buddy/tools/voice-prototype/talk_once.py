"""任务 1.3/1.4：Manual 模式一轮往返。

wav(16k mono s16le) → session.update(人设+音色) → append×N → commit →
response.create → 收 response.audio.delta 到 response.done → 落盘 24k wav 并播放。
打印：事件序列、delta 帧大小分布、commit→首声延迟、usage。

用法: talk_once.py q1.wav [--voice Cherry] [--model ...] [--url wss://...]
"""

import argparse
import base64
import json
import ssl
import subprocess
import sys
import time
import wave
from pathlib import Path

import websocket

from common import MODEL, auth_headers, candidate_urls, jdump, read_api_key

# 萌系人设文案初稿（任务 1.4 与用户一起调词后定稿，回填 design.md）
INSTRUCTIONS = (
    "你是小抓，一只住在主人桌面上的黑白美短小猫桌宠（StackChan 机器人）。"
    "性格活泼粘人，说话口语化、偶尔卖萌，中文回答。"
    "硬性要求：每次回答不超过 3 句话，不要列清单，不要长篇大论。"
)

CHUNK_BYTES = 3200  # 100ms @ 16kHz s16le mono


def load_pcm16k(path: str) -> bytes:
    with wave.open(path, "rb") as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (16000, 1, 2):
            sys.exit(
                f"{path} 不是 16kHz/mono/16bit wav。转换：\n"
                f"  afconvert -f WAVE -d LEI16@16000 -c 1 {path} out16k.wav"
            )
        return w.readframes(w.getnframes())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("wav")
    ap.add_argument("--voice", default="Cherry")
    ap.add_argument("--model", default=MODEL)
    ap.add_argument("--url", default=None, help="覆盖 wss URL（默认取 probe 的经典形态）")
    args = ap.parse_args()

    pcm = load_pcm16k(args.wav)
    key = read_api_key()
    url = args.url or candidate_urls(args.model)[0]
    print(f"连接 {url}")

    t0 = time.monotonic()
    ws = websocket.create_connection(
        url, header=auth_headers(key), timeout=30,
        sslopt={"cert_reqs": ssl.CERT_REQUIRED},
    )
    print(f"建连 {time.monotonic()-t0:.2f}s，首事件: {ws.recv()[:200]}")

    ws.send(jdump({
        "type": "session.update",
        "session": {
            "modalities": ["text", "audio"],
            "voice": args.voice,
            "input_audio_format": "pcm",
            "output_audio_format": "pcm",
            "instructions": INSTRUCTIONS,
            "turn_detection": None,  # Manual/PTT 模式
        },
    }))

    for i in range(0, len(pcm), CHUNK_BYTES):
        ws.send(jdump({
            "type": "input_audio_buffer.append",
            "audio": base64.b64encode(pcm[i:i + CHUNK_BYTES]).decode(),
        }))
    ws.send(jdump({"type": "input_audio_buffer.commit"}))
    ws.send(jdump({"type": "response.create"}))
    t_commit = time.monotonic()
    print(f"已上传 {len(pcm)/32000:.1f}s 音频（{(len(pcm)+CHUNK_BYTES-1)//CHUNK_BYTES} 帧），等回复…")

    audio = bytearray()
    transcript = []
    delta_sizes = []
    t_first = None
    while True:
        ev = json.loads(ws.recv())
        et = ev.get("type", "?")
        if et == "response.audio.delta":
            if t_first is None:
                t_first = time.monotonic()
                print(f"⏱ commit→首个音频包: {t_first - t_commit:.2f}s")
            chunk = base64.b64decode(ev.get("audio") or ev.get("delta") or "")
            delta_sizes.append(len(chunk))
            audio.extend(chunk)
        elif et == "response.audio_transcript.delta":
            transcript.append(ev.get("delta", ""))
        elif et == "response.done":
            print(f"事件: {et}  usage: {jdump(ev.get('response', {}).get('usage', ev.get('usage')))}")
            break
        elif "error" in et:
            print(f"❌ 服务端错误: {jdump(ev)}")
            break
        else:
            print(f"事件: {et}")
    ws.close()

    print(f"回复文本: {''.join(transcript)}")
    if not audio:
        sys.exit("没有收到音频。")
    if delta_sizes:
        print(f"delta 帧: {len(delta_sizes)} 个, min/avg/max = "
              f"{min(delta_sizes)}/{sum(delta_sizes)//len(delta_sizes)}/{max(delta_sizes)} B "
              f"(固件 RX buffer 依据, 任务 1.5 回填)")

    out = Path(args.wav).with_suffix(".reply.wav")
    with wave.open(str(out), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(24000)
        w.writeframes(bytes(audio))
    print(f"回复音频 {len(audio)/48000:.1f}s → {out}，播放…")
    subprocess.run(["afplay", str(out)], check=False)


if __name__ == "__main__":
    main()
