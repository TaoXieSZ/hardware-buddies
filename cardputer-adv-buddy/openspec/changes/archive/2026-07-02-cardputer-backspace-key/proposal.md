## Why

Cardputer 的物理键盘有一颗专用 Backspace/Del 键，M5Cardputer 库已经把它解出到每帧的
`KeysState.del`（upstream `inputText.ino` 用它做退格），但固件 NORMAL 态从没读过这个字段——
按下去什么反应都没有。语音听写（PTT 'v' 键）或 nudge 打字（`1`/`c`/`f`…）打错一个词，
用户在 cardputer 上完全没法删，只能回到 Mac 键盘上手动改，desk pet 的「离手操作」体验就断了。
daemon 那侧其实早就支持 `{"cmd":"key","name":"backspace"}`（Tab5 键盘中继同款协议，
`buddy_core/core.py` 已有 `backspace`→`kVK_Delete` 的映射），cardputer 固件只是没接上这根线。

## What Changes

- NORMAL 态（无审批/会话列表/帮助/问答覆盖层时）新增一个按键分支：`ks.del`（物理
  Backspace/Del 键，逐字核对 upstream `inputText.ino` 的读法）触发时，经既有
  `cclink::sendKeyName("backspace")` 通道回送一次退格键给聚焦的 Claude 终端。
- 复用 NUDGES 表同款的 toast + 提示音反馈（如 `sent: backspace`），与其余快捷键行为一致。
- 每次物理按键（`isChange()` 边沿）发一次退格，不做按住重复——与本文件其余 NUDGES 键
  及 upstream 示例的单发语义保持一致，最简实现先满足「能删」这个刚需。

## Capabilities

### New Capabilities
- `backspace-key-relay`: cardputer NORMAL 态下物理 Backspace/Del 键经 BLE 回送 `cmd:key
  name:backspace` 给 Mac 侧聚焦终端，用于纠正语音听写或 nudge 打字打错的内容。

### Modified Capabilities
(none — 现有 NUDGES 快捷键表暂无独立 capability spec，本次不倒填，只新增这一条需求)

## Non-goals

- 不做「按住连续退格」（重复删除）——单次按键单次退格，够用即止，需要再开后续 change。
- 不做「删词」（如 option+backspace 语义）——只做最基础的单字符退格。
- 不改 Mac 侧 daemon / `buddy_core/core.py`——`cmd:key name:backspace` 协议已存在（Tab5
  键盘中继同款路径），本次纯固件接线，不触碰 `claude-code-buddy/`。
- 不改 PTT 语音录制本身的取消/撤回语义（那是另一个问题：录完直接发送、无法中途作废）——
  本 change 只解决「打错字后能删」，不解决「录完想反悔整段作废」。

## Impact

- 仅 `cardputer-adv-buddy/src/main.cpp` 的 NORMAL 态按键分发块（`ks.word` 循环旁新增
  `ks.del` 分支）。不改协议、不改 daemon、不改其它子项目，与仓库内另一个正在
  `claude-code-buddy/` 做 Tab5 开发的会话没有文件交集。
