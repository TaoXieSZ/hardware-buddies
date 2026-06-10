# Proposal: M5Stack Tab5 as the flagship coding-agent buddy

**Status:** Draft for discussion · **Date:** 2026-06-10 · **Author:** research
agents `tab5-toolchain` + `tab5-capabilities`, synthesized in-session

## TL;DR — Recommendation

Tab5 (ESP32-P4 + ESP32-C6, 5″ 1280×720 touch, dual-mic AEC, camera, USB
host+device) can be a step-change over the stick/StackChan buddies: a
**multi-session agent dashboard with touch approval, full-duplex voice, and
direct HID typing into the Mac**. Build it in gated phases:

| Phase | Scope | Effort | Go/No-Go gate |
|-------|-------|--------|---------------|
| **P0** | Dev-env bringup: pioarduino + M5Unified hello world (display, touch, mic level, battery) | ~0.5–1 day | Builds, flashes over USB-C, screen + touch + mic verified. If the ST7121-batch screen misbehaves with current M5GFX, **stop and reassess library versions**. |
| **P1** | Buddy status screen over **WiFi WebSocket**: daemon WS writer + heartbeat JSON reuse; session card UI + scrolling transcript + touch Approve/Deny | ~3–5 days | A real Claude Code permission prompt resolved by tapping the screen, end-to-end. |
| **P2** | Voice input: dual-mic 16 kHz capture → WS stream → existing BlackHole→Doubao path (reuse the StickS3 chain, minus BLE/ADPCM) | ~2–3 days | Dictation lands in the focused Mac app with on-screen VU + transcript. |
| **P3** | **USB-C HID keyboard device**: Tab5 types into the Mac directly (Enter/undo/PTT-hotkey, later full text) — replaces the Quartz-synthesis + Accessibility-permission stack | ~2–4 days | Doubao's right-Cmd hotkey triggered by Tab5-as-keyboard with zero macOS permissions. |
| **P4** | Stretch: esp-sr AFE barge-in (speak while TTS plays) + wake word; camera presence detection (esp-who); USB-A macro keyboard | per-feature | Each gated on P1–P3 being in daily use. |

**Why this order:** P1 is the core buddy value and forces the new transport;
P2 reuses a chain we just shipped and verified on the StickS3; P3 removes the
two most fragile macOS dependencies we have (Quartz HID-state events,
Accessibility/TCC grants); P4 items are upside, not foundation.

## Hardware: what Tab5 adds over the existing fleet

| Capability | Plus2 stick | StickS3 | CoreS3 StackChan | **Tab5** |
|---|---|---|---|---|
| Screen | 1.14″ 135×240 | 1.14″ 135×240 | 2.0″ 320×240 | **5″ 1280×720 IPS touch (5-pt)** |
| Input | 2 buttons | 2 buttons | touch | **touch + USB-A keyboard host + Reset/Boot key** |
| Mic | PDM (GPIO0 conflict) | ES8311 single | none used | **ES7210 AEC front-end + dual-mic array** |
| Speaker | buzzer | disabled | 1W WAV | **1W + 3.5mm jack + HP detect** |
| Radio | BLE | BLE | BLE | **WiFi 6 (via C6); BLE impractical (see below)** |
| Camera | — | — | GC0308 | **SC2356 2MP MIPI-CSI + on-chip ISP + PPA** |
| Acts as USB device | no | no | no | **yes — USB-C OTG can be an HID keyboard** |
| Wake | — | — | IMU poll | **HW wake: PMS150G aggregates BMI270 motion INT + RX8130 RTC INT** |
| Power | battery | battery | USB | USB-C 24/7 (designed for it) or NP-F550 ~6 h |

## Locked architecture decisions

1. **Transport: WiFi WebSocket, not BLE.** ESP32-P4 has no radio; everything
   tunnels to the C6 over ESP-Hosted/SDIO. BLE works only on the ESP-IDF
   route (Hosted-HCI); on Arduino/M5Unified the P4 BLE examples do not even
   compile (arduino-esp32 #11788). WiFi sockets are standard lwIP and proven
   on Tab5. The daemon side adds a WS writer alongside `BleWriter` in
   `tools/buddy_core/core.py`; the heartbeat JSON protocol is unchanged.
   Bonus: tens-of-Mbps link makes full transcripts and raw 16 kHz PCM
   trivial — no ADPCM, no 240-byte frames, no seq/CRC bookkeeping.
2. **Firmware stack: start Arduino (pioarduino + M5Unified) in P0–P2,
   keep ESP-IDF + esp-bsp + LVGL 9 as the planned migration for P3+/P4.**
   Arduino matches this repo's entire codebase and M5Unified already drives
   Tab5's display/touch/mic/speaker/IMU/RTC. But the differentiating
   features live in IDF-land: `usb_device` HID (P3 may still be reachable
   from Arduino's `USBHIDKeyboard.h` — verify early), esp-sr AFE/WakeNet,
   esp-who, esp_video. Decision checkpoint at end of P2.
3. **Version pins (from real-project breakage):** pioarduino
   `#55.03.35` (Arduino 3.3.5 / IDF 5.5.1) — IDF 5.5.2 introduced a Tab5
   MIPI-DSI backlight-flicker regression (M5GFX #185, arduino-esp32 #12417).
   M5Unified ≥0.2.17 + M5GFX ≥0.2.22 — required for the 2026-04 ST7121
   screen batch; older libs can white-screen on new units.

### Reference platformio.ini (from M5Stack docs + M5Tab-Macintosh)

```ini
[env:m5stack-tab5]
platform = https://github.com/pioarduino/platform-espressif32.git#55.03.35
framework = arduino
board = esp32-p4-evboard
board_build.mcu = esp32p4
board_build.flash_mode = qio
upload_speed = 1500000
monitor_speed = 115200
build_flags =
    -DBOARD_HAS_PSRAM
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    m5stack/M5Unified@^0.2.17
    m5stack/M5GFX@^0.2.22
```

## Phase notes

### P1 — agent dashboard (the core)

- UI budget is real now: 1280×720 at 60+ FPS (LVGL 9.3 measured 62 FPS;
  M5GFX also viable for P0–P1 simplicity). PSRAM 32 MB holds double
  720P RGB565 buffers with room to spare.
- Layout sketch: N session cards (one per Claude Code/Cursor/Codex
  session via cmux/daemon), each with state color, current tool, token
  counter; tap card → full-screen scrolling transcript; permission prompt
  → modal with Approve/Deny touch buttons (replaces stick A/B).
- Daemon: `MultiWsWriter` mirroring `MultiBleWriter`; same `apply_event`
  adapters; mDNS or static IP + `wifi_secrets.ini` for provisioning.

### P2 — voice

- M5.Mic on Tab5 is a complete M5Unified path (ES7210, 16 kHz OK) — no
  custom codec driver, and no TG1-watchdog minefield like the StickS3's
  ES8311 (still: bring mic up once at boot, never `end()` mid-loop).
- Stream raw PCM16 over the WS link; daemon plays into BlackHole exactly
  as `tools/sticks3_voice.py` does today. PTT = on-screen touch button
  (hold-to-talk) instead of a physical key.

### P3 — USB HID device

- Tab5's USB-C is OTG; as an HID keyboard it types into the Mac with no
  Accessibility permission, no Quartz event-source tricks, no TCC fragility.
  Caveat: USB-C is also the flash/serial port — switching to device mode
  loses the serial console; need OTA or accept re-flash friction.
- USB5V_EN on the PI4IOE5V6408-2 expander gates USB-A host power —
  firmware must enable it before a keyboard on USB-A will enumerate (P4).

## Known gotchas (verified)

- Charging only works while powered on; cutting power without soft
  shutdown can wedge IMU init on next boot (wait 5 s before re-power).
- No Classic Bluetooth ever (C6 is BLE-only) — Bluetooth Serial is off
  the table regardless of stack.
- The screen panel changed twice (ILI9881C → ST7123 2025-10 → ST7121
  2026-04); library floors above are mandatory on new units.
- espp's third-party `m5stack-tab5` component lacks camera support;
  camera work should use esp_video + esp_cam_sensor (SC2356 = SC202CS,
  officially supported).

## Sources

| Topic | URL |
|---|---|
| Tab5 official docs (specs, pinmap, PlatformIO, screen revisions) | https://docs.m5stack.com/en/core/Tab5 |
| pioarduino releases | https://github.com/pioarduino/platform-espressif32/releases |
| MIPI-DSI flicker regression | https://github.com/m5stack/M5GFX/issues/185 · https://github.com/espressif/arduino-esp32/issues/12417 |
| Real Tab5 project with version pins | https://github.com/amcchord/M5Tab-Macintosh/blob/master/platformio.ini |
| M5Unified / M5GFX releases (Tab5 support: 0.2.6 / 0.2.7) | https://github.com/m5stack/M5Unified/releases · https://github.com/m5stack/M5GFX/releases |
| Factory demo (ESP-IDF 5.4.2, SDL2 desktop sim) | https://github.com/m5stack/M5Tab5-UserDemo |
| Official ESP-IDF BSP (IDF ≥5.4, LVGL examples) | https://components.espressif.com/components/espressif/m5stack_tab5 |
| ESP-Hosted BLE design (Hosted-HCI over SDIO) | https://github.com/espressif/esp-hosted-mcu/blob/main/docs/bluetooth_design.md |
| Arduino P4 BLE broken | https://github.com/espressif/arduino-esp32/issues/11788 |
| esp-sr AFE (AEC/VAD/WakeNet) supports P4 | https://github.com/espressif/esp-sr |
| esp-who P4 support | https://github.com/espressif/esp-who |
| SC2356/SC202CS sensor support | https://github.com/espressif/esp-video-components/blob/master/esp_cam_sensor/README.md |
| LVGL 9.3 on Tab5 at 62 FPS | https://forum.lvgl.io/t/lvgl-running-on-a-m5stack-device-on-60-fps/23435 |
| CNX reviews (teardown, IDF/Arduino bringup, PPA) | https://www.cnx-software.com/2025/05/14/m5stack-tab5-review-part-1-unboxing-teardown-and-first-try-of-the-esp32-p4-and-esp32-c6-5-inch-iot-devkit/ · https://www.cnx-software.com/2025/05/18/m5stack-tab5-review-getting-started-esp32-p4-esp-idf-framework-arduino-ide/ |
