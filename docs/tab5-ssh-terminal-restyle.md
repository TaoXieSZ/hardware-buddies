# Design — Tab5 SSH 终端「buddy 审美」改造蓝图

把 upstream `Tab5_SSH_Client` 的界面从 M5GFX 原生粗糙配色，改成 monorepo buddy dashboard 已跑通的
`th::` 设计系统。两个工作项：**A. 终端 ANSI 调色板**（修「tmux 颜色怪」）+ **B. chrome 重绘**（修「顶部 tab 生硬」）。

> 这是探索期产出的实现蓝图（2026-06-29）。代码改在 clone `/Users/txie/OpenSourceProjects/Tab5_SSH_Client`，
> 蓝图存本 repo（clone 是 MIT 上游，改了即分叉——见末尾 fork 说明）。配套:
> `docs/tab5-ssh-terminal-runbook.md`（怎么 build/flash）。

---

## 根因（两个问题同一来源）

| 问题 | 根因（代码） |
|---|---|
| tmux 颜色怪 | `main.cpp` `terminalColor()` 的 16 基色表 `normal[]` 用 M5GFX 原生 `TFT_*` 常量（TFT_DARKGREEN 脏、TFT_OLIVE 发褐、TFT_GREEN 刺眼）。256 色 cube/灰阶是标准 xterm 公式，**不用动**。 |
| 顶部 tab 生硬 | `main.cpp` `drawHeader()` = `fillRect(...,TFT_DARKGREY)` 扁平灰条 + `drawButton()` = `fillRect`+`drawRect(TFT_LIGHTGREY)` 直角硬边框 + `TFT_DARKGREEN/TFT_MAROON` 硬色动作钮。 |

## 目标设计系统（buddy `th::`，见 `claude-code-buddy/src/tab5/ui.cpp`）

```
BG #0E1116  PANEL #141920  CARD #1C232E  CARD_HI #242D3A(1px lift 边)
ACCENT #D97757 珊瑚  TEXT #E6EDF3  DIM #8B949E  FAINT #4A5562
ERR #F85149  DONE #3FB950  BUSY #4493F8  ATTN #D29922  BTN_OK #2EA043
```

---

## A. 终端 ANSI 16 色板（改 `terminalColor()` 的 `normal[]`）

定一套协调终端配色：语义色锚定 buddy（绿/红/蓝/黄 = DONE/ERR/BUSY/ATTN），其余取 GitHub-dark 同族。
直接用现有 `rgb565(r,g,b)` helper（main.cpp:565）替换 `normal[16]`：

```cpp
static const uint16_t normal[] = {
    rgb565(0x0E,0x11,0x16), // 0  black   = th::BG（终端底色 = buddy 底色，与 chrome 融为一体）
    rgb565(0xF8,0x51,0x49), // 1  red     = th::ERR
    rgb565(0x3F,0xB9,0x50), // 2  green   = th::DONE（tmux 状态栏变好看）
    rgb565(0xD2,0x99,0x22), // 3  yellow  = th::ATTN
    rgb565(0x44,0x93,0xF8), // 4  blue    = th::BUSY
    rgb565(0xBC,0x8C,0xFF), // 5  magenta  (GitHub purple)
    rgb565(0x39,0xC5,0xCF), // 6  cyan     (GitHub cyan)
    rgb565(0xB1,0xBA,0xC4), // 7  white   = 浅前景（非纯白，柔和）
    rgb565(0x4A,0x55,0x62), // 8  br-black = th::FAINT（可见的 "注释灰"）
    rgb565(0xFF,0x7B,0x72), // 9  br-red
    rgb565(0x56,0xD3,0x64), // 10 br-green
    rgb565(0xE3,0xB3,0x41), // 11 br-yellow
    rgb565(0x79,0xC0,0xFF), // 12 br-blue
    rgb565(0xD2,0xA8,0xFF), // 13 br-magenta
    rgb565(0x56,0xD4,0xDD), // 14 br-cyan
    rgb565(0xE6,0xED,0xF3), // 15 br-white = th::TEXT
};
```

附带（同函数/同文件，可选但建议）：
- **默认底色**：ANSI 0 已是 #0E1116，但代码里多处硬编 `TFT_BLACK`（如 393、659、终端清屏）。把终端区域的
  `TFT_BLACK` 统一换成 `rgb565(0x0E,0x11,0x16)`，否则终端底是纯黑、chrome 是 #0E1116，会有一条色差。
- **光标色**：用珊瑚 `rgb565(0xD9,0x77,0x57)`（th::ACCENT）——一眼认得，且把 buddy 签名色带进终端。
  找到画光标的地方（反显 cell）替换。

> 代价：极小（换常量）。收益：高（直接修掉 tmux 抱怨，所有彩色 CLI 统一到 buddy 色系）。**先做这个。**

---

## B. Chrome 重绘（改 `drawHeader()` + `drawButton()`）

### B1. `drawButton()` → 圆角卡片 + th:: 配色
```cpp
// 现状：fillRect + drawRect(TFT_LIGHTGREY) 直角硬边
// 改为：圆角 + 无硬边（或 1px CARD_HI lift 边），默认 th::CARD 底 / th::TEXT 字
void drawButton(const Rect& r, const char* label,
                uint16_t fg = /*th::TEXT*/ rgb565(0xE6,0xED,0xF3),
                uint16_t bg = /*th::CARD*/ rgb565(0x1C,0x23,0x2E)) {
    screenSprite.fillRoundRect(r.x, r.y, r.w, r.h, 6, bg);
    screenSprite.drawRoundRect(r.x, r.y, r.w, r.h, 6, /*CARD_HI*/ rgb565(0x24,0x2D,0x3A));
    screenSprite.setTextColor(fg, bg);
    // 文字居中（label 较短的钮可考虑 setTextDatum 居中而非左对齐 +6）
    ...
}
```

### B2. `drawHeader()` → PANEL 底条 + 珊瑚 active tab + 语义动作钮
```
现状:  [TFT_DARKGREY 扁平灰条]  方块钮  TFT_DARKGREEN/MAROON 硬色
改为:
  ┌──────────────────────────────────────────────────────────┐
  │ ▎TERM   WIFI   SSH   FONT   CONF              ● CONN       │  ← PANEL #141920 底
  │ └珊瑚3px accent rail┘  DIM字  圆角  绿点=连上  圆角语义钮   │
  └──────────────────────────────────────────────────────────┘  ← 底部 1px CARD_HI 分隔线
```
具体：
- **底条**：`fillRect(0,0,W,HeaderH, th::PANEL)`，底部加 `drawFastHLine(0,HeaderH-1,W, th::CARD_HI)` 做分隔。
- **导航 tab（TERM/WIFI/SSH/FONT/CONF）**：不再用方块钮。
  - 当前屏（active）：文字 `th::TEXT` + 底部 3px 珊瑚 rail（`fillRect(x, HeaderH-3, w, 3, th::ACCENT)`）。
  - 非 active：文字 `th::DIM`，无 rail。
  - （对齐 buddy dashboard 的 active-session-tab：accent rail + 全对比文字。）
- **动作钮（CONN/DISC/SAVE/DEL）**：用 B1 的圆角 `drawButton`，颜色走语义：
  - CONN/SAVE → bg `th::BTN_OK` #2EA043；DISC/DEL → bg `th::ERR` #F85149；字 `th::TEXT`。
- **状态文字（WiFi ok / SSH ok）**：`th::DIM`；连上时前面加个小圆点 `th::DONE` 绿（`fillCircle`），断开 `th::FAINT`。

> 代价：中（圆角绘制 + active 态 + 配色）。收益：高（chrome 天天盯）。布局坐标（HeaderH=44、各 Rect）基本不动，**只换绘制方式和颜色**，风险可控。

---

## 要改的文件 / 锚点

| 项 | 文件 | 函数/位置 |
|---|---|---|
| A 调色板 | `Tab5_SSH_Client/src/main.cpp` | `terminalColor()`（~570 行）的 `normal[16]` |
| A 默认底色/光标 | 同上 | 终端区 `TFT_BLACK` 硬编处（393/659/清屏）+ 光标反显处 |
| B1 钮样式 | 同上 | `drawButton()`（fillRect→fillRoundRect + th:: 默认色） |
| B2 header | 同上 | `drawHeader()`（底条/tab/动作钮/状态） |

可加一个 `namespace th { ... }`（照抄 buddy ui.cpp 的常量）放 main.cpp 顶部，避免散落硬编 rgb565。

---

## 验证

1. `IDF_GITHUB_ASSETS=dl.espressif.cn/github_assets pio run -e tab5 -t upload --upload-port /dev/cu.usbmodem1401`
2. **A**：SSH 进去 `tmux` —— 状态栏绿应是柔和的 #3FB950，`claude` TUI 的彩色不刺眼、底色与 chrome 一致。
   `ls --color` / `vim` / 跑个 256-color 测试脚本看 cube 正常。
3. **B**：顶栏 PANEL 底 + 当前 tab 珊瑚 rail + 圆角钮 + 连上时绿点。对照 buddy dashboard 观感是否一族。
4. 回归：各屏（WIFI/SSH/FONT/CONF/Terminal）切换、按钮点击命中区不变（HeaderTouchH 没动）。

## Fork / 维护说明

`Tab5_SSH_Client` 是 MIT 上游 clone，本改造让它分叉。建议：在 clone 里 `git checkout -b buddy-aesthetic`
把改动单独成 commit，方便日后 `git pull upstream` 后 rebase/cherry-pick。本蓝图（在 monorepo）是 source of truth，
clone 丢了也能照着重做。
