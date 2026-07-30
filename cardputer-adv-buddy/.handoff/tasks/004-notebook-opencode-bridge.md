# Task Spec: 004 Keyboard Notebook + OpenCode Bridge

- **ID**: 004-notebook-opencode-bridge
- **Created**: 2026-07-05
- **Status**: firmware done, OpenCode bridge done, BLE broken

## What was implemented

### Firmware (cardputer-adv-buddy)
- **recorder.{h,cpp}**: playback (double-buffer + tight spin), volume boost (200/255),
  mic gain (magnification=48), `deleteNote()`, `adjVolume()`, `saveTextNote()`
- **clawd_player.{h,cpp}**: NOTES list overlay, NOTEBOOK (text editor) overlay,
  opencode agent tag ("oc", color 0x05FF)
- **main.cpp**: `l` key → notes list, `k` key → notebook, `-`/`=`/`backspace` in notes,
  notebook keyboard block (ASCII typing → `esc` saves to `/txt_XXXX.txt`)

Build: `pio run -e cardputer-adv` → [SUCCESS], RAM 91020/327680 (27.8%), Flash 45.1%

### OpenCode bridge (claude-desktop-buddy/tools/opencode-bridge/)
- `bridge.py`: cmux session file polling → push ext_sessions to cc-bridge every 2s
- `cmux_control.py`: added `focus_by_opencode_sid()` — matches by agent.sessionId
- `cc-bridge/bridge.py`: added `_cmux.focus_by_opencode_sid(sid)` to selectSession chain
- `buddy_core/core.py`: **ext_sessions now get priority slots** (was: local Claude 16
  slots fill first → ext dropped; now: ext first, remaining slots to local)

## Known bugs

### 1. BLE stuck on "Connecting..." (BLOCKER)
Cardputer shows "Connecting..." after firmware flash. Attempted:
- Killed/restarted cc-bridge → still stuck
- Removed CC_BRIDGE_TAB5_SERIAL (was pointing to cardputer's USB port) → still stuck
- USB unplug/replug → still stuck

Suspect: cardputer needs a **real cold power cycle** (battery removal?) because
USB-powered RTS hard-reset may not fully restart the BLE radio. Or firmware
introduced a BLE bug (but ble_link.cpp/cclink.cpp were NOT touched).

### 2. OpenCode session not visible in tab list (DEPENDS ON #1)
Fix in buddy_core (ext_sessions priority) is committed but can't verify because
BLE doesn't connect. Manual push via `nc -U /tmp/cc-bridge.sock` works.

### 3. Text notes not visible in `l` list
`listNotes()` only scans `note_*.wav` — text notes `txt_*.txt` are invisible.
Need to extend the NOTES overlay to show both types.

## Running bridges (after restart)
```
cc-bridge        PID 18369  (no TAB5_SERIAL)
codex-bridge     PID 2582   (pre-existing)
opencode-bridge  PID 19430
```

## Flash command
```
~/.platformio/penv/bin/esptool.py --chip esp32s3 --port /dev/cu.usbmodem21401 \
  --before no-reset --after hard-reset write-flash 0x10000 firmware.bin
```
Must enter ROM download mode: OFF → hold G0 → ON → release G0
