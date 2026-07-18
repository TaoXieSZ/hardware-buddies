# Design: cores3-voice-assistant

## Context

CoreS3 StackChan 已有：表情渲染（`character_chan`）、舵机（`motion`）、16 kHz PCM 流式播放
（`audio_play.cpp`：ring buffer → `M5.Speaker.playRaw`，非阻塞双缓冲）、触摸屏、WiFi(2.4G)。
缺：麦克风采集、TLS WebSocket、DashScope Realtime 协议。

DashScope Realtime API（qwen-audio-3.0-realtime-flash，2026-07-15 发布）：
- WSS + Bearer API key；OpenAI Realtime 风格 JSON 事件。
- **Manual 模式**（`turn_detection: null`）：`input_audio_buffer.append`（base64 PCM 16k）
  → `input_audio_buffer.commit` → `response.create`；回复走 `response.audio.delta`
  （base64 PCM 24k）→ `response.done`。与 PTT 一一对应。
- `session.update` 携带 `instructions`（人设）与 `voice`（音色）。
- 计费：flash 音频 ≈ 12.5 token/秒，多轮上下文逐轮累积。

用户决策（已对齐）：独立固件 / 设备直连 / PTT / 纯对话 / 萌系中文人设答复 ≤3 句 /
懒连接 + 空闲 5 分钟断开（可调）。

## Goals / Non-Goals

**Goals:**
- 新 env `cores3-stackchan-voice`：按住屏幕说话 → StackChan 用萌系中文回答并动嘴。
- 端到端延迟（松手 → 首声）目标 < 2.5 s（连接已建立时）。
- 不影响任何现有 buddy 固件的编译与行为。

**Non-Goals:**
- AEC / 全双工打断、唤醒词、Function Call、与 buddy 状态融合（二期）。
- WebRTC 接入（仅 WebSocket）。
- Mac 侧运行时组件（Python 原型只是开发工具）。

## Decisions

1. **入口分离，模块复用。** `build_src_filter = -<*> +<stackchan/> -<stackchan/main.cpp>
   +<stackchan_voice/>`；新目录 `src/stackchan_voice/` 只放语音助手入口与新模块，
   复用 `stackchan/` 的 character/motion/settings/audio 基建。避免在 buddy `main.cpp`
   里堆 `#ifdef`。
2. **WebSocket 库：`links2004/arduinoWebSockets`。** 成熟、WSS（WiFiClientSecure）、
   事件回调式，PlatformIO registry 可锁版本。备选 esp_websocket_client 在 arduino
   框架下要拖 IDF component，复杂度不值。**必须先抄该库 `examples/` 的 WSS 初始化
   verbatim**（仓库 ground-truth 规矩）。
3. **TLS 校验：内置阿里云根 CA**（编译期 PROGMEM）；`-DVOICE_TLS_INSECURE` 仅调试用。
4. **Mic/Speaker 半双工调度。** M5Unified 在 CoreS3 上 `M5.Mic` 与 `M5.Speaker` 不能
   并发（共享 I2S 资源）：LISTENING 态 `Speaker.end()→Mic.begin()`，进入播放前反向切换。
   PTT 设计天然规避并发需求。采集参数（16 kHz mono）以 M5Unified CoreS3 mic 示例为准。
5. **上行分帧 ~100 ms**（3200 B PCM → base64 ≈ 4.3 KB/帧）边录边发 `append`，
   松手即 `commit`+`response.create`——录音期间就在传输，缩短松手后等待。
6. **下行解析避开大 JSON。** `response.audio.delta` 的 base64 可能很大；WebSocket 文本帧
   先做轻量类型嗅探（`"type":"response.audio.delta"` 字符串定位），audio 字段流式
   base64 解码直入 PSRAM ring buffer（复用 `audio_ringbuf.h`），24 kHz `playRaw` 播放；
   其余小事件才走 ArduinoJson。
7. **会话状态机**：`SLEEP →(touch) CONNECTING → READY →(hold) LISTENING →(release)
   THINKING → SPEAKING → READY →(idle 5 min) SLEEP(断开)`。SPEAKING 驱动嘴型
   （复用 `audioPlayIsActive()` 模式），THINKING 用现有 busy 表情。
8. **上下文与费用护栏**：空闲断开即弃上下文；同一连接内超过 N 轮（默认 20）主动重建
   session，防累积计费失控。超时/轮数入 `settings.cpp` 可调项。
9. **凭据**：`wifi_secrets.ini` 新增 `dashscope_key`（tracked 占位符机制不变），
   经 `-DSTACKCHAN_DASHSCOPE_KEY` 注入。
10. **先原型后固件**：`tools/voice-prototype/`（Python，`dashscope>=1.23.9` 或裸
    websocket-client）先验证：正确的 wss URL 形态（`dashscope.aliyuncs.com/api-ws/v1/realtime?model=…`
    vs 新 `{WorkspaceId}.maas` 域名）、模型 ID 字符串、Manual 模式事件序列、音色试听
    （Cherry/Ethan…）。固件里的协议常量以原型实测为准。

## Risks / Trade-offs

- [协议新、文档在漂移（发布仅 3 天）] → 原型先行；固件协议常量集中一个头文件，改动面小。
- [TLS+WSS 握手 1-2 s，首句慢] → 懒连接是既定决策；触摸唤醒（SLEEP→CONNECTING）提前
  于 PTT 按住，掩盖部分握手时间。
- [Mic/Speaker I2S 切换爆音或失败] → 切换点集中在状态机一处；若实测切换不稳，
  退路是 `Mic.end()/Speaker.begin()` 间加短暂静默帧。
- [大 base64 帧撑爆内存] → 所有音频缓冲入 PSRAM；WebSocket 库 buffer 上限设定 +
  服务器帧过大时的分片处理在原型阶段实测。
- [费用失控] → 回答 ≤3 句（instructions）、空闲断开、轮数上限三重护栏；串口日志打印
  每轮 usage 便于观察。
- [2.4 GHz WiFi 环境受限] → 与现有 cores3 WiFi 路径同一约束，无新增风险。

## 实测定案（2026-07-18，tools/voice-prototype/ 真连实测）

- **URL**：`wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=qwen-audio-3.0-realtime-flash`
  经典 DashScope 形态即可，**无需 WorkspaceId**；鉴权 `Authorization: Bearer sk-…`。
- **必须显式 `turn_detection: null`**——session.created 显示服务端默认开 `server_vad`。
- **音色**：Cherry/Ethan 等 omni 音色不适用。本模型音色表（服务端 error 返回的权威清单）：
  longanqian(默认)、longanlingxin、longanlufeng、longanlingxi、longanxiaoxin、
  longanfengyue、longanyuanfei、longanhuan_v3.6、longjielidou_v3.6、longpaopao_v3.6、
  longhuohuo_v3.6、longchuanshu_v3.6、loongmary、loongeva_v3.6、loongjohn。
  最终选择：**longpaopao_v3.6**（用户 2026-07-18 试听拍板）。
- **实测时延**：TLS+WSS 建连 ~0.3s（设计预估 1.6s 大幅富余）；commit→首个音频包
  0.96–1.06s（目标 <2.5s 轻松达标）。
- **`response.audio.delta` 帧大小**：解码后 min/avg/max = 7680/18788/**19200 B**，
  base64 文本帧 ~25 KB → 固件 WebSocket RX buffer 需 **≥32 KB**（入 PSRAM）。
- **事件序列确认**：committed → conversation.item.created →
  input_audio_transcription.delta*(fun-asr 自动附带) → response.created →
  audio.delta* / audio_transcript.done → response.done（含 usage）。
- **usage 实例**：1.7s 提问 + 11s 回复 ≈ 292 tokens（input 110 = text89+audio21，
  output 182 = text45+audio137）——音频 token 与 12.5/s 公式吻合。
- **人设验证**：instructions 生效良好，稳定 ≤3 句萌系中文。
