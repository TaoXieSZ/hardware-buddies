## Why

When you're working through an agent, the thing you most want to glance at is **how much room is left** — context window %, rate-limit headroom, which model you're on. cc-bridge already computes and ships all of this on every heartbeat (`context_pct`, `limit_5h`, `limit_7d`, `model`, `tokens_today`, `session_ms` — see `tools/buddy_core/core.py` `to_payload`), but the **Tab5 dashboard throws it away**: `src/tab5/feed.cpp` parses only `tokens`. The big 1280×720 screen is exactly where this glance-data belongs.

This is the highest-leverage UX add available right now: **the data is already on the wire**, so it's pure Tab5 firmware (parse the fields the daemon already sends + render a usage panel). Zero daemon / wire-protocol change.

Originally surfaced as a cardputer idea, but the 240×135 cardputer screen is too cramped for a usage panel — **deliberately scoped to Tab5** (big DSI panel, sidebar + header bands with room). Not urgent: parked as a ready-to-build change.

## What Changes

- **Parse the dropped fields** (`src/tab5/feed.cpp`): read `context_pct`, `limit_5h`, `limit_7d`, `model`, `tokens_today`, `session_ms` from the heartbeat alongside the existing `tokens`, and carry them into the UI layer (extend `uiFeedState` / a new `uiFeedUsage`).
- **Render a usage panel** (`src/tab5/ui.cpp`): show context-window % (the headline — "should I /compact?"), 5h / 7d rate-limit bars, current model name, today's token total, and session elapsed time. Placement TBD in design (sidebar block vs header strip — D1).
- **Dirty-band discipline**: usage updates mark only their own band dirty (no full-frame push), consistent with the existing `DR_SIDEBAR`/`DR_HEADER`/`DR_BODY` mechanism.
- **Graceful absence**: any missing/zero field renders blank or is omitted — never a misleading 0% or a crash (older daemons, or a session with no usage yet).

## Non-goals

- **No daemon / wire-protocol change.** The fields already exist in `to_payload`; this change only consumes them. (If a gating spike finds the *serial* heartbeat trims them, that becomes a tiny separate daemon tweak — flagged below, not assumed here.)
- **Not on cardputer.** 240×135 is too small; this capability is Tab5-only. (Cardputer keeps its lean top-bar: agent state + sessions + battery.)
- **No history / graphs / sparklines.** Just current values. Trends are a later idea.
- **No new derived metrics.** Display what the daemon already computes; don't recompute burn-rate / ETA on-device this round.
- **No per-session usage breakdown beyond what's already shipped.** `tokens` is per-session today; context%/limits are session-scoped as the daemon emits them — render them for the selected session, don't invent a fleet aggregate.

## Capabilities

### Modified Capabilities
- `tab5-dashboard-ui`: gains a usage panel surfacing context-window % + rate-limit headroom + model + today/elapsed, parsed from heartbeat fields the dashboard currently discards.

### 依赖 / context
- Tab5 dashboard is the `claude-code-buddy/src/tab5/` firmware (ESP32-P4, M5GFX, 1280×720), fed the cc-bridge snapshot over USB-CDC serial. See `docs/tab5-buddy-dev.md` before touching `src/tab5/`.

## ⚠️ Gating spike (verify before building)

1. **Serial heartbeat carries the fields.** Confirm the cc-bridge → Tab5 USB-CDC NDJSON actually includes `context_pct` / `limit_5h` / `limit_7d` / `model` / `tokens_today` / `session_ms` (capture one live heartbeat line). They're in `to_payload`, but confirm the serial path isn't trimming them. If trimmed → one-line daemon fix to include them (separate from this firmware change).
2. **Field semantics / ranges.** Confirm `context_pct` / `limit_5h` / `limit_7d` are 0–100 ints (for bar fill) and `model` is a short string that fits the panel; confirm what they read when a session is idle / unknown (so "graceful absence" renders right).
