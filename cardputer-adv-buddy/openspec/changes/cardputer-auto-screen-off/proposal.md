## Why

Cardputer 的屏幕现在**永不熄灭**——即使进入 sleep 态（`sleep.gif`），背光仍全亮。`setSleeping()`
（`src/clawd_player.cpp:389`）只切换 GIF，从不碰背光；`main.cpp` 全程没有任何
`setBrightness`/背光控制。长时间挂机时屏幕一直点亮，既伤屏（LCD 老化/残影）又费电。
同目录的 CoreS3/StackChan 固件早有「自动息屏」（settled 计时 + 背光关 + tap-to-wake），
cardputer 该补上同类能力。

## What Changes

- 在既有 sleep 机制之上叠加**自动息屏**：设备在 IDLE/SLEEP 态持续无状态变化超过阈值后，
  调 `M5Cardputer.Display.setBrightness(0)` 关背光（屏灭）。
- **唤醒**：任意按键、IMU 检测到明显运动（拿起/敲击，复用现有 `motion.cpp` 的 BMI270 读取）、
  或收到新的会话状态变化（BLE 推送）时，恢复背光到原亮度。
- 息屏与现有 `sleep.gif` 解耦但协同：sleep.gif 仍照常播（背光关时不可见、开时立刻可见），
  息屏只管背光开关，不改 GIF 状态机。

## Capabilities

### New Capabilities
- `auto-screen-off`: 设备在持续无活动后关闭背光熄屏，按键/体感/新状态到达时唤醒，
  降低 LCD 老化与耗电。

### Modified Capabilities
(none — 现有 sleep 是 GIF 层行为，无独立 spec；本 change 新增背光层，不改 sleep 语义)

## Non-goals

- **不做可配置超时的 NVS 持久化**：cardputer 现在没有 settings/NVS 层（不像 StackChan 的
  `settings.cpp`），本次息屏阈值用编译期常量（对齐现有 `STILL_FOR_SLEEP` 的做法），
  需要可调再另开 change。
- **不做深睡/关机省电**（light-sleep/deep-sleep CPU 休眠）：只关背光，主循环/BLE 继续跑，
  保证能被新状态唤醒。CPU 级省电是后续独立议题。
- **不改 sleep.gif 触发逻辑本身**（`STILL_FOR_SLEEP` 180s）——息屏是叠加的背光层。

## Impact

- `cardputer-adv-buddy/src/main.cpp`：新增「settled 无活动」计时 + 到阈值 `setBrightness(0)`；
  按键/IMU 运动/状态变化时 `setBrightness(<原值>)` 唤醒 + 重置计时。
- `cardputer-adv-buddy/src/clawd_player.{h,cpp}`（可能）：暴露一个背光开关/亮度设置的小接口，
  或直接在 main.cpp 用 `M5Cardputer.Display.setBrightness`。
- 不改协议、不改 cc-bridge、不动其它子项目。
