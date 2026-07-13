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

### Final evidence

- Host geometry tests cover mapping, clipping, stride, bounds, and framebuffer
  overlap.
- Physical touch passed all corners, center, edges, and drag direction.
- LVGL partial refresh, Lua dirty/full drawing, display-owner switching, and
  competing Lua display ownership were verified on the device.
- The known portrait image at `c182a77` remains the documented rollback.

## Current apply progress

- Landscape geometry was extracted into a pure C helper and now has host tests
  for full/partial mapping, clipping, stride, bounds, and framebuffer overlap.
- The refactored firmware built and flashed after MAC verification; boot
  geometry remained correct.
- Physical touch acceptance hit TL/TR/BL/BR/C and recorded a positive
  left-to-right drag delta.
- The final local bare-Dispatcher gateway uses isolated `config.tab5.yaml` and
  `.env.tab5`, sends sanitized events directly, and exits cleanly on SIGTERM.
- Lua HTTP now supports `poll(timeout_ms)`, backward-compatible
  `serve_forever()`, and native NVS-backed bearer gating before body reads.
- The Agent Farm Skill, two-column terminal, automatic boot job, native bearer
  gate, NVS masking, bounded history, persistent error reaction, and
  concurrent-task fallback were flashed and physically accepted.
- One owner-authorized device Web Chat completed the full path and returned
  `WEBCHAT_OK`; usage was attributed to the fixed `tab5-operator`.
- Independent reviews approved the final ESP-Claw and Agent Farm diffs.
- The existing DHCP/Wi-Fi configuration is intentionally unchanged. Address
  drift is recovered by reading `wifi --status`, updating `.env.tab5`, and
  restarting only the gateway.

### Thermal regression resolved

The device was reconnected and a severe USB-C-area heat regression was traced
to backlight duty, not primarily to PPA rotation. ESP-Claw configured a true
100-percent LEDC duty, while the previous SSH firmware's
`M5.Display.setBrightness(100)` used a 0–255 scale (about 39 percent). A static
screen reduced heat only slightly; changing the ST7121 default to 40 percent
reduced duty from 1023 to 409 and produced a clearly cooler three-minute idle
A/B result while retaining landscape operation.
