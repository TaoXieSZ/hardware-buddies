## ADDED Requirements

### Requirement: Cursor 会话的 per-session 状态

`cursor-bridge` SHALL 像 cc-bridge 一样，按 `session_id` 给每个 Cursor 会话维护 per-session 状态（idle/thinking/tool/waiting，从 Cursor hook 事件派生，派生顺序对齐 cc-bridge），并在进入 waiting 时分配单调递增 FIFO 序号。复用 `buddy_core` 共享的 `BuddyState.set_session_state`。

#### Scenario: Cursor 事件落 per-session 状态

- **WHEN** Cursor hook 报告某会话 PreToolUse
- **THEN** cursor-bridge SHALL 把该会话桶 `st` 置为 tool
- **WHEN** 该会话触发权限/待输入
- **THEN** SHALL 置 waiting 并分配 FIFO 序号

### Requirement: 单 BLE owner 聚合 Claude 与 Cursor 会话

一个 BLE owner 的 payload `sessions[]` SHALL 能同时包含 Claude 与 Cursor 的会话，按 cmux 会话（surface）归一以避免两 agent 的 sid 命名空间冲突；每条 SHALL 可带 `agent` 字段（`claude`/`cursor`）。设备一次只被一个 central 占用，故聚合 SHALL 收敛到单一 owner，SHALL NOT 让多个 bridge 并行抢同一设备的链路。

#### Scenario: 一块 cardputer 看到两个 agent 的会话

- **WHEN** cmux 同时跑着 1 个 Claude 会话与 1 个 Cursor 会话
- **THEN** 该 cardputer 收到的 `sessions[]` SHALL 同时含这两条，各带自己的 `st`/`ws`
- **AND** 聚合字段（total/running/...）SHALL 自洽且向后兼容

#### Scenario: 选中 Cursor 会话聚焦其 pane

- **WHEN** 用户在设备上选中一个 Cursor 会话并回送 selectSession
- **THEN** daemon SHALL 聚焦该 Cursor 会话对应的 cmux pane（依赖 cmux 提供其 surface 绑定）

### Requirement: 跨 agent 共用同一 FIFO 钉队列

待输入的钉 SHALL 跨 agent 统一排序：Claude 与 Cursor 会话的 waiting 共用同一单调 FIFO 序号空间，设备 SHALL 钉「最早进入等待」的那个，无论它是 Claude 还是 Cursor。

#### Scenario: Cursor 会话先等则先钉

- **WHEN** 一个 Cursor 会话先于一个 Claude 会话进入待输入
- **THEN** 设备 SHALL 先钉该 Cursor 会话（FIFO，跨 agent）
