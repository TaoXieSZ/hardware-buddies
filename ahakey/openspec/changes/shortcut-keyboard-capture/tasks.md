## 1. HID keymap module

- [x] 1.1 Add `web/src/hidKeymap.ts` exporting a frozen `Record<string, number>` from `KeyboardEvent.code` → USB HID usage (page 0x07): letters, digits, F1–F24, Enter/Esc/Backspace/Tab/Space, punctuation, arrows/nav, and modifiers `ControlLeft 0xE0 … MetaRight 0xE7`.
- [x] 1.2 Add a `MODIFIER_HID` set and a `codeToHid(code)` helper returning `number | null` (null = unmappable).
- [x] 1.3 Add `formatHidLabel(hidCodes)` producing a readable label (e.g. `⌘ + S`), reusing the modifier glyphs; fall back to `0x..` hex for unknown codes (keep `actionLabel` behavior consistent).

## 2. Capture interaction in the editor

- [x] 2.1 Add capture state to `App` (e.g. `capturing: boolean`) and a "录入" toggle button next to the existing preset `<select>` in the shortcut field.
- [x] 2.2 Render a focused capture target with hint text "按下要绑定的键… (Esc 取消)" while `capturing` is true.
- [x] 2.3 Implement `onKeyDown`: `preventDefault()` + `stopPropagation()`, collect held modifiers (Ctrl/Shift/Alt/GUI) + the main key via `codeToHid`, build `[...modifierUsages, keyUsage]` in deterministic order, write `action.hidCodes`, then exit capture.
- [x] 2.4 Handle edge cases: bare modifier press records just that modifier; `Esc` and blur cancel without changing the binding; unmappable key shows "不支持的按键" and records nothing.

## 3. Validation & integration

- [x] 3.1 Before committing a captured value, enforce code `0…255` and `hidCodes.count ≤ 98`; on violation, block and surface via the existing `validationErrors` box (do not write hardware). (Also added the count check to `validateProfile`.)
- [x] 3.2 Confirm captured `hidCodes` flow unchanged through `saveProfile` / `applyDryRun` / `writeHardware` (no `shortcutOptions`-only assumptions remain in the save path).
- [x] 3.3 Keep the preset dropdown working; switching preset↔capture leaves no stale value.

## 4. Device verification (uses the connected AhaKey)

- [ ] 4.1 Single-key: capture a letter, write to hardware, confirm the AhaKey emits that key.
- [ ] 4.2 Combo: capture `⌘ + S`, dry-run to inspect the command plan, write, and observe whether firmware produces a true chord or a sequence; record the finding and adjust combo UX/labeling (or limit to single-key) per the result.
- [ ] 4.3 OS-swallowed combo (`⌘Q`/`⌘Tab`): confirm the UI fails honestly rather than recording a lone modifier.

## 5. Docs

- [x] 5.1 Note the capture feature and the verified combo behavior in `web/README.md`.
