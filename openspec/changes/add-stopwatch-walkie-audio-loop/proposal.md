## Why

The StopWatch walkie-talkie concept depends on a stable push-to-talk audio path before agent dispatch, steering, or approval workflows are worth building. The highest-risk assumptions are the StopWatch I2S capture path and sustained raw PCM transfer over WebSocket, so the first change must prove that loop end to end with real hardware.

## What Changes

- Add a seventh monorepo subproject, `stopwatch-walkie/`, with firmware for the M5 StopWatch and a Mac-side `walkie-bridge` prototype.
- Capture 16 kHz, signed 16-bit, mono PCM only while KEYA is held; releasing KEYA completes the utterance and gives tactile confirmation.
- Stream the captured PCM to the configured Mac bridge over a LAN WebSocket connection.
- Transcribe the completed utterance with DashScope `qwen3-asr-flash` and return the recognized text to the StopWatch.
- Show connection, recording, transcription, result, and recoverable error states on the round display.
- Keep this change limited to the audio-risk spike: no agent dispatch, session steering, TTS playback, permission approval, offline ASR fallback, or production launchd installation.

## Capabilities

### New Capabilities
- `stopwatch-audio-loop`: define the push-to-talk capture, PCM WebSocket exchange, ASR transcription, on-device feedback, cancellation, and recoverable error behavior for the StopWatch-to-Mac audio loop.

### Modified Capabilities
<!-- none — no existing capability requirements are changing -->

## Impact

- **New project**: `stopwatch-walkie/` firmware and `stopwatch-walkie/tools/walkie-bridge/` prototype daemon.
- **Hardware/platform**: M5 StopWatch (ESP32-S3), official `espressif32@6.12.0`, `qio_opi`, M5Unified, M5PM1, and M5IOE1; pioarduino is explicitly excluded.
- **External service**: DashScope receives captured voice audio for `qwen3-asr-flash` transcription and requires a local API credential that remains outside git.
- **Network contract**: one LAN WebSocket connection carries binary PCM upstream and structured control/result messages in both directions.
- **Existing systems**: no cc-bridge, buddy protocol, or existing firmware behavior changes in this slice.
