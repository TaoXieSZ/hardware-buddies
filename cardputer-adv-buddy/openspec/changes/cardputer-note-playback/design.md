## Context

`cardputer-voice-notes`（已实现、真机验证录音）新增了 `recorder.{h,cpp}`：`sdMount()`（`SPI.begin
(40,39,14,12)` + `SD.begin(12,SPI,25000000)`）、录音分块 tick、WAV 写入。本 change 叠在其上加
「浏览 + 回放」，复用同一 SD 挂载与 WAV 约定（16kHz/mono/16-bit，头 44 字节）。

Upstream ground truth（`mic_wav_record.ino`，已核对）：
- 列表：`File dir = SD.open("/")`；遍历 `entry`，`!entry.isDirectory() &&
  String(entry.name()).endsWith(".wav")` → 收集文件名。
- 回放：`file.seek(44)`（跳 WAV 头）→ `file.read((uint8_t*)buf, chunk)` → `M5Cardputer.
  Speaker.playRaw(buf, len, 16000)`，配合 `isPlaying()`。示例回放前 `Mic.end(); Speaker.begin()`。

现有 UI：`clawd_player.cpp` 的 SESSIONS 覆盖层（`showSessions`/`drawSessions`/`sessionsMove`/
`sessionsSelectedSid`）是可滚动可选中列表的成熟范式；main.cpp `tab` 开、`,`/`.` 滚、`enter` 选、
`esc`/`` ` `` 关——笔记列表直接复刻这套。screen-off 的 `activity` 现含 `keyEvent ||
stillMs()<T || recorder::isRecording()`。

## Goals / Non-Goals

**Goals:**
- 设备端列出 SD 根目录 `note_*.wav`，滚动选中，`enter` 就地回放，可停、播完回列表。
- 回放**分块流式**，不整文件进 RAM、不长阻塞主循环。
- 复用 recorder 的 SD 挂载与 SESSIONS 覆盖层交互范式，最小新增。

**Non-Goals:**
- 不做删除/重命名/时间戳命名/进度条/快进/子目录（见 proposal Non-goals）。

## Decisions

- **回放归入 `recorder.{h,cpp}`**（不另起模块）：它已 own SD 挂载与 WAV 常量，回放
  `listNotes()`/`playNote()`/`tickPlayback()`/`isPlaying()`/`stopPlayback()` 与录音 tick 同构、
  共享 `g_buf`/文件句柄语义，一处管音频文件最省。
- **分块流式回放**，与录音 tick 对称：`playNote()` 打开文件、`seek(44)`、置 playing 态；
  `tickPlayback()` 每帧读一块（`RECORD_LEN` 样本）→ `Speaker.playRaw(g_buf, n, 16000)`，
  用 `Speaker.isPlaying()`/内部队列节流，读到 EOF 或被停 → 关文件、退出 playing 态。**不**用
  示例那种 `do{}while(isPlaying())` 阻塞抽干（会冻住 clawd/BLE）。
- **Mic/Speaker 互斥**：回放只需 Speaker（正常态已 `begin`）。**录音进行中禁止回放**
  （`if (recorder::isRecording()) 忽略/提示`）；回放中禁止起录（对称）。回放不碰 Mic。
- **列表覆盖层复用 SESSIONS 范式**：clawd_player 加 `showNotes(names,n)`/`drawNotes()`/
  `notesMove()`/`notesSelected()`/`hideNotes()`，渲染照抄 `drawSessions`。main.cpp 用一个新
  overlay 快照 `snapNotes`，与 APPROVAL/QUESTION/SESSIONS/HELP 同级排他（列表打开时键盘归它）。
- **入口键 `l`**（list，NORMAL 态空闲，已 grep 确认）。列表内：`,`/`.` 滚、`enter` 播放选中/
  播放中再 `enter` 或 `esc` 停、`esc`/`` ` `` 关列表。回放中列表可显示「▶ playing」。
- **列表来源**：`recorder::listNotes()` 扫 SD 根 `note_*.wav`，回文件名数组（上限如 32，超出
  截断并 log，不静默吞——对齐仓库「no silent caps」）。空列表显示「(no notes)」。
- **screen-off 协调**：浏览/回放期间算活动——把 `snapNotes || recorder::isPlaying()` 并进
  main.cpp 的 `activity` 表达式，避免看列表/听回放时屏灭。

## Risks / Trade-offs

- [分块 playRaw 节流不当 → 断音/卡顿或读 SD 阻塞掉帧] → 缓解：块取小（RECORD_LEN=240，与录音
  一致）、靠 `isPlaying()`/队列节流；真机听是否连贯，不连贯再调块大小/预读双缓冲。
- [SD 上有非本机产生的 `.wav`（或大文件/异格式）] → 缓解：只列 `note_*.wav` 前缀；回放按 16k/
  mono/16-bit 假定（本机所录），异格式播出来变调可接受（非本机文件本就不保证）；读失败安全退出。
- [回放中断电/拔卡] → 缓解：read 返回值检查，失败 `stopPlayback()`（关文件、退出 playing），不崩。

## Open Questions

- 停止回放的键（再按 `enter` vs `esc`）——实现时定，二者皆可，注释写明。
