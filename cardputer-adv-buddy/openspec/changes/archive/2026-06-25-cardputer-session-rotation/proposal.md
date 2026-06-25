## Why

clawd 主形象现在只反映**一个聚合状态**：`deriveAgentState` 把所有会话压成 total/running/waiting/msg，派生出单一动画。多会话并行时（A 在 thinking、B 在 tool-use、C 等输入），屏上只能看到「聚合」结果，看不出**每个会话各自在干什么**。大屏 + per-session 数据的价值只用了一半。

本 change 让主形象从「显示一个聚合态」升级为「**轮播每个会话各自的状态**」，并在有会话**需要输入**时按 FIFO **钉**在最早等待的那个上——把 Cardputer 变成一块多会话的「状态墙」。

## 关键现实（spike 已验，2026-06-24）

per-session 状态/输入归属**当前不在 wire 上，但可行**——数据都在，只是 daemon 把它 collapse 成聚合了：

- **问答归属**：`parse_pending_questions` 每条已带 `sid = workstream_id`（cmux_control.py:372）。✅ 现成。
- **审批归属**：permission hook 有 `session_id`（hook_permission.py:95），但只用来拼 rid、没塞进发给 daemon 的 req；daemon 设的是聚合 `state.waiting`（core.py:1225）。🟡 plumb 一个字段即可。
- **per-session 状态**：每个 hook 事件都带 `session_id`（core.py:1186），daemon 现在 collapse 成聚合 msg。🟡 改成「按 session_id 分桶」即可。

结论：无 blocker，是三层 plumbing 活（daemon 按 session 分桶 → wire 加字段 → 固件轮播/钉）。

## What Changes

- **daemon（cc-bridge ×2 仓）按 session 分桶**：`_sessions[sid]` 从只存 `running` 扩到存每个会话自己的 `state`（thinking/tool/waiting/done/idle，从该 session 的 hook 事件派生）+ `waiting_since` 时间戳。审批的 `session_id` 从 hook plumb 到下游。
- **wire / payload**：`sessions[]` 每条从 `{sid,label,running}` 扩到 `{sid,label,running,state,waiting_since}`。聚合字段保留（兄弟 buddy 仍读聚合）。
- **firmware 轮播控制器**：`SessionInfo` 加 `state`/`waitingSince`；无覆盖层时，主形象在**所有** session 间轮播（每个 ~2.5s + 显示 label），播放各自状态的 clawd 动画。
- **firmware FIFO 钉输入**：≥1 个会话需要输入（waiting/approval/question）时，**钉在 `waiting_since` 最早的那个**上，不轮播；处理完再钉下一个（严格队列）。
- **可调旋钮（稀释缓解）**：「全部都轮」时 idle 会话稀释忙碌会话的曝光；提供 dwell 权重（idle 短、active 长）作为可调项，默认值待 tune。

## Non-goals

- 不改现有审批/问答**覆盖层**的输入 UI 本身（它们仍是全屏拍板入口）；本 change 管的是**主形象/环境层**的轮播与钉，以及覆盖层的 per-session 归属与排队。
- 不做多会话**同屏分格**（小屏放不下）——轮播是时间复用，不是空间复用。
- 不动 `tab` 会话列表的选中/切换（已由 session-switcher 提供）。

## Capabilities

### New Capabilities
- `session-state-rotation`：主形象按 per-session 状态轮播 + 有输入需求时 FIFO 钉的行为，含 wire 上的 per-session 状态/等待序号，以及「主形象来源从单一聚合改为轮播选中会话」。

### 依赖（非 Modified）
- 本 change 在 `agent-state-animation`（由 active change `cardputer-feedback-channels` 定义、尚未 archive，故不在 baseline）的 per-state 动画之上工作：轮播控制器选中某会话后，仍调用该 capability 定义的 thinking/tool/notification/idle 等动画。两个 change 都触及主形象状态来源——**实现顺序上 feedback-channels 应先 archive**，再把本 change 的「轮播来源」叠加，避免对同一派生入口的冲突编辑。

## 风险 / 待解

- **payload 体积**：per-session 状态把 `sessions[]` 撑大（≤16 条 × ~20B）。这正好压到行缓冲——cardputer 行缓冲已扩到 2048（够），但 claude-code-buddy 的 `_LineBuf<1024>` 更紧（见 memory「cmux/RX 行缓冲」线索），移植时需复核。
- **覆盖层与钉的关系**：审批/问答覆盖层全屏时主形象不显示，钉主要作用于 notification 态 + 多个待输入的**排队**。覆盖层排队的具体形态留实现期定。
