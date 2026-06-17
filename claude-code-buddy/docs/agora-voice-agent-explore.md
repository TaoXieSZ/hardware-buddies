# Agora ConvoAI × StackChan — 语音 agent 探索

> 草稿。目的：评估用 Agora Conversational AI Engine 把 StackChan 变成语音 agent。

## Agora ConvoAI 是什么

云端语音 agent 管线。你的后端 `POST /join` 启动一个 agent，agent 作为参与者
加入一个 RTC 频道；一个客户端用麦克风/扬声器加入同一频道，就能对话。

```
客户端(麦克风+扬声器)  ──RTC音频──►  Agora ConvoAI Agent (云端)
        ▲                              ASR → LLM → TTS
        └──────RTC音频(TTS回放)────────┘
        ◄──────RTM(转写/状态/指标)─────  同名频道
```

- 三个 token：客户端 RTC token、客户端 RTM token、后端调 REST 的 ConvoAI token。
- 状态机：IDLE→STARTING→RUNNING→STOPPING→STOPPED（含 RECOVERING/FAILED）。
- 转写和 agent 状态通过 RTM 频道下发（需 `enable_rtm:true` + `data_channel:"rtm"`）。
- LLM/TTS/ASR 供应商在 `/join` 的 `properties` 里配置，可换 OpenAI/Gemini 等。

## 关键问题：RTC 客户端跑在哪？

这是唯一决定难度的分叉。StackChan 是 CoreS3（ESP32-S3，内置 PDM 麦克风 + 1W 扬声器、
2.0" LCD、两个舵机、12 RGB LED），已有 BLE NUS 协议 + GIF 表情 + WAV 播放 + 跳舞动作 +
Mac 守护进程（buddy_core）。

### Path A — Mac 当客户端，StackChan 当“表情/语音外设”（推荐，最快）

- Mac 跑 Agora RTC 客户端（官方 web quickstart 或 Python），Mac 麦克风收音、
  Mac 扬声器放 agent 的 TTS。
- 云端 ConvoAI 负责对话。
- **复用现有守护进程**：daemon 订阅 RTM 的转写+agent 状态 → 走 BLE 推给 StackChan →
  StackChan 在 agent 说话时动嘴、LCD 显示转写、状态切换时跳舞、LED 反馈。
- 复用 100% 现有 BLE 协议和动画基建，固件**不需要**跑 RTC。
- 数小时即可出可用 demo。代价：必须开着 Mac，StackChan 不独立。

### Path B — StackChan(CoreS3) 直接当 RTC 客户端（真·独立智能音箱，难）

- 在 ESP32-S3 上跑 Agora 嵌入式 RTC SDK（RTSA / IoT SDK）。设备麦克风 → 频道 →
  云端 agent → 设备扬声器。
- CoreS3 有 PDM 麦克风 + 扬声器 + PSRAM，硬件够。但 Agora 的 ESP32 SDK 在
  PlatformIO/ESP-IDF 上集成是大工程，需要申请 SDK / 评估授权。
- 回报大：脱离 Mac 的独立对话机器人。
- 工期：周级，不是小时级。

### Path C — 手机/网页当客户端，StackChan 只做镜像

- 用 Agora 官方 web/移动 quickstart 当客户端；StackChan 经 daemon 镜像状态。
- 本质同 Path A，只是客户端换成手机/浏览器。

## 建议路线

1. 先走 **Path A** 拿到可跑的 baseline（本周）。
2. baseline 跑通后再评估 **Path B** 是否值得做独立版。

## 任意路径的前置条件

- Agora 账号：App ID + App Certificate（免费额度 10k 分钟/月）。
- ConvoAI 的 LLM/TTS/ASR 供应商配置（可用内置组合起步）。
- skill 的硬性要求：**先 clone 官方 ConvoAI quickstart 跑通**，再改 prompt/persona，
  不允许凭记忆造 `/join` 负载或脚手架。

## 已就绪

- skill 安装在 `~/.claude/skills/agora/`（含 RTC/RTM/ConvoAI/CLI/token 全套参考）。
- docs MCP `agora-docs`（https://mcp.agora.io）已注册并连通，可查实时官方文档。
- 当前 stackchan 固件已备份到 `src/stackchan.backup-20260524-150203/`。
