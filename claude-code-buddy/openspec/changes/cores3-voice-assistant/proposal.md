# Proposal: cores3-voice-assistant

## Why

StackChan (CoreS3) 目前只是 Claude Code 会话状态的"观众"。阿里云百炼 2026-07-15 发布的
qwen-audio-3.0-realtime-flash 提供语音进语音出的实时对话 API（毫秒级响应、预置音色、
Manual/PTT 模式），让这台已有喇叭、麦克风、表情和舵机的设备可以低成本变成一只真正能
对话的中文语音桌宠。

## What Changes

- 新增独立固件 env `cores3-stackchan-voice`（PlatformIO），与现有 buddy 固件互不影响，
  刷哪个固件设备就是哪个产品。
- 复用 `src/stackchan/` 的表情（character_chan）、舵机（motion）、喇叭播放（sound/audio_play）
  模块；新增麦克风采集（ES7210, 16 kHz PCM）与 DashScope Realtime WebSocket 客户端。
- 交互为 PTT：按住触摸屏说话，松开后模型回复（24 kHz PCM 播放），播放期间闭麦；
  不做 AEC / 全双工（留二期）。
- 懒连接：首次 PTT 时建立 WSS，空闲超时（默认 5 分钟，可配置）断开；连接期间保留多轮上下文。
- 萌系中文桌宠人设（system instructions 硬限回答 ≤3 句），说话状态驱动嘴型与小动作。
- 一期纯对话，不接 Function Call / 工具。
- 先交付 Mac Python 协议原型（`tools/` 下，dashscope SDK）验证协议与音色选择，再移植固件；
  原型仅为开发工具，非运行时依赖。
- DashScope API key 与 WiFi 凭据同机制，走 `wifi_secrets.ini` 构建注入。

## Capabilities

### New Capabilities

- `cores3-voice-assistant`: CoreS3 StackChan 语音助手固件的端到端行为——PTT 采集、
  Realtime WebSocket 会话生命周期（懒连接/超时断开/多轮上下文）、回复播放、
  表情动作联动、异常与断网表现。

### Modified Capabilities

（无——不改动任何现有 buddy 能力的需求。）

## Impact

- 新增：`platformio.ini` 的 `[env:cores3-stackchan-voice]`；`src/stackchan/` 下新增
  语音助手入口与音频上行/WSS 模块（buddy 入口不变）；`tools/` 下新增 Python 原型脚本。
- 依赖：ESP32 WebSocket/TLS 客户端库（选型见 design.md）；Mac 侧 `dashscope>=1.23.9`（仅原型）。
- 外部：需要百炼 DashScope API key（北京地域）；费用按音频时长计（flash ≈ 12.5 token/秒，
  多轮上下文逐轮累积计费）。
- 不触碰：BLE 桥、cc-bridge 守护进程、其他 buddy 固件与规格。
