## Why

The StopWatch bridge currently exposes its runtime only through redacted terminal logs, so a successful recording and ASR result can still look like “nothing happened” when deterministic routing rejects the transcript. A dedicated local dashboard is needed now to make the physical voice-to-agent path observable and testable without weakening command privacy or execution safeguards.

## What Changes

- Add a StopWatch-specific localhost Web dashboard, separate from the existing cc-bridge settings dashboard, showing watch connectivity, protocol/authentication state, audio/ASR/TTS activity, control-plane health, proposals, tasks, permissions, and bounded recent events.
- Add an in-memory runtime snapshot and bounded event feed inside walkie-bridge so the dashboard reflects the same state transitions that drive the watch.
- Show the latest recognized transcript and routing outcome in the local dashboard, capped at 160 UTF-8 characters, held only in memory, and never written to normal logs or disk.
- Surface deterministic routing failures such as `target_required`, `target_not_found`, `target_ambiguous`, and `control_plane_unavailable`, including bounded candidate labels and an actionable targeting hint.
- Serve the dashboard and JSON status/event endpoints on `127.0.0.1:8766` by default, with configuration to disable it or select another loopback port.
- Preserve protocol-v1/v2 wire behavior, proposal confirmation rules, cc-bridge ownership, and the existing `127.0.0.1:18765` dashboard unchanged.

## Capabilities

### New Capabilities

- `stopwatch-bridge-dashboard`: Local runtime observability, bounded in-memory event/state projection, routing diagnostics, and the browser dashboard contract for walkie-bridge.

### Modified Capabilities

None.

## Impact

- **Walkie bridge:** `stopwatch-walkie/tools/walkie-bridge/` gains a runtime state projector, local HTTP server, dashboard assets, configuration, and tests.
- **Operator workflow:** the dashboard becomes the primary Mac-side view for diagnosing StopWatch connection, ASR, routing, proposal, permission, and task lifecycle behavior.
- **Privacy/security:** the HTTP server binds to loopback only; transcript previews and event details remain bounded and memory-only; secrets, PCM, cwd values, raw terminal output, and credentials remain excluded.
- **Dependencies:** no new third-party runtime dependency is introduced; the implementation uses the Python standard library and browser polling or server-sent events as selected in design.
- **Related change:** builds on the protocol and control-plane behavior in `add-stopwatch-multi-agent-control-plane` without expanding command authority.
