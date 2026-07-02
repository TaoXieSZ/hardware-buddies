## Context

`src/main.cpp` 的 NORMAL 态按键分发块（`loop()` 里 `!snapApproval && !snapSessions &&
!snapHelp && !snapQuestion` 分支）已经有一张 `NUDGES` 表把单字符键映射成
`cclink::sendKeyName(...)` / `cclink::sendKeyText(...) + enter`，回送给 Mac 侧聚焦的
Claude 终端。物理 Backspace/Del 键不在 `ks.word`（可打印字符流）里，M5Cardputer 库
把它单独解到 `KeysState.del`（HID `KEY_DELETE` → `_keys_state_buffer.del`，逐字核对
`.pio/libdeps/cardputer-adv/M5Cardputer/src/utility/Keyboard/Keyboard.cpp:169-170` 与
upstream 官方示例 `examples/Basic/keyboard/inputText/inputText.ino:54-56` 的
`if (status.del) data.remove(...)` 用法），固件目前完全没读这个字段。daemon 侧
`buddy_core/core.py:302` 的 `_KEY_NAMES` 表已经把 `"backspace"` 映射到 `kVK_Delete`
(`0x33`)，是 Tab5 键盘中继复用的同一条协议，cardputer 这边只需要接线。

## Goals / Non-Goals

**Goals:**
- NORMAL 态下按物理 Backspace/Del 键，回送一次 `{"cmd":"key","name":"backspace"}` 给
  Mac 侧聚焦终端，抵消语音听写/nudge 打字打错的一个字符。
- 复用既有 toast/音效反馈路径，行为上和 NUDGES 表其余键一致，不引入新状态机。

**Non-Goals:**
- 不做按住连续退格（重复删除）——`isChange()` 边沿触发，一次物理按键一次退格。
- 不做「删词」或修饰键组合语义。
- 不碰 PTT 语音录制的取消/撤回（`v` 键按住即发，release 即送出，本 change 不改）。
- 不改 `claude-code-buddy/tools/buddy_core/core.py`（协议已支持，无需改动）。

## Decisions

- **【已按真机实测修正】用 `ks.backspace` 而非 `ks.del`**：最初按 upstream 唯一的官方示例
  （`inputText.ino` 用 `status.del`）假设接 `ks.del`，理由是「upstream examples 是 ground
  truth」。但真机加诊断打印（`Serial.printf("[del-key] del=%d backspace=%d")`）实测：按下
  该键打出 `del=0 backspace=1`——Cardputer-**ADV** 这颗键实际发送的 HID 码是
  `KEY_BACKSPACE`，与标准版 Cardputer（upstream 示例基于的机型）不同。已改接 `ks.backspace`
  并去掉诊断打印，真机复测确认字符能正常删除。教训已记入仓库根 `CLAUDE.md`「Cross-cutting
  hardware gotchas」与本项目 `README.md`：upstream example 是 ground truth，但**同一块库在
  不同板卡变体上可能有不同物理映射**，跨变体时仍需真机验证，不能照抄示例了事。
- **接入点选在现有 NORMAL 态循环里，紧邻 NUDGES 处理**：`ks.del` 和 `ks.word`（NUDGES 用
  的可打印字符流）是 `KeysState` 里两个独立字段，同帧可能同时非空但物理上是同一个人
  按同一个键位不会撞车；直接在 `for (auto c : ks.word)` 循环之外单独加一个 `if (ks.del)`
  分支，不需要改 NUDGES 表结构或新增状态变量。
- **单发不重复**：沿用整份文件「`isChange()` 一次事件处理一次」的既有约定（PTT 的
  `isKeyPressed` 逐帧轮询是唯一例外，且那是为了识别按住/松开两个边沿，不是为了重复触发
  同一动作）。按住连发需要新增一个「上次发送时间戳 + 节流间隔」的小状态机，权衡「够用
  优先、最简先行」后放进 Non-goals，需要时再开后续 change。

## Risks / Trade-offs

- [物理键位与 `ks.del` 假设有误（该键实际只在 Fn 层可达，或 ADV 键盘布局与标准
  Cardputer 不同）] → 缓解：任务里加一步用现有 `[approval-key]` 同款串口诊断打印
  `ks.del`/`ks.backspace` 实测哪个为真，flash 后先用 Serial Monitor 验证再收尾（复刻本
  仓库审批键当初的验证流程）。
- [单发退格对整段说错的话删除效率低（需要连按多次）] → 接受的权衡，已在 proposal 里
  写明 Non-goals；如果实测「删一整句」是高频需求，后续开 change 加按住连发或删词。

## Open Questions

（无——scope 足够小，风险已在上面覆盖）
