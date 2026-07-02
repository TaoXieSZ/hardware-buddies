## Context

`clawd_player.cpp` 的 `fileForState()`（约 104-117 行）是个纯硬编码 switch，`AgentState::ToolUse`
分支已经有一个变体轮播先例：`busyVariant_`（0-3 循环）+ `busyNextMs_` 计时器，在 `tick()`
里每 ~2.5s 顺序 `+1 & 3` 切下一个 `busy_N.gif`，`applyTarget()` 重开新变体文件（见
`tick()` 约 480-492 行）。这套机制是现成的，Idle 态要加变体可以照抄形状，但语义不同：
working 是"均匀轮播四个同权重变体"，Idle 要的是"大多数时候还是 idle.gif，偶尔随机
飘一下 idle-reading"（对齐 upstream `clawd-on-desk` 把它标成 "Idle (random)" 而非
"Idle (rotate)"）。

新素材 `clawd-idle-reading.gif` 走 `claude-code-buddy/tools/clawd-gif/`（`build-pack.sh` +
`recrop2.sh`）同款转换方法——同一个上游仓库、同一个 bitbank2 AnimatedGIF 解码器，两条
硬规矩必须照做：全局调色板（`+remap`，否则解码器颜色反相）、按珊瑚色主体 bbox 归一化
（避免角色在不同状态间跳变大小）。那套脚本目标是 Tab5 的 ~148px 大屏，cardputer 现有
资产是 120×80（见 `data/characters/clawd/idle.gif` 实测尺寸），需要另起一份参数（同方法、
换目标框尺寸），不能直接跑 `build-pack.sh` 原样套用。

## Goals / Non-Goals

**Goals:**
- Idle 态偶尔（低频、随机）切换到 `clawd-idle-reading.gif`，停留几秒后切回 `idle.gif`，
  打破长时间单一循环的枯燥感。
- 复用 `busyVariant_`/`busyNextMs_` 同款「计时器 + applyTarget() 重开文件」的机制形状，
  不引入新的架构模式。

**Non-Goals:**
- 不做均匀轮播（不是 `busy_0..3` 那种固定节奏顺序切换）——具体概率/停留时长不做强约束，
  实现时按手感定（用户已确认不纠结数字）。
- 不牵扯 working 态细分（typing/building/juggling）——那需要 cc-bridge 协议改动，用户
  已明确排除，见 proposal Non-goals。
- 不改 Idle 之外任何状态的渲染路径。

## Decisions

- **照抄 `busyVariant_` 的机制形状，但把"顺序 `+1 & 3`"换成"随机小概率切入"**：新增
  `idleVariant_`（0=idle.gif / 1=idle-reading.gif）+ `idleNextMs_` 计时器。在 `tick()`
  里，仅当 `baseState_ == Idle && !sleeping_ && reactionMs_ <= 0` 且到达 `idleNextMs_`
  时判定一次：若当前是 variant 0，小概率（如 15-20%，具体值实现时调）切到 variant 1
  并设一个较短的停留时长（如 4-6s）；若当前是 variant 1，到点必定切回 variant 0。这样
  "大多数时候 idle.gif、偶尔飘一下 reading"的手感不需要额外状态机，跟现有 busy 计时器
  同构，改动量小。
- **【已按实测修正】转换方法不是照搬 `recrop2.sh` 参数，而是量出本项目自己的裁剪约定**：
  最初设想直接套用 `claude-code-buddy/tools/clawd-gif/recrop2.sh` 的 TARGET/FINAL 参数
  （220 方形框、主体只占 ~45%），但实测量出本项目已有 `idle.gif` 的真实内容 bbox 是
  116×68（画布 120×80，四周仅留 2-6px）——跟 Tab5 那套"留大量边距给道具延伸"的约定
  完全不同，cardputer 是紧裁剪风格。改用：PIL 逐帧取非透明像素 bbox 的并集定位内容框
  （而非 `recrop2.sh` 的单帧珊瑚色 mask，因为 idle-reading 的书本道具颜色不是珊瑚色，
  mask 会漏掉道具范围）、加约 6px padding、`magick -coalesce -background black -alpha
  remove -alpha off` 摊平透明为纯黑完整帧（对齐 `086f18c` commit 记录的教训——
  优化过的差分透明帧在真机 GIFDRAW 回调下会花屏，必须是完整帧）、裁剪、`-resize 120x`
  等比缩放、`+remap` 强制单一全局调色板。
- **素材放 `data/characters/clawd/clawd-idle-reading.gif`，跟现有命名风格一致**（其余
  Idle/Thinking 用的都是 `<name>.gif` 或 `clawd-<name>.gif` 混用现状，沿用 upstream
  原名前缀不额外改名，方便对照 upstream 源文件排查问题）。
- **不读 `manifest.json` 的 `states` 字段驱动渲染**：现状 `manifest.json` 只是资产清单
  文档（`fileForState` 纯硬编码 switch，未解析该文件），为保持文档准确仍需同步补一条
  `idle` 数组化记录，但不影响运行时逻辑，也不会给 `manifest.json` 增加"读取驱动"的新
  职责（维持现状分工）。

## Risks / Trade-offs

- [新素材转换参数（bbox 归一化目标框尺寸）没有现成 120×80 版本的脚本可抄，需要自己
  跑一遍 `recrop2.sh` 的方法论并调参] → 缓解：先转一版，真机 flash 对比 idle.gif 的
  显示大小是否一致（角色高矮不应跳变），不一致就调整目标框重转，成本可控（单文件）。
- [随机切换概率/时长凭感觉定，可能出现"太久不出现"或"太频繁像在抽搐"的观感问题]
  → 缓解：先给一个保守低频的初始值，真机跑一段时间观察，用户可随时反馈调整——这本就是
  proposal Non-goals 里明确"不纠结具体数字"的部分。

## Open Questions

（无——scope 小,风险已覆盖,数值细节留给实现阶段迭代)
