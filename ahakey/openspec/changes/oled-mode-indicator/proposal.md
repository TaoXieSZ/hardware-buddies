## Why

The AhaKey has three work modes (Mode 0 默认层 / Mode 1 AI 助手层 / Mode 2 工具层), and the firmware already shows a *per-mode* OLED animation — each mode has its own image slot, auto-displayed when the user switches modes. But the current images don't encode *which* mode they are: Mode 0 shows the Claude GIF, Mode 1 the Cursor GIF, and **Mode 2 has no default at all**. A user glancing at the OLED can't reliably tell the active mode, and after switching has no confirmation of where they landed.

The OLED format is now confirmed: **160×80, RGB565 (16-bit color), ≤74 frames per mode**. So a legible, colored mode marker is feasible.

## What Changes

- **Stamp a persistent mode marker onto every encoded OLED frame** (in `OLEDFrameEncoder`), so any image — built-in default *or* a user-uploaded GIF — carries an unmistakable indicator of its mode. The marker is a small fixed-position badge: the mode digit (0/1/2) plus a per-mode accent color, sized for 160×80.
- **Provide a Mode 2 default asset** so the third mode is no longer blank.
- Surface the marker in the editor's OLED preview so what the user sees matches what the device will show.
- **(Optional, secondary)** A transient "切换到 Mode N" flash: the host already detects `workMode` changes (poll + notification); on switch it may push a brief banner frame. Kept out of the core unless the baked-in marker proves insufficient.

## Capabilities

### New Capabilities
- `oled-mode-indicator`: Making the active work mode visually identifiable on the device OLED by embedding a per-mode marker into the displayed frames (and giving every mode a default image).

### Modified Capabilities
<!-- None: no existing spec defines OLED rendering behavior. -->

## Impact

- **`platforms/macos/Sources/Utilities/OLEDFrameEncoder.swift`** — add a marker overlay during `encodeFrame` (after the centered draw, before RGB565 packing), parameterized by mode.
- **`platforms/macos/Sources/Utilities/DefaultOLEDAssets.swift` + `Resources/DefaultOLED/`** — add a Mode 2 default GIF.
- **`platforms/macos/Sources/Views/OLEDManagerView.swift`** — preview reflects the marker.
- **No protocol change**: `uploadOLEDFrames(mode:)` / `updatePicture(mode:)` and the 160×80 RGB565 contract are unchanged (`AhaKeyProtocol`: `oledWidth 160`, `oledHeight 80`, `oledMaxFrames 74`).
- **Scope note**: OLED upload currently lives only in the SwiftUI GUI app, not the Web Studio. This change targets the GUI/encoder path; bringing OLED upload to Web Studio is a separate, larger effort.
