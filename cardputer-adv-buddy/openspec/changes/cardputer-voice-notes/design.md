## Context

Ground truth 来自 upstream `M5Cardputer/examples/Basic/mic_wav_record/mic_wav_record.ino`
与 `Basic/sdcard/sdcard.ino`（已逐行核对）：

- **SD 引脚（Cardputer-ADV）**：`SD_SPI_SCK_PIN=40, MISO=39, MOSI=14, CS=12`；
  `SPI.begin(40,39,14,12); SD.begin(12, SPI, 25000000);`。SD 用 Arduino `SD.h`（SPI），
  与固件放 clawd GIF 的 LittleFS（内部 flash）是**两套独立 FS**，互不影响。
- **Mic 录制**：`M5Cardputer.Speaker.end(); M5Cardputer.Mic.begin();` 后
  `M5Cardputer.Mic.record(int16_t* data, size_t len, 16000)`；停止/回放前
  `M5Cardputer.Mic.end(); M5Cardputer.Speaker.begin();`。**Mic 与 Speaker 共用 I2S、互斥**
  （示例正是靠 end/begin 来回切）。采样：16000 Hz / mono / int16。
- **WAV header**：示例有现成 struct（RIFF/WAVE/fmt(16)/audioFormat=1/numChannels=1/
  sampleRate=16000/bitsPerSample=16/data + fileSize、dataSize），录完回填 size 字段。
- 现有 `sound_player.cpp` 用 `M5Cardputer.Speaker`（tone/playWav）；`main.cpp` 的 `v` 键 PTT
  发 `cmd:mic` 给 Mac（非本机录）。NORMAL 键已占：`1-5 r c f v h - = tab enter backspace`。

## Goals / Non-Goals

**Goals:**
- 按键起停本地录音 → 16kHz mono WAV → SD，文件名自增。
- 分帧采集不阻塞 clawd 动画 / BLE 主循环。
- 与 Speaker 提示音安全互斥（录音让出 I2S，录完归还）。

**Non-Goals:**
- 不做上传/转写/回放/文件管理 UI；不做 RTC 时间戳；不复用 PTT 键。（见 proposal Non-goals）

## Decisions

- **专用录音键 = `r`? 否——`r` 已是 retry**。选一个 NORMAL 未占用键：建议 **`n`**（note，助记，
  NORMAL 态空闲；`n` 在审批层是 deny，但那是审批覆盖层独占，不冲突）。实现时在 NORMAL 键
  分发里加 `n` → toggle 录音。（最终键位实现时定，spec 不锁死具体字母，锁「一个专用键 toggle」。）
- **分帧采集，融入 loop()**：进入录音态后，每次 `loop()` 调一次 `Mic.record(buf, RECORD_LEN, 16000)`
  抓一小段（如 RECORD_LEN=240 samples，示例值），追加写入已打开的 SD `File`。避免示例那种
  阻塞式录满整块——那会冻住 GIF/BLE。缓冲小、每帧写一块。
- **WAV 落盘策略**：起录时 `SD.open("/note_XXXX.wav", FILE_WRITE)`，先写一个占位 header
  （size 先填 0），录制中持续 `file.write((uint8_t*)buf, RECORD_LEN*2)` 并累加 dataSize；
  停录时 `file.seek(0)` 回填 fileSize/dataSize，`file.close()`。（示例同款回填思路。）
- **文件名自增**：起录时扫 SD 根目录现有 `note_*.wav`，取最大号+1，`note_%04d.wav`。
  （抄示例的 file_counter 思路，避免 RTC 依赖。）
- **I2S 互斥 + 提示音协调**：起录 = `sound::` 停当前音 + `Speaker.end()` + `Mic.begin()`；
  录音态**禁用 nudge/提示音**（sound_player 加个 `g_muted`/在录音态短路）；停录 =
  `Mic.end()` + `Speaker.begin()` 恢复。封装成 `sound::releaseForMic()` / `sound::resume()`。
- **SD 挂载时机 = 懒加载**：第一次起录时才 `SPI.begin`+`SD.begin`（SD 可能没插）。失败→
  toast `no SD` + 不进录音态、Speaker 立即归还。挂载成功后可缓存标志避免重复 begin。
- **录音态屏幕指示**：复用 `clawd::setToast` 不够（1.5s 自动消失），录音是持续态——加一个
  持久 REC 指示（顶栏红点/`REC 00:07` 时长），类似审批/问题的持久覆盖或一个轻量顶栏标记。
  MVP 可先用顶栏一行 `REC mm:ss`，不做满覆盖层。

## Risks / Trade-offs

- [Mic.begin/Speaker.end 切换有噪声/延迟，或切换失败导致提示音再也不响] → 缓解：真机验证
  起停后 Speaker 能正常恢复（录一次→停→按 nudge 键听是否有声）；`releaseForMic/resume`
  成对调用、加状态标志防错序。
- [每帧写 SD 卡阻塞时间过长 → GIF 掉帧/卡顿] → 缓解：RECORD_LEN 取小（240 samples≈15ms@16k），
  SD 25MHz 写一小块很快；真机观察录音时 clawd 是否明显卡。必要时降低 GIF 帧率优先保录音。
- [SD 未插 / 写失败 / 卡满] → 缓解：`SD.begin` 与每次 `open`/`write` 返回值都检查，失败 toast
  提示并安全退出录音态、归还 Speaker，绝不崩。
- [录音中断电/复位 → WAV header size 未回填 → 文件损坏] → 接受：MVP 不做崩溃恢复；可选缓解
  是每写 N 块也回填一次 header（后续优化，不进 MVP）。

## Open Questions

- 录音键最终选哪个字母（`n`? 或其它）——实现时定，真机确认不与既有键手感冲突。
- 屏幕录音指示的具体样式（顶栏时长 vs 满屏 REC）——实现时定，先做顶栏最简版。
