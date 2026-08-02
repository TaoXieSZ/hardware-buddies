# design.md — kimi-session-identity

## Context

2026-08-02 的真机状态：Kimi hooks 与 Claude hooks 共用 `hook.py` → `/tmp/cc-bridge.sock`，事件 JSON 同构，daemon 无法区分来源；`to_payload` 的无标签条目不带 `agent`，固件缺省渲染 `cc` 黄标。聚焦侧，cmux 0.64.20 的 session 文件**没有** Kimi 的 `agent.kind`（只有 claude/codex），但发现两个可利用事实：Kimi 会把 pane 标题设为首条用户消息；`~/.kimi-code/sessions/wd_<dir>_<hash>/session_<uuid>/` 目录名同时编码了工作目录和 session id。

约束：`agent` 字段对固件是可选键，旧固件必须兼容；`hook.py` 被 Claude/Kimi 两处调用，改动必须对 Claude 调用零影响。

## Goals / Non-Goals

**Goals:**
- 设备列表能区分 Kimi 会话（`ki` 标）与 Claude（`cc` 标）。
- 设备点选 Kimi 会话能聚焦到对应 cmux pane（同目录多 pane 时尽力而为 + 日志）。

**Non-Goals:**
- 不给 Kimi 会话做 cmux 自动命名 label（标题即首条消息，固件 sid 前缀已可读）。
- 不动 Claude/Cursor/Codex/OpenCode 的现有匹配规则与顺序（Kimi 回退追加在末尾）。
- Kimi 审批门禁（PreToolUse 同步阻塞）仍不在范围。

## Decisions

**D1：来源标记走 hook.py 参数注入，而不是 daemon 侧猜测。**
`hook.py --agent kimi` 把 `agent` 注入事件 JSON。备选"daemon 按 sid 形状猜"（Kimi sid 带 `session_` 前缀）被否：sid 格式是两个产品的内部细节，硬编码脆弱；显式参数把知识留在配置侧（`~/.kimi-code/config.toml`），Claude 调用零改动。

**D2：Kimi 聚焦走 state.json 标题匹配，而不是 cmux agent.kind 或 cwd。**
真机排除了这些候选：cmux session 文件不标 Kimi pane（0.64.20 只有 claude/codex kind）；pane 的 workspace cwd 和 `requested_working_directory` 都是 pane 创建时的快照，不跟踪会话真实 cwd（pane 在家目录打开、session 跑在项目目录，实测两者不符）。可靠键是 `state.json` 的 `title`——Kimi 把 pane 标题设为首条用户消息且逐字记录。标题缺失时退化 `cwd` basename 唯一匹配。若未来 cmux 给 Kimi pane 标 kind，可平滑替换（规则顺序不变）。

**D3：固件 `ki` 标复用现有映射表，颜色选青色系。**
`clawd_player.cpp:325` 的 if-else 链加一行；`agent[16]` 足够。青色（如 0x067F）与现有黄/灰蓝/绿/青蓝区分开。旧固件收到 `agent:"kimi"` 时 strcmp 全不命中 → 回退 `cc`，向后兼容。

## Risks / Trade-offs

- [同目录多个 Kimi 会话聚焦错 pane] → 接受：标题排除 + 最近活动启发式，日志可排查；spec 已写明歧义时按未命中处理。
- [`wd_<dir>_<hash>` 目录命名变化] → 解析失败 = 聚焦不可用但其余功能不受影响；单测覆盖解析函数。
- [hook.py 参数拼错导致 agent 注入失败] → fail-open：hook.py 原有"任何异常静默退出 0"语义不变，最坏情况是回到无标记现状。

## Migration Plan

daemon + hook.py + 单测 → `make test-py` → kickstart daemon → 改 `~/.kimi-code/config.toml` hooks 命令（加 `--agent kimi`）→ 固件加映射 + 下载模式重刷 → 真机验证（列表 `ki` 标 + 点选聚焦）。回滚逐段独立：daemon 单文件 checkout、config.toml 去参数、固件重刷旧 bin。

## Open Questions

- Kimi pane 标题排除模式的具体实现（首条消息可能与 Claude 标题撞车）——实施时用 `CmuxClient.list_sessions()` 的 title/kind 信息尽量排除，最差退回 cwd 唯一匹配。
