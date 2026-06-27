# power-indicator Specification

## Purpose
TBD - created by archiving change cardputer-battery-indicator. Update Purpose after archive.
## Requirements
### Requirement: NORMAL 顶栏常驻电量百分比

固件 SHALL 在 NORMAL（idle / agent HUD）顶栏常驻显示电池电量百分比，数值取自 `M5.Power.getBatteryLevel()`（ADC 路径，0–100）。当返回值 `< 0`（unknown / 读取失败）时 SHALL 不显示电量（静默），SHALL NOT 显示占位数字或误导值。电量 SHALL 与现有 `T/R` 会话角标共存于顶栏右侧，二者 SHALL NOT 重叠。

#### Scenario: 纯电池运行显示电量
- **WHEN** 设备处于 NORMAL 且 `getBatteryLevel()` 返回 0–100
- **THEN** 顶栏 SHALL 显示该百分比（如 `85%`）
- **AND** 现有 `T/R` 会话角标 SHALL 仍可见、不被电量覆盖

#### Scenario: 读取失败时静默
- **WHEN** `getBatteryLevel()` 返回 `< 0`
- **THEN** SHALL 不绘制电量，顶栏其余元素照常

### Requirement: 仅 NORMAL 显示，覆盖态不画

电量 SHALL 仅在 NORMAL 顶栏出现。APPROVAL / QUESTION / SESSIONS / HELP / KEYMAP 等整屏覆盖态 SHALL NOT 绘制电量角标，与现有 `T/R` 角标同款守卫。

#### Scenario: 审批态不画电量
- **WHEN** 设备进入 APPROVAL（或任一全屏覆盖态）
- **THEN** 该屏 SHALL NOT 含电量角标
- **WHEN** 退回 NORMAL
- **THEN** 电量角标 SHALL 重新出现

### Requirement: 三色电量档

电量 SHALL 按区间着色以一眼传达状态：≥50% 绿、20–49% 黄、<20% 红。<20% 的红色 SHALL 作为「需充电」的视觉信号。

#### Scenario: 低电红色
- **WHEN** 电量 <20%
- **THEN** 电量数字 SHALL 以红色显示
- **WHEN** 电量 ≥50%
- **THEN** SHALL 以绿色显示

### Requirement: 周期轮询 + 变化才重绘

固件 SHALL 周期性（约 30s）轮询电量而非每帧读取（ADC 开销），并 SHALL 仅在电量跨整数百分位或跨色档变化时触发重绘，电量不变时 SHALL NOT 周期性强刷整屏或打断 clawd GIF 播放。

#### Scenario: 静置不闪烁
- **WHEN** 设备 NORMAL 静置、电量在两次轮询间未变
- **THEN** SHALL NOT 因电量轮询产生可见重绘 / GIF 卡顿

### Requirement: 不引入 USB / 充电状态

本能力 SHALL NOT 显示 USB / 充电状态文字或图标。cardputer-ADV 的 `isCharging()` 恒返回 `charge_unknown` 且无 VBUS / CHG_STAT 信号，USB 区分 SHALL 作为独立后续 change 处理，不在本能力范围内。插入 USB 时设备 SHALL 表现为电量百分比升高（而非显示「USB」字样）。

#### Scenario: 插 USB 不显示 USB 字样
- **WHEN** 设备插入 USB 供电
- **THEN** SHALL 继续显示电量百分比（随充电升高）
- **AND** SHALL NOT 显示「USB」或充电图标

