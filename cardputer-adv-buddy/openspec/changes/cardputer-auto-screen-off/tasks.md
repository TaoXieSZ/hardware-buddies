## 1. 背光控制封装

- [x] 1.1 在 `main.cpp`（或 `clawd_player`）加背光 helper：启动时记录原亮度
      （`M5Cardputer.Display.getBrightness()`，真机确认该 API 可用，不可用则存默认 128/255
      并注释）；`screenOff()` 调 `setBrightness(0)`，`screenOn()` 调 `setBrightness(saved)`。
      维护一个 `g_screenOff` 标志防重复调。

## 2. 活动计时 + 熄屏/唤醒

- [x] 2.1 `main.cpp` 主循环加 `g_lastActivityMs`：每帧若 `keyEvent` OR IMU 明显运动
      为真，则更新为 `now` 并（若已熄屏）调 `screenOn()`。**不要**把 `cclink::changed()`
      当活动源（它每心跳都为真，`cclink.cpp:135`，会导致连接时永不熄屏）。
- [x] 2.2 若 `!g_screenOff && now - g_lastActivityMs > SCREEN_OFF_MS`（新常量，给保守默认如
      60000，注释说明与 STILL_FOR_SLEEP 的关系），调 `screenOff()`。
- [x] 2.3 IMU 唤醒：复用 `motion.cpp` 现有拿起/摇晃手势或加速度增量阈值做「明显运动」判定；
      仅在 `g_screenOff` 时参与唤醒（避免亮屏时误重置）。不新拍阈值、沿用已验证的手势阈值。

## 3. 编译 + 真机验证

- [x] 3.1 `pio run -e cardputer-adv` 编译通过。
- [x] 3.2 真机：挂机不动，确认到阈值后**背光熄灭**（屏黑）；`sleep.gif` 状态机不受影响。
- [x] 3.3 真机：熄屏后按任意键 → 亮；拿起/敲设备 → 亮；从电脑发起一个新会话动作
      （cc-bridge 推新状态）→ 自动亮。三种唤醒都验。
- [x] 3.4 真机：确认熄屏→唤醒→再挂机能反复正常循环，不卡死在熄屏；恢复的亮度 = 熄屏前亮度。
- [x] 3.5 `git diff` 确认只动 `cardputer-adv-buddy/`，不碰其它子项目。
