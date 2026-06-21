## ADDED Requirements

### Requirement: 拿起唤醒

系统 SHALL 通过 BMI270（经 M5Unified 的 `M5.Imu`）检测设备被拿起的运动，并在检测到时让宠物从睡眠/平静切换到「被唤醒并看向用户」的反应。IMU 初始化 MUST 复用 M5Unified 的内置 BMI270 支持，不直接裸写寄存器。

#### Scenario: 拿起设备唤醒宠物

- **WHEN** 设备处于静止/睡眠态且被拿起（加速度出现明显瞬时变化）
- **THEN** 宠物 SHALL 在 500ms 内播放一次「睁眼/看向你」的唤醒反应
- **AND** 唤醒后 SHALL 回到当前会话状态对应的表情

### Requirement: 晃动惊吓反应

系统 SHALL 检测明显的晃动（短时间内加速度大幅波动），并在检测到时让宠物播放一次短暂的「被惊到/激灵」反应。

#### Scenario: 晃动触发激灵

- **WHEN** 设备被明显晃动
- **THEN** 宠物 SHALL 播放一次不超过 1 秒的惊讶/激灵表情
- **AND** 反应结束后 SHALL 自动回到当前会话状态对应的表情

### Requirement: 静止入睡

系统 SHALL 在「会话状态为 IDLE 且 IMU 连续 N 秒（默认 30s）无明显运动」时，让宠物进入睡眠表情；一旦检测到运动或状态离开 IDLE，SHALL 立即退出睡眠。

#### Scenario: 长时间静止且空闲则入睡

- **WHEN** 会话状态为 IDLE 且连续 30 秒未检测到明显运动
- **THEN** 宠物 SHALL 切换为睡眠表情（如闭眼/zZ）

#### Scenario: 睡眠中被打扰立刻醒来

- **WHEN** 宠物处于睡眠表情且检测到运动，或会话状态离开 IDLE
- **THEN** 宠物 SHALL 立即退出睡眠并回到对应的清醒表情
