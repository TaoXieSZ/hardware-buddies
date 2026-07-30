# Task Spec: Cardputer auto screen-off (backlight off on inactivity + wake)

- **ID**: 002-cardputer-auto-screen-off
- **Workdir**: /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy
- **Executor**: cursor
- **Created**: 2026-07-05
- **Iteration**: 0

## Goal

The Cardputer-ADV LCD backlight currently stays ON forever (harms the panel, wastes
power). Add auto screen-off: after a period of inactivity, turn the backlight OFF via
`M5Cardputer.Display.setBrightness(0)`; restore it on the next physical activity. This
is a firmware-only change (ESP32-S3, PlatformIO/Arduino). Comments in Chinese to match
the file.

## Background / Context

Authoritative spec (READ FIRST):
/Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/openspec/changes/cardputer-auto-screen-off/{proposal,design}.md
and specs/auto-screen-off/spec.md, tasks.md — the plan below already reflects them.

Ground truth already verified:
- `src/clawd_player.cpp:389` `setSleeping()` only swaps to sleep.gif — it NEVER touches
  the backlight. No `setBrightness`/backlight call exists anywhere in the firmware.
  That's why the screen is always lit even when sleeping.
- `M5Cardputer.Display.setBrightness(uint8_t)` exists (M5GFX; 0=off, 255=max).
  `getBrightness()` may exist — if it compiles, use it to capture the pre-off value;
  if not, store a sensible default (e.g. keep a module `g_savedBrightness` set from a
  known init value) and note it.
- `src/motion.{h,cpp}` already reads the BMI270 IMU each frame and exposes
  `stillMs()` (ms since last significant motion — resets to 0 on motion) plus pickup/
  shake gestures. REUSE these for "significant motion"; do NOT add a new IMU read or
  invent a new accel threshold.
- `src/main.cpp` loop already computes: `bool online`, `bool keyEvent =
  M5Cardputer.Keyboard.isChange()`, `g_motion.stillMs()`, and calls
  `clawd::setSleeping(online && idle && g_motion.stillMs() > STILL_FOR_SLEEP)` near the
  end (STILL_FOR_SLEEP = 180000).

### ⚠️ HARD CONSTRAINT — do NOT use `cclink::changed()` for activity/wake

`cclink.cpp:135` sets `g_changed = true` after EVERY successful heartbeat parse
(daemon emits ~every 1-2s), so `cclink::changed()` is true almost every frame. Using
it as an activity/wake signal would keep the screen on forever while BLE is connected —
exactly the bug we're fixing. Activity = PHYSICAL input only: key press OR IMU motion.

## Files in Scope

| File | Change |
|------|--------|
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/main.cpp | modify: inactivity timer + screen off/on + wake on key/IMU |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/clawd_player.h | optional: declare a `setBacklight`/brightness helper if you put it here |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/clawd_player.cpp | optional: helper impl (or keep it all in main.cpp) |
| /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/motion.h | read only; add a small "significant motion since last frame" accessor ONLY if stillMs/gestures don't already suffice |

Do NOT edit anything outside `cardputer-adv-buddy/src/`. Do NOT touch cclink, ble_link,
sound_player, the GIF assets, or any other subproject.

## Plan

1. Backlight helper (in main.cpp or clawd_player): `g_savedBrightness` captured once at
   startup (after `M5Cardputer.begin`) via `getBrightness()` if available else a
   constant default; `screenOff()` → `setBrightness(0)` + set `g_screenOff=true`;
   `screenOn()` → `setBrightness(g_savedBrightness)` + `g_screenOff=false`. Guard against
   redundant calls.
2. In `loop()`, add `static uint32_t g_lastKeyMs`. Each frame: if `keyEvent` is true OR
   the IMU shows significant motion (use `g_motion.stillMs()` small / a pickup/shake
   gesture), treat as activity: update `g_lastKeyMs = now` for the key case, and if
   `g_screenOff` call `screenOn()`.
3. Screen-off decision each frame: if `!g_screenOff && g_motion.stillMs() > SCREEN_OFF_MS
   && (now - g_lastKeyMs) > SCREEN_OFF_MS` → `screenOff()`. Add
   `static constexpr uint32_t SCREEN_OFF_MS = 60000;` with a comment noting the relation
   to STILL_FOR_SLEEP (180000) — screen may blank before/independent of the sleep.gif
   state; that's fine (backlight layer is independent of the GIF layer).
4. IMU wake only matters while `g_screenOff` (before that the screen is already on).
   Reuse `g_motion` — do not read the IMU separately.
5. Decide and COMMENT whether the wake key is "consumed" (used only to wake, not also
   acted on) or passes through to normal handling. Either is acceptable; pick one and
   note it in a comment (recommendation: let the wake key still be processed normally —
   simpler, and matches user expectation that a keypress both wakes and acts).
6. Do NOT change `setSleeping`/sleep.gif logic or `STILL_FOR_SLEEP`.

## Constraints

- Only files under `cardputer-adv-buddy/src/`. Additive; don't refactor unrelated code
  (there are recent features in main.cpp/clawd_player.cpp — busy/idle GIF timers,
  Connecting state, backspace relay — leave them alone).
- New comments in Chinese, matching surrounding style.
- Restore brightness to the saved pre-off value; never hardcode 255.
- Activity/wake = key or IMU only; NEVER cclink::changed().

## Acceptance Criteria

- [ ] A `SCREEN_OFF_MS` constant + inactivity timer exist; after that long with no key
      and no IMU motion, `setBrightness(0)` is called.
- [ ] Key press or IMU motion while blanked restores brightness to the pre-off value
      (not a hardcoded max).
- [ ] `cclink::changed()` is NOT used as an activity/wake source anywhere.
- [ ] `setSleeping`/sleep.gif/`STILL_FOR_SLEEP` unchanged; screen-off is an added
      backlight layer.
- [ ] `pio run -e cardputer-adv` succeeds.
- [ ] `git status --porcelain` shows only files under `cardputer-adv-buddy/src/` changed.

## Verification

```bash
cd /Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy
pio run -e cardputer-adv           # MUST end "[SUCCESS]"
git status --porcelain             # only cardputer-adv-buddy/src/* modified
grep -n "setBrightness" src/main.cpp src/clawd_player.cpp   # backlight control present
! grep -n "changed()" src/main.cpp | grep -i "activity\|wake\|lastKey"  # not used for wake
```

Do NOT run any `-t upload`/`uploadfs` (no device attached to you). On-device behavior
(actual blank after timeout, key/pickup wake, brightness restore) is HUMAN verification
by the repo owner — list it in your summary as pending device test, do not claim it.
