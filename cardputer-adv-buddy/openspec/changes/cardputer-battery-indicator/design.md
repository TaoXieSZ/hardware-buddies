# Design — cardputer-battery-indicator

## 背景：cardputer-ADV 电源能力实查（探索期结论）

依据本项目 `.pio/libdeps/cardputer-adv/M5Unified/src/utility/Power_Class.cpp` 与 `M5Unified.cpp`（实库源码，非记忆）：

| 能力 | 状态 | 依据 |
|---|---|---|
| 电量 % | ✅ 可靠 | cardputer `_pmic=pmic_adc`；`getBatteryLevel()` 的 `pmic_adc` 分支 `mv=_getBatteryAdcRaw()*_adc_ratio`→level |
| 电池电压 | ✅ | `getBatteryVoltage()` 同 ADC 路径 |
| 充电中? | ❌ | `isCharging()` default-switch 有 M5PaperMono/StickS3/Tab5… 无 Cardputer → `return charge_unknown` |
| USB 插入? | ❌ | cardputer-ADV power-init 不挂 CHG_STAT 引脚（对比 M5PaperS3 同 pmic_adc 却挂 `M5PaperS3_CHG_STAT_PIN`）；无 VBUS sense ADC |
| USB host? | ⚠️ 半 | ESP32-S3 USB-JTAG 可探 SOF（host 在），但探不到纯充电器供电 |

范本：`M5Unified/examples/Basic/HowToUse/HowToUse.ino:500`
```cpp
int battery = M5.Power.getBatteryLevel();   // 0–100，-1=unknown/error
if (prev_battery != battery) { ... if (battery >= 0) { /* 显示 */ } }
```

## D1 — 电量角标位置（默认，待审批可改）

顶栏（y=0..13，240px）现状：左 = session tag `proj [2/5]`（`drawSessionTag`，0x8410），右 ~40px = `T/R` 会话角标（`drawBadge`，`%d/%d` total/running）。

**默认方案**：电量画在**顶栏最右**，`T/R` 角标左移，二者共存于右侧加宽区（~44px→~64px）。

```
现状:  proj [2/5]                         3/1
默认:  proj [2/5]                    3/1  85%
                                     └badge┘└bat┘  右对齐，bat 最右
```

- 备选 1（审批时若嫌挤）：电量替代 `T/R`，二者轮显 / 只在某 displayMode 显示。
- 备选 2：电量画成小电池图标（更省横向像素，~12px），数字省略。
- 取舍：默认走「数字共存」最直白；中文 session label 较长时右侧两个角标可能逼近，gating spike #2 真机看一眼。

## D2 — 只在 NORMAL 顶栏显示

APPROVAL / QUESTION / SESSIONS / HELP / KEYMAP 都 `fillSprite(BG)` 整屏重绘，不画顶栏角标。电量同 `T/R` 角标，仅 NORMAL（clawd GIF + 顶栏）出现——即桌面主视图。覆盖态是瞬态，无需电量。实现上电量绘制挂在 `drawBadge` 同级、同样的 NORMAL 守卫下。

## D3 — 轮询与重绘

- 轮询周期 30s（对齐 `claude-code-buddy` StackChan CoreS3：`M5.Power.getBatteryLevel()` 每 30s）。ADC 读不宜每帧。
- 存 `g_batPct`（int8，-1=unknown）。仅当跨整数百分位或跨色档变化才置 HUD dirty → lazy 重绘，不每 30s 强刷。
- 首次读在 `clawd::begin()` 后、BLE 连接前即可，不依赖 daemon。

## D4 — 三色档

| 档 | 区间 | 颜色（RGB565） | 含义 |
|---|---|---|---|
| 高 | ≥50% | 绿 0x07E0 | 健康 |
| 中 | 20–49% | 黄 0xFFE0 | 留意 |
| 低 | <20% | 红 0xF800 | 该充了 |
| 未知 | <0 | 不显示 | ADC 读失败/无电池信息 |

颜色与现有 HUD 配色不冲突（agent 标记用的是另一组色）。

## 为什么不做 USB 区分（呼应 proposal Non-goals）

三条潜在 USB 判据都不干净：
- **isCharging()**：cardputer-ADV 恒返回 `charge_unknown`，废。
- **电压阈值**（V>~4.15V→USB）：满电刚拔会误判 USB 几分钟，需真机标定，不可靠。
- **USB-JTAG host（SOF）**：能覆盖「插电脑」这一最常见场景，但漏「只接充电器」，且要碰 S3 寄存器。

任一都该自带 spike + 真机标定，属于独立问题域。本 change 先把确定可靠的电量做掉，USB 区分留作后续 change（若仍需要）。
