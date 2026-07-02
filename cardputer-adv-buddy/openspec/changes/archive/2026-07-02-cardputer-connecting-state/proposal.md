## Why

cardputer 每次开机、BLE 还没连上 cc-bridge 时，`main.cpp` 直接用全零的 `BuddyState` 走
`deriveAgentState()`，落到 `AgentState::Idle → idle.gif`——跟"真的空闲没事干"长得一模
一样。用户拿起刚开机的设备完全看不出它是"正在连"还是"已经连上、真的没事干"，容易误以为
设备坏了或配对失败。这个区分只需要现有的 `online`（`cclink::connected()`）布尔量，
不需要等 cc-bridge 推任何数据，是当前信息里被浪费掉的一个免费信号。

## What Changes

- 新增 `AgentState::Connecting`，映射到新素材 `clawd-carrying.gif`（clawd 扛着箱子的
  形象——upstream 原语义是 "WorktreeCreate"，本次是挪用其"在忙着搬东西"的视觉隐喻表示
  "连接中"，不是跟随 upstream 既有约定）。
- `main.cpp` 主循环：当 `!online` 时短路，直接展示 Connecting 态，不跑
  `deriveAgentState`/多会话轮播那套逻辑（反正 `bs` 此时必然是全零，跑了也没意义）。
- NORMAL 模式新增一行**常驻**文案 `Connecting...`（跟现有 toast 文案统一用英文），
  在 `!online` 期间持续显示，直到连上 cc-bridge 后消失、恢复现有角标/会话标识渲染。

## Capabilities

### New Capabilities
- `connecting-state`: cardputer 在 BLE 尚未连上 cc-bridge 期间，展示区别于「真实
  Idle」的专属视觉（`clawd-carrying.gif` + 常驻 "Connecting..." 文案），连上后恢复
  既有渲染逻辑。

### Modified Capabilities
(none)

## Non-goals

- 只覆盖「BLE 未连接」这一种情况；「已连上 cc-bridge 但暂时没有活跃 Claude 会话」
  （如所有会话都结束了）仍按现状归为真实 Idle，不在本次范围内——用户已明确只要覆盖
  "刚开机没连上"这一段（讨论中的边界①，不含边界②）。
- 不改 cc-bridge / BLE 连接协议本身，纯本地渲染层改动。
- 不影响审批/会话列表/帮助/问答覆盖层的既有优先级——这些覆盖层依赖的数据
  （`promptId`/`hasQuestion`/`sessions[]`）本就只在连上后才可能非空，`!online` 时它们
  天然不会触发，无冲突。

## Impact

- `cardputer-adv-buddy/src/agent_state.h`：新增 `Connecting` 枚举值。
- `cardputer-adv-buddy/src/clawd_player.{h,cpp}`：`fileForState` 新增分支；NORMAL 模式
  合成新增常驻文案渲染位置。
- `cardputer-adv-buddy/src/main.cpp`：`!online` 时的状态派生短路逻辑。
- `cardputer-adv-buddy/data/characters/clawd/`：新增 `clawd-carrying.gif` 资产
  （同款 clawd-gif 转换管线）。
- 不改协议、不改 cc-bridge、不改其它子项目。
