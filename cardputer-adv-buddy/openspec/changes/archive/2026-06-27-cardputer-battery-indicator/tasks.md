# Tasks — cardputer-battery-indicator

## Spike（开工前，真机）
- [x] S1. ~~串口打印~~ 改用屏幕直读：真机 NORMAL 顶栏显示电量，拔 USB 后**数字随放电下降**（用户实测「数字有在减少」）→ 确认 `getBatteryLevel()` 是真 ADC 读数，非恒 -1 / 恒 100。
- [x] S2. 真机 `100%` + `T/R` 角标 + 左侧 session label 在 240px 顶栏并存、不重叠（design D1 默认布局，用户实测无挤压反馈）。

## 实现
- [x] 1. **读电量**（`src/main.cpp`）：主循环每 30s `M5.Power.getBatteryLevel()` → `clawd::setBattery()`。`pio run` 编译过 + 真机数字随放电变化。
- [x] 2. **画角标**（`src/clawd_player.cpp`）：`drawBadge()` NORMAL 守卫下，顶栏最右画 `%d%%`（D4 三色档），`batPct_ < 0` 不画；`T/R` 角标左移共存（D1）。真机右上同时见电量+会话角标。
- [x] 3. **三色档**（`clawd_player.cpp`）：≥50 绿 / 20–49 黄 / <20 红。满电观测到绿档；黄/红为同一阈值表达式分支（编译验证，未在真机走到低电）。
- [x] 4. **lazy 重绘自检**：电量不变不强刷。真机静置 GIF 流畅、无周期性闪烁（用户无闪烁反馈）。

## 验证收尾
- [x] 5. 真机端到端：拔 USB 纯电池跑，电量数字随放电缓慢下降（用户实测通过）。插电时显示升高的电量%、不显「USB」=预期。
- [x] 6. 回归：电量绘制仅在 `drawBadge` 的 NORMAL 路径，覆盖态各自 `fillSprite` 整屏重绘不调 `drawBadge`（代码守卫保证；真机未逐态走查，低风险）。

## 验收结论（2026-06-26 真机）
- 两个 gating spike 全过：电量为真 ADC 读数（拔 USB 数字下降）、顶栏三元素无重叠。
- 直接观测：满电 100% 绿档显示、放电下降、无闪烁、无挤压。
- 未直接观测（低风险、代码/编译保证）：黄/红档配色、各全屏覆盖态不画电量。

## 备注
- 纯固件改动，无 daemon / 线协议变更。
- 改动文件仅 `src/main.cpp` + `src/clawd_player.cpp`（含必要的 `.h` 声明）。
- 用户看不了 cpp：先批本 change 的 spec/design，再实现+烧录（项目规矩 review-via-openspec-not-code）。
