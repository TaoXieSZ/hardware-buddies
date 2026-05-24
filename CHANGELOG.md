# Changelog

All notable changes to this project are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Project doesn't ship semver releases yet — entries live under **Unreleased**
until that changes.

## [Unreleased]

### Added
- **Voice control-plane secretary (MVP core).** Speak to StackChan and a chosen
  Claude/Cursor session running in **cmux** executes your verbatim command,
  gated by a thumbs-up gesture. New `tools/control_plane/`: `cmux_control.py`
  (enumerate sessions via `cmux rpc workspace.list`; `route(number,text)` =
  `cmux send --workspace <uuid>` + Enter, targeting the stable UUID),
  `stager.py` (stage→confirm/cancel with TTL), `board.py` (numbered Mac board +
  read-screen status), `smoke_test.py` (safe throwaway-workspace check). Daemon
  gains a `stage_route` socket action and routes thumbs-up→commit /
  thumbs-down→cancel through the existing camera-gesture stream
  (`tools/buddy_core/core.py`, `tools/cc-bridge/bridge.py`). buddy-voice gains
  `app/api/stage-route` to forward the agent's `route_to_session` tool to the
  daemon (in the separate `../buddy-voice` Agora quickstart project, not this
  repo). Spec: `.omc/specs/deep-interview-voice-control-plane.md`; docs:
  `tools/control_plane/README.md`. `route()` auto-focuses the target session
  (`cmux rpc workspace.current`) so it pops to the front as the command lands;
  `demo.py` exercises the full board→stage→confirm→execute path against a
  throwaway session (no voice/camera needed). (Live voice→gesture→cmux is a
  user hardware step; StackChan on-screen board is deferred to phase 2.)
- **StackChan voice via Agora ConvoAI (Path A2).** The agent's TTS audio now
  plays from StackChan's speaker. The Mac browser stays the RTC client and
  taps the agent's remote audio track, downsamples to 16 kHz mono PCM, and
  streams it over a WebSocket to a standalone relay (`tools/audio-relay`),
  which forwards it as sequenced UDP datagrams to the device. Firmware adds a
  UDP receiver + jitter buffer + `M5.Speaker` streaming
  (`src/stackchan/audio_play.cpp`, pure logic in `audio_packet.h` /
  `audio_ringbuf.h` with host tests). WiFi now comes up at boot when
  `wifi_secrets.ini` has real creds (shared with the camera path). Wire format
  in REFERENCE.md; bring-up in `docs/agora-stackchan-voice-bringup.md`. Opt-in
  via `NEXT_PUBLIC_STACKCHAN_RELAY=1`. (Audible-on-device + browser runtime are
  user-verified hardware steps.)
- **StackChan Zelda heart-row battery indicator.** 5 hearts under the
  character's feet, each representing 20 % of battery. Binary full/empty
  fill, Hyrule-red on dark-red outline. CHAR_BOX trimmed 16 px to free the
  strip; GIFs scale into the new box, no cropping. ([bdee1f3])
- **StackChan tap-to-wake.** Once the screen is blanked by the idle-dwell
  timer, a finger tap on the body wakes it instantly via the BMI270
  accelerometer. Threshold tuned for tap vs desk bump (1.2 g). ([8ff70ab])
- **StackChan auto screen-off.** New `Screen-off delay` slider on the dashboard
  (0–600 s, 0 = always on). Backlight blanks once the character has sat in
  IDLE/SLEEP without a state change for the configured dwell; any incoming
  hook event wakes it instantly. Default 60 s. Setting persists to NVS
  (`soff` key). Saves heat on always-on desks. ([04be7fc], [263de46])

### Fixed
- **Hook stalls when paired with StackChan only.** `_handle_wait_permission`
  used to burn the full 8 s timeout on every non-safe PreToolUse hook even
  though StackChan has no permission button to respond with. Across the
  many tool calls in a typical turn this looked like Claude Code was stuck
  after submitting a prompt. Daemon now short-circuits to `decision=ask`
  immediately when no permission-capable peer (prefix not containing `SC`)
  is connected. ([4c4eefe])
- **Continuous static from StackChan speaker.** Removed
  `board_build.arduino.memory_type = qio_qspi` and
  `-mfix-esp32-psram-cache-issue` from the CoreS3 envs. Both are
  ESP32-classic workarounds that corrupt octal-PSRAM DMA reads on
  ESP32-S3, garbaging the I²S audio buffer. ([fdc6db3])
- **Hook event JSON truncation in `buddy_core`.** `handle_client()` used a
  single best-effort `reader.read(64 KiB)` for the inbound hook frame, which
  silently truncated payloads when the client's write+flush raced our read
  (~500 `bad event JSON` warnings/day, cutting off near offset 200). Now
  reads the first line with `readuntil(b"\n")` and drains the rest until
  EOF. ([ddbe751])
- **Initial screen-off trigger never fired.** First cut required `CHAR_SLEEP`
  state held for N s, but the daemon emits an idle heartbeat every ~10 s
  that the firmware maps to `CHAR_IDLE`, so the device almost never reached
  SLEEP. Switched the trigger to "no state CHANGE for N s while in IDLE or
  SLEEP" — keepalives no longer reset the dwell clock. ([263de46])

[Unreleased]: https://github.com/TaoXieSZ/claude-code-buddy/compare/ddbe751...HEAD
[8ff70ab]: https://github.com/TaoXieSZ/claude-code-buddy/commit/8ff70ab
[bdee1f3]: https://github.com/TaoXieSZ/claude-code-buddy/commit/bdee1f3
[263de46]: https://github.com/TaoXieSZ/claude-code-buddy/commit/263de46
[04be7fc]: https://github.com/TaoXieSZ/claude-code-buddy/commit/04be7fc
[4c4eefe]: https://github.com/TaoXieSZ/claude-code-buddy/commit/4c4eefe
[fdc6db3]: https://github.com/TaoXieSZ/claude-code-buddy/commit/fdc6db3
[ddbe751]: https://github.com/TaoXieSZ/claude-code-buddy/commit/ddbe751
