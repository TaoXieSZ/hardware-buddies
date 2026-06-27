# Tasks — cardputer-battery-indicator

## Spike（开工前，真机）
- [ ] S1. 真机读 `M5.Power.getBatteryLevel()` 串口打印，确认返回 0–100 合理值（非恒 -1 / 恒 100）。参照 `M5Unified/examples/Basic/HowToUse/HowToUse.ino:500`。若恒 100 或恒 -1 → ADC 标定问题，先停下来对齐库源码 `_adc_ratio`。
- [ ] S2. 临时把电量打到屏幕角落，肉眼看 `85%` + `T/R` 角标 + 左侧 session label 在 240px 顶栏不重叠（验 design D1 默认布局）。

## 实现
- [ ] 1. **读电量**（`src/main.cpp`）：加 `g_batPct`（int8，初值 -1）；主循环每 30s `M5.Power.getBatteryLevel()` 写入；跨整数百分位/跨色档才置 HUD dirty。验证：`pio run` 编译过 + 串口打印电量随时间变化。
- [ ] 2. **画角标**（`src/clawd_player.cpp`）：在 `drawBadge` 同级、NORMAL 守卫下加 `drawBattery()`：顶栏最右画 `%d%%`，色按 design D4 三色档，`g_batPct < 0` 不画；`T/R` 角标按 D1 左移共存。验证：真机 NORMAL 屏右上同时见电量+会话角标。
- [ ] 3. **三色档**（`clawd_player.cpp`）：≥50 绿 / 20–49 黄 / <20 红。验证：真机（或调电量阈值临时硬编码）看三档颜色正确。
- [ ] 4. **lazy 重绘自检**：确认电量不变时不会每 30s 强刷整屏 / 不打断 clawd GIF 播放。验证：真机静置观察 GIF 流畅、无周期性闪烁。

## 验证收尾
- [ ] 5. 真机端到端：拔 USB 纯电池跑，电量数字合理且随放电缓慢下降；插 USB 充一会儿，电量回升（注意：本 change 不显示「USB」字样，插电时就是显示升高的电量%——这是预期，非 bug）。
- [ ] 6. 回归：审批/问答/会话列表/help 全屏态不画电量、不串色；NORMAL 顶栏 session 轮播+电量并存不打架。

## 备注
- 纯固件改动，无 daemon / 线协议变更。
- 改动文件仅 `src/main.cpp` + `src/clawd_player.cpp`（含必要的 `.h` 声明）。
- 用户看不了 cpp：先批本 change 的 spec/design，再实现+烧录（项目规矩 review-via-openspec-not-code）。
