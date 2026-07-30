## Why

`cardputer-voice-notes` 让设备能录语音便签存 SD，但**只能录不能查**——设备上看不到已录列表、
也不能回放，只能拔卡插电脑听。用户要把它当**随身笔记本**用，就必须能在设备上**浏览已录笔记 +
就地回放**。upstream `mic_wav_record.ino` 示例本身就带列表(`SD.open("/")` 扫 `.wav`)与回放
(`Speaker.playRaw`)，是现成 ground truth；回放用 Speaker（与录音的 Mic 互斥切换逻辑已在
voice-notes 里就位），技术低风险。

## What Changes

- 新增**笔记列表覆盖层**：NORMAL 态按专用键 `l`（list）弹出 SD 上 `note_*.wav` 的可滚动列表
  （复用现有 SESSIONS 覆盖层的列表交互：`,`/`.` 滚动、`esc`/`` ` `` 关）。
- 新增**就地回放**：列表里 `enter` 播放选中笔记——`file.seek(44)` 跳过 WAV 头后**分块流式**
  读 SD → `M5Cardputer.Speaker.playRaw(buf, len, 16000)`，不把整文件读进 RAM、不长时间阻塞
  主循环；播放中可按键停止，播完自动回列表。
- 回放期间沿用 Mic/Speaker 互斥约束：正在录音时禁止回放；回放用 Speaker（正常态已 begun）。

## Capabilities

### New Capabilities
- `note-playback`: cardputer 设备端浏览 SD 上的语音便签列表并就地回放（Speaker 流式播放），
  无需拔卡到电脑。

### Modified Capabilities
(none — 叠在 `voice-notes`（录音+SD+recorder 模块）之上，复用其 SD 挂载与 WAV 约定，不改其录音语义)

## Non-goals

- **不做删除 / 重命名 / 时间戳命名**（用户本轮只要「列表+回放」）；删除等后续再开 change。
- **不做进度条 / 快进快退 / 音量单独控制**：MVP 只做「选中→从头播→可停」。
- **不做跨目录 / 子文件夹**：只列 SD 根目录的 `note_*.wav`（voice-notes 就存在根）。
- **不改 voice-notes 的录音路径**：纯新增浏览+回放。

## Impact

- `cardputer-adv-buddy/src/recorder.{h,cpp}`：新增 `listNotes()`（扫根目录 `note_*.wav`）、
  `playNote(name)` / 回放分块 `tickPlayback()` / `isPlaying()` / `stopPlayback()`——复用已有
  SD 挂载与 WAV 知识，回放逻辑与录音 tick 同构（分块不阻塞）。
- `cardputer-adv-buddy/src/main.cpp`：NORMAL 态 `l` 键开列表覆盖层；列表内 `,`/`.`/`enter`/`esc`
  分发；每帧若在回放则 `recorder::tickPlayback()`；回放/列表期间与 screen-off activity 协调
  （浏览/回放算活动，屏不灭）。
- `cardputer-adv-buddy/src/clawd_player.{h,cpp}`：新增笔记列表覆盖层（复用 SESSIONS 覆盖层
  渲染模式）+ 回放中指示。
- 不改协议、不改 cc-bridge、不动其它子项目。
