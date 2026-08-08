## 1. Ground Truth and Project Scaffold

- [x] 1.1 Re-read the pinned M5Unified microphone/button examples and StopWatch user demo, record any required initialization deviations, and pin exact M5Unified, M5GFX, M5PM1, M5IOE1, and WebSocket client revisions.
- [x] 1.2 Create the `stopwatch-walkie/` PlatformIO project with the official `espressif32@6.12.0`, `esp32s3box`, 16 MB flash, and `qio_opi` settings, plus ignored local configuration and a safe `config.h.example`.
- [x] 1.3 Create `tools/walkie-bridge/` with isolated runtime/test dependencies, environment-variable configuration, and no committed credentials.

## 2. Bridge Protocol and ASR

- [x] 2.1 Add failing Python tests for WebSocket control-message validation, one-active-utterance correlation, invalid frame order, cancellation, disconnection cleanup, format rejection, and the 60-second buffer limit.
- [x] 2.2 Implement the asyncio WebSocket connection handler and bounded PCM accumulator until the protocol tests pass.
- [x] 2.3 Add failing tests for PCM-to-WAV wrapping, successful/fake transcription, no-speech responses, timeout/service failures, credential absence, and log redaction.
- [x] 2.4 Implement the injected ASR interface, WAV assembly, structured result/error messages, and non-blocking `qwen3-asr-flash` client call until the ASR tests pass.
- [x] 2.5 Add a credential-gated live DashScope integration test using non-personal fixture audio and document how the test is skipped when credentials are absent.

## 3. StopWatch Hardware Bring-up

- [ ] 3.1 Build and flash a serial-only bring-up that verifies the pinned toolchain, display initialization, physical KEYA→`M5.BtnA` and KEYB→`M5.BtnB` mapping, and `M5.update()` polling without networking.
- [ ] 3.2 Add the official 16 kHz `int16_t` M5Unified microphone sequence and verify sample count, non-silent amplitude, and sustained five-second capture over serial without enabling the speaker.
- [ ] 3.3 Verify a short release-acknowledgement vibration on real hardware without recording vibration noise into the utterance, and capture the chosen timing/intensity in code comments.

## 4. Firmware Audio Loop

- [x] 4.1 Implement the explicit connecting/ready/recording/transcribing/result/error state machine and test pure transition logic independently from hardware where practical.
- [x] 4.2 Implement versioned JSON control messages, unique utterance IDs, result/error correlation, and rejection of late or mismatched responses.
- [x] 4.3 Implement 20 ms PCM capture chunks, a bounded producer/consumer queue, binary WebSocket transmission, queue high-water diagnostics, and fail-fast behavior instead of silent sample drops.
- [x] 4.4 Wire KEYA press/hold/release to start/stream/end, KEYB to cancel and discard, and connection loss to partial-utterance cleanup.
- [x] 4.5 Implement the round-display states and retry behavior for connection, recording, transcription, result, no-speech, and recoverable errors.
- [x] 4.6 Implement bounded WebSocket reconnect backoff without automatically replaying audio or submitting duplicate ASR requests.

## 5. End-to-End Verification and Documentation

- [x] 5.1 Run all bridge unit tests and static/syntax checks, then run a clean PlatformIO build for the StopWatch environment.
- [ ] 5.2 On a trusted LAN, verify successful transcription, KEYB cancellation with no ASR call, unsupported/invalid frame errors, ASR failure recovery, and mid-utterance disconnect recovery.
- [ ] 5.3 Complete the physical 20-utterance stability run with utterances of at least five seconds and confirm no device reboot, bridge restart, cross-utterance audio, permanent stuck state, or silent queue overflow.
- [x] 5.4 Review logs and repository contents to confirm that PCM/Base64 audio, full transcripts, Wi-Fi credentials, DashScope credentials, and local endpoint secrets are not logged or tracked.
- [x] 5.5 Document build, configuration, bridge startup, hardware validation, known prototype security limits, and factory-firmware rollback in `stopwatch-walkie/README.md`.
- [x] 5.6 Add the seventh subproject to the root `AGENTS.md`, `CLAUDE.md`, and `README.md` tables without changing unrelated project documentation.

## 6. Round UI Hardware Trial

- [x] 6.1 Create a durable `stopwatch-walkie/DESIGN.md` contract for the 466×466 round display, six runtime states, physical-key cues, motion, contrast, and trial acceptance criteria.
- [x] 6.2 Implement one reusable M5GFX renderer for runtime and an offline `m5stack-stopwatch-ui-demo` build that cycles states with KEYA/KEYB and drives the recording waveform from the real microphone without transmitting audio.
- [x] 6.3 Run native tests and clean runtime/showcase builds, flash the showcase to the identified StopWatch, and capture serial evidence that the PSRAM canvas initializes without a boot loop.

### UI Trial Checkpoint — 2026-08-07

- Native state/protocol/queue tests passed 7/7; clean `m5stack-stopwatch` and `m5stack-stopwatch-ui-demo` builds both succeeded.
- The showcase was flashed to StopWatch MAC `28:84:85:43:AE:38` over `/dev/cu.usbmodem1101`; all written image hashes verified.
- Cold-start serial reported `[ui] psram 466x466 canvas on 468x468 display`, initialized M5IOE1 at `0x4F`, reached `[ui-demo] offline showcase ready; KEYA=next KEYB=back`, and showed no reboot loop during the capture window.
- Physical review of text size, ring brightness, key mapping, and microphone-driven waveform remains an owner trial; it does not block the verified showcase flash.

## Hardware Validation Checkpoint — 2026-08-07

- Flashed the validated `m5stack-stopwatch` firmware to StopWatch MAC `28:84:85:43:AE:38` over `/dev/cu.usbmodem1101`; all image hashes verified and the device showed no boot loop.
- The device joined `Roblox_Public` and received `192.168.200.109`; the Mac was on a different network at `192.168.6.82`.
- Mac-to-device ping failed and a 12-second listener on port 8765 received no device connection, confirming that the current networks cannot carry the local WebSocket loop.
- Resume tasks 3.1–3.3 and 5.2–5.3 at home: put the Mac and StopWatch on the same trusted LAN, update the ignored `include/walkie_config.h` host/Wi-Fi values, rebuild and flash, start the bridge, then perform the physical and 20-utterance checks.
- Wi-Fi and DashScope credentials are intentionally not recorded in this checkpoint.
