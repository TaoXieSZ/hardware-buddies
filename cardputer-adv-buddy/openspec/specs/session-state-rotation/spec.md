# session-state-rotation Specification

## Purpose
TBD - created by archiving change cardputer-session-rotation. Update Purpose after archive.
## Requirements
### Requirement: per-session 状态进入 payload

`cc-bridge` SHALL 按 `session_id` 分桶追踪每个会话自己的状态（idle/thinking/tool/waiting/done，从该 session 带 `session_id` 的 hook 事件派生，派生顺序对齐 firmware：thinking 先于 tool），并在 payload `sessions[]` 的每条上附 `st`（per-session 状态）与 `ws`（进入等待的单调递增序号；非等待为 0/缺省）。聚合字段（total/running/waiting/msg）SHALL 保留，使不解析 per-session 的兄弟 buddy 仍可读聚合。

#### Scenario: 多会话各自状态进 payload

- **WHEN** 会话 A 处于 thinking、会话 B 处于 tool-use
- **THEN** payload `sessions[]` SHALL 含 A 的 `st`=thinking 与 B 的 `st`=tool（各自的），而非只有一个聚合 msg
- **AND** 聚合字段 SHALL 仍然存在且自洽

#### Scenario: 老固件/兄弟 buddy 向后兼容

- **WHEN** 一个不解析 `st`/`ws` 的客户端收到扩展后的 payload
- **THEN** 它 SHALL 忽略 `st`/`ws` 并照旧读聚合字段，不报错

### Requirement: 审批的 session 归属

permission 输入 SHALL 可归属到具体 `session_id`：`hook_permission.py` SHALL 把事件的 `session_id` 下发给 daemon，`_handle_wait_permission` SHALL 据此把 `state=waiting` + 等待序号落到对应会话桶，而非只设聚合 `waiting`。问答输入沿用 `parse_pending_questions` 已有的 `sid`（无需新增 plumb）。

#### Scenario: 审批归属到发起它的会话

- **WHEN** 会话 B 触发一个 PreToolUse 审批
- **THEN** daemon SHALL 把「待输入」标到会话 B 的桶（含等待序号），而非匿名聚合

### Requirement: 主形象按会话轮播

无覆盖层（非审批/问答/会话列表/帮助）时，固件 SHALL 让主形象（clawd）在 `sessions[]` 间轮播：每个会话停留一个 dwell 时长，期间播放该会话 `st` 派生的动画并在屏上显示其**会话标识**（`label` 优先；`label` 为空时回退到短 `sid`，如前 6-8 字符）。会话标识 SHALL 在主形象（活动）界面常驻显示——即便只有一个会话、不轮播时，也 SHALL 标出当前是哪个会话。轮播成员 SHALL 为全部会话；idle 会话 MAY 用更短 dwell 以减少对忙碌会话曝光的稀释（可调）。无会话时 SHALL 回退到 idle/sleep（现状）。

#### Scenario: 多会话轮播

- **WHEN** A=thinking、B=tool、C=idle 且无待输入
- **THEN** 主形象 SHALL 依次显示 A(thinking)、B(tool)、C(idle)，每个附其 label
- **AND** 每个会话的停留 SHALL 约为配置的 dwell（active 默认长于 idle）

#### Scenario: 单会话退化为直显

- **WHEN** 只有一个会话
- **THEN** 主形象 SHALL 直接持续显示该会话状态，不做无意义轮播
- **AND** SHALL 仍标出该会话的会话标识（label 或短 sid）

#### Scenario: 活动界面常驻会话标识

- **WHEN** 主形象正在显示某会话的状态动画（轮播中或单会话）
- **THEN** 屏上 SHALL 同时显示该会话的标识（label 优先，缺则短 sid），使用户一眼知道当前是哪个会话

### Requirement: 待输入时 FIFO 钉

当 ≥1 个会话需要输入（`st`=waiting，或有 pending 审批/问答）时，固件 SHALL 停止轮播并**钉在等待序号最小（最早）的那个会话**上，显示其 notification 动画 + label。被钉会话的输入被处理（daemon 撤其 waiting）后，固件 SHALL 自动钉次小者；待输入集合为空后 SHALL 恢复轮播。

#### Scenario: 多个待输入按 FIFO 钉

- **WHEN** 会话 A 先、会话 B 后进入待输入
- **THEN** 固件 SHALL 先钉 A（不轮播、不闪）
- **AND** A 的输入处理完后 SHALL 自动钉 B

#### Scenario: 钉期间不被忙碌会话打断

- **WHEN** 钉在等待中的 A，同时会话 C 在 tool-use
- **THEN** 主形象 SHALL 保持钉在 A，不切去显示 C

