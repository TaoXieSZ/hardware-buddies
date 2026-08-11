## 1. Runtime Projection and Privacy Baseline

- [x] 1.1 Add failing unit tests for a thread-safe `DashboardState` snapshot, monotonic event sequences, 200-event capacity, explicit cursor gaps, immutable response copies, and concurrent publish/read access.
- [x] 1.2 Add failing privacy tests proving transcript/proposal previews are UTF-8 bounded to 160 characters and secrets, API keys, PCM, cwd values, surface UUIDs, raw payloads, terminal output, and unbounded permission data never enter dashboard JSON.
- [x] 1.3 Implement the bounded in-memory dashboard state projector and allowlisted semantic event schema until the projection/privacy tests pass.
- [x] 1.4 Add configuration fields and examples for dashboard enablement and port selection, enforcing IPv4 loopback binding and preserving port 8765 behavior when disabled.

## 2. Bridge Instrumentation and Failure Diagnosis

- [x] 2.1 Add failing bridge tests for watch connect/disconnect, protocol/authentication, utterance start/end, ASR success/failure, TTS, proposal, decision, task, permission, terminal, and reconnect transitions updating the shared dashboard projection.
- [x] 2.2 Add failing regression tests for successful ASR followed by `target_required`, `target_not_found`, `target_ambiguous`, `spawn_not_supported`, and `control_plane_unavailable`, asserting bounded error code/candidates/hints and zero dispatch.
- [x] 2.3 Instrument `ConnectionHandler`, router orchestration, and bridge startup with semantic dashboard transitions without parsing logs or changing protocol-v1/v2 messages.
- [x] 2.4 Add bounded router-error logging by code and identifier only, preserving the existing prohibition on full transcripts, commands, cwd values, credentials, and terminal output in normal logs.
- [x] 2.5 Reproduce the recorded “ASR completed but no proposal” case and verify the state resolves to an actionable routing error rather than appearing idle.

## 3. Local HTTP API

- [x] 3.1 Add failing HTTP tests for `GET /`, `GET /api/status`, and cursor-based `GET /api/events`, including content types, no-store caching, security headers, invalid cursors, cursor gaps, unknown routes, and rejected non-GET methods.
- [x] 3.2 Implement an optional daemon `ThreadingHTTPServer` on `127.0.0.1:8766` using only the Python standard library and same-origin static assets.
- [x] 3.3 Make dashboard bind/handler failures isolated and non-fatal, with a bounded diagnostic while the WebSocket bridge remains available.
- [x] 3.4 Add lifecycle tests proving dashboard shutdown releases its port and repeated test/server starts do not leak threads or sockets.

## 4. Responsive Browser Dashboard

- [x] 4.1 Create the local HTML/CSS/JavaScript dashboard with a health strip, voice pipeline, recognized-text/routing card, agent-session cards, proposal/task/permission state, and newest-first event timeline.
- [x] 4.2 Implement one-second status polling, cursor-based event polling, hidden-tab backoff/full-refresh recovery, offline/error states, and no-CDN/no-remote-resource behavior.
- [x] 4.3 Add responsive narrow/wide layouts, non-color status labels/icons, keyboard-readable semantics, and reduced-motion support.
- [x] 4.4 Add UI fixture states for disconnected, ready, recording, transcribing, target-required, proposal, running, waiting-permission, completed, and failed so every visual state is deterministically reviewable.

## 5. Integration, Visual QA, and Documentation

- [x] 5.1 Run walkie-bridge unit/integration tests, protocol-v1/v2 regressions, affected cc-bridge tests, Python static compilation, and `git diff --check`, fixing only regressions introduced by this change.
- [ ] 5.2 Start the real dashboard with the physical StopWatch and verify connection, authentication, ASR, routing failure, proposal, reject, approve-once, running, permission, terminal, and reconnect states against API evidence.
- [x] 5.3 Inspect the live dashboard at wide and narrow Mac browser sizes, capture visual evidence for the principal pipeline/error states, and fix all observed clipping, stale-state, contrast, or update defects before review.
- [x] 5.4 Update `stopwatch-walkie/README.md` with the dashboard URL, configuration, privacy boundary, targeting examples, troubleshooting flow, disable/rollback instructions, and distinction from port 18765.
- [x] 5.5 Record API, screenshot, and physical-device evidence under the change using the repository topic/type artifact organization, then run `openspec validate add-stopwatch-bridge-dashboard --strict` with no errors.
