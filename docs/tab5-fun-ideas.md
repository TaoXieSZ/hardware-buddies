# Tab5 — fun / delightful feature ideas (brainstorm capture)

Running scratch of "好玩 + 好看" ideas for the Tab5 desktop buddy. Captured in
OpenSpec explore mode — **ideas only, not committed scope**. Promote to an
OpenSpec change when one crystallizes.

## Make Clawd feel ALIVE (2026-06-14)

Today the avatar already maps 5 agent states → 5 GIFs (`avatar.cpp`):
`IDLE→idle.gif, BUSY→busy_0.gif, ATTN→attention.gif, DONE→celebrate.gif,
ERR→dizzy.gif`. The `heart` and `sleep` poses in `docs/posters-assets/` are
**unused**. Layers, current coverage in (parens):

1. **Macro state → GIF** — *(done)*.
2. **Idle life** — no-one watching → it has a life of its own. *(missing)*
   Escalation timeline:
   ```
   0s ── 30s ───── 2min ──────── 5min
   idle  look around/blink  yawn/rub eyes(drowsy)  curled asleep Zzz (sleep)
                                          ↑ any touch / agent activity → wake
   ```
3. **You ↔ it interaction** — *(missing)*
   - Tap the avatar → `heart` pose + cute "♪" sound → back to idle after ~2s.
   - Easter egg: triple-tap → bashful spin.
   - You hold-to-talk / type → it perks up ears / nods.
4. **FX overlay** — *(missing)* confetti particles on task DONE (the pitch's
   "撒花" done as real falling particles, not just the GIF swap).
5. **Event micro-reactions** — *(missing)* permission request → turns to look
   at you + "?" bubble; subagent spawn → "calls a friend"; compact → stretch.

### Open design questions
- `heart`/`sleep` as static PNG vs single-frame GIF (avatar pipeline is
  GIF-only today — needs a static layer or PNG→gif).
- "Thank it" trigger: (a) tap avatar [simplest], (b) daemon NLP on the prompt
  ("谢谢/干得好"), (c) skip, tap only.
- Confetti on the single-buffer DSI panel: must use small dirty-region pushes
  or accept slight tearing (same rotation-3 columnar-write constraint as the
  scroll work).

## Touchless gesture control (2026-06-14)

Hardware: **M5Stack Unit Gesture (PAJ7620U2)** — optical IR, **9 discrete
gestures** (↑ ↓ ← →, ⟳ cw, ⟲ ccw, ⊙ forward/approach, ⊗ backward/recede,
〰️ wave), range 5–15cm, I2C **@0x73**, up to 240Hz, works in the dark. NOT
continuous x/y/z hand tracking (that's the MGC3130 GestIC — different chip), so
"Clawd follows my hand" can only be crude near/far via forward/backward.

Direction chosen by user: **touchless control** ("隔空控制好玩").

### Activation model — chosen: **D (tiered)**
Central tension is false triggers (reaching for coffee, gesturing while
talking). Tiered gating:
```
Always listening (low false-positive / safe):
  〰️ wave   → wake screen + arm gesture mode for 5s (Clawd waves back)
  ⊙ push    → [ONLY while a permission card is pending] approve / Allow
  ⊗ recede  → [ONLY while a permission card is pending] deny
Within the 5s armed window (after a wave):
  ← / →     → switch Claude/Cursor tab
  ↑ / ↓     → scroll transcript
  (no gesture for 5s → auto-disarm, armed indicator off)
```
Other models considered: A always-on (too many false triggers), B context-only
(safest, fewer toys), C wake-then-act for everything (uniform, one extra step).

### Killer flow — hands-free permission approve/deny
```
agent requests permission → card + Clawd turns to look at you
                            + hint "⊙ push = Allow   ⊗ recede = Deny"
   ⊙ push forward → ✓ green flash + Clawd nods  + send approve
   ⊗ pull back    → ✗            + Clawd shakes head + send deny
```
The ~8s permission window naturally scopes when ⊙/⊗ are live → near-zero false
triggers. Big demo moment: approve a tool call without touching anything.

### Feedback (every recognized gesture → 3 cues)
1. Clawd reacts (ties into the "alive" layers): wave back / nod / shake / spin.
2. On-screen: ghost arrow + toast ("← switched to Cursor", "✓ approved by gesture").
3. Sound: reuse one of the 33 hook WAVs.
Additive only — touch + keyboard relay keep working; gestures are an extra
channel. An "armed" indicator (e.g. a ✋ glyph in the header) shows when
swipe/scroll are being listened for.

### Bring-up — VALIDATED on hardware (2026-06-14, probe env `tab5-gesture-probe`)
Spike proved all 9 gestures + push/pull work on the Tab5. Hard-won facts:
- **Port A (red Grove) = G53(SDA)/G54(SCL) = `M5.Ex_I2C` (I2C port 0).**
  Internal bus = G31/G32 = `M5.In_I2C` (port 1). Sensor probes at **0x73**,
  partID **0x7620**.
- **EXT5V (Port A's 5V) is OFF after `M5.begin()`** — M5GFX leaves the IO-
  expander pin as an input, so the sensor is unpowered until you drive it.
  Enable via the internal IO-expander **PI4IOE5V6416 #1 @0x43, P2**, with a
  read-modify-write (NO chip reset → LCD/touch stay alive):
  ```cpp
  M5.In_I2C.bitOn(0x43, 0x03, 0x04, 400000); // IO_DIR  P2 = output
  M5.In_I2C.bitOn(0x43, 0x05, 0x04, 400000); // OUT_SET P2 = high → 5V on
  ```
  (reg map verbatim from m5stack/M5Tab5-UserDemo `bsp_io_expander_pi4ioe_init`).
- **Use `M5.Ex_I2C`, NOT Arduino `Wire`**, with the lib: reconfiguring Arduino
  Wire to Grove pins breaks `M5.update()` (ESP_ERR_INVALID_STATE). The official
  M5Unit-GESTURE example says the same.
- **Library:** `m5stack/M5Unit-GESTURE` (pulls M5UnitUnified/M5Utility/M5HAL).
  ```cpp
  Units.add(unit, M5.Ex_I2C) && Units.begin();   // begin() auto-starts gesture mode
  // loop: M5.update(); Units.update(); if (unit.updated()) g = unit.gesture();
  ```
  `unit.readObjectSize(size)` / `readNoObjectCount(n)` give raw hand presence.
- The throwaway probe (`[env:tab5-gesture-probe]`, `src/tab5_gesture_probe/`)
  was REMOVED after validation — these notes are the reusable record.

### Still open (for the real feature, not the spike)
- Debounce / one-gesture-per-event so a single wave doesn't fire repeatedly.
- Integrate the D gating model + feedback (Clawd reaction + toast) into the
  dashboard firmware; gesture poll lives alongside `kbdPoll()` / `feedPoll()`.

### DECISION 2026-06-14 — gesture sensor goes to StickS3, NOT Tab5 (parked)
After the spike proved the PAJ7620 works on Tab5, we decided **not** to ship it
on the Tab5. Reasons:
1. **One free external I2C only**, already taken by the A164 keyboard — see the
   bus-conflict analysis above / `docs/tab5-i2c-conflict.html`. Coexistence
   needs a hacky multiplex, a Grove hub, or unproven LP_I2C.
2. **Tab5 is a screen+keyboard slab** — awkward to physically mount add-on
   I2C units against.
3. **Tab5 has a built-in camera** → if we ever want touchless/vision input on
   the Tab5, do it with the camera (CV hand detection), not an external sensor.
4. **StickS3 mates cleanly with M5 modules** → the PAJ7620 gesture unit belongs
   there. Revisit when doing StickS3 add-ons. NOTE: StickS3 has different I2C
   pins/power than the Tab5 — the EXT5V + Port A G53/G54 specifics above are
   Tab5-only; redo the m5stack-i2c bring-up for StickS3's bus.

The Tab5 spike (`[env:tab5-gesture-probe]`, `src/tab5_gesture_probe/`) was a
throwaway validation; its reusable takeaway is "M5Unit-GESTURE + M5UnitUnified
reads all 9 gestures cleanly via the unit's I2C bus."
