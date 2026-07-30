# Design — tab5-usage-panel

## What's already on the wire (the whole premise)

`tools/buddy_core/core.py` `BuddyState.to_payload()` emits, every heartbeat:

| field | meaning | type (as emitted) |
|---|---|---|
| `tokens` | selected-session token count | int — **already rendered** (sidebar "12.3k tok") |
| `context_pct` | context window used % | int 0–100 |
| `limit_5h` | rolling 5h rate-limit used % | int 0–100 |
| `limit_7d` | rolling 7d rate-limit used % | int 0–100 |
| `model` | current model name | string |
| `tokens_today` | today's cumulative tokens | int |
| `session_ms` | session elapsed time | int (ms) |

`src/tab5/feed.cpp` currently reads only `tokens` (line ~25) and passes it via `uiFeedState`. Everything else is dropped at the parse boundary. This change widens that boundary.

## D1 — Where the panel lives (decide at spec review)

Tab5 layout: 1280×720, sidebar `SB_W=300` (avatar + 2 session cards + token + mic/perm pills at bottom), header band `HDR_H=100`, body = transcript (`x0 = SB_W + PAD`).

```
┌─ 300px sidebar ─┬──────── header (100px) ────────────────┐
│   avatar        │  session name   [state chip]    clock   │
│  ┌───────────┐  ├─────────────────────────────────────────┤
│  │ Claude  ● │  │                                          │
│  │ 12.3k tok │  │   transcript ...                         │
│  ├───────────┤  │                                          │
│  │ Cursor  ○ │  │                                          │
│  └───────────┘  │                                          │
│  [usage?]       │                                          │
│  mic pill       │                                          │
│  perm pill      │                                          │
└─────────────────┴──────────────────────────────────────────┘
```

Options:
- **A. Sidebar block** (below session cards, above mic pill) — vertical stack of small bars. Pro: glance-zone next to tokens; lives in `DR_SIDEBAR`. Con: sidebar is already busy; vertical room is tight.
- **B. Header strip** (right of the clock, in the 100px header) — context% + a compact `5h/7d` readout. Pro: top-of-eye, always visible regardless of scroll; lives in `DR_HEADER`. Con: header is narrow vertically.
- **C. Hybrid (default recommendation):** context% as a **header chip** (it's the one you watch live — "/compact soon?"), and the fuller breakdown (5h/7d bars, model, today, elapsed) as a **sidebar block**. Splits "live glance" from "reference detail".

Default for spec: **C**. Revisit at review.

## D2 — Visual encoding

- **context_pct**: labeled bar `context ▓▓▓▓▓▓▓░░░ 64%`. Color ramp: <70 normal, 70–89 amber, ≥90 red ("compact now"). The red is the actionable signal.
- **limit_5h / limit_7d**: two thin bars, same ramp; ≥90 red = "approaching the wall."
- **model**: plain short string (e.g. `opus-4.8`); truncate gracefully if long.
- **tokens_today**: humanized `1.24M` / `847k`.
- **session_ms**: `2h13m` / `47m`.

Reuse the battery-indicator color discipline from cardputer (tiered color = instant read), but with the panel's own palette consistent with the Tab5 theme (`th::` colors in ui.cpp).

## D3 — Graceful absence

Each field renders independently; a `0` / missing value:
- `context_pct == 0` → likely "no active session / unknown" → render the bar empty with no % or hide the row, NOT a misleading "0%". (Spike #2 confirms what idle reads.)
- `model == ""` → omit the model line.
- `tokens_today == 0` / `session_ms == 0` → omit those readouts.

Never crash, never show a value that reads as real when it's just an unset default. Mirrors the battery `< 0 → don't draw` rule.

## D4 — Dirty-band discipline

`feed.cpp` already OR-marks `DR_SIDEBAR` when `tokens` changes. Usage fields follow the same pattern: changing a sidebar-block field marks `DR_SIDEBAR`; changing the header context chip marks `DR_HEADER`. No usage update may trigger `DR_ALL` / full-frame push. Consistent with the existing transcript/chip render discipline (`tab5-dashboard-ui` "Dirty-band render discipline" requirement).

## Open question for review

- D1 placement (A / B / **C**).
- Whether to show 7d at all, or just 5h + context (the two you act on most). Could trim to reduce sidebar clutter.
