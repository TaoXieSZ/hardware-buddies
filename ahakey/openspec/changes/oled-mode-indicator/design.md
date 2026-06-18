## Context

OLED frames are stored and addressed per work mode. `AhaKeyBLEManager.uploadOLEDFrames(mode:)` → `AhaKeyCommand.updatePicture(mode:)` writes a mode's animation; the firmware auto-displays the active mode's frames on a physical mode switch. `OLEDFrameEncoder.encodeFrame` rasterizes each source image onto a 160×80 RGBA context (aspect-fit, centered on black) then packs it to **RGB565 big-endian, 25,600 bytes/frame** (slot 28,672; ≤74 frames/mode). Confirmed constants: `oledWidth 160`, `oledHeight 80`, `oledMaxFrames 74`. Defaults: Mode 0 `claude_0.gif`, Mode 1 `cursor_0.gif`, Mode 2 none. The host also tracks the live mode via `AhaKeyBLEManager.workMode` (polled + notification), so it always knows the current mode. OLED upload UI exists only in the SwiftUI GUI app.

## Goals / Non-Goals

**Goals:**
- Make the active mode identifiable from the OLED, for both default and user-uploaded images.
- Give Mode 2 a default image.
- Keep the marker correct by construction (applied at encode time) and previewable.

**Non-Goals:**
- Adding OLED upload to the Web Studio (separate, larger effort).
- Changing the BLE protocol, frame format, or per-mode slot model.
- A live host-driven HUD as the primary mechanism (the firmware already swaps per-mode images; a host flash is at most an optional extra).

## Decisions

- **Overlay the marker in `encodeFrame`, parameterized by mode.** Thread a `mode` (0/1/2) into `frames(fromGIFAt:mode:)` → `encodeFrame(_:mode:)`. After the centered draw and before RGB565 packing, draw a fixed-position badge onto the same CGContext. Doing it here means *every* path — factory asset or user GIF — gets a consistent marker, satisfying the "marker survives a custom image" requirement without per-asset editing.
- **Marker = digit + per-mode accent, with a contrasting backing.** A small filled rounded rect (e.g. top-left, ~22×18 px on the 160×80 canvas) in a per-mode color (Mode 0 / 1 / 2 → three distinct accents) with the mode digit in white. A solid backing guarantees legibility over any underlying frame (the "readable on a busy frame" requirement). Digits are safest at this resolution; an optional 1–2 char layer initial can be added if it renders cleanly.
- **Reserve the marker region from the fitted art.** The aspect-fit draw already letterboxes; keep the badge in a corner so it overlaps minimal content. No need to shrink the art beyond the existing fit.
- **Mode 2 default asset.** Add a `Resources/DefaultOLED/<name>.gif` and map `AhaKeyModeSlot.mode2` to it in `DefaultOLEDAssets.bundledFileName`. The asset itself need not pre-bake the marker (the encoder adds it), so a simple distinct base image suffices.
- **Preview reuses the encoder's marker.** `OLEDManagerView` preview should render through (or mirror) the same overlay so what's previewed equals what's written.
- **Verify format against hardware before finalizing colors.** Use `readPicState(mode:)` on the connected device to confirm current frame slots per mode, and write one marked frame to confirm the marker's color/position survives the real panel (gamma, color order) before locking the palette.

## Risks / Trade-offs

- **RGB565 color fidelity.** 16-bit color + possible panel gamma/byte-order quirks may shift the accent colors; pick high-contrast, well-separated hues and confirm on-device (the encoder comment already notes RGB565 big-endian, no padding — trust it but verify the marker visually).
- **Small canvas legibility.** At 160×80 a multi-character Chinese label ("默认层") is cramped; defaulting to a digit + color keeps it crisp. Layer-name text is a stretch goal gated on a device readability check.
- **Per-frame cost.** Stamping every frame (≤74) is cheap CPU but must not regress the existing upload throughput; the overlay is a constant-size draw per frame.
- **Coverage of underlying content.** A corner badge hides a small region of the animation; acceptable trade for always-on legibility. Position chosen to overlap the letterboxed margin where possible.
- **GUI-only reach.** Users who only use the Web Studio won't get this until OLED upload is ported there; documented as out of scope here.
