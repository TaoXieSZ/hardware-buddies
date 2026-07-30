# cores3-voice-assistant Spec Delta

## ADDED Requirements

### Requirement: 音色、人设与说话动作可运行时调整
音色、人设 instructions、说话时是否跳舞 SHALL 可在运行时调整并持久化到 NVS；
编译期常量降级为**出厂默认值**，NVS 中存在值时以 NVS 为准。音色与人设的变更
MUST 在下次建立会话时才带入 `session.update`，不得重连当前会话。

#### Scenario: 人设持久化
- **WHEN** 用户改掉人设文案后重启设备
- **THEN** 下次唤醒建立会话时使用改后的人设，回答风格随之改变

#### Scenario: 出厂默认仍可用
- **WHEN** 设备 NVS 中没有音色/人设记录（新设备或清空 NVS 后）
- **THEN** 使用编译期默认（音色 longpaopao_v3.6、小咪萌系人设），行为与本变更前一致

### Requirement: 说话时跳舞
当"摇头跳舞总开关"与"说话时来段舞"均开启时，小咪在播放回复期间 SHALL 使用更活泼的
摆动动作；任一项关闭时 SHALL 退回原有行为（关闭总开关=舵机不动，仅关跳舞=轻微摆头）。
动作幅度 MUST 保持在既有舵机速度上限内，不得引入供电骤降。

#### Scenario: 边说边跳
- **WHEN** 两个开关都开启，小咪回答问题
- **THEN** 回答期间摆头跳舞，回答结束回到待命动作，且本轮 underrun 仍为 0

#### Scenario: 安静模式
- **WHEN** 摇头跳舞总开关关闭
- **THEN** 小咪回答时舵机完全不动
