# 04 — Agent 后端

> dispatch + agent-host 调用链, agent 定义体系, 信任模型

## 整条调用链 (CANVAS.md, 2026-07-07 已验证)

```
CoreS3 触屏 → USB Serial @ASK "现在几点了？"
  → relay.py (Mac, pyserial, 115200bps, DTR/RTS=false)
  → POST :60630/v1/voice/chat/completions (Bearer: VOICE_GATEWAY_TOKEN)
  → voice-gateway.ts: token 校验 → 提取最后一条 user message
  → dispatch("stackchan-voice", prompt)  [create-or-resume 稳定 session]
  → agent-host :60620 → Cursor/Claude Opus
  → reply blob (~10-20s) → dispatch → HTTP 200
  → relay.py → @REPLY "下午三点零八分。" → CoreS3 LCD
```

## Agent 定义体系 (config.yaml)

| Agent ID | 引擎 | 模型 | 触发源 | 信任 | MCP 工具 |
|----------|------|------|--------|------|----------|
| personal-assistant | local | opus | 飞书 DM / Cron 3×/day / owner 群 @ | 高 (飞书认证) | ask-agent, feishu-docs, feishu-sheets, notify_user, request_approval |
| stackchan-voice | local | opus | POST /v1/voice/chat/completions | 低 (空工具) | 无 MCP |
| sentinel | local | sonnet | Cron 每 30min | 中 | ask-agent, notify_user, request_approval |
| thread-helper | local | sonnet | 一次性群聊帮助 | — | — |
| remote-helper | remote-vm | sonnet | 飞书群 @ (on luobutest VM) | — | — |

## stackchan-voice 详细配置

- engine: local, model: opus
- reset_after: 12 (12 轮后自动回收上下文)
- pool_size: 0 (不预初始化 — 只在触屏/语音触发时创建)
- memory: true, memory_key: personal-assistant (共享 PA 的同一份长期记忆)
- mcp_servers: 未定义 — 空列表, 零高危工具

## 信任模型 (design.md D6)

信任是定义级的 — stackchan-voice 的 mcp_servers 字段为空,
挂不上危险工具。personal-assistant 通过飞书账号认证拥有全量 MCP。
两个 agent 是不同进程 (各自有 busy 状态), 不会互斥锁死, 但共享同一份
living memory。

## 进度表

| 组 | 内容 | 状态 |
|----|------|------|
| G1 | Voice Gateway (gateway + gateway-serve + logic-check) | ✅ |
| G2 | 固件 + 串口 (main.cpp + relay.py + USB live 验证) | ✅ |
| G3 | Agora ConvoAI 实时语音管线 (ASR/TTS/AEC) | ❌ |
| G4 | L0 具身 (表情/LED/舵机) | ❌ |
| G5 | 语音输出 (CoreS3 扬声器 TTS) | ❌ |
| G6 | 主动关注 (agent 主动打招呼 + 转头) | ❌ |

## 端口与拓扑

| 服务 | 端口 | 位置 |
|------|------|------|
| dispatch dashboard + voice-gateway | :60630 | Mac2 |
| agent-host (local) | :60620 | Mac2 |
| agent-host (remote-vm) | 72.240.64.6:60620 | luobutest VM |
| relay.py | — | Primary Mac (USB 串口 ↔ HTTP) |
