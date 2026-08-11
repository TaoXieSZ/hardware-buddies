# StopWatch Walkie

M5 StopWatch push-to-talk audio prototype. The watch captures 16 kHz mono
signed 16-bit PCM while KEYA is held, streams it to a trusted-LAN Mac bridge,
displays the final DashScope `qwen3-asr-flash` transcript, and either plays that
text through the watch speaker (protocol v1) or presents an authenticated
proposal for steering one existing local coding-agent session (protocol v2).

Protocol v2 supports live Claude Code, Codex, OpenCode, and Kimi Code sessions
already visible to cc-bridge. It never starts a new agent. The exact recognized
command remains server-side until KEYA approves it; KEYB rejects it without
sending terminal input.

## Prerequisites

- PlatformIO Core 6.x
- Python 3.10 or newer
- macOS `say` and `afconvert` for the current dependency-free TTS path
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

### Enable protocol-v2 control mode

Control mode is opt-in and falls back to protocol-v1 audio-only operation when
disabled. Generate one random 32-byte base64url secret locally:

```bash
python3 -c 'import base64,secrets; print(base64.urlsafe_b64encode(secrets.token_bytes(32)).rstrip(b"=").decode())'
```

Put the same value in the ignored files only:

- `include/walkie_config.h`: set `WALKIE_CONTROL_ENABLED` to `1` and
  `WALKIE_CONTROL_SECRET` to the generated value.
- `tools/walkie-bridge/.env`: set `WALKIE_CONTROL_ENABLED=1` and
  `WALKIE_CONTROL_SECRET` to the same value.

The secret must decode to exactly 32 bytes. Never commit either local file.
Protocol v2 authenticates and replay-protects control JSON with HMAC-SHA-256;
PCM audio remains plaintext on the trusted LAN.

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

The `m5stack-stopwatch-ui-demo` environment uses the same embedded 466×466 PNG
assets as the runtime firmware but skips Wi-Fi and WebSocket startup. It is intended for
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
export WALKIE_TTS_VOICE='Tingting'
python bridge.py --host 0.0.0.0 --port 8765
```

启动后在 Mac 浏览器打开 [StopWatch Bridge 看板](http://127.0.0.1:8766/)。
8765 是手表连接的 WebSocket 端口；8766 是只绑定 IPv4 loopback 的只读
看板；现有 cc-bridge 页面使用的 18765 与这两个端口不是同一个服务。
看板默认开启，也可以在忽略提交的 `.env` 中调整：

```dotenv
WALKIE_DASHBOARD_ENABLED=1
WALKIE_DASHBOARD_PORT=8766
```

For control mode, start cc-bridge first, then walkie-bridge. The default local
socket is `/tmp/cc-bridge.sock` and is created owner-only (`0600`). A typical
ignored `.env` addition is:

```dotenv
WALKIE_CONTROL_ENABLED=1
WALKIE_CONTROL_SECRET=<same-base64url-secret-as-the-watch>
WALKIE_CC_BRIDGE_SOCKET=/tmp/cc-bridge.sock
WALKIE_PROPOSAL_TTL=60
WALKIE_CONTROL_ALIASES_JSON={"小表 codex":{"agent":"codex","project_label":"hardware-buddies"}}
```

Targeting is deterministic. A configured alias must prefix the spoken command,
or a supported agent name may prefix a unique live agent/session/project label.
Examples: `小表 codex 跑单元测试`, `claude alpha review this diff`, and
`opencode website fix the header`. Missing or ambiguous targets show a bounded
retry error; the bridge does not ask an LLM to guess the destination.

看板会把语音链路明确标成录音、百炼识别、路由、手表确认和 Agent 执行。
如果只看到 ASR 完成而没有提案，先看“识别与诊断”：`target_required`
表示语音没有以 Agent/别名开头，`target_not_found` 表示没有匹配会话，
`target_ambiguous` 表示需要补充会话/项目标签，`spawn_not_supported` 表示
试图从手表新建会话，`control_plane_unavailable` 表示 cc-bridge 控制面离线。
这些错误都不会发送终端输入。

| Agent | Steer existing unique session | Reply to permission on watch |
|---|---:|---:|
| Claude Code | Yes | Yes |
| OpenCode | Yes | Yes |
| Codex | Yes | No; answer in terminal |
| Kimi Code | Yes | No; answer in terminal |

Codex or Kimi panes whose available native identity collides remain visible but
are marked non-steerable. No “first pane wins” fallback is used.

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
   `Ready -> Recording -> Transcribing -> Result`, a readable transcript, one
   short vibration after capture stops, and matching speech from the watch.
2. Hold KEYA, press KEYB, and confirm the watch returns to `Ready` without an
   ASR request.
3. During recording, stop the bridge. Confirm the partial utterance is discarded,
   the watch shows a connection error, and it returns to `Ready` after reconnect.
4. Repeat at least 20 five-second utterances in the same watch and bridge
   processes. Check serial logs for a stable boot, correlated utterance IDs,
and no `device_queue_overflow` event.

With protocol v2 enabled, additionally verify a harmless existing session:

1. Speak an explicit alias and command. Confirm the proposal screen shows the
   intended agent/project/session and exact bounded preview before any terminal
   input occurs.
2. Press KEYB and confirm the target pane receives nothing.
3. Repeat, press KEYA once, and confirm the exact text plus one Enter arrives
   once. Repeating or replaying the decision must not send it again.
4. Disconnect and reconnect while a task is running. The watch may restore the
   latest state or cached terminal result, but must not redispatch.

The serial-only hardware acceptance still requires confirming that physical
KEYA maps to `M5.BtnA`, KEYB maps to `M5.BtnB`, microphone samples are non-silent,
and the 45 ms / strength 60 release vibration is perceptible but absent from the
captured utterance.

## Prototype security and privacy limits

- Protocol-v1 WebSocket traffic is unauthenticated. Protocol v2 authenticates
  control messages and rejects replay, but both still use plaintext `ws://`.
  Use only on a trusted development LAN; do not port-forward or expose the
  bridge publicly. WSS is required before production or untrusted networks.
- Audio is sent only between KEYA press and release and is buffered in memory for
  at most 60 seconds, but the completed utterance is sent to DashScope for ASR.
- Normal logs include IDs, byte counts, latency, queue watermarks, and error codes.
  They must not contain PCM/Base64 data, API keys, Wi-Fi passwords, or full
  transcripts.
- The dashboard is read-only, memory-only, and bound to `127.0.0.1`. It keeps at
  most 200 semantic events and at most 160 UTF-8 bytes of transcript/proposal
  preview. It never exposes secrets, PCM, cwd/surface/session identifiers, raw
  control payloads, terminal output, or a command-entry endpoint. Physical
  KEYA/KEYB confirmation remains the only approval authority.
- There is no production launchd service, WSS, multi-device routing, mDNS, or offline
  transcription fallback in this change.

To roll back, set `WALKIE_CONTROL_ENABLED=0` in both ignored configurations,
restart walkie-bridge, and rebuild/reflash the runtime firmware. cc-bridge's
existing BLE/session behavior is unchanged; no data migration is required.
To disable only the browser view without changing watch behavior, set
`WALKIE_DASHBOARD_ENABLED=0` and restart walkie-bridge.

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
