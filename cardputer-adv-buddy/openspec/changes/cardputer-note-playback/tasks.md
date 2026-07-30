## 1. recorder 模块加浏览+回放

- [ ] 1.1 `recorder.h/.cpp` 加 `uint8_t listNotes(char names[][N], uint8_t maxN)`：扫 SD 根
      （`SD.open("/")` 遍历，`!isDirectory() && name 前缀 note_ 且 .wav`）收集文件名，超上限
      截断并 `Serial` log（不静默吞）。SD 未挂载 → 返回 0。
- [ ] 1.2 `playNote(const char* name)`：`sdMount()`；`SD.open("/<name>")`；`seek(44)` 跳 WAV 头；
      置 playing 态、记文件句柄。录音中则拒绝（返回 false）。
- [ ] 1.3 `tickPlayback()`：每帧读一块（RECORD_LEN 样本）→ `Speaker.playRaw(g_buf,n,16000)`，
      靠 `Speaker.isPlaying()`/队列节流；EOF 或读失败 → `stopPlayback()`。不用阻塞抽干循环。
- [ ] 1.4 `isPlaying()` / `stopPlayback()`（关文件、退 playing 态）。回放只用 Speaker，不碰 Mic；
      与 `isRecording()` 互斥（对称拒绝）。

## 2. clawd_player 笔记列表覆盖层

- [ ] 2.1 复用 SESSIONS 覆盖层范式加 `showNotes(names,n)`/`drawNotes()`/`notesMove(±1)`/
      `notesSelected()`/`hideNotes()`/`notesVisible()`；空列表画 `(no notes)`；回放中显示 `▶ playing`。

## 3. main.cpp 接线

- [ ] 3.1 NORMAL 态 `l` 键 → `showNotes(recorder::listNotes(...))`；新 overlay 快照 `snapNotes`，
      与 APPROVAL/QUESTION/SESSIONS/HELP 同级排他。
- [ ] 3.2 列表内键分发：`,`/`.` `notesMove`；`enter` → `recorder::playNote(notesSelected())`
      （播放中再 `enter`/`esc` → `stopPlayback`）；`esc`/`` ` `` 关列表。
- [ ] 3.3 每帧 `if (recorder::isPlaying()) recorder::tickPlayback();`
- [ ] 3.4 screen-off `activity` 追加 `|| snapNotes || recorder::isPlaying()`（浏览/回放不熄屏）。

## 4. 编译 + 真机验证

- [ ] 4.1 `pio run -e cardputer-adv` 编译通过。
- [ ] 4.2 真机：先录 1-2 条；按 `l` → 出笔记列表；`,`/`.` 选；`enter` → 从喇叭听到回放、听得清、
      不断音；主形象/BLE 回放时仍活。
- [ ] 4.3 真机：回放中按停止键 → 停、回列表；播完自动回列表。
- [ ] 4.4 真机：录音中按 `l`/回放被拒（互斥）；回放中屏不熄。
- [ ] 4.5 真机：空 SD（或无 note）按 `l` → `(no notes)` 不崩。
- [ ] 4.6 `git diff` 确认只动 `cardputer-adv-buddy/`（recorder.{h,cpp}/clawd_player.{h,cpp}/main.cpp）。
