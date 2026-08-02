# StackChan Agent — 创造追踪画布

_Last updated: 2026-07-07_

## 一、整条调用链

```mermaid
sequenceDiagram
    participant 手指 as 👆 手指
    participant 屏 as CoreS3 触屏
    participant 固件 as main.cpp<br/>(ESP32-S3)
    participant 串口 as USB Serial<br/>115200bps
    participant 中继 as relay.py<br/>(Mac 本机)
    participant 网关 as voice-gateway<br/>:60630
    participant 调度 as Dispatcher<br/>(dispatch)
    participant 宿主 as agent-host<br/>:60620
    participant LLM as Claude Opus<br/>(Cursor API)

    手指->>屏: 点击任意位置
    屏->>固件: M5.Touch.wasPressed()
    固件->>固件: 取当前预设 prompt
    固件->>串口: @ASK 现在几点了？\n
    串口->>中继: 读串口，过滤非 @ 行
    中继->>网关: POST /v1/voice/chat/completions<br/>Authorization: Bearer <token>
    网关->>网关: Token 验证
    网关->>调度: dispatch("stackchan-voice", prompt)
    调度->>宿主: createAgent / send
    宿主->>宿主: 注入 system prompt<br/>注入 living memory (PA 共享)
    宿主->>LLM: prompt（口语化、简短）
    LLM->>宿主: "下午三点零八分。"
    宿主->>调度: 回复
    调度->>网关: choices[0].message.content
    网关->>中继: HTTP 200
    中继->>串口: @REPLY 下午三点零八分。\n
    串口->>固件: readProtocolLine() 匹配 @REPLY
    固件->>屏: LCD 显示回复 + beep
```

## 二、Agent 身份图谱

```mermaid
graph TD
    subgraph 飞书入口
        FS[飞书消息] --> DISPATCH[dispatch<br/>Mac2 控制面]
    end

    subgraph 桌宠入口
        TOUCH[CoreS3 触屏] --> USB[USB 串口]
        USB --> RELAY[relay.py]
        RELAY --> GW[voice-gateway<br/>:60630]
        GW --> DISPATCH
    end

    DISPATCH --> PA[personal-assistant<br/>定义: 高信任<br/>全量 MCP 工具]
    DISPATCH --> SV[stackchan-voice<br/>定义: 低信任<br/>零高危 MCP]

    PA --> MEMORY[(Living Memory<br/>memory_key:<br/>personal-assistant)]
    SV --> MEMORY

    PA --> CURSOR1[Cursor Agent<br/>PA 实例]
    SV --> CURSOR2[Cursor Agent<br/>SV 实例]

    CURSOR1 --> CLAUDE[Claude Opus]
    CURSOR2 --> CLAUDE

    style SV fill:#4a9,stroke:#333,color:#fff
    style PA fill:#49a,stroke:#333,color:#fff
    style MEMORY fill:#f90,stroke:#333
    style GW fill:#94a,stroke:#333,color:#fff
```

## 三、文件清单

| 层级 | 文件 | 说明 |
|------|------|------|
| **固件** | `src/main.cpp` | 触控 → 串口协议 → 显示，零网络 |
| **配置** | `include/config.h` (gitignored) | USB 串口参数 |
| **中继** | `tools/relay.py` | 串口 ↔ HTTP 桥，自动重连 |
| **网关** | `dispatch/src/voice-gateway.ts` | OpenAI-compatible 端点 |
| **网关服务** | `dispatch/src/voice-gateway-serve.ts` | 独立 Express server |
| **校验** | `dispatch/src/voice-gateway-check.ts` | 22 项 mock 断言 |
| **Agent 定义** | `dispatch/config.yaml` | stackchan-voice 定义 |
| **信任模型** | `dispatch/src/logic-check.ts` | testVoiceTrustModel() |
| **扩展点** | `dispatch/src/config.ts` | memory_key 字段 |
| **路由** | `dispatch/src/dispatcher.ts` | memory_key 注入 |
| **面板** | `dispatch/src/dashboard.ts` | gateway 挂载（token gate 之前） |
| **画布** | `CANVAS.md` | 本文件 |

## 四、信任模型（design.md D6）

```
                  ┌─────────────────────────────┐
                  │      personal-assistant      │
                  │  ✅ filesystem               │
                  │  ✅ ask-agent (内部通信)       │
                  │  ✅ notify_user (推送)        │
                  │  ✅ self-mod (改自己代码)      │
                  │  ✅ shell (任意命令)           │
                  │  🧠 高信任：飞书账号认证        │
                  └─────────────────────────────┘

                  ┌─────────────────────────────┐
                  │      stackchan-voice          │
                  │  ✅ filesystem (读取项目)      │
                  │  ❌ ask-agent（未挂载）        │
                  │  ❌ notify_user（未挂载）      │
                  │  ❌ self-mod（未挂载）         │
                  │  ❌ shell（未挂载）            │
                  │  🧠 低信任：旁边任何人都能说     │
                  │  📦 共享 PA 的 living memory   │
                  └─────────────────────────────┘
```

信任是**定义级**的，不由每次请求决定——`stackchan-voice` 的 `mcp_servers` 字段为空，挂不上危险工具。

## 五、进度

| 组 | 内容 | 状态 |
|---|---|---|
| G1: Voice Gateway | gateway + gateway-serve + logic-check | ✅ 完成 |
| G2: 固件 + 串口 | main.cpp + relay.py + USB 直连 | ✅ 完成，已验证 live |
| G3: Agora ConvoAI | 实时语音管线 | ❌ 未开始 |
| G4: L0 具身 | 表情/LED/舵机 | ❌ 未开始 |
| G5: 语音输出 | CoreS3 扬声器 TTS | ❌ 未开始 |
| G6: 主动关注 | agent 主动打招呼 | ❌ 未开始 |
| G7: 运维 | PM2 持久化 relay + gateway | ✅ 完成 |

## 六、运维（PM2 持久化）

```bash
pm2 list                    # 查看所有服务
pm2 logs voice-gateway      # gateway 日志
pm2 logs stackchan-relay    # relay 日志
pm2 restart voice-gateway   # 重启 gateway
pm2 restart stackchan-relay # 重启 relay（自动重连串口）
pm2 restart all             # 全部重启
pm2 save                    # 保存当前进程列表（重启后自动恢复）
```

PM2 管理的两个 StackChan 服务：

| 服务 | PM2 名称 | 端口/设备 | 说明 |
|------|----------|-----------|------|
| voice-gateway | `voice-gateway` | `0.0.0.0:60630` | OpenAI-compatible 端点 |
| relay | `stackchan-relay` | `/dev/cu.usbmodem*` | 串口 ↔ HTTP 桥 |

## 七、设计决策（来自 design.md）

| ID | 决策 | 说明 |
|----|------|------|
| D1 | 分离 agent 定义 | voice 和 feishu 互不阻塞 busy 状态 |
| D2 | memory_key 共享 | voice 和 PA 是「同一个人」，记得同一份记忆 |
| D3 | gateway-serve 独立进程 | 不启动完整 dispatch，零 agent startup |
| D4 | USB 串口直连 | 绕开公司 WiFi，USB 同时供电+通信 |
| D5 | 文本回合先行 | 先验证链路，语音后上（Agora） |
| D6 | 定义级信任 | 信任是 agent 定义的属性，非 per-turn 参数 |
