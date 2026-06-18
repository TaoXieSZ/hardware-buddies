## Context

The Web Studio editor (`web/src/App.tsx`) renders a per-key shortcut field as a `<select>` bound to a 9-entry `shortcutOptions` list; the selected value writes `action.hidCodes: number[]`. That array flows unchanged through `saveProfile` / `applyDryRun` / `writeHardware` → `POST /api/apply` → `ahakeyd` → `AhaKeyWebBridgeHelper apply-shortcut --hid-codes …` → `AhaKeyCommand.setKeyMapping(mode:keyIndex:hidCodes:)` over BLE. The firmware accepts arbitrary HID usage codes (page `0x07`); modifiers are themselves usage codes (e.g. Right GUI `0xE7`). The only ceiling is `0…255` per code and `count ≤ 98`, enforced in both `AhaKeyProfileValidator` and `WebBridgeHelper.validate`. So the capability gap is purely in the front-end input surface, not the pipeline.

## Goals / Non-Goals

**Goals:**
- Let the user bind *any* mappable key by pressing it, with optional modifiers, instead of choosing from 9 presets.
- Produce `hidCodes` that are valid by construction (range + count) and previewed before save.
- Keep the change confined to the front end; no protocol, daemon, or firmware edits.

**Non-Goals:**
- Remapping the AhaKey's own physical keys or its hardware scan codes (this binds what a key *sends*, not which key triggers it).
- Macro/sequence recording (multiple distinct keystrokes over time). Capture records one chord.
- Remote/over-the-air keymap config; this stays in the local Web Studio.

## Decisions

- **Key identity = `KeyboardEvent.code`, not `.key`.** `.code` is the physical-position identity (`KeyA`, `Digit1`, `F5`, `ArrowUp`), layout- and modifier-independent, which is what maps cleanly to a HID usage code. `.key` shifts with layout/shift state and is unreliable for binding.
- **Static map module `web/src/hidKeymap.ts`.** A frozen `Record<string, number>` from `event.code` → HID usage (page `0x07`): `KeyA…KeyZ` → `0x04…0x1D`, `Digit1…0` → `0x1E…0x27`, `Enter 0x28`, `Escape 0x29`, `Backspace 0x2A`, `Tab 0x2B`, `Space 0x2C`, punctuation `0x2D…0x38`, `F1…F24` → `0x3A…0x73`, arrows/nav `0x4F…0x52`,`0x49…0x4E`, plus modifiers `ControlLeft 0xE0 … MetaRight 0xE7`. Unknown `code` → capture rejects (spec: unmappable key).
- **Modifier order is deterministic.** Emit held modifiers first (Ctrl, Shift, Alt, GUI order), then the main key: `[…modifierUsages, keyUsage]`. Pressing a bare modifier records just that modifier usage.
- **Capture is an explicit, bounded mode.** A "录入" toggle puts one focused element into capture; its `onKeyDown` calls `preventDefault()` + `stopPropagation()`, reads the chord, writes `hidCodes`, and exits capture. A visible "按下要绑定的键… (Esc 取消)" hint signals the mode. This scopes the global side effects of swallowing keys to a deliberate window.
- **Preset dropdown stays.** Capture writes into the same `action.hidCodes`; the dropdown still quick-picks. `actionLabel` already falls back to `0x..` hex for non-preset codes, so captured values render without a preset entry.
- **Validate at the boundary.** Reuse the existing `validateProfile`/range rules; the capture handler refuses to emit `hidCodes` that violate `0…255` or `≤ 98` and shows the existing error surface.

## Risks / Trade-offs

- **Combo firmware semantics are unverified.** It is not yet confirmed whether the firmware replays a multi-byte `hidCodes` array as a *simultaneous chord* (true modifier+key) or a *sequence*. Single-key capture is safe regardless; combo support must be tested on the now-connected device before being trusted. Mitigation: ship single-key first, gate combo behind a device test task; if firmware only sequences, present combos as "may not behave as a true chord."
- **OS-swallowed shortcuts can't be captured.** `⌘Q`, `⌘Tab`, `⌘Space`, and similar never reach the page. The UI must fail honestly (spec scenario) rather than record a lone modifier. No code workaround exists in a browser.
- **`preventDefault` blast radius.** While in capture mode the page swallows keys; a stuck capture mode would trap the user. Mitigation: `Esc` and blur both exit capture; capture auto-exits on first non-modifier key.
- **Browser/layout edge cases.** Non-US layouts and dead keys may map oddly; anchoring on `event.code` (physical position) minimizes but does not fully erase this. Out-of-scope keys simply reject.
