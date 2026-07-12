# Implementation Status

This change is intentionally used as a cross-repository coordination spec.
The user explicitly authorized implementation edits in:

- `/Users/txie/OpenSourceProjects/esp-claw`
- `/Users/txie/OpenSourceProjects/agent-farm/dispatch`
- this OpenSpec change directory

## Landscape baseline audit

### Code covered

- Logical 1280×720 geometry with physical 720×1280 DSI timing.
- Shared panel-boundary RGB565 PPA rotation for emote, LVGL, and Lua display.
- Logical clipping, original source stride, serialized blocking PPA, and
  in-place framebuffer rejection.
- ST7123 physical-to-landscape coordinate transform with edge clamping.
- Shared geometry across emote/Lua display ownership changes.
- Boot diagnostics and a known portrait rollback commit.

### Existing evidence

- Full ESP-IDF v5.5.4 build completed successfully.
- Firmware was flashed only after confirming device MAC
  `80:f1:b2:d1:51:7d`.
- Boot log reported logical 1280×720 and physical 720×1280.
- User confirmed the screen is landscape and upright with USB on the left.

### Missing evidence

- Host tests for rectangle mapping, clipping, stride, bounds, and in-place
  rejection.
- Physical four-corner/center/edge touch and horizontal/vertical drag tests.
- LVGL partial-flush and Lua full/dirty-rectangle runtime evidence.
- Repeated animation and display owner-switch stress evidence.
- A proven portrait rollback binary or a rollback/reflash drill.

The implementation is therefore functionally promising but not yet accepted
against the complete `tab5-esp-claw-landscape` specification.

## Current apply progress

- Landscape geometry was extracted into a pure C helper and now has host tests
  for full/partial mapping, clipping, stride, bounds, and framebuffer overlap.
- The refactored firmware built and flashed after MAC verification; boot
  geometry remained correct.
- Physical touch acceptance hit TL/TR/BL/BR/C and recorded a positive
  left-to-right drag delta.
- Gateway event forwarding now sends the sanitized event as the direct JSON
  body, and a subprocess test proves SIGTERM closes both listener and SSE.
- Lua HTTP now supports `poll(timeout_ms)`, backward-compatible
  `serve_forever()`, and native NVS-backed bearer gating before body reads.
- The Agent Farm Skill and initial two-column Lua terminal are implemented but
  not yet flashed or physically accepted.

### Blocker

The Tab5 USB-Serial-JTAG device stopped enumerating after the latest test run.
No `/dev/cu.usbmodem*` port is currently present, so the native bearer gate,
NVS masking, packaged terminal, and cooperative HTTP/LVGL loop cannot be
flashed or accepted until the device is reconnected.

### Thermal regression resolved

The device was reconnected and a severe USB-C-area heat regression was traced
to backlight duty, not primarily to PPA rotation. ESP-Claw configured a true
100-percent LEDC duty, while the previous SSH firmware's
`M5.Display.setBrightness(100)` used a 0–255 scale (about 39 percent). A static
screen reduced heat only slightly; changing the ST7121 default to 40 percent
reduced duty from 1023 to 409 and produced a clearly cooler three-minute idle
A/B result while retaining landscape operation.
