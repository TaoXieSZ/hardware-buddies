> 进度说明：代码 + `pio run` 编译验证可达的任务已勾选；**纯真机验证**（板子尚未到货）保留未勾，集中在 1.4 / 4.4 / 5.4，板子到货烧录后即可逐项确认。

## 1. avatar 集成 spike（先解最大风险 R1）

- [x] 1.1 在 `platformio.ini` 的 `lib_deps` 加入 m5stack-avatar：pin `https://github.com/meganetaaan/m5stack-avatar.git#v0.10.0`（git ls-remote 核对 tag 存在，注释写明出处）
- [x] 1.2 对照 m5stack-avatar `examples/` + `src/{Avatar,Expression,ColorPalette}.h` 逐字核对 API；结论记入 design.md Open Questions（自起绘制线程、用全局 M5.Display、无需手传 display 实例）
- [x] 1.3 最小 spike：`M5Cardputer.begin(cfg,true)` + `avatar.init()` 显示默认脸，`pio run -e cardputer-adv` 编译通过（链接 libM5Stack-Avatar.a，SUCCESS）
- [ ] 1.4 真机验证：脸在 240x135 横屏完整不裁切、会眨眼呼吸；若不行则按 R1 用 `setScale/setPosition` 调，仍不行回退「自绘简化脸」并在 design.md 记录（架构不变）〔待板子到货〕

## 2. 状态源抽象与现有状态接入

- [x] 2.1 新增 `src/state_source.h`：定义 `StateSource` 抽象接口（`update`/`state`/`consumeChanged`）
- [x] 2.2 把键盘自测逻辑抽成 `KeyboardStateSource`（任意键循环切换 AgentState），main 通过接口消费，行为与 Phase 1 一致
- [x] 2.3 `pio run` 编译通过（键盘读取路径与 Phase 1 同源，真机切换行为不变）

## 3. pet-avatar：状态→表情映射（满足 pet-avatar spec）

- [x] 3.1 新增 `src/pet_avatar.{h,cpp}`：封装 avatar，暴露 `setState(AgentState)`
- [x] 3.2 五态→{Expression, 嘴开度, 强调色, 台词, 视线} 映射表（各唯一可区分；APPROVAL 用 Sad+视线上扬+「approve me?」恳求语）
- [x] 3.3 `setSpeechText` 显示台词；render 仅在状态/睡眠/reaction 变化时调用（避免每帧重绘 → 切换即时、无整屏闪烁，满足 spec 第 3 条）
- [x] 3.4 main `loop()` 接上：状态源变化 → `setState`；`pio run` 通过（真机逐态目检留作板到后确认）

## 4. motion-interaction：BMI270 手势（满足 motion-interaction spec）

- [x] 4.1 对照 M5Unified `examples/Basic/Imu` 核对 `M5.Imu.update()/getImuData().accel`/`getType()`；`Motion::begin()` 开机串口打印 IMU 类型自检（期望 bmi270）
- [x] 4.2 新增 `src/motion.{h,cpp}`：采样加速度幅值，低通+阈值+时间窗判定 `{PickedUp, Shaken}` + 静止时长，阈值集中为常量
- [x] 4.3 接入反应：PickedUp→唤醒看向用户(700ms 回落)、Shaken→900ms 激灵后回落、IDLE 且连续 30s 静止→睡眠；运动或离开 IDLE 立即醒
- [ ] 4.4 真机标定阈值（区分拿起 vs 晃动）+ 逐场景目检〔待板子到货；可用 `-DPET_DEBUG_OVERLAY=1` 串口看 still/accel〕

## 5. pet-mood：心情模型（满足 pet-mood spec）

- [x] 5.1 新增 `src/mood.{h,cpp}`：有界心情值（0–100）DONE 提升 / 长 IDLE 缓降 + anxiety（0–1）久等 APPROVAL 上升、离开回落，全程 clamp
- [x] 5.2 `pet_avatar.applyMood(happy01, anxiety01)`：用心情/焦虑调制视线（满足「眨眼频率或视线」之一），不覆盖状态主表情类别
- [x] 5.3 main 把会话事件+时间喂 mood，再把 mood 喂 avatar
- [ ] 5.4 真机验证：同一状态下高/低心情的神态有可观察差异，主表情类别不变〔待板子到货〕

## 6. 收尾与文档

- [x] 6.1 调试改为编译期开关 `-DPET_DEBUG_OVERLAY=1` 的**串口**输出 state/mood/still/imu（屏幕被 avatar 线程独占，屏上叠层会被覆盖，已在 design 记录）
- [x] 6.2 更新 `README.md`：Phase 1 文字 HUD → 电子宠物，补 m5stack-avatar 出处、模块结构、调试与 roadmap
- [x] 6.3 全量 `pio run -e cardputer-adv` 绿灯（Flash 14.3% / RAM 6.9%）；确认 `.pio/` 被忽略，仅源码+openspec 纳入版本控制
- [x] 6.4 `openspec validate cardputer-coding-pet --strict` → valid
