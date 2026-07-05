## ADDED Requirements

### Requirement: 无活动超阈值后自动熄屏

固件 SHALL 跟踪「距上次活动的时长」，并在其超过熄屏阈值（编译期常量）后调用
`M5Cardputer.Display.setBrightness(0)` 关闭背光熄屏。活动 SHALL 定义为以下任一物理输入：
任意按键、或 IMU 检测到的明显运动（`stillMs()`）。活动 SHALL NOT 使用 `cclink::changed()`
——它在每次心跳解析后都为真（`cclink.cpp:135`），会导致 BLE 连接时永不熄屏。熄屏 SHALL NOT
改变 clawd 的 GIF 状态机（sleep.gif 等照常，只是背光关时不可见）。

#### Scenario: 持续无活动后熄屏

- **WHEN** 设备连续超过熄屏阈值无任何活动（无按键、无明显运动、无新状态）
- **THEN** 固件 SHALL 调 `setBrightness(0)` 关背光
- **AND** SHALL NOT 改变当前 clawd GIF 状态

#### Scenario: 熄屏对在线/离线都生效

- **WHEN** 设备处于离线（Connecting）或在线挂机，且持续无活动超阈值
- **THEN** 固件 SHALL 同样熄屏（熄屏判定不依赖 online，与 sleep.gif 的 online&&idle 条件解耦）

### Requirement: 活动到达时唤醒背光

固件 SHALL 在熄屏状态下，一旦检测到任意按键或 IMU 明显运动，恢复背光到熄屏前的亮度并重置
活动计时。恢复的亮度 SHALL 为熄屏前记录的原亮度，SHALL NOT 硬编码为最大值（避免覆盖
用户/程序设过的亮度）。

#### Scenario: 按键唤醒

- **WHEN** 屏幕已熄，用户按任意键
- **THEN** 固件 SHALL 恢复背光到原亮度
- **AND** SHALL 重置活动计时（不立刻再次熄屏）

#### Scenario: 体感唤醒（tap-to-wake）

- **WHEN** 屏幕已熄，用户拿起或敲击设备（IMU 明显运动）
- **THEN** 固件 SHALL 恢复背光到原亮度

#### Scenario: 唤醒键不被其它逻辑吞掉

- **WHEN** 熄屏状态下的第一次按键用于唤醒
- **THEN** 固件唤醒行为 SHALL NOT 阻止该按键仍按既有 NORMAL/覆盖层语义处理（或按实现明确
  定义「唤醒键是否消费」——本 spec 允许两种，但 SHALL 在 design/实现中写明选择）
