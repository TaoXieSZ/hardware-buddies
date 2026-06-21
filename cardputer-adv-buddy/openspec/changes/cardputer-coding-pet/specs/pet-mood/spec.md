## ADDED Requirements

### Requirement: 心情模型随事件与时间演化

系统 SHALL 维护一个有界的心情/精力数值（例如 0–100），并随会话事件与时间演化：DONE（任务完成）SHALL 提升心情；长时间 IDLE SHALL 缓慢降低心情（无聊）；长时间停留在 APPROVAL SHALL 让心情向「焦虑」偏移。该数值 MUST 限制在有效范围内，且仅存于内存（不要求跨重启持久化）。

#### Scenario: 完成任务提升心情

- **WHEN** 会话状态进入 DONE
- **THEN** 心情数值 SHALL 增加且不超过上界

#### Scenario: 长时间空闲降低心情

- **WHEN** 会话状态持续为 IDLE 一段时间
- **THEN** 心情数值 SHALL 随时间缓慢下降且不低于下界

### Requirement: 心情调制宠物表现

系统 SHALL 用当前心情值调制 avatar 的细节表现（至少影响眨眼频率与视线/朝向之一），使相同会话状态下、不同心情时宠物的「神态」有可观察的差异。心情调制 MUST NOT 覆盖会话状态决定的主表情类别。

#### Scenario: 高心情与低心情神态不同

- **WHEN** 会话状态相同但心情值分别处于高位与低位
- **THEN** 宠物的眨眼频率或视线表现 SHALL 出现可观察的差异
- **AND** 两种情况下的主表情类别 SHALL 仍由会话状态决定，保持一致
