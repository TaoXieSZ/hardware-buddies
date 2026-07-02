## ADDED Requirements

### Requirement: BLE 未连接时展示专属 Connecting 视觉

固件 SHALL 在 `!online`（`cclink::connected()` 为假，即尚未连上 cc-bridge）期间展示
`AgentState::Connecting` 对应的 `clawd-carrying.gif`，并 SHALL 在顶栏左侧（复用
`drawSessionTag()` 的绘制区域）常驻显示文案 `Connecting...`，直到连上 cc-bridge。
该状态 SHALL NOT 与真实 `AgentState::Idle`（`idle.gif`，无常驻顶栏文案）视觉混淆。

#### Scenario: 开机未连接时显示 Connecting

- **WHEN** 设备刚上电或 BLE 尚未连上 cc-bridge（`online == false`）
- **THEN** 固件 SHALL 展示 `clawd-carrying.gif`
- **AND** SHALL 在顶栏左侧常驻显示 `Connecting...`
- **AND** SHALL NOT 跑 `deriveAgentState`/多会话轮播逻辑（`bs` 此时必然是全零，跳过
  该计算不影响正确性）

#### Scenario: 连上后恢复正常渲染

- **WHEN** 设备从 `!online` 变为 `online`（BLE 连上 cc-bridge）
- **THEN** 固件 SHALL 撤下 `Connecting...` 文案与 `clawd-carrying.gif`
- **AND** SHALL 恢复既有渲染路径（`deriveAgentState` + 角标 + 会话标识/轮播）

#### Scenario: 已连接但零活跃会话不触发 Connecting

- **WHEN** `online == true` 且 `bs.nSessions == 0`（如所有 Claude 会话都已结束）
- **THEN** 固件 SHALL 按现状展示真实 `AgentState::Idle`（`idle.gif`），SHALL NOT 展示
  Connecting 视觉（本需求只覆盖「BLE 未连接」这一种情况，不含此边界）

#### Scenario: Connecting 期间不影响覆盖层优先级

- **WHEN** 固件处于 Connecting 视觉期间
- **THEN** 审批/会话列表/帮助/问答覆盖层 SHALL 保持现有触发条件不变（其触发数据
  `bs.promptId`/`bs.hasQuestion`/`bs.sessions` 在 `!online` 时必然为空，天然不会
  与 Connecting 产生冲突）
