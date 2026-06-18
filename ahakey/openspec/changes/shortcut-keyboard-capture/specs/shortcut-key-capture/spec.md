## ADDED Requirements

### Requirement: Capture a key press into a binding

The profile editor SHALL provide a keyboard-capture control that records a physical key press, with any modifiers held at the moment of the press, into the selected key's `action.hidCodes`.

#### Scenario: Capture a single key
- **WHEN** the user activates capture and presses a single key (e.g. `S`) with no modifiers
- **THEN** `action.hidCodes` is set to that key's USB HID usage code (e.g. `[0x16]`) and the editor shows a readable label (e.g. `S`)

#### Scenario: Capture a modifier combo
- **WHEN** the user activates capture and presses a key while holding one or more modifiers (e.g. `⌘` + `S`)
- **THEN** `action.hidCodes` includes the held modifiers' usage codes and the key's usage code, and the editor shows a combined label (e.g. `⌘ + S`) and the resulting hex bytes

#### Scenario: Capture leaves the page unaffected
- **WHEN** a captured key press would otherwise trigger a browser or OS action (e.g. `⌘W`, `Tab`, `Space` scrolling)
- **THEN** the editor suppresses the default action during capture so no navigation, focus change, or scroll occurs

### Requirement: Translate browser keys to firmware HID codes

Capture SHALL map the physical key (layout-independent identity, i.e. `KeyboardEvent.code`) to USB HID keyboard usage-page (`0x07`) codes, covering letters, digits, function keys, navigation keys, common punctuation, and the four modifier pairs (Ctrl/Shift/Alt/GUI, left and right).

#### Scenario: Unmappable key is rejected
- **WHEN** the user presses a key that has no defined HID usage mapping
- **THEN** the binding is not changed and the editor shows a message naming the key as unsupported

#### Scenario: OS-swallowed combo is surfaced
- **WHEN** the user attempts a combo the OS intercepts before the page sees it (e.g. `⌘Q`, `⌘Tab`)
- **THEN** the editor does not silently record a partial result; it either records nothing or shows that the combo could not be captured

### Requirement: Capture respects firmware validation bounds

A captured binding SHALL satisfy the same constraints as any other binding: every code in `0…255` and `hidCodes.count ≤ 98`, consistent with `AhaKeyProfileValidator` and `WebBridgeHelper.validate`.

#### Scenario: Captured binding stays within limits
- **WHEN** a capture produces `hidCodes`
- **THEN** the values are within `0…255` and the count does not exceed `98`, and an out-of-bounds result is blocked with a validation message rather than sent to hardware

### Requirement: Capture coexists with presets

The keyboard-capture control SHALL augment, not replace, the existing preset quick-pick; the user can still choose a preset, and a captured value is editable/replaceable afterward.

#### Scenario: Switch between preset and capture
- **WHEN** the user picks a preset and then activates capture (or vice versa)
- **THEN** the most recent action determines `action.hidCodes`, with no stale value from the prior method left applied
