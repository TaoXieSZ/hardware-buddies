# Clawd 随机头像设计

日期：2026-08-11

## 目标

用 `cardputer-adv-buddy` 已验证的小尺寸 Clawd GIF 替换全屏猫脸。素材保持原始像素尺寸，不放大、不插值；播放器负责在底部面板上方居中，并保证任何扫描线都不会写入面板。

## 资产来源

来源目录：`../cardputer-adv-buddy/data/characters/clawd/`。

复制 15 个 GIF 到 `data/characters/clawd/`：

- 固定状态：`sleep.gif`、`clawd-notification.gif`。
- 随机池：`idle.gif`、`clawd-idle-reading.gif`、`busy_0.gif`、`busy_1.gif`、`busy_2.gif`、`busy_3.gif`、`attention.gif`、`celebrate.gif`、`clawd-thinking.gif`、`clawd-carrying.gif`、`error-120.gif`、`dizzy.gif`、`heart.gif`。

不复制 `celebrate_1.gif`：它是 220×198，超过这组原尺寸小头像的布局边界。

## 状态映射

| `AgentState` | 选择语义 |
| --- | --- |
| `STATE_IDLE` | 进入状态时从 13 项随机池选择一次 |
| `STATE_THINKING` | 进入状态时从 13 项随机池选择一次 |
| `STATE_REPLYING` | 进入状态时从 13 项随机池选择一次 |
| `STATE_ERROR` | 进入状态时从 13 项随机池选择一次 |
| `STATE_REMINDER` | 固定 `clawd-notification.gif` |
| `STATE_SLEEP` | 固定 `sleep.gif` |

“进入状态”以 `gifFaceSetState()` 收到不同于当前状态的值为准。同一状态内 GIF 播放结束时只重新打开当前文件，不重新抽选。随机池会跳过上一次抽中的索引，因此相邻两次随机状态进入不会显示同一个 GIF；其余 12 项条件等概率。

## 原尺寸居中与裁切

所有复制资产最大为 120×135，小于当前 320×148 可见头像区。打开 GIF 后计算：

```text
visibleHeight = g_bandY in 1..240 ? g_bandY : 240
originX = (320 - gifWidth) / 2
originY = (visibleHeight - gifHeight) / 2 + yNudge
```

当 GIF 高度小于可见区时，`originY` 还会限制在 `0..visibleHeight-gifHeight`，确保微调不会把素材推出头像区。

- GIF 不缩放，保留像素画锐利边缘。
- `drawCb()` 把 GIF 局部坐标平移到 `originX/originY`，并裁掉屏幕外列。
- `ucDisposalMethod == 2` 时，透明索引按 AnimatedGIF 契约恢复为 GIF 背景色；其他透明帧继续跳过透明 run，避免残影。
- `y >= g_bandY` 的扫描线直接丢弃，底部倒计时/提醒面板拥有绝对绘制权。
- `gifFaceSetYOffset()` 保持 API 兼容，但只接受 ±8px 微调；`main.cpp` 的头像偏移设为 0。
- 切换状态时先清空可见头像区；同一 GIF 循环重开不清屏，避免循环边界闪烁。

## 故障回退

选中的随机资产打开失败时：

1. 串口输出失败路径与 AnimatedGIF 错误码。
2. 输出回退提示。
3. 尝试打开 `/characters/clawd/idle.gif`。
4. idle 也失败时输出第二条错误并停止播放，不触碰底部面板。

固定资产和循环重开也使用同一防护路径。

## 测试清单

- 资产目录恰好包含上述 15 个 GIF，不含 `celebrate_1.gif` 和旧 `cat/`。
- 随机池源码恰好 13 项，固定状态不进入随机池。
- 连续多次状态切换不重复上一次随机索引；同一状态循环不重新抽选。
- 每个 GIF 尺寸不超过 120×135，居中后位于 320×148 可见区内。
- `drawCb()` 对 `y >= g_bandY` 不写屏，并正确处理水平裁切、透明色 run 和 disposal=2 背景恢复。
- 缺失任一随机文件时串口记录错误并回退 `idle.gif`。
- 后续获准构建时运行 `~/.platformio/penv/bin/pio run -e cores3-standup`；本次按任务约束不构建、不烧录。
