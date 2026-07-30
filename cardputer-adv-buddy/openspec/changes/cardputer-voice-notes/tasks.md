## 1. SD 挂载

- [ ] 1.1 新增 `recorder.{h,cpp}`（或并入音频模块）：`sdMount()` 懒加载——
      `SPI.begin(40,39,14,12); SD.begin(12, SPI, 25000000)`（引脚抄 upstream `sdcard.ino`）；
      返回成功/失败，缓存标志避免重复 begin。失败不崩。
- [ ] 1.2 确认 `SD`/`SPI` 库在 Arduino 框架内置可用（`pio run` 能链接）；如缺则在
      `platformio.ini` `lib_deps` 补。

## 2. 录音核心（Mic + WAV + 分帧）

- [ ] 2.1 WAV header struct（RIFF/WAVE/fmt16/PCM/mono/16k/16-bit）——抄 `mic_wav_record.ino`
      verbatim；`beginRecord()`：选自增文件名（扫 `note_*.wav` 取下一号）→ `SD.open(...,FILE_WRITE)`
      → 写占位 header（size=0）。
- [ ] 2.2 `Speaker.end(); Mic.begin();`（起录）；`tickRecord()`：每帧 `Mic.record(buf,RECORD_LEN,16000)`
      → `file.write(buf, RECORD_LEN*2)` → 累加 dataSize（RECORD_LEN 取小如 240，避免卡顿）。
- [ ] 2.3 `endRecord()`：`file.seek(0)` 回填 fileSize/dataSize → `file.close()` →
      `Mic.end(); Speaker.begin();`。所有 SD/write 返回值检查，失败走安全退出（关文件、归还
      Speaker、toast 错误）。

## 3. 与 sound_player 的 I2S 协调

- [ ] 3.1 `sound_player` 加 `releaseForMic()`（stop + Speaker.end）与 `resume()`（Speaker.begin），
      及录音态 `g_muted` 短路 play/tone，保证录音期间不出声、录后恢复。成对调用防错序。

## 4. main.cpp 接线 + 屏幕指示

- [ ] 4.1 NORMAL 键分发加专用录音键（建议 `n`，实现时定，确认不与既有键冲突）toggle 起停。
- [ ] 4.2 录音态每帧调 `tickRecord()`；`clawd` 加持久 REC 指示（顶栏 `REC mm:ss`），停录撤下。
      确认分帧录音时 clawd 动画/BLE 不明显卡。

## 5. 编译 + 真机验证

- [ ] 5.1 `pio run -e cardputer-adv` 编译通过。
- [ ] 5.2 真机：按录音键 → 顶栏出 REC；说几秒话；再按停 → SD 生成 `note_0001.wav`。
      取卡到电脑，确认 WAV 能正常播放、听得清、时长对。
- [ ] 5.3 真机：再录一次 → `note_0002.wav`（自增不覆盖）。
- [ ] 5.4 真机：录音期间 clawd 仍动、BLE 状态仍更新（分帧不阻塞）；录后按 nudge 键确认
      Speaker 提示音恢复（Mic/Speaker 互斥切换正确）。
- [ ] 5.5 真机：不插 SD 按录音键 → toast `no SD` 不崩、Speaker 仍可用。
- [ ] 5.6 `git diff` 确认只动 `cardputer-adv-buddy/`。
