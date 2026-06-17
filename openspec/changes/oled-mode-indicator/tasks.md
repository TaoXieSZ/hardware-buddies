## 1. Confirm hardware specifics

- [x] 1.1 Confirmed via device: all 3 modes share ONE frame buffer (~74 slots); each mode occupies a non-overlapping range declared by `updatePicture(mode, startIndex, frameCount)`. **Critical:** uploading every mode to `startIndex 0` overwrites them — must allocate non-overlapping ranges (claude 0–29, cursor 30–34, tools 35). The official client does this dynamically via `readPictureState` (0x83) + `resolveOLEDUploadStartIndex`.
- [x] 1.2 Confirmed on real panel: RGB565 byte order/colors render correctly; blue/green/orange accents are clearly distinguishable. OLED shows the mode image when idle and key-label text on keypress (normal firmware behavior).

## 2. Marker overlay in the encoder

- [x] 2.1 Thread `mode: AhaKeyModeSlot` into `OLEDFrameEncoder.frames(fromGIFAt:mode:)` and `encodeFrame(_:mode:)`.
- [x] 2.2 After the centered draw and before RGB565 packing, draw a fixed-position badge (bottom-right ~24×18 px): rounded accent backing + white mode digit (Core Text). Rendered preview confirms legibility.
- [x] 2.3 Define per-mode palette (`modeAccentColor(for:)`: blue/green/orange) and badge geometry (`badgeRect(width:height:)`) in one place for preview reuse.

## 3. Mode 2 default asset

- [x] 3.1 Add `Resources/DefaultOLED/tools_0.gif` (160×80, "TOOLS").
- [x] 3.2 Map `AhaKeyModeSlot.mode2` → `"tools_0"` in `DefaultOLEDAssets.bundledFileName(for:)`.

## 4. Editor preview parity

- [ ] 4.1 Render the same marker in the editor's OLED preview (SwiftUI). Helpers `modeAccentColor(for:)` / `badgeRect(...)` are exposed for reuse. Deferred: preview lives in the SwiftUI app the user isn't using; low priority vs. device verification.

## 5. Update callers

- [x] 5.1 `AhaKeyStudioView.swift:1806` (the only `OLEDFrameEncoder.frames` call site) now passes `mode: targetMode`. Builds clean.

## 6. Device verification

- [x] 6.1 Uploaded marked defaults to all three modes via `AhaKeyWebBridgeHelper upload-oled` with non-overlapping start indices (claude→0, cursor→30, tools→35). **User confirmed on device: all three modes show distinct images + correct 蓝0/绿1/橙2 badges, no garble.**
- [x] 6.2 Marker is applied at encode time for any GIF (the helper encodes the source GIF + badge), so custom uploads carry it too.
- [x] 6.3 Digit + accent-color marker is legible on the real 160×80 panel (user-confirmed). Layer-name text not needed.

## 7. Docs

- [ ] 7.1 Document the mode marker (palette, position, "every mode has a default") near the OLED docs.
