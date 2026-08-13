# StopWatch Agent Walkie 架构图

这张图把已经跑通的语音链路与当前实现的多 Agent steer-only 控制面放在同一视图中。`NOW` 表示代码与自动化测试已落地；`NEXT` 继续表示尚未实现的新会话启动、WSS、离线 ASR、mDNS 和生产 launchd。

## 查看

- 交互架构图：[stopwatch-agent-walkie.html](./output/stopwatch-agent-walkie.html)
- 静态预览：[stopwatch-agent-walkie-preview.png](./assets/stopwatch-agent-walkie-preview.png)
- Archify 源数据：[stopwatch-agent-walkie.architecture.json](./data/stopwatch-agent-walkie.architecture.json)

交互图支持明暗主题、节点搜索、路径探测、聚焦视图和 SVG/PNG 导出。顶部四个 Guided Views 分别用于查看当前语音闭环、腕上确认闭环、多 Agent 编排以及数据与信任边界。

## 架构判断

- 当前原型继续保持轻量：StopWatch 负责采集和反馈，Mac Bridge 负责协议与模型调用，百炼 `qwen3-asr-flash` 只负责 ASR。
- 当前控制面不让语音转写直接触发开发动作，而是先生成提案，通过腕上 A/B 键完成显式确认。
- 中立的 `Multi-Agent Router` 负责显式别名和现有会话解析，通过 cc-bridge 的 owner-only socket 驱动 OpenCode、Codex、Claude Code 与 Kimi Code；它不会启动新会话，也不会让 LLM 猜目标。
- 四种 Coding Agent 都是平级执行后端；后续增加新 Agent 时只新增适配器，不改变手表协议和确认流程。
- API Key 留在 Mac；项目代码、Agent 会话和执行结果均留在本机。控制 JSON 已有 HMAC-SHA-256 双向认证与重放防护，但音频仍是可信局域网内的明文；WSS 仍属于 `NEXT`。
