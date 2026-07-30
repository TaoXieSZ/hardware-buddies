# Task Spec: Cardputer local voice-note recording (mic → WAV → microSD)

- **ID**: 003-cardputer-voice-notes
- **Workdir**: /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy
- **Executor**: cursor
- **Created**: 2026-07-05
- **Iteration**: 0

## Goal

Add a local voice-note recorder to the Cardputer-ADV firmware: press a key in NORMAL
mode to start recording from the built-in mic, press again to stop; audio is saved as a
16 kHz / mono / 16-bit WAV file to the inserted microSD card (auto-incrementing name).
Firmware-only (ESP32-S3, PlatformIO/Arduino). Comments in Chinese.

## Background / Context (authoritative spec + verbatim ground truth)

READ FIRST — the OpenSpec change this is distilled from:
/Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/openspec/changes/cardputer-voice-notes/{proposal,design}.md + specs/voice-notes/spec.md + tasks.md

Ground truth is the upstream M5Cardputer examples (COPY the API verbatim, do not invent):
- `.pio/libdeps/cardputer-adv/M5Cardputer/examples/Basic/mic_wav_record/mic_wav_record.ino`
  — mic record + WAV header + SD write. Contains the WAV header struct (RIFF/WAVE/fmt
  size16 / audioFormat=1 / numChannels=1 / sampleRate=16000 / bitsPerSample=16 / data),
  and the toggle: `M5Cardputer.Speaker.end(); M5Cardputer.Mic.begin();` to record, then
  `M5Cardputer.Mic.record(int16_t* buf, size_t len, 16000)`, and
  `M5Cardputer.Mic.end(); M5Cardputer.Speaker.begin();` to release.
- `.pio/libdeps/cardputer-adv/M5Cardputer/examples/Basic/sdcard/sdcard.ino` — SD pins for
  Cardputer-ADV: `SCK=40, MISO=39, MOSI=14, CS=12`; `SPI.begin(40,39,14,12);
  SD.begin(12, SPI, 25000000);`.

Hard facts about the current firmware:
- **Mic and Speaker are mutually exclusive** (share the I2S peripheral). `sound_player.cpp`
  drives `M5Cardputer.Speaker` (tone / playWav / playEvent). To record you MUST stop the
  Speaker first (`Speaker.end()`), and restore it after (`Speaker.begin()`); during
  recording no Speaker sound may play.
- SD uses Arduino `SD.h` over SPI — a SEPARATE filesystem from the LittleFS clawd GIF
  pack on internal flash (`board_build.filesystem = littlefs`). Mounting SD does NOT
  touch LittleFS.
- The `v` key is PTT (sends a mic hotkey to the Mac for dictation) — DO NOT touch or
  reuse it; local recording is different. NORMAL-mode keys already taken: `1 2 3 4 5 r
  c f v h - = tab enter backspace/del`. Use **`n`** (note) for record toggle — it is free
  in NORMAL mode (the `n` at main.cpp:168 is the approval-overlay "deny", a different
  mode, no conflict).
- Screen-off (just landed, main.cpp ~line 385-395): `activity = keyEvent ||
  g_motion.stillMs() < SCREEN_OFF_MS`; blanks backlight after 60s no key + no motion.
  **You MUST make recording count as activity** (add `|| recorder::isRecording()` to that
  `activity` condition) so the screen does not blank mid-recording and hide the REC
  indicator.

## Files in Scope

| File | Change |
|------|--------|
| .../src/recorder.h | create: recorder API (begin/toggle/tick/isRecording/elapsedMs) |
| .../src/recorder.cpp | create: SD mount, Mic start/stop, per-frame capture, WAV write + finalize |
| .../src/main.cpp | modify: `n` key toggles recording; call `recorder::tick()` each frame while recording; add `|| recorder::isRecording()` to the screen-off activity condition; show/hide REC indicator |
| .../src/sound_player.h | modify: declare `releaseForMic()` / `resume()` (+ record-mute) |
| .../src/sound_player.cpp | modify: `releaseForMic()` = stop + `Speaker.end()`; `resume()` = `Speaker.begin()`; guard play/tone while released |
| .../src/clawd_player.h | modify: declare a persistent REC indicator (e.g. `setRecording(bool, uint32_t ms)`) |
| .../src/clawd_player.cpp | modify: draw a top-bar `REC mm:ss` while recording (persistent, not a 1.5s toast) |
| .../platformio.ini | only if `SD`/`SPI` don't link (they are Arduino-ESP32 builtin; add to lib_deps ONLY if the build fails without) |

Do NOT edit cclink, ble_link, motion, the GIF assets, or any other subproject.

## Plan

1. `recorder.{h,cpp}`:
   - `bool sdMount()` — lazy, once: `SPI.begin(40,39,14,12); SD.begin(12, SPI, 25000000)`;
     cache success; return false on failure (do NOT crash).
   - `bool beginRecord()` — mount SD (bail with false if fail); pick next free
     `/note_%04d.wav` by scanning root for `note_*.wav`; `SD.open(path, FILE_WRITE)`; write
     a placeholder WAV header (sizes = 0); `sound::releaseForMic()`; `M5Cardputer.Mic.begin()`;
     set recording=true, record start millis.
   - `void tick()` — while recording: `M5Cardputer.Mic.record(buf, RECORD_LEN, 16000)`
     (RECORD_LEN small, e.g. 240, per example) then `file.write((uint8_t*)buf,
     RECORD_LEN*sizeof(int16_t))`, accumulate dataSize. Follow the example's record/read
     pattern. Keep the per-call work small so the main loop / GIF isn't blocked. On any
     SD write failure → stop + fail-safe (see below).
   - `void endRecord()` — `M5Cardputer.Mic.end()`; seek(0), rewrite header with real
     fileSize/dataSize; close; `sound::resume()`; recording=false.
   - `bool isRecording()`, `uint32_t elapsedMs()`.
   - Fail-safe: any SD/open/write failure → close file if open, `Mic.end()`,
     `sound::resume()`, recording=false, surface an error (return code the caller toasts).
2. `sound_player`: `releaseForMic()` (stop current + `Speaker.end()`), `resume()`
   (`Speaker.begin()`), and a `g_muted`/released flag so `play`/`tone`/`playEvent`
   short-circuit while released. Pair them; never leave Speaker ended after a stop.
3. `main.cpp`:
   - In the NORMAL-mode key handling, `n` → `if (recorder::isRecording()) recorder::endRecord();
     else { if (!recorder::beginRecord()) clawd::setToast("no SD"); }`. Toast start/stop.
   - Each loop() iteration: `if (recorder::isRecording()) recorder::tick();`
   - Add `|| recorder::isRecording()` to the screen-off `activity` expression so the
     screen stays on while recording.
   - Drive `clawd::setRecording(recorder::isRecording(), recorder::elapsedMs())` so the
     REC indicator shows/updates/hides.
4. `clawd_player`: persistent top-bar `REC mm:ss` (red) while recording; cleared when not.
   Keep it minimal — a top strip, not a full overlay. Must not fight the existing badge /
   session tag drawing (draw in a spot that doesn't collide, or take priority while recording).
5. Build: `pio run -e cardputer-adv` must succeed. Do NOT run upload/uploadfs.

## Constraints

- Only files listed in Files in Scope, all under `cardputer-adv-buddy/`. Additive; do not
  refactor unrelated recent features (screen-off, idle/busy GIF timers, Connecting,
  backspace relay, PTT).
- Comments in Chinese, matching surrounding style.
- Copy mic/SD/WAV init VERBATIM from the two example files cited above — do not guess pins,
  sample rate, or the record API. Cite the example in a comment.
- Mic/Speaker calls must be paired (release → begin … end → resume); never leave the
  Speaker dead after recording (else nudge sounds break).
- Never block the loop for long; never crash on missing/failing SD.

## Acceptance Criteria

- [ ] New `recorder.{h,cpp}`; `n` toggles recording in NORMAL mode.
- [ ] Recording writes a valid 16k/mono/16-bit WAV to SD with auto-incrementing
      `/note_%04d.wav`; header sizes finalized on stop.
- [ ] `Speaker.end()`/`Mic.begin()` on start, `Mic.end()`/`Speaker.begin()` on stop;
      no Speaker sound during recording; sounds work again after.
- [ ] Recording is chunked per loop() (not one blocking capture); GIF/BLE keep running.
- [ ] Screen-off `activity` includes `recorder::isRecording()` (screen stays on while
      recording).
- [ ] Missing/failed SD → toast (e.g. "no SD"), no crash, Speaker still usable.
- [ ] Persistent REC indicator shows while recording, cleared after.
- [ ] `pio run -e cardputer-adv` succeeds; `git status --porcelain` shows only in-scope
      files under `cardputer-adv-buddy/`.

## Verification

```bash
cd /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy
pio run -e cardputer-adv          # MUST end "[SUCCESS]"
git status --porcelain            # only in-scope cardputer-adv-buddy files
grep -n "Mic.begin\|Mic.record\|Speaker.end\|SD.begin" src/recorder.cpp   # verbatim init present
```

Do NOT run upload/uploadfs (no device attached to you). On-device behavior (record →
SD file plays on a computer, auto-increment, Speaker recovery, no-SD toast, no GIF stall,
screen stays on while recording) is HUMAN verification by the repo owner — list as
pending device test in your summary; do not claim it.
