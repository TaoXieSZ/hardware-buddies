## Context

`main.cpp` 主循环每帧算 `online`、读键盘、读 `g_motion`（BMI270），末尾调
`clawd::setSleeping(online && idle && g_motion.stillMs() > STILL_FOR_SLEEP)`（180s 静止→
`sleep.gif`）。`setSleeping()`（`clawd_player.cpp:389`）只存标志 + 切 GIF，**从不碰背光**。
全工程无 `setBrightness`/背光调用（已 grep 确认）。M5GFX 提供
`M5Cardputer.Display.setBrightness(uint8_t)`（0=灭，255=最亮）。`motion.cpp` 已在读 BMI270
加速度并算 `stillMs()`（静止时长）与拿起/摇晃手势——可直接复用做 tap-to-wake，无需新读 IMU。

参考实现：CoreS3/StackChan 的自动息屏（`claude-code-buddy/src/stackchan/`）——「IDLE/SLEEP 态
下无状态 CHANGE 达 N 秒 → 背光关」，用 `g_state_settled_ms` 记 settled 起点，
`wakeScreenIfBlanked()` 唤醒，IMU 加速度阈值 tap-to-wake。cardputer 复用同思路，但用
自己的 `stillMs()`/手势，不引 StackChan 的 settings 层。

## Goals / Non-Goals

**Goals:**
- 无活动超阈值 → 背光关（屏灭），省电 + 护屏。
- 按键 / IMU 明显运动 / 新会话状态到达 → 立刻恢复背光。
- 复用现有 `stillMs()`（IMU 静止时长）+ `motion` 手势 + 按键计时，不新增外设读取。

**Non-Goals:**
- 不做 NVS 可配置超时（用常量）。
- 不做 CPU light/deep-sleep（只关背光，主循环/BLE 继续，才能被按键/体感唤醒）。
- 不改 sleep.gif 触发阈值与语义。
- **不做「收到新审批/问题自动亮屏」**：`cclink::changed()` 每心跳都为真（见 Decisions），
  不能当唤醒源；要做需 prompt/question 上升沿检测，MVP 从简。声音提示（`sound::playEvent`
  在 `play` 事件时响）在息屏态仍会响，用户不会漏掉需关注的事件。

## Decisions

- **⚠️ 活动信号绝不能用 `cclink::changed()`**：实测 `cclink.cpp:135` 在每次心跳成功解析后
  都置 `g_changed=true`（daemon ~1-2s 一次心跳），故 `changed()` 每心跳都为真。若把它当活动/
  唤醒源，BLE 一连着屏就永远息不掉——正是要修的「常亮」。活动只认**物理信号**：任意按键 OR
  IMU 明显运动（`stillMs()` 在运动时自动归零，与心跳无关）。
- **背光层独立于 sleep GIF 层，不复用 `sleeping_`**：sleep.gif 触发条件含 `online && idle`；
  息屏还应覆盖「离线挂机」（`!online` 的 Connecting 态也该能息屏省电）。故息屏判定：
  维护 `g_lastKeyMs`（按键时更新），当 `stillMs() > SCREEN_OFF_MS && now - g_lastKeyMs >
  SCREEN_OFF_MS`（既无运动又无按键达阈值）→ 熄屏。阈值给保守默认（如 `SCREEN_OFF_MS =
  60000`，实现时定、写常量+注释，与 `STILL_FOR_SLEEP` 180s 的关系注明）。
- **背光开关封装成一个 helper**（`main.cpp` 内静态函数或 `clawd::setBacklight(bool)`），
  记录「原亮度」以便恢复（`M5Cardputer.Display.getBrightness()` 取当前值存一次）；
  息屏 `setBrightness(0)`，唤醒 `setBrightness(saved)`。避免硬编码 255（用户可能调过）。
- **唤醒信号两来源**（都是物理输入），任一即唤醒并重置计时：
  1. `keyEvent`（`M5Cardputer.Keyboard.isChange()`，已在每帧算）→ 同时更新 `g_lastKeyMs`；
  2. IMU 运动——复用 `g_motion` 的拿起/摇晃或加速度增量阈值（tap-to-wake），仅在息屏态下判
     （息屏前正常读，省得亮屏时误触）。
  （不含 `cclink::changed()`——见上，每心跳为真会导致永不息屏。）
- **息屏时跳过屏幕合成开销**（可选优化）：背光关时 `clawd::tick()` 的 push 可继续（无害），
  或省略以省 CPU；MVP 先继续 push（最简），息屏只是背光=0。

## Risks / Trade-offs

- [IMU tap-to-wake 阈值太敏感 → 桌面震动误唤醒；太钝 → 敲不醒] → 缓解：复用 `motion.cpp`
  已调过的拿起/摇晃手势阈值（真机验证过的），而非新拍一个阈值；真机调档。
- [关背光时若正好有新审批/问题推来，用户看不到] → 缓解：设备在这些事件上仍会响提示音
  （`sound::playEvent`），听觉不漏；视觉自动亮屏因 `changed()` 每心跳为真而无法简单实现，
  列为 Non-goal（后续可用 prompt/question 上升沿做）。
- [`getBrightness()` 若库未实现 → 无法取原值] → 缓解：真机验证该 API 存在；不存在则存一个
  固定默认（如 128/255）作为恢复值，写进注释。

## Open Questions

- 息屏阈值具体秒数（60s？对齐 180s？）——实现时给个保守默认，真机手感调，非阻塞。
