# StopWatch Walkie UI Design

## Source of truth

- Status: Active
- Last refreshed: 2026-08-07
- Primary product surfaces: M5 StopWatch 466×466 round AMOLED; physical KEYA and KEYB
- Evidence reviewed: `docs/stopwatch-walkie-talkie-design.md`, the active OpenSpec change, current firmware state machine, M5Unified examples, and the pinned M5StopWatch UserDemo
- Assumption: this iteration is a hardware UI trial, so it includes a local showcase build that exercises every state without a reachable bridge

## Brand

- Personality: calm field instrument, compact, direct, quietly futuristic
- Trust signals: explicit connection state, unmistakable recording boundary, visible cancellation affordance, correlated result/error state
- Avoid: desktop-style panels, tiny dashboards, decorative gradients, mascot UI, ambiguous color-only status, dense telemetry

## Product goals

- Goals: make the current state readable in under one second; make KEYA/KEYB behavior obvious; keep recording visually alive; fit short transcripts inside the circular safe area
- Non-goals: touch navigation, agent selection, approval UI, TTS controls, settings, notifications, or final production visual polish
- Success signals: the user can identify all six states at arm's length and can cycle the showcase using the two physical keys without instructions

## Personas and jobs

- Primary persona: the device owner testing a push-to-talk agent terminal at a desk or while walking
- User jobs: know whether the Mac is reachable, know when audio is being captured, cancel safely, recognize that transcription is pending, read a short result, recover from an error
- Key contexts of use: one-handed, brief glances, indoor lighting, occasional motion, no assumption that touch is available

## Information architecture

- Primary navigation: none in runtime; the state machine owns the whole screen
- Core screens: connecting, ready, recording, transcribing, result, error
- Content hierarchy: state ring → central symbol/activity → state title → one-line detail → physical-key hints
- Showcase navigation: KEYA advances one state; KEYB moves back one state

## Design principles

- Edge carries state: a thick perimeter arc makes status visible even when the center is obscured by a wrist angle
- Center carries action: one large symbol or animated activity communicates what the device is doing now
- Text confirms, never competes: short uppercase labels and one concise detail line; transcript is the only multiline content
- Physical controls stay physical: persistent yellow `A` and blue `B` cues mirror the actual keys
- Tradeoff: prefer large, stable geometry over displaying IP addresses, latency, queue depth, or other engineering diagnostics on the main UI

## Visual language

- Color: AMOLED black `#050708`; connecting amber `#FFB84D`; ready mint `#42F5B3`; recording coral `#FF4F6D`; transcribing cyan `#55C7FF`; result mint/white; error orange-red `#FF7657`
- Typography: built-in M5GFX fonts only; large bold state title, medium action/detail, small uppercase chrome
- Spacing/layout rhythm: 22 px outer safe area, 42 px header zone, 250 px central activity zone, 52 px key-hint zone
- Shape/radius/elevation: circular rings, round dots, pills; no shadows or fake depth
- Motion: low-frequency breathing/pulse for connecting/ready, amplitude-driven waveform for recording, rotating dots for transcribing; 12–20 fps maximum
- Imagery/iconography: the approved browser/SVG reference is rendered into deterministic 466×466 PNG state assets so anti-aliased typography and icons survive on hardware; generated assets remain reproducible from `ui-preview/scripts/`

## Components

- Existing components to reuse: `DeviceState`, `AudioLoop`, M5Unified display and buttons
- New/changed components: `WalkieUi`, generated state assets, state palette, perimeter progress ring, microphone glyph, waveform, spinner, result card, error mark, physical-key hints
- Variants and states: one visual variant for each explicit device state plus a showcase indicator
- Token/component ownership: colors, geometry, motion, copy, and renderer live in `include/walkie_ui.h` and `src/walkie_ui.cpp`

## Accessibility

- Target standard: readable at arm's length on the physical watch rather than formal web accessibility conformance
- Keyboard/focus behavior: KEYA and KEYB actions are always represented by letter, color, and verb
- Contrast/readability: black background with high-luminance foreground; no critical gray-on-black text below medium size
- Screen-reader semantics: not applicable to this firmware surface
- Reduced motion and sensory considerations: animations are slow and bounded; no flashing; vibration remains one short post-recording acknowledgement

## Responsive behavior

- Supported breakpoints/devices: only the 466×466 M5 StopWatch round AMOLED
- Layout adaptations: long transcript text is truncated and wrapped inside a central safe rectangle; outer corners are treated as unavailable
- Touch/hover differences: touch is not required or used in this slice

## Interaction states

- Loading: amber orbit and `FINDING MAC`
- Empty: ready microphone symbol with `HOLD A TO TALK`
- Error: orange-red ring, explicit error code, and `A RETRY`
- Success: mint check mark and bounded transcript preview
- Disabled: disconnected input remains on connecting/error UI and does not start capture
- Offline/slow network: connecting pulse; transcribing spinner never implies progress percentage

## Content voice

- Tone: short, calm, operational
- Terminology: `CONNECTING`, `READY`, `RECORDING`, `TRANSCRIBING`, `RESULT`, `ERROR`
- Microcopy rules: use one action per line; avoid punctuation; do not expose internal hostnames, secrets, raw protocol errors, or full identifiers

## Implementation constraints

- Framework/styling system: Arduino + M5Unified/M5GFX with build-time browser-rendered PNG assets; no LVGL or new runtime dependency
- Design-token constraints: state colors and dimensions are compile-time constants
- Performance constraints: decode the selected embedded PNG into a PSRAM-backed full-screen `M5Canvas`, reuse the canvas, and never allocate per frame; primitive fallback remains available when decoding fails
- Compatibility constraints: preserve the pinned official PlatformIO platform and microphone initialization sequence
- Test/screenshot expectations: native state tests remain green; both runtime and showcase environments build; showcase flashes to the known StopWatch and cycles all states via KEYA/KEYB

## Open questions

- [ ] Validate text size, ring brightness, and key-hint placement on a wrist in home lighting / owner / may tune tokens only
- [ ] Decide whether production copy should remain English or switch to Chinese after the visual trial / owner / affects font and wrapping strategy
