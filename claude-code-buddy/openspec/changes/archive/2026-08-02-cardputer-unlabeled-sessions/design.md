# design.md — cardputer-unlabeled-sessions

## Context

`BuddyState.to_payload`（`tools/buddy_core/core.py:155-216`）构建心跳 `sessions[]` 时有两个互斥分支：`session_labels` 非空走 labels 分支（只含 cmux Claude pane），为空走 `_sessions` 回退分支。设计初衷是"列表 = selectSession 能聚焦的集合"，但它隐含假设了**所有值得显示的会话都在 cmux 里跑 Claude Code**。Kimi Code 接入（2026-08-02，`~/.kimi-code/config.toml` 的 cc-bridge hooks 块）打破了这个假设：Kimi 会话被 hook 正常跟踪，却因没有 `checkpoint_id` 永远进不了 labels，在 labels 分支下被整个丢弃。

约束：固件行缓冲上限（StickC 1024B / cardputer 2048B）→ 16 条 cap 不可动；`sessions[]` 条目 schema（`sid/running/label?/st?/ws?/agent?`）固件已按可选字段解析，不可破坏。

## Goals / Non-Goals

**Goals:**
- hook 跟踪到的会话全部能在 cardputer `tab` 列表出现，无论有无 cmux 标签。
- 可聚焦的 Claude 会话（有标签）永远优先于不可聚焦的（无标签）。
- 纯 Claude + cmux 部署的输出逐字节不变。

**Non-Goals:**
- 不给无标签会话编造 label（cmux auto-name 是 Claude pane 专属机制；Kimi pane 的标题识别另案处理）。
- 不让无标签会话支持 selectSession 聚焦（本来就是 logged no-op；Kimi pane 的聚焦匹配是后续独立 change）。
- 不动 ext_sessions 合并、计数器、reaper 逻辑。

## Decisions

**D1：在 labels 分支尾部追加无标签会话，而不是合并两个分支的数据源。**
labels 分支构建完 `sess` 后，遍历 `_sessions`，跳过已在 `session_labels` 里的 sid，把剩余条目（不带 `label`）追加到 16 条上限。备选方案"给 Kimi pane 也生成 cmux 标签"被否：依赖 cmux session 文件的 agent.kind 识别 + 标题解析，脆弱且超出本变更范围；追加方案零外部依赖、固件零改动。

**D2：保持 labels 优先的排序，不做交错。**
标签会话全部在前——它们是 selectSession 唯一可聚焦的集合，列表上部是黄金位置。无标签会话按 `_sessions` 插入顺序（≈会话出现顺序）追加。备选"按 running 状态排序"被否：改变现有 Claude 用户的列表顺序，且 labels 顺序本身跟随 cmux surface 顺序，有稳定含义。

**D3：16 条 cap 的检查点合并到一处。**
现状 labels 分支和回退分支各自 `len(sess) >= 16: break`。追加逻辑在 labels 分支的 break 之后继续填充剩余槽位，复用同一个上限常量语义，不引入新常量。

## Risks / Trade-offs

- [无标签条目把 Claude 标签会话挤出前 16] → 不可能：D2 保证标签会话先占位，被挤的只会是无标签会话。
- [固件把无 label 条目渲染成空白行] → cardputer 固件已有 sid 前缀回退（`cardputer-adv-buddy` 会话列表在无 label 时显示 sid 前几位），2026-08-02 前已真机验证过回退分支（cmux 缺失时）渲染正常。
- [selectSession 点了无标签会话无反应，用户困惑] → 与今天 cmux 缺失时的行为一致；daemon 日志会记 `no matching cmux surface (ignored)`，可排查。后续 change 再补 Kimi pane 聚焦。

## Migration Plan

纯 daemon 侧变更：改 `core.py` + 单测 → `make test-py` → 重启 launchd daemon（`launchctl kickstart -k gui/$(id -u)/com.cc-bridge`）→ 真机看 `tab` 列表。回滚 = `git checkout` 单文件 + 再 kickstart。固件无需重刷。

## Open Questions

- Kimi pane 的 selectSession 聚焦（需要 cmux session 文件的 agent.kind 识别 + `focus_by_kimi_sid`）——留给下一个 change。
