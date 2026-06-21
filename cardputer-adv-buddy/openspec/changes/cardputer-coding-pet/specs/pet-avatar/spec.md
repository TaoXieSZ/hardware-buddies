## ADDED Requirements

### Requirement: 程序化宠物脸渲染

系统 SHALL 在开机后于 Cardputer-ADV 屏幕（横屏 240x135）上渲染一张基于 `m5stack-avatar` 的程序化脸，作为宠物主体，并 SHALL 持续播放呼吸与眨眼的待机动画，使其看起来「活着」。该脸的尺寸与位置 MUST 适配 135px 高的小屏，不被裁切。

#### Scenario: 开机显示会动的脸

- **WHEN** 固件启动完成并进入主循环
- **THEN** 屏幕显示一张完整、未被裁切的宠物脸
- **AND** 在无任何输入时，脸 SHALL 周期性眨眼并有轻微呼吸起伏

### Requirement: 会话状态到表情的映射

系统 SHALL 把已有的会话状态枚举（IDLE / THINKING / TOOL / APPROVAL / DONE）各自映射到一组确定的「表情 + 强调色 + 气泡台词」。每个状态 MUST 有唯一且可区分的表情，使用户一眼能分辨宠物当前处于哪种会话状态。

#### Scenario: 每个状态有可区分的表情

- **WHEN** 当前会话状态为五态中的任意一个
- **THEN** 宠物 SHALL 显示该状态对应的表情（如 TOOL→专注/兴奋、IDLE→平静、APPROVAL→期待/恳求、DONE→开心、THINKING→沉思）
- **AND** 屏幕 SHALL 同时显示该状态对应的强调色与一句短气泡台词

#### Scenario: 等待审批时表现出「求点头」

- **WHEN** 会话状态切换为 APPROVAL
- **THEN** 宠物 SHALL 显示恳求/期待类表情并配相应台词（如「等你点头～」）
- **AND** 该表现 MUST 与其它四态明显不同，以提示用户需要操作

### Requirement: 状态切换即时反映

系统 SHALL 在会话状态发生变化后的 200ms 内更新宠物表情与气泡，且更新 MUST NOT 导致明显闪烁或卡顿。

#### Scenario: 切换状态快速且不闪烁

- **WHEN** 会话状态从一个值变为另一个值
- **THEN** 宠物表情 SHALL 在 200ms 内切换到新状态对应的表情
- **AND** 切换过程 MUST NOT 出现整屏闪白或撕裂
