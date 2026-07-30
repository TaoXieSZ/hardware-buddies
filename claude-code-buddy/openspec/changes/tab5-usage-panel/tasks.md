# Tasks — tab5-usage-panel

> Parked / not urgent. Build when Tab5 work resumes. Read `docs/tab5-buddy-dev.md` first
> (build/flash loop, daemon runbook, branch topology — Tab5 lives on `feat/sticks3-buddy`).

## Spike (before building)
- [ ] S1. Capture one live cc-bridge → Tab5 USB-CDC heartbeat line; confirm it carries `context_pct` / `limit_5h` / `limit_7d` / `model` / `tokens_today` / `session_ms`. If trimmed on the serial path → tiny separate daemon fix to include them (note it, don't fold into this firmware change).
- [ ] S2. Confirm field ranges/semantics: pct fields are 0–100 ints; `model` short string; capture what they read for an idle / no-session state (drives D3 "graceful absence").

## Implement
- [ ] 1. **Parse fields** (`src/tab5/feed.cpp`): read the six fields alongside `tokens`; thread them into the UI layer (extend `uiFeedState` or add `uiFeedUsage(sess, ...)`). Verify: serial-feed a synthetic heartbeat, values land in UI state.
- [ ] 2. **Header context chip** (`src/tab5/ui.cpp`, per D1-C): `context NN%` chip in the header band, color ramp <70 / 70–89 amber / ≥90 red. Marks `DR_HEADER` only. Verify: on-device chip tracks a changing context_pct.
- [ ] 3. **Sidebar usage block** (`src/tab5/ui.cpp`): 5h + 7d bars, model, today (humanized), elapsed (`Hh Mm`), below the session cards. Marks `DR_SIDEBAR` only. Verify: on-device block shows live values.
- [ ] 4. **Graceful absence** (D3): zero/empty fields render blank/omitted, never a misleading 0% or crash. Verify: feed a heartbeat with the fields absent → no panel garbage.
- [ ] 5. **Dirty-band check** (D4): usage updates never trigger full-frame push; only their band repaints. Verify: watch for full-frame flicker on a usage-only change.

## Verify / wrap
- [ ] 6. On-device end-to-end: real session running, context% climbs as the conversation grows, ≥90 turns red; 5h/7d bars track; model name correct; today/elapsed sane.
- [ ] 7. Regression: transcript / session tabs / permission card / mic pill unaffected; no new full-frame pushes; CJK transcript still clean.

## Notes
- Pure Tab5 firmware + (only if spike S1 fails) a one-line daemon include. No wire-protocol redesign.
- Files: `src/tab5/feed.cpp`, `src/tab5/ui.cpp` (+ `ui.h` decls). Possibly `sound.*` untouched.
- Extends existing capability `tab5-dashboard-ui`; keep dirty-band discipline intact.
