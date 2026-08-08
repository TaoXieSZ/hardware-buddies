# StopWatch Walkie

M5 StopWatch push-to-talk audio prototype. The watch captures 16 kHz mono
signed 16-bit PCM while KEYA is held, streams it to a trusted-LAN Mac bridge,
and displays the final DashScope `qwen3-asr-flash` transcript.

This prototype proves the audio loop only. It does not dispatch an agent,
steer an existing session, play TTS, or approve permissions.

## Prerequisites

- PlatformIO Core 6.x
- Python 3.10 or newer
- An M5 StopWatch connected by USB
- The Mac and StopWatch on the same trusted LAN
- A DashScope API key and workspace-specific OpenAI-compatible base URL

The firmware toolchain and board libraries are pinned in `platformio.ini`.
Do not replace the official `espressif32@6.12.0` platform with pioarduino.

## Pinned hardware ground truth

The implementation was checked against these exact upstream revisions:

| Component | Pinned revision | Initialization used here |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified/tree/4fb444784c85791e0b0207701392b42be234b2e7) | `4fb444784c85791e0b0207701392b42be234b2e7` | `M5.config()`, `M5.begin(cfg)`, `M5.Speaker.end()`, `M5.Mic.begin()`, then `M5.Mic.record(..., 16000)` |
| [M5GFX](https://github.com/m5stack/M5GFX/tree/729297d6e3d657ddc1ec5189bac2f2ea68828085) | `729297d6e3d657ddc1ec5189bac2f2ea68828085` | Display owned by M5Unified; no direct panel initialization |
| [M5PM1](https://github.com/m5stack/M5PM1/tree/be9a5456c007c333e7ac963f33bfde1ffa5d82ee) | `be9a5456c007c333e7ac963f33bfde1ffa5d82ee` | Power sequencing owned by M5Unified |
| [M5IOE1](https://github.com/m5stack/M5IOE1/tree/846eec7d05e25c09013be2acdb8804487f48a62e) | `846eec7d05e25c09013be2acdb8804487f48a62e` | StopWatch demo address fallback `0x4F -> 0x6F`, IO9/PWM channel 0 at 5 kHz |
| arduinoWebSockets | `2.7.2` | One client connection with library auto-reconnect disabled |

The microphone path intentionally does not deviate from the pinned M5Unified
example. The vibration code keeps the factory demo's IO-expander pins, PWM
frequency, polarity, and strength mapping; its only deliberate deviation is a
synchronous 45 ms one-shot after capture ends instead of a background vibration
task. This keeps the acknowledgement outside the recorded utterance and avoids
adding a long-lived task for a single prototype pulse.

## Configure the firmware

Create the ignored local configuration:

```bash
cd stopwatch-walkie
cp include/config.h.example include/walkie_config.h
```

Set `WIFI_SSID`, `WIFI_PASSWORD`, `WALKIE_WS_HOST`, `WALKIE_WS_PORT`, and
`WALKIE_WS_PATH` in `include/walkie_config.h`. `WALKIE_WS_HOST` must be the Mac's LAN
address, not `localhost`. Leave `WALKIE_DEVICE_ID` empty to derive it from the
ESP32 MAC.

Never commit `include/walkie_config.h`. The project-specific installed name
avoids a collision with the ESP32 framework's unrelated generic `config.h`.

## Build and test the firmware

Run the host-side state-machine, protocol, and bounded-queue tests first:

```bash
cd stopwatch-walkie
pio test -e native
```

Then perform a clean device build:

```bash
pio run -e m5stack-stopwatch -t clean
pio run -e m5stack-stopwatch
```

## Try the interface without Wi-Fi

The `m5stack-stopwatch-ui-demo` environment uses the same round-screen renderer
as the runtime firmware but skips Wi-Fi and WebSocket startup. It is intended for
visual and physical-key review when the Mac and watch are on different networks.

```bash
cd stopwatch-walkie
pio run -e m5stack-stopwatch-ui-demo
pio run -e m5stack-stopwatch-ui-demo -t upload --upload-port /dev/cu.usbmodemNNNN
```

KEYA advances through `Connecting`, `Ready`, `Recording`, `Transcribing`,
`Result`, and `Error`; KEYB moves back. The `Recording` screen reads the real
microphone and drives its waveform from the measured sample peak, but the demo
does not retain or transmit audio. Reflash `m5stack-stopwatch` when returning to
the live Mac bridge.

The durable visual and interaction contract is in `DESIGN.md`.

Only flash the `m5stack-stopwatch` environment after the build succeeds:

```bash
pio run -e m5stack-stopwatch -t upload --upload-port /dev/cu.usbmodemNNNN
pio device monitor --port /dev/cu.usbmodemNNNN --baud 115200
```

Resolve the port from the device identity rather than assuming a fixed port
number. The development StopWatch used for initial diagnostics reported USB
serial/MAC `28:84:85:43:AE:38`, 16 MB flash, and 8 MB embedded octal PSRAM.

## Start the Mac bridge

The bridge has an isolated Python environment and does not install a daemon:

```bash
cd stopwatch-walkie/tools/walkie-bridge
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt

export DASHSCOPE_API_KEY='...'
export DASHSCOPE_BASE_URL='https://<WorkspaceId>.cn-beijing.maas.aliyuncs.com/compatible-mode/v1'
python bridge.py --host 0.0.0.0 --port 8765
```

Use the workspace endpoint for the selected DashScope region. The bridge exits
with a local configuration error when either required DashScope setting is
missing; it never sends the API key to the watch.

Run automated bridge checks with:

```bash
pytest
```

The live test is opt-in and uses generated non-personal fixture audio:

```bash
export WALKIE_BRIDGE_LIVE_ASR=1
pytest -m live
```

Without the opt-in flag or DashScope settings, the live test is skipped.

## Hardware validation

After the bridge reports that it is listening and the watch shows `Ready`:

1. Hold KEYA for at least five seconds while speaking, then release it. Confirm
   `Ready -> Recording -> Transcribing -> Result` and one short vibration only
   after capture stops.
2. Hold KEYA, press KEYB, and confirm the watch returns to `Ready` without an
   ASR request.
3. During recording, stop the bridge. Confirm the partial utterance is discarded,
   the watch shows a connection error, and it returns to `Ready` after reconnect.
4. Repeat at least 20 five-second utterances in the same watch and bridge
   processes. Check serial logs for a stable boot, correlated utterance IDs,
   and no `device_queue_overflow` event.

The serial-only hardware acceptance still requires confirming that physical
KEYA maps to `M5.BtnA`, KEYB maps to `M5.BtnB`, microphone samples are non-silent,
and the 45 ms / strength 60 release vibration is perceptible but absent from the
captured utterance.

## Prototype security and privacy limits

- The WebSocket is plain `ws://` with no authentication. Use it only on a trusted
  development LAN; do not port-forward or expose the bridge publicly.
- Audio is sent only between KEYA press and release and is buffered in memory for
  at most 60 seconds, but the completed utterance is sent to DashScope for ASR.
- Normal logs include IDs, byte counts, latency, queue watermarks, and error codes.
  They must not contain PCM/Base64 data, API keys, Wi-Fi passwords, or full
  transcripts.
- There is no production launchd service, TLS, multi-device routing, or offline
  transcription fallback in this change.

## Restore the factory demo

Before this prototype was flashed, the connected device booted the official
`StopWatch-UserDemo` built with ESP-IDF 5.5.4. To restore it, use the pinned
official source and its documented full ESP-IDF build/flash flow:

```bash
git clone https://github.com/m5stack/M5StopWatch-UserDemo.git
cd M5StopWatch-UserDemo
git checkout 6b4aa125288b6fe9dca661f10159f6e1e5ee785c
python3 ./fetch_repos.py
. /path/to/esp-idf-v5.5.4/export.sh
idf.py build
idf.py -p /dev/cu.usbmodemNNNN flash monitor
```

This restores the factory project's bootloader, partition table, and application;
stopping the Mac bridge alone does not modify any existing buddy project.
