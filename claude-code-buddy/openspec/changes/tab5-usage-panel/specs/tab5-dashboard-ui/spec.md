## ADDED Requirements

### Requirement: Usage panel surfacing context and rate-limit headroom

The Tab5 dashboard SHALL parse and display the usage fields the cc-bridge
heartbeat already carries — `context_pct`, `limit_5h`, `limit_7d`, `model`,
`tokens_today`, `session_ms` — which the dashboard currently discards (only
`tokens` is read today). It SHALL present at minimum the context-window percent
and the 5h rate-limit headroom for the selected session, so the user can tell at
a glance whether to compact or whether they are approaching a rate limit. No
daemon or wire-protocol change is required; this requirement consumes fields
already emitted by `to_payload`.

#### Scenario: Context percent is shown
- **WHEN** a heartbeat carries `context_pct` in 0–100 for the selected session
- **THEN** the dashboard SHALL display that context-window percent

#### Scenario: Rate-limit headroom is shown
- **WHEN** a heartbeat carries `limit_5h` (and/or `limit_7d`) in 0–100
- **THEN** the dashboard SHALL display the rate-limit usage for the selected session

#### Scenario: Model and totals are shown
- **WHEN** a heartbeat carries a non-empty `model`, and/or non-zero `tokens_today` / `session_ms`
- **THEN** the dashboard SHALL display the model name, today's token total, and session elapsed time

### Requirement: Usage display tiers context and limits by severity

Context and rate-limit values SHALL be color-tiered so a critical level reads
instantly: a high context percent (≥90) SHALL be rendered in a critical color as
the "compact now" signal, and a high rate-limit percent (≥90) SHALL likewise be
rendered critical as the "approaching the wall" signal. Mid and low levels SHALL
use distinct, calmer colors.

#### Scenario: Near-full context flagged critical
- **WHEN** `context_pct` ≥ 90
- **THEN** the context indicator SHALL render in the critical color

#### Scenario: Low usage rendered calm
- **WHEN** `context_pct` and `limit_5h` are well below their thresholds
- **THEN** they SHALL render in the normal (non-critical) color

### Requirement: Usage fields degrade gracefully when absent

Each usage field SHALL render independently and tolerate absent / zero / unset
values without showing a misleading real-looking value and without crashing. A
zero or missing `context_pct` for an idle/unknown session SHALL NOT render as a
solid "0%" that reads as real; an empty `model` SHALL be omitted; zero
`tokens_today` / `session_ms` SHALL be omitted. Older daemons that omit these
fields SHALL leave the rest of the dashboard fully functional.

#### Scenario: Idle/unknown session does not show fake 0%
- **WHEN** the selected session reports no usable `context_pct`
- **THEN** the context indicator SHALL render empty/hidden rather than a misleading 0%

#### Scenario: Missing model omitted
- **WHEN** `model` is empty
- **THEN** the model line SHALL be omitted, other usage fields unaffected

### Requirement: Usage updates honor dirty-band discipline

Usage rendering SHALL repaint only the band(s) it occupies — a header context
indicator marks only the header dirty, a sidebar usage block marks only the
sidebar dirty — and SHALL NOT trigger a full-frame push, consistent with the
dashboard's existing dirty-band mechanism.

#### Scenario: Usage-only change repaints one band
- **WHEN** only a usage value changes between heartbeats
- **THEN** only its band (header or sidebar) is repainted, with no full-frame wipe
