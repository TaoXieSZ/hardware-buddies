## Why

clawd 在 Idle 态目前永远只播 `idle.gif` 一个循环，一放就是几十分钟甚至几小时（没工具跑、没审批、没会话——桌搭最常待的状态）。working 态早就有 `busy_0..3` 四变体轮播避免枯燥，Idle 态却没有，desk pet 停在原地不动的时间占比最高、也最该有点生命感。upstream（`clawd-on-desk`）本就把 "Idle (random)" 列成正式状态，配的 `clawd-idle-reading.gif`（戴眼镜捧本小书）跟现有素材是同一贴身构图，不是会把角色挤小的"桌面场景"宽幅姿势，能直接走现成的全局调色板+主体归一化管线转换。

## What Changes

- Idle 态新增第二个 GIF 变体 `clawd-idle-reading.gif`：大多数时间仍播 `idle.gif`，间或（低权重、随机）切到 idle-reading 停留几秒再切回，呼应 upstream "Idle (random)" 的语义，不做 `busy_0..3` 那种均匀顺序轮播（避免跟 working 态观感撞衫）。
- 新增素材 `clawd-idle-reading.gif` 需经 `claude-code-buddy/tools/clawd-gif/` 同款管线（全局调色板 `+remap` + 珊瑚色主体 bbox 归一化）转换到 cardputer 的 120×80 尺寸，放入 `data/characters/clawd/`，`buildfs` 打进 littlefs。

## Capabilities

### New Capabilities
- `idle-animation-variety`: clawd 在 Idle 态偶尔切换到 `clawd-idle-reading.gif` 变体，而非永远单一循环 `idle.gif`。

### Modified Capabilities
(none)

## Non-goals

- 不做 upstream 那一整套按会话数分档的 working 细分状态（typing/headphones-groove/building）——那需要 cc-bridge payload 新增字段（当前工具类型/子agent数），协议改动超出本次范围，用户已明确排除。
- 不引入 `clawd-typing`/`clawd-building` 这类宽幅"桌面场景"姿势——`claude-code-buddy/tools/clawd-gif/README.md` 记录过这类构图在小屏上角色会被挤得很小，不适合 cardputer 更小的 120×80 画布。
- 不改变 Idle 之外任何状态的渲染逻辑。
- 具体的切换概率/停留时长不做强约束，实现时按手感调（用户已确认"随便"，不纠结具体数字）。

## Impact

- `cardputer-adv-buddy/src/clawd_player.cpp`：`fileForState`（Idle 分支改为变体选择）+ 新增一个类似 `busyVariant_`/`busyNextMs_` 的 idle 变体计时状态。
- `cardputer-adv-buddy/data/characters/clawd/`：新增 `clawd-idle-reading.gif` 资产（需先用 clawd-gif 管线转换，非直接照抄 upstream 原始尺寸）。
- 不改协议、不改 cc-bridge、不改其它子项目。
