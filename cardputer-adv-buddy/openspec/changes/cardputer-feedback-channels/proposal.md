## Why

clawd 此前只有「视觉镜像」一条反馈通道——把 Claude 会话状态映射成 GIF。但它相对 buddy 全家桶的优势（够大的屏 + ES8311 喇叭 + 56 键键盘）只用了一半。本 change 补齐**完整的反馈通道**：更丰富的状态动画（视觉）、hook 事件音效（听觉）、以及音量控制。

落地过程中还修复了 `cardputer-claude-buddy` 的一个真机验证盲区：**实时审批面板在真实 cc-bridge 流量下从未真正弹出过**——接收缓冲只有 640 字节，会把多 session 的大 payload（`entries[8]×~91` + `sessions` + `tokens` + …，轻松 >1KB）整帧丢弃，导致 `prompt` 字段永远收不到。之前只在 `ble_test_prompt.py` 的 ~95 字节小 payload 下验证过，所以盲区一直没暴露。缓冲扩到 2048 后，实时审批闭环（含决定回送）现已真机验证。

## What Changes

- **状态动画扩展**：clawd 状态机从 5 态扩到 7 态，新增 `thinking`（模型推理中）与 `notification`（等待用户输入）；`error`（工具失败）以 reaction 形式短暂覆盖。reaction 机制（error/dizzy/heart）由体感事件与 hook 事件共用。
- **hook 事件音效**：固件读取 cc-bridge 心跳中的 `play` 字段（one-shot 小写事件名），从 LittleFS 读 `/sounds/<event>.wav` 到 RAM，经 M5 `Speaker.playWav` 播放。关键 4 事件（stop / permissionrequest / posttoolusefailure / notification）配 wav；其余事件无文件自动忽略。本地 tone（连接/断开/nudge/开机）保留。
- **音量控制**：键盘 `-`/`=` 调节音量（0-255，tone 与 wav 共用），HELP 覆盖层显示 `-/=vol`。

## Non-goals

- 不做 tamagotchi 心情系统（`cardputer-coding-pet` 的 `pet-mood` 从未落地，本 change 一并作废）。
- 不做 m5stack-avatar 程序化脸（已确定走 clawd GIF 路线，`pet-avatar` 作废）。
- 不做「每个 hook 事件都发声」（频繁的 pretooluse/posttooluse 会吵，仅关键 4 事件配 wav）。
- 不做 wav 流式播放（M5 `playWav` 走内存 buffer，wav 必须 RAM-safe：16kHz mono ~1.5s ≈ 48KB）。

## Capabilities

### New Capabilities
- `agent-state-animation`: clawd 完整状态机（7 态 → GIF）+ reaction 机制（error/dizzy/heart，体感与 hook 共用）。取代 `cardputer-coding-pet` 未落地的 `pet-avatar`，并吸收其 `motion-interaction` 的真实行为（拿起/晃动/静止 → reaction）。
- `audio-feedback`: hook 事件 wav 音效（bridge `play` 字段 → `/sounds/<event>.wav`）+ 本地 tone + 音量控制。

### Removed Capabilities
<!--
cardputer-coding-pet 描述的 pet-avatar（m5stack-avatar 程序化脸）与 pet-mood（tamagotchi 心情）
从未落地，随本 change 作废；motion-interaction 的真实行为并入 agent-state-animation。
该 change 应标记 superseded 后归档/删除（见 tasks 5.4）。
-->

## Impact

- **代码**：`agent_state.h`（加 `Notification` 枚举）、`link_state.h`（`deriveAgentState` 加 thinking/notification，thinking 顺序前置）、`clawd_player.{h,cpp}`（`fileForState` 加 notification + `reactError`）、`sound_player.{h,cpp}`（`playEvent`/`volumeUp`/`volumeDown` + wav 缓冲）、`cclink.cpp`（读 `play` 字段 + 接收 buffer 640→2048）、`main.cpp`（`-/=` 音量键 + 清理与 wav 重复的 tone + error reaction 边沿触发）。
- **资源**：`data/sounds/*.wav`（4 个，shanraisshan/claude-code-hooks 转 16kHz mono）、`data/characters/clawd/`（clawd-thinking / clawd-notification / error-120 新增，dizzy 替换）。**需重烧 littlefs**。
- **内存**：wav 读入 RAM（~48KB），空闲 ~142KB，RAM-safe。
- **关联**：填补 `cardputer-claude-buddy` 的真机验证盲区（buffer/deny/subagent 修复）；作废 `cardputer-coding-pet`。
- **验证**：`pio run` + `buildfs`；真机端到端确认 thinking/dizzy/error 动画、posttoolusefailure wav、音量键、审批闭环（space/n/a 决定回送）。
