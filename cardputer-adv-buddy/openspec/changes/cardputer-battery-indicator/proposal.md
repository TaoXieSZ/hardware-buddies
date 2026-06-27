## Why

桌宠摆在桌上，用户想**一眼看出它还有多少电、要不要充**，而不用拿起来戳屏幕或查别处。cardputer-ADV 自带 1750mAh 电池，但固件现在**完全不读电量**（`src/` 里 0 处 power 代码）。加一个常驻的电量角标，是性价比最高的体感提升。

本 change 只做**方案 A：显示电池电量百分比**。用户最初设想「没插 USB 显示电量、插了显示 USB」，但探索发现 **USB 检测在这块板子上开箱即死**（见下「关键发现」），故 USB 区分本轮明确不做、留作后续。

## 关键发现（探索期，写入 design.md 详档）

对照 M5Unified 库源码（`utility/Power_Class.cpp`，本项目 `.pio` 实库）确认 cardputer-ADV 的电源能力：

- **电量 % 能读、可靠**：cardputer 的 `_pmic = pmic_adc`，`getBatteryLevel()` 走 ADC 读电池电压→换算 level（范本 `M5Unified/examples/Basic/HowToUse/HowToUse.ino:500`：`int battery = M5.Power.getBatteryLevel(); if (battery >= 0) ...`）。
- **USB 插没插测不出**：`isCharging()` 的实现里有 M5PaperMono / StickS3 / Tab5 等板的 case，**唯独没有 Cardputer** → 落到 `default: return charge_unknown`。cardputer-ADV 的 power-init **不挂任何 CHG_STAT 引脚**（对比 M5PaperS3 同为 pmic_adc 却 `pinMode(M5PaperS3_CHG_STAT_PIN, input)`），也无 VBUS sense ADC。ESP32-S3 原生 USB-JTAG 只能探到 USB **host**（SOF 包），探不到「纯充电器供电」。

结论：电量是确定能做的；USB 区分是另一个问题域（需选检测策略 + 真机标定），不该拖住电量显示。

## What Changes

- **固件读电量（小改，纯新增）**：`main.cpp` 主循环每 ~30s `M5.Power.getBatteryLevel()`，存入一个变量，`< 0`（unknown/error）时不显示。频率参照 StackChan（`claude-code-buddy` CoreS3 固件每 30s 轮询）避免 ADC 开销。
- **固件画角标（小改）**：`clawd_player.cpp` 在 **NORMAL（idle/agent HUD）顶栏**画电量。电量变化（跨整数百分位/跨色档）才 lazy 重绘，沿用现有 HUD dirty 思路。
- **位置（待审批确认，design D1 给默认）**：顶栏最右画 `85%`，把现有 `T/R` 会话角标左移共存；右侧保留区由 ~44px 加宽到 ~64px。
- **三色档（低风险，含在内）**：≥50% 绿 / 20–49% 黄 / <20% 红，红色即「该充了」的视觉信号。

## Non-goals

- **不做 USB / 充电检测**：cardputer-ADV 硬件/库不暴露 USB-present（见关键发现）。「插 USB 显示 USB」留作独立后续 change（届时需选 host-SOF / 电压阈值策略 + 真机标定）。
- **不在全屏覆盖态画电量**：APPROVAL / QUESTION / SESSIONS / HELP / KEYMAP 各自整屏重绘，电量只在 NORMAL 顶栏出现（桌面主视图）。
- **不加 daemon 侧电量上报 / dashboard**：纯设备端显示；cardputer 当前不发 telemetry，本轮不引入。
- **不做低电告警 / 自动休眠 / 充电曲线**：只显示数字+颜色。

## Capabilities

### New Capabilities
- `power-indicator`：cardputer 在 NORMAL 顶栏常驻显示电池电量百分比（ADC 读取，三色档，lazy 重绘），unknown 时静默不显；不含 USB/充电状态区分。

### 依赖
- 与现有 capability 解耦——电量角标只读 `M5.Power`，不碰 agent 状态/会话/BLE 链路。与 `T/R` 会话角标（`cardputer-session-overview`）共享顶栏右侧空间，需协调像素布局（design D1）。

## ⚠️ Gating spike（开工前必须真机验）

1. **getBatteryLevel() 在 cardputer-ADV 返回真值**：真机读一次，确认返回 0–100 的合理值（不是恒 -1 / 恒 100）。库源码已确认走 ADC 路径，但 ADC 标定（`_adc_ratio`）是否对该板准确需真机看。
2. **顶栏像素不撞车**：`85%` + `T/R` + 左侧中文 session label 在 240px 顶栏共存不重叠（design D1 的默认布局真机看一眼）。
