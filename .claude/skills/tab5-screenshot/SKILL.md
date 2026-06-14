---
name: tab5-screenshot
description: >-
  Capture the M5Stack Tab5 dashboard screen as a PNG so the agent can SEE the
  UI it is iterating on (instead of asking the user for a photo). Use whenever
  working on Tab5 firmware UI (src/tab5/ui.cpp), after flashing a UI change, or
  when the user says "show me the tab5 screen", "screenshot the tab5",
  "看一下 tab5 界面", "tab5 截图", "拍一下屏幕", "看看现在长啥样". Captures a
  static frame only (not motion/scroll smoothness).
---

# Tab5 screenshot — see the device UI

The Tab5 firmware streams its framebuffer back over USB-CDC serial on the
`{"cmd":"shot"}` command (downsampled ×2 to 640×360, RGB565). Capture it to a
PNG, then **Read the PNG** to actually see the UI.

## Pick the method by who owns the serial port

The serial port (`/dev/cu.usbmodem2101`, or another `/dev/cu.usbmodem*`) is fed
by a bridge daemon. Check first:

```bash
lsof /dev/cu.usbmodem2101
```

- **A Python daemon holds it** (normal running state) → use the daemon path.
- **Port is free** (you stopped cc-bridge/cursor-bridge to flash) → use direct.

## Method A — via the running daemon (preferred)

```bash
python3 tools/tab5-shot/shot.py          # prints the PNG path on success
```

Then Read `/tmp/tab5-shot.png` (or the printed path). The daemon sends the shot
command over the port it owns and writes the PNG.

## Method B — direct serial (daemon stopped / port free)

Needs `pyserial`, present in the bridge venvs:

```bash
~/.cc-bridge/venv/bin/python3 tools/tab5-shot/shot_direct.py   # prints PNG path
```

`--port` auto-detects `/dev/cu.usbmodem*` (override if multiple devices);
`--out` defaults to `/tmp/tab5-shot.png`.

## After capture

Always **Read the PNG file** to view it, then describe / iterate. Embed it in
the reply so the user sees the same frame.

## Gotchas

- One daemon owns the port; running both `shot.py` and the daemon, or two
  direct readers, garbles the frame. macOS lets two processes open `cu.*`
  simultaneously without erroring — it just corrupts the stream.
- The firmware build must include the `cmd:shot` handler (`uiScreenshot()` in
  `src/tab5/ui.cpp`, dispatched in `src/tab5/feed.cpp`). Stock/old builds won't
  respond.
- It's a **still frame** — it cannot show scroll smoothness, animation, or
  tearing. For motion behavior, read serial logs or ask the user to observe.
- Flashing needs the port free, so the daemon must be stopped first; after
  flashing, restart it (`launchctl start com.cursor-bridge`) to restore the
  feed and Method A.
