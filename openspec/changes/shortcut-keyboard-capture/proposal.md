## Why

Today the AhaKey Web Studio only lets a user bind a shortcut by picking from a fixed dropdown of 9 presets (`shortcutOptions`: Enter / Escape / Tab / Space / Right Command / F17–F20). Any other key — a letter, a digit, an arrow, or a modifier combo like `⌘S` — is unreachable through the UI, even though the firmware accepts arbitrary HID usage codes. Users expect to *press the key they want* and have it recorded, the way every keybinding editor works.

## What Changes

- Add a **"键盘录入" (keyboard capture)** mode to the shortcut field in the profile editor: the user clicks "录入", presses a physical key (optionally with modifiers held), and the UI records the resulting USB HID usage code(s) into `action.hidCodes`.
- Introduce a **browser-key → HID usage code** mapping (USB HID keyboard usage page `0x07`) covering letters, digits, function keys, navigation, punctuation, and the four modifiers (Ctrl/Shift/Alt/GUI, left & right).
- During capture, **`preventDefault`/`stopPropagation`** the keydown so the browser/OS does not act on it (e.g. `⌘W` closing the tab), and show a live human-readable preview (e.g. `⌘ + S → 0xE3 0x16`).
- Keep the existing preset dropdown as a quick-pick fallback; capture augments it, does not remove it.
- Enforce existing validation in the capture path: each code `0…255`, total `hidCodes.count ≤ 98`; surface unmappable/uncapturable keys (e.g. `⌘Q`, `⌘Tab` are swallowed by the OS) with a clear message.

## Capabilities

### New Capabilities
- `shortcut-key-capture`: Capturing a physical key press (with optional modifiers) in the Web Studio profile editor and translating it into validated firmware HID usage codes for a key binding.

### Modified Capabilities
<!-- None: no existing spec under openspec/specs/ defines shortcut binding behavior yet. -->

## Impact

- **Frontend (primary)**: `web/src/App.tsx` — shortcut field UI in the editor panel; new keycode-map module (e.g. `web/src/hidKeymap.ts`); capture state + keydown handler.
- **No firmware/protocol change required**: `action.hidCodes: number[]` already flows through `saveProfile` → `/api/apply` → `AhaKeyWebBridgeHelper` → `setKeyMapping`. Validation bounds (`mode 0…2`, `keyIndex 0…3`, code `0…255`, count `≤ 98`) are unchanged and already enforced in `AhaKeyCore/AhaKeyProfileValidator` and `WebBridgeHelper.validate`.
- **Open question to verify on the connected device**: how firmware interprets a multi-byte `hidCodes` array for a *combo* (simultaneous modifier+key chord vs. sequential replay). Single-key capture is unambiguous; combo encoding must be confirmed against hardware before shipping combo support.
