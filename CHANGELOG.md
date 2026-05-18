# Changelog

All notable changes to this project are documented here.
Format loosely follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Project doesn't ship semver releases yet — entries live under **Unreleased**
until that changes.

## [Unreleased]

### Added
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
[263de46]: https://github.com/TaoXieSZ/claude-code-buddy/commit/263de46
[04be7fc]: https://github.com/TaoXieSZ/claude-code-buddy/commit/04be7fc
[4c4eefe]: https://github.com/TaoXieSZ/claude-code-buddy/commit/4c4eefe
[fdc6db3]: https://github.com/TaoXieSZ/claude-code-buddy/commit/fdc6db3
[ddbe751]: https://github.com/TaoXieSZ/claude-code-buddy/commit/ddbe751
