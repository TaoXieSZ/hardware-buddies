# StopWatch Agent 对讲机 · 设计文档

> 2026-08-02  grilling 会话定稿。设备已购（M5 StopWatch, SKU:C152），代码未动。
> 换电脑续作时从「MVP 切片」开始。

## 一句话

把 M5 StopWatch 变成 AI agent 对讲机：**按住对它说话，它调度这台 Mac 上的 agent
干活**——派新任务、指挥已有会话、结果语音播报，危险操作在表上震动审批。

## 硬件依据（StopWatch 提供的能力）

| 硬件 | 用途 |
|---|---|
| MEMS 麦 + ES8311 + 1W 喇叭 | 对讲机音频链路 |
| KEYA（黄）/ KEYB（蓝） | PTT 按住说话 / 打断·取消·拒绝 |
| 震动电机 | 审批请求、长任务完成通知（buddy 家族首个触觉设备） |
| 1.75" AMOLED 466×466 圆屏触控 | 派发前确认目标、结果详情、状态列表 |
| BMI270 IMU / RX8130CE RTC | 抬腕亮屏 / 离线时钟（后期） |
| 450mAh + M5PM1 PMIC | 桌面插电为主，佩戴半天 |

参考：<https://docs.m5stack.com/zh_CN/core/StopWatch>（含官方 PlatformIO env：
`espressif32@6.12.0` + `qio_opi`，官方平台，不用 pioarduino）。

## 架构

```
┌─ StopWatch (ESP32-S3) ──────────────────────────────┐
│  按住 KEYA 说话（PTT）· KEYB 打断/取消                 │
│  I2S 采音 16kHz PCM ──WebSocket──┐                   │
│  震动: 审批请求 / 长任务完成       │  LAN (mDNS:       │
│  圆屏: 派发前确认目标 + 结果详情   │  walkie.local,    │
│  喇叭: TTS 播报                 │  静态IP兜底)        │
└──────────────────────────────────│────────────────────┘
                                   ▼
┌─ Mac: walkie-bridge daemon（新子项目 tools/ 下）───────┐
│  ASR:  DashScope qwen3-asr-flash（断网降级 whisper.cpp）│
│  大脑: kimi -p 无头会话                                 │
│        输入 = 转写文本 + 项目注册表 + cc-bridge 会话列表  │
│        输出 = 结构化调度指令                             │
│  调度: ├─ 新任务 → spawn kimi -p / claude -p @项目目录   │
│        └─ 老会话 → cc-bridge steer → cmux 注入终端       │
│  TTS:  edge-tts（断网降级 say）→ 回表播放                │
│  审批: agent 要权限 → 推表震动 → KEYA批/KEYB拒          │
│        （复用 buddy 审批链；"放手去干"口令才 yolo）       │
└────────────────────────────────────────────────────────┘
```

## 关键决策（grilling 结论，每条都是拍过板的）

1. **大脑在 Mac**，StopWatch 是哑音频终端。产品的灵魂是"调度**我电脑的** agent"，
   云端大脑（Agora/小智）碰本地会话都别扭。固件也省事——不在 ESP32 上跑唤醒词/VAD。
2. **PTT 交互**：按住 KEYA 说话、松开发送、震动确认。永不误触发，且天然给出
   语音边界（省 VAD）。KEYB = 打断/取消。
3. **调度新老通吃**：新任务 spawn 无头 agent；老会话 steer（"让 stackchan 那个
   会话把波特率改了"）。
4. **结果回传 = 语音摘要 + 圆屏详情**；>30s 长任务自动转异步：挂起、完成时震动、
   等用户主动问。
5. **音频传输 = 裸 PCM over WebSocket**（16kHz/16bit 单声道）。LAN 内 256kbps
   无压力，Opus/WebRTC 的复杂度用不上。要出公网那天再换。
6. **ASR = DashScope Qwen ASR（qwen3-asr-flash）**，断网自动降级本地 whisper.cpp。
   注意：这意味着语音内容出云上阿里，已接受。
7. **寻址 = 注册表 + LLM 匹配**：daemon 维护项目注册表（名字/路径/别名）+
   cc-bridge 实时会话列表，大脑 LLM 做目标匹配。**派发前圆屏显示理解结果，
   KEYA 确认才执行，KEYB 重说**——语音链路必须有一次眼见为实。
8. **steer 通道 = cc-bridge 加 steer 命令**，走 cmux rpc 往终端窗口注入文本+回车。
   对所有终端 TUI agent（kimi/claude）通吃。kimi 出官方 steer API 后再议。
9. **权限 = 继承 buddy 审批链**：spawn 的 agent 走正常权限模式，审批请求推给表，
   震动 + 屏幕 + KEYA批/KEYB拒。显式口令"放手去干"才 yolo。
10. **大脑 = `kimi -p` 无头会话**：复用 Kimi Code 登录态，零新密钥；大脑自带工具
    能力（读注册表、查会话列表）。备选 `claude -p`，配置项。
11. **服务发现 = mDNS（`walkie.local`）**，找不到时读配置里的静态 IP 兜底。

## 项目形态（按 monorepo 惯例）

- **新子项目**（第七个），如 `stopwatch-walkie/`：PlatformIO 官方 `espressif32@6.12.0`
  + M5Unified（I2S 麦/喇叭 API 现成）+ M5PM1/M5IOE1 库。**不要** pioarduino
  （monorepo 血泪教训：S3 上平台混用会变砖，见根 AGENTS.md）。
- Mac daemon 放该子项目 `tools/walkie-bridge/`，模式照抄 cc-bridge（launchd 托管、
  venv、install.sh），复用 buddy_core 的会话/状态模式。
- steer 命令要动 cc-bridge —— 那是 OpenSpec 管辖范围，到时走
  `/opsx:propose`（claude-code-buddy 内）。

## MVP 切片

| 切片 | 内容 | 验证点 |
|---|---|---|
| **0** | 按住说话 → PCM 流到 Mac → ASR 转文字 → 圆屏回显 | 音频链路（全部技术风险在这：I2S + WebSocket 在 S3 上的稳定性）。不通就回头改决策 5 |
| **1** | 派新任务闭环：语音→大脑→spawn agent→TTS 念结果 | 核心产品价值 |
| **2** | 老会话 steer（cmux 注入） | 依赖 cc-bridge steer（OpenSpec change） |
| **3** | 审批震动闭环 | buddy 审批链接入新 spawn 的会话 |

切片0 是第零步验证，应该最先做——它发现音频链路不稳的代价最小。

## 凭证

- DashScope API key 一枚（ASR）。
- 大脑白嫖 Kimi Code 登录态，无需新密钥。

## 未定的实现期细节（不影响架构）

daemon 起名、配置文件格式与位置（建议 `~/.config/walkie-bridge/`）、项目注册表
schema、圆屏 UI 细节、TTS 音色。
