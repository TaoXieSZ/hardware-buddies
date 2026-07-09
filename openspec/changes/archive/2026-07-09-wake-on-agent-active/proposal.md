## Why

The Cardputer-ADV auto-dims its backlight after 60s of no physical input (main.cpp `SCREEN_OFF_MS`) to save the screen and battery. But when the device is sitting dark on the desk and a Claude/opencode session starts running or hits a permission prompt, the user sees nothing — the avatar reaction and approval panel are invisible until they physically pick it up. The device should wake itself when an agent becomes active, the way it already wakes on a keypress or motion.

## What Changes

- Add a **level-based** screen-on condition on agent activity: when any session is `running` (≥1) or `waiting` (≥1, which includes permission prompts), the backlight stays on — the device treats agent activity the same as physical activity for the auto-dim layer.
- When all sessions go idle, the normal 60s inactivity timer re-dims the screen as usual.
- No change to the physical-activity wake path (keypress/IMU/recording/notes) or to the `sleep.gif` state machine.
- Uses the `bs.running`/`bs.waiting` counts directly (level), NOT `cclink::changed()` (which is true every heartbeat and would never let it dim).

## Capabilities

### New Capabilities
- `wake-on-agent-active`: keep the backlight on while agent activity (running or waiting) is present, so a dark device shows the busy avatar and approval panels without the user having to touch it.

### Modified Capabilities
<!-- none — no existing spec is changing -->

## Impact

- **Code**: `cardputer-adv-buddy/src/main.cpp` — the auto-dim block (~line 519). Add `agentActive = bs.running > 0 || bs.waiting > 0` and fold into the existing `activity` bool that gates `screenOn()`.
- **Behavior**: screen stays on while any session is running or waiting; re-dims 60s after all go idle. Slightly higher power use during active sessions; acceptable — these are exactly the moments the user wants to look.
- **Risk**: must use level (`bs.running`/`bs.waiting` counts), not `cclink::changed()` (true every heartbeat → never dims).
- **Non-goal**: waking on `completed` (the celebrate flash is nice-to-have but not requested; re-waking for every completion is noisy).
