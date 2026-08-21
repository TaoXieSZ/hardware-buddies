# StopWatch Agent 对讲机 · 设计文档

> 2026-08-21 实现状态：切片 1 真机 E2E 已过（语音 → ASR → 腕上确认 → cmux 注入
> Codex 会话并收到回话）。**编排层已升级为 DSH 插件**（`dsh-walkie` + bridge 回环
> brain API）：语音路由由 DSH 值班会话用 LLM 选目标（大脑优先），确定性
> `MultiAgentRouter` 兑底；DSH 任意会话都可用工具推提案/仲裁审批/让表播报。
> 详见「编排层 = DSH 插件」一节与 `docs/plans/2026-08-20-walkie-dsh-plugin-design.md`。
> Kimi 暂不可 steer（cmux 0.64.20 不给 kimi 面板写 `terminal.agent` 元数据，
> 控制面快照看不到，需要 cc-bridge 控制面按 hook 事件反配面板，后续独立 change）。
> 本文中的 spawn、WSS、离线 ASR、mDNS 与 launchd 均为 NEXT，不是当前能力。

## 一句话

把 M5 StopWatch 变成 AI agent 对讲机：**按住对它说话，它调度这台 Mac 上的 agent
干活**——当前可在腕上确认后指挥已有会话、观察结果和处理受支持的权限；派新任务属于后续能力。

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
│  震动: 审批请求 / 长任务完成       │  trusted LAN       │
│  圆屏: 派发前确认目标 + 结果详情   │  静态 IP           │
│  喇叭: TTS 播报                 │                    │
└──────────────────────────────────│────────────────────┘
                                   ▼
┌─ Mac: walkie-bridge daemon（新子项目 tools/ 下）───────┐
│  ASR:  DashScope qwen3-asr-flash                       │
│  认证: HMAC-SHA-256 双向证明 + session/seq 防重放         │
│  路由: brain 优先（DSH 插件决策）→ MultiAgentRouter 兑底  │
│  调度: proposal → KEYA → stage/confirm → cmux exact surface│
│  TTS:  macOS say → 回表播放 bounded pane 摘要             │
│  审批: Claude/OpenCode 可 A批/B拒；Codex/Kimi 仅通知终端   │
│  brain API: 127.0.0.1:8767 回环 HTTP（Bearer token）     │
└───────────────▲──────────────────────────────────────┘
                │ 长轮询队列 / decision / 提案 / TTS
┌─ DSH 插件 dsh-walkie（walkie-dsh-plugin/）─────────────┐
│  操作面: walkie_status/propose/resolve/say/... 工具     │
│  路由脑: 专职 headless 值班会话（硬化 prompt → JSON）    │
└────────────────────────────────────────────────────────┘
```

## 关键决策（grilling 结论，每条都是拍过板的）

1. **大脑在 Mac**，StopWatch 是哑音频终端。产品的灵魂是"调度**我电脑的** agent"，
   云端大脑（Agora/小智）碰本地会话都别扭。固件也省事——不在 ESP32 上跑唤醒词/VAD。
2. **PTT 交互**：按住 KEYA 说话、松开发送、震动确认。永不误触发，且天然给出
   语音边界（省 VAD）。KEYB = 打断/取消。
3. **当前只 steer 现有会话**：新任务 spawn 暂不开放；找不到目标时 fail closed。
4. **结果回传 = 语音摘要 + 圆屏详情**；>30s 长任务自动转异步：挂起、完成时震动、
   等用户主动问。
5. **音频传输 = 裸 PCM over WebSocket**（16kHz/16bit 单声道）。LAN 内 256kbps
   无压力，Opus/WebRTC 的复杂度用不上。要出公网那天再换。
6. **ASR = DashScope Qwen ASR（qwen3-asr-flash）**。本地 whisper.cpp 仍是 NEXT。
   注意：这意味着语音内容出云上阿里，已接受。
7. **寻址 = 大脑优先 + 唯一会话匹配**：转写先交给 DSH 插件的大脑（值班会话用 LLM
   选目标、改写谐音），大脑超时/不可用/拒绝时由确定性 `MultiAgentRouter`（别名 +
   cc-bridge 规范化快照）兑底。无论哪条路径，目标必须过"唯一匹配 + steer 能力"
   校验。**派发前圆屏显示精确提案，KEYA 确认才执行，KEYB 拒绝**。例外：命令命中
   配置的免确认白名单正则（默认只含 git status/log/diff、查看/看看/查一下、tail、
   cat 这类查询）时直接注入，dashboard 事件标记 `direct=true`。别名/标签后接受
   全角标点边界（ASR 转写中文用「，。」不用空格，2026-08-18 修）；同音误识别用
   谐音别名兜底（「测试会话」≈「测试绘画」）。
8. **steer 通道 = cc-bridge 加 steer 命令**，走 cmux rpc 往终端窗口注入文本+回车。
   对所有终端 TUI agent（kimi/claude）通吃。kimi 出官方 steer API 后再议。
9. **权限 = 能力驱动**：Claude Code / OpenCode 复用 pending future，first-response-wins；
   Codex / Kimi Code 在未验证双向适配器前只提示去终端处理。禁止 yolo/bypass。
10. **大脑 = DSH 插件,不是 bridge 内嵌 LLM**：原 M2 计划的 `kimi -p` 无头编排器
    改为 `dsh-walkie` 插件——DSH 专职 headless 值班会话做路由大脑,任意 DSH 会话
    都是第二块操作面(推提案/仲裁审批/TTS)。bridge 保持确定性可独立工作,大脑
    只是升级层。见下节。
11. **连接 = 配置静态 IP**。mDNS（`walkie.local`）仍是 NEXT。

## 编排层 = DSH 插件（brain API）

2026-08-21 起,编排大脑从 bridge 内迁到 DSH。bridge 新增**回环 brain API**
(`brain_api.py`,127.0.0.1:8767,Bearer token,未配 token 则行为与旧版完全一致):

| 端点 | 作用 |
|---|---|
| `GET /api/v1/status` / `/api/v1/events` | 快照 / 事件流(dashboard 同款投影) |
| `GET /api/v1/brain/queue?wait_ms=` | 长轮询工作项(`transcript` / `permission`) |
| `POST /api/v1/brain/decision` | 大脑回执:route / reject / permission 决议 |
| `POST /api/v1/proposals` | DSH 主动推提案上圆屏(同样受白名单/手表闸) |
| `POST /api/v1/tts` | 让手表 TTS 播报(≤400 字符) |

插件 `stopwatch-walkie/tools/walkie-dsh-plugin/`:
- **操作面工具**(全局注册):`walkie_status` / `walkie_events` / `walkie_propose` /
  `walkie_resolve` / `walkie_say` / `walkie_wait`。
- **值班 headless 会话**:懒创建,标题 `walkie-duty`;转写到达 → 硬化 prompt(转写
  标注为不受信任数据、尖括号中和)→ 结构化 JSON → 本地校验 → POST decision。
  串行处理、失败退避、会话崩溃自动重建。

安全不变式:**bridge 是唯一权威**。大脑不能直接 stage/confirm;decision 目标必须
过唯一匹配校验;免确认仅限白名单正则(默认查询类);权限仲裁手表与大脑
first-response-wins;brain API 只绑 127.0.0.1 + token,8767 不对公网。

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
| **1** | 已有会话 steer：认证→显式目标→腕上提案→cmux 注入 | 真机 E2E 已过（Codex 目标，2026-08-18）；Claude/OpenCode 目标待 smoke，Kimi 待控制面适配 |
| **2** | 能力驱动的权限与任务反馈 | Claude/OpenCode 可操作；Codex/Kimi 通知型 |
| **3 (NEXT)** | 新会话 spawn、WSS、离线 ASR、mDNS | 后续独立 OpenSpec change(LLM 自动选目标已由 dsh-walkie 大脑覆盖) |

切片0 是第零步验证，应该最先做——它发现音频链路不稳的代价最小。

## 凭证

- DashScope API key 一枚（ASR）。
- 每台表一枚随机 32-byte control secret，仅存 ignored 本地配置。
- brain API 一枚随机 32-byte Bearer token（`WALKIE_BRAIN_TOKEN`），仅存
  `tools/walkie-bridge/.env`（gitignored）；插件经 `~/.config/walkie-bridge/brain.json`
  的 `bridge_env` 指针读取，不复制 token。

## 未定的实现期细节（不影响架构）

daemon 起名、配置文件格式与位置（建议 `~/.config/walkie-bridge/`）、项目注册表
schema、圆屏 UI 细节、TTS 音色。
