## Context

`main.cpp` 主循环无条件跑 `deriveAgentState(bs)` + 多会话轮播（约 309-345 行），`!online`
时 `bs` 是 setup() 时默认构造的全零 `BuddyState`，`deriveAgentState` 落到
`AgentState::Idle`——跟真实空闲视觉上无法区分。`online`（`cclink::connected()`）本身
已经是每帧都在算的布尔量，只是没被用来门控渲染分支。

顶栏左侧目前有一块"会话标识"专用区：`clawd_player.cpp` 的 `drawSessionTag()`（约
172-193 行）在 `rotTotal_ <= 0 || !rotTag_[0]` 时直接 `return`（不画），有数据时清一次
`(0,0)-(canvasW-66,13)` 矩形条再画 `efontCN_12` 字体的会话名。`!online` 期间
`rotTotal_` 必然是 0（没有会话数据），这块区域天然空着——正好是常驻 "Connecting..."
文案的位置，不需要新开一块画布区域。

## Goals / Non-Goals

**Goals:**
- `!online` 期间清楚区分于真实 Idle：专属 GIF（`clawd-carrying.gif`）+ 顶栏左常驻
  "Connecting..." 文案。
- 复用现有顶栏左侧空白区（`drawSessionTag` 的绘制区域/清除矩形），不新增画布分区。
- 连上 cc-bridge 后无缝恢复现状（角标/会话标识/轮播）。

**Non-Goals:**
- 不处理「已连接但零活跃会话」（proposal Non-goals 已明确排除，边界②留给以后如果
  真有需要再开新 change）。
- 不改 BLE/cc-bridge 连接协议或握手逻辑，纯本地渲染层。
- 不影响 APPROVAL/SESSIONS/HELP/QUESTION 覆盖层的优先级判断——这些覆盖层的触发数据
  （`bs.promptId`/`bs.hasQuestion`/`bs.sessions`）在 `!online` 时必然是初始零值，
  天然不会触发，`Connecting` 只影响 NORMAL 模式的基础层渲染。

## Decisions

- **【已按实测修正】素材转换方法沿用 `cardputer-idle-variety` change 里实测修正过的流程，
  不是 `claude-code-buddy/tools/clawd-gif/recrop2.sh` 的 Tab5 220 方形参数**：详见该
  change design.md 的对应 Decision——量出本项目 `idle.gif` 的真实紧裁剪约定（内容几乎
  填满画布），PIL 逐帧取非透明像素 bbox 并集定位内容框、`magick -coalesce -background
  black -alpha remove -alpha off` 摊平透明为纯黑完整帧、裁剪、`-resize 120x`、
  `+remap` 强制单一全局调色板。`clawd-carrying.gif` 最终 120×96、45 帧、100KB。

- **状态放进 `AgentState` 枚举而非新开一个渲染 mode**：`clawd_player.h` 顶部注释写明
  四种 mode（NORMAL/APPROVAL/SESSIONS/HELP）的优先级关系，Connecting 不是键盘交互式
  覆盖层，只是 NORMAL 模式下 GIF 内容 + 顶栏文案的替换，加一个 `AgentState::Connecting`
  枚举值、复用 NORMAL 渲染管线（`fileForState`/`drawBadge`/顶栏文案）比新增一整个
  mode 更省，也符合"最简方案优先"。
- **顶栏文案复用 `drawSessionTag()` 的绘制区域，而非新建一个 `drawConnecting()` 画布区**：
  `!online` 时 `rotTotal_` 恒为 0，`drawSessionTag()` 现在直接跳过不画——把判断顺序
  改成"若 `!online` 则画 Connecting 文案；否则走原 `rotTotal_`/`rotTag_` 判断"，同一块
  矩形、同一次清除，零新增画布占用。
- **门控点选在 `main.cpp` 主循环顶部（`online` 刚算出来之后）**：`if (!online) { 走
  Connecting 分支，跳过 deriveAgentState/轮播 } else { 现有逻辑不变 }`，改动集中在
  一处 if/else，不用在 `deriveAgentState` 内部塞 `online` 参数（那个函数目前是纯
  `BuddyState→AgentState` 的映射，不该扎入连接状态这个额外维度）。
- **文案用英文 `Connecting...`**：跟现有 toast 文案（`sent: backspace`/`voice sent`/
  `REC...`）风格一致（用户已确认「保持一致」）。

## Risks / Trade-offs

- [`drawSessionTag()` 改动可能影响真实会话标识的既有绘制路径] → 缓解：只在函数入口
  加一个 `!online` 的早分支（画 Connecting 后 return），原有 `rotTotal_`/`rotTag_`
  分支逻辑完全不动，行为等价于"多了一个更早的 return 分支"。
- [`clawd-carrying.gif` 是挪用 upstream "WorktreeCreate" 语义的素材，第一次看到的人
  可能困惑"为什么连接中显示扛箱子"] → 接受的权衡：proposal 里已明确写清这是自定义
  映射非跟随上游约定；后续若观感不合适，换素材只需改 `fileForState` 一行 + 换文件，
  不影响其余设计。

## Open Questions

（无——scope 小,边界已在讨论中钉死为仅覆盖 `!online`)
