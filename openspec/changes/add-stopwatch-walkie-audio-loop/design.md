## Context

This change implements the risk-isolation slice described in `docs/stopwatch-walkie-talkie-design.md`; see `proposal.md` for motivation and `specs/stopwatch-audio-loop/spec.md` for observable behavior.

The codebase has no StopWatch project today. The implementation spans a new ESP32-S3 firmware target and a new Python bridge on macOS. The M5Stack StopWatch documentation specifies the official PlatformIO `espressif32@6.12.0` platform, `esp32s3box` board definition, 16 MB flash layout, and `qio_opi` memory type. It also requires current StopWatch support in M5Unified/M5GFX. Hardware initialization must be copied from the official examples rather than inferred from schematics:

- M5Stack StopWatch PlatformIO and Arduino documentation: <https://docs.m5stack.com/en/core/StopWatch>
- M5Unified microphone baseline at commit `4fb444784c85791e0b0207701392b42be234b2e7`: <https://github.com/m5stack/M5Unified/tree/4fb444784c85791e0b0207701392b42be234b2e7/examples/Basic/Microphone>
- StopWatch factory/user demo at commit `6b4aa125288b6fe9dca661f10159f6e1e5ee785c`: <https://github.com/m5stack/M5StopWatch-UserDemo/tree/6b4aa125288b6fe9dca661f10159f6e1e5ee785c>

DashScope `qwen3-asr-flash` accepts a complete Base64-encoded or local audio file rather than the device's raw WebSocket stream directly. The bridge therefore owns utterance assembly and WAV wrapping before synchronous ASR submission. Region/workspace-specific endpoints are configuration, not hard-coded assumptions. Reference: <https://help.aliyun.com/en/model-studio/qwen-asr-api-reference>.

## Goals / Non-Goals

**Goals:**

- Produce the smallest firmware and bridge that prove KEYA-bounded microphone capture, sustained LAN transfer, cloud transcription, and display feedback on the physical StopWatch.
- Keep every protocol message correlated so failures can be diagnosed from device and bridge logs without recording or printing audio content.
- Make protocol parsing and ASR handling testable on the Mac without requiring hardware for every test run.
- Preserve a deterministic hardware toolchain and upstream initialization baseline.

**Non-Goals:**

- Production daemon installation, mDNS discovery, multiple-device routing, authentication, TLS, or exposure outside a trusted development LAN.
- Streaming partial transcripts; only a final transcript is returned after KEYA release.
- Agent dispatch, existing-session steering, TTS playback, permission approval, offline Whisper fallback, wake-on-wrist, or battery optimization.
- A production-final UI or shared abstraction with existing buddy firmware; the
  hardware trial does include a focused round-screen interface and an offline
  showcase for physical review.

## Decisions

### 1. Create an independent `stopwatch-walkie/` subproject

The project will follow the monorepo's independent-subproject convention:

```text
stopwatch-walkie/
├── platformio.ini
├── include/
│   └── config.h.example
├── src/
│   └── main.cpp
├── tests/
└── tools/
    └── walkie-bridge/
        ├── bridge.py
        ├── requirements.txt
        └── tests/
```

`platformio.ini` will reproduce the official StopWatch environment: `espressif32@6.12.0`, `esp32s3box`, Arduino, 16 MB flash, and `qio_opi`. M5Unified, M5GFX, M5PM1, M5IOE1, and the chosen Arduino WebSocket client will be pinned to exact versions or commits. The safe `config.h.example` template is copied to the ignored, project-specific `walkie_config.h` name so it cannot resolve the ESP32 framework's unrelated generic header; Python environment files are ignored as well.

Alternative considered: place the firmware in `claude-code-buddy`. Rejected because the design establishes StopWatch as a separate product and this slice does not use cc-bridge behavior.

### 2. Use M5Unified's StopWatch support as the hardware boundary

Firmware startup will begin with the official `M5.config()` / `M5.begin(cfg)` sequence. The loop will call `M5.update()` frequently so `M5.BtnA` and `M5.BtnB` transitions are not missed. Audio capture will follow the official microphone example's `M5.Speaker.end()`, `M5.Mic.begin()`, and chunked `M5.Mic.record(..., 16000)` sequence. KEYA maps to `M5.BtnA`; KEYB maps to `M5.BtnB`, with the physical mapping confirmed by a serial-only bring-up test before networking is added.

The firmware will not duplicate ES8311, PMIC, IO-expander, display, or button pin initialization already owned by the supported M5 libraries. Any required deviation from the pinned upstream examples must be documented beside the changed call.

Alternative considered: initialize I2S and board peripherals directly. Rejected because it duplicates board-specific power sequencing and creates the same platform-mismatch risk documented elsewhere in this repository.

### 3. Model the firmware as one explicit audio-loop state machine

The device states are `connecting`, `ready`, `recording`, `transcribing`, `result`, and `error`. Only these transitions can start or stop microphone reads and WebSocket messages:

```text
connecting ──connected──> ready ──KEYA down──> recording
     ^                      ^                    │  │
     │                      │ KEYB cancel        │  └─KEYA up──> transcribing
     │                      └────────────────────┘                  │
     │                                                             ├─transcript──> result
     └────────connection lost / reconnect──────────────────────────┴─error
```

One short vibration pulse occurs after a valid KEYA release, not on press, so it acknowledges that the utterance boundary was accepted without contaminating the recording. Result/error screens return to `ready` on the next valid KEYA press; connection errors return to `ready` only after reconnect.

Alternative considered: drive behavior directly from button callbacks. Rejected because network and ASR responses are asynchronous and ad-hoc callbacks make cancellation and reconnect races difficult to test.

### 4. Use a small versioned WebSocket framing protocol

The device opens one WebSocket to a configured `ws://<host>:<port>/audio` endpoint. Text frames are UTF-8 JSON control messages; binary frames contain only raw PCM. WebSocket ordering provides frame order, while explicit IDs prevent a late result from being displayed for a newer utterance.

Control messages use this shape:

```json
{"type":"hello","protocol":1,"device_id":"<stable-id>"}
{"type":"utterance.start","id":"<uuid>","audio":{"rate":16000,"bits":16,"channels":1,"encoding":"pcm_s16le"}}
{"type":"utterance.end","id":"<uuid>"}
{"type":"utterance.cancel","id":"<uuid>"}
{"type":"transcript","id":"<uuid>","text":"..."}
{"type":"error","id":"<uuid-or-null>","code":"...","message":"...","retryable":true}
```

Only one utterance may be active per connection. The bridge buffers at most 60 seconds (1,920,000 PCM bytes) per utterance; exceeding that bound cancels the utterance with a structured `utterance_too_large` error. Device binary chunks target 20 ms (640 bytes), large enough to avoid excessive frame overhead and small enough to keep button-release latency low. The bridge validates even byte length and the declared format before ASR submission.

Alternative considered: send WAV chunks from the device. Rejected because the final WAV length is unknown until release and WAV framing adds no value on the LAN hop. Alternative considered: Opus/WebRTC. Rejected for this risk spike because 256 kbit/s raw PCM is acceptable on a trusted LAN and codec complexity would obscure I2S/WebSocket stability.

### 5. Assemble WAV and call DashScope after utterance completion

The Python bridge uses an asyncio WebSocket server. Each connection owns a small utterance accumulator containing ID, declared format, byte count, and PCM bytes. On `utterance.end`, the bridge validates the buffer, writes a standard mono PCM WAV representation in memory or an OS-managed temporary file, and invokes the official DashScope Python client for `qwen3-asr-flash` in a worker thread so the asyncio receive loop remains responsive.

The bridge reads `DASHSCOPE_API_KEY` and region/workspace base URL configuration from environment variables. The API key is never sent to the device or included in logs. Logs contain event type, utterance ID, byte count, duration, latency, and error code; they do not contain PCM, Base64 audio, credential values, or full recognized text by default.

The ASR client is an interface injected into the connection handler. Unit tests use a deterministic fake; one credential-gated integration test exercises the live service with a committed synthetic/non-personal fixture or a generated tone-plus-speech fixture kept under the topic's test assets.

Alternative considered: stream PCM directly into a real-time ASR WebSocket. Rejected because `qwen3-asr-flash`'s documented API consumes complete audio inputs and the slice explicitly validates final transcription after PTT release.

### 6. Keep retry ownership simple

The firmware reconnects the bridge WebSocket with bounded exponential backoff and never replays a partial or completed utterance automatically. The bridge does not retry ambiguous ASR failures after a request may have reached the service; it returns a retryable error and lets the user press KEYA again. This prevents duplicate cloud submissions and keeps correlation behavior observable.

### 7. Use one round-screen renderer for runtime and offline review

The visual contract lives in `stopwatch-walkie/DESIGN.md`. A reusable M5GFX
renderer maps the six existing device states to a high-contrast perimeter ring,
central activity symbol, concise state copy, and physical KEYA/KEYB hints. The
recording view derives its waveform from the actual microphone sample peak.

The `m5stack-stopwatch-ui-demo` build uses the same renderer and microphone path
but deliberately skips Wi-Fi and WebSocket startup. KEYA and KEYB cycle forward
and backward through all states, making interface review possible without a
reachable Mac bridge. The demo never retains or transmits audio. This is a build
variant rather than a second UI implementation, so production state rendering
cannot drift from the reviewed screens.

The renderer allocates one 16-bit full-screen canvas in PSRAM after display
initialization and reuses it for every frame; if allocation fails, it falls back
to a minimal direct-display state screen. No LVGL, bitmap asset, font package, or
other dependency is added.

## Risks / Trade-offs

- **[M5Unified StopWatch support is recent]** → Pin the validated dependency commits, copy initialization from the cited examples, build before flashing, and verify buttons/mic/display separately before combining them.
- **[Raw PCM production can outrun WebSocket transmission]** → Use fixed 20 ms chunks, a bounded producer/consumer queue, queue high-water diagnostics, and fail the utterance rather than silently dropping samples.
- **[M5 microphone and speaker share audio resources]** → Keep the speaker disabled throughout this slice and follow the official microphone enable sequence; TTS playback remains a later change.
- **[Connection loss can leave half an utterance]** → Both endpoints discard partial state on disconnect; no automatic replay occurs.
- **[Cloud ASR increases latency and sends voice off-device]** → Show a distinct transcribing state, log latency without content, and retain the explicit privacy boundary already accepted in the product design.
- **[Plain `ws://` is observable and unauthenticated on the LAN]** → Bind only on a trusted development network, document no port forwarding/public exposure, and treat authentication/TLS as a requirement before production use.
- **[A 60-second in-memory cap limits long dictation]** → This slice targets short PTT commands; the cap prevents runaway memory and can be revisited with streaming storage if real usage requires longer commands.

## Migration Plan

1. Add the new subproject without changing any existing buddy or bridge.
2. Build the pinned firmware and run serial-only display/button/microphone bring-up before enabling Wi-Fi or WebSocket code.
3. Run bridge unit tests with a fake ASR client, then a credential-gated live ASR fixture test.
4. Flash the StopWatch and validate one utterance, cancellation, disconnect recovery, and the 20-utterance stability run on a trusted LAN.
5. If the audio loop is unstable, stop after collecting serial/bridge evidence and revisit raw PCM/WebSocket buffering before starting MVP slice 1.

Rollback does not affect existing repository products: stop the prototype bridge and reflash the StopWatch factory firmware or the last known-good image. No existing data or protocol migration is required.
