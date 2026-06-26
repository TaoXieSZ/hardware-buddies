## ADDED Requirements

### Requirement: Codex 会话的 per-session 状态

`codex-bridge` SHALL 像 cc-bridge / cursor-bridge 一样，按 `session_id` 给每个 Codex 会话维护 per-session 状态（idle/thinking/tool/waiting，从 Codex hook 事件派生，派生顺序对齐 cc-bridge），并在进入 waiting 时分配单调递增 FIFO 序号。复用 `buddy_core` 共享的 `BuddyState.set_session_state`。Codex hook 事件名已是 Claude 形状（SessionStart/UserPromptSubmit/PreToolUse/PostToolUse/Stop/PermissionRequest），故 `apply_event` SHALL 直接复用，不引入与 Cursor 同级的事件名翻译层。

#### Scenario: Codex 事件落 per-session 状态

- **WHEN** Codex hook 报告某会话 PreToolUse
- **THEN** codex-bridge SHALL 把该会话桶 `st` 置为 tool
- **WHEN** 该会话触发 PermissionRequest（待输入）
- **THEN** SHALL 置 waiting 并分配 FIFO 序号

### Requirement: Codex 会话经 ext_sessions 并入单 BLE owner

codex-bridge SHALL NOT 自己拥有 BLE 链路；它 SHALL 把本机 Codex 会话以 `{action:"ext_sessions", agent:"codex", sessions:[...]}` best-effort 推送到 cc-bridge socket。cc-bridge 的 payload `sessions[]` SHALL 同时包含 Claude、Cursor 与 Codex 会话，每条 Codex 会话标 `agent:"codex"`；现有 ext 合并约束（陈旧丢弃、全表上限）SHALL 一并适用于 codex 桶，无需为 codex 单列新约束。

#### Scenario: 一块 cardputer 看到三个 agent 的会话

- **WHEN** cmux 同时跑着 1 个 Claude、1 个 Cursor、1 个 Codex 会话
- **THEN** 该 cardputer 收到的 `sessions[]` SHALL 同时含这三条，各带自己的 `st`/`ws`/`agent`
- **AND** 聚合字段（total/running/...）SHALL 自洽且向后兼容

#### Scenario: cc-bridge 未运行时 codex-bridge 不报错

- **WHEN** codex-bridge 推送 ext_sessions 但 cc-bridge socket 不可达
- **THEN** codex-bridge SHALL 静默跳过（best-effort），SHALL NOT 崩溃或阻塞 Codex hook

### Requirement: Codex 会话列表与聚焦按 cwd 对账 cmux 活 pane

由于 cmux 不在 Codex pane 上暴露 session-id（title 恒为 `codex`），Codex 会话的列表来源 SHALL 是 cmux 活 codex pane，join key SHALL 为工作目录 cwd（codex hook `cwd` 对 cmux pane `requested_working_directory`）。设备列表 SHALL 等于可聚焦集合（只列 cmux 活 codex pane，对齐用户「只看 cmux 活会话」约束）。选中聚焦 SHALL focus cwd 匹配的 cmux codex surface。

#### Scenario: 只列 cmux 活 codex pane 并 join 状态

- **WHEN** cmux 有 1 个活 codex pane，cwd=X，且 codex-bridge 从 hook 收到 cwd=X 会话的 thinking/tool/waiting
- **THEN** 设备 Codex 列表 SHALL 含该会话，状态取自 hook（按 cwd join），label 取自该 pane
- **WHEN** codex-bridge 的 hook 历史里有 cwd=Y 的会话但 cmux 无 cwd=Y 的活 codex pane
- **THEN** 设备列表 SHALL NOT 含 cwd=Y（僵尸不显）

#### Scenario: 选中 Codex 会话聚焦其 pane

- **WHEN** 用户在设备上选中一个 Codex 会话并回送 selectSession
- **THEN** daemon SHALL 聚焦 cwd 匹配的 cmux codex pane

#### Scenario: 同目录多 codex 撞 cwd（已知局限）

- **WHEN** 同一 cwd 下有多个 codex 会话
- **THEN** 设备 SHALL 把它们合并显示为该 cwd 的一条 Codex 会话（状态取最近活跃者），且聚焦该 cwd 的 cmux pane；本 change SHALL NOT 试图区分同 cwd 的多个 codex 会话

### Requirement: 跨三 agent 共用同一 FIFO 钉队列

待输入的钉 SHALL 跨 Claude/Cursor/Codex 统一排序：三者 waiting 共用同一单调 FIFO 序号空间，设备 SHALL 钉「最早进入等待」的那个，无论它属于哪个 agent。

#### Scenario: Codex 会话先等则先钉

- **WHEN** 一个 Codex 会话先于一个 Claude 会话进入待输入
- **THEN** 设备 SHALL 先钉该 Codex 会话（FIFO，跨 agent）

### Requirement: 固件区分 Codex 会话

设备会话列表 SHALL 为 `agent:"codex"` 的会话渲染与 Claude（黄 `cc`）/Cursor（灰蓝 `cu`）不同的标记 `cx` + 区分色，使三 agent 在列表中一眼可辨。线协议无新增字段（复用既有 `agent`）。

#### Scenario: 三 agent 列表各有标记

- **WHEN** 列表同时含 Claude、Cursor、Codex 会话
- **THEN** 三者 SHALL 分别显示 `cc`/`cu`/`cx` 标记与各自区分色
