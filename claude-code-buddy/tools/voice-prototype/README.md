# voice-prototype — DashScope Realtime 协议原型（开发工具，非运行时）

CoreS3 语音助手（openspec change `cores3-voice-assistant`）的 Mac 侧协议验证脚本。
目的：在写固件前，用最接近固件的方式（裸 WebSocket，不用官方 SDK 封装）实测
qwen-audio-3.0-realtime-flash 的 URL 形态、事件序列、帧大小与音色，把协议常量定下来。

## 准备

```bash
cd tools/voice-prototype
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt

# API key（二选一；不要提交进 git）
export DASHSCOPE_API_KEY=sk-xxx
# 或写入 ~/.dashscope-key（chmod 600），脚本会自动读取
```

## 用法

```bash
# 1) 探测哪个 wss URL / 模型 ID 能连通（任务 1.2）
.venv/bin/python probe.py

# 2) 生成一个 16kHz 测试提问 wav（用 macOS 自带 say，不用麦克风）
./make_test_wav.sh "你好呀，你是谁？" q1.wav

# 3) Manual 模式一轮往返：上传 wav → 播放回复 → 打印事件/帧大小/延迟/usage（任务 1.3）
.venv/bin/python talk_once.py q1.wav

# 4) 音色试听（任务 1.4）：换音色反复听
.venv/bin/python talk_once.py q1.wav --voice Cherry
.venv/bin/python talk_once.py q1.wav --voice Ethan
```

跑通后把实测结论（URL、模型 ID、典型 delta 帧大小、usage 字段）回填到
`openspec/changes/cores3-voice-assistant/design.md`（任务 1.5）。
