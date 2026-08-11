## Context

The running walkie-bridge already knows every important point in the StopWatch path: WebSocket connect/disconnect, protocol version, mutual-authentication result, utterance byte count, ASR request/result, deterministic route resolution, proposal decisions, cc-bridge dispatch, permission events, task observation, TTS, and errors. That information is currently split between transient Python objects and deliberately redacted terminal logs. In the observed failure, audio and DashScope ASR both succeeded, but a router error produced no Mac-visible diagnostic, leaving the operator unable to distinguish silence, ASR failure, missing target, or an unavailable agent.

The existing dashboard on port 18765 belongs to cc-bridge and configures buddy devices. The new surface must remain owned by walkie-bridge, must not compete for that port, and must not turn observability into another command-authority path. The prototype runs on a trusted Mac and already depends on `websockets`; repository policy discourages adding dependencies for a feature that the standard library can provide.

## Goals / Non-Goals

**Goals:**

- Make the complete StopWatch voice-to-agent pipeline understandable at a glance on the Mac.
- Show exactly where the latest utterance stopped and give an actionable explanation for deterministic routing failures.
- Project runtime state once and use the same projection for the Web UI and tests.
- Keep transcript previews bounded, memory-only, and visible only through a loopback HTTP endpoint.
- Preserve bridge availability if the optional dashboard fails to bind or render.
- Provide a responsive, polished browser view suitable for ongoing physical-device smoke testing.

**Non-Goals:**

- Sending, approving, rejecting, or cancelling commands from the browser.
- Replacing physical confirmation on the StopWatch.
- Replacing the cc-bridge settings dashboard, terminal Fleet Board, or architecture document.
- Persisting history, analytics, PCM, transcripts, terminal output, cwd values, or credentials.
- Adding authentication for remote access; the server is loopback-only and MUST NOT be exposed to the LAN.
- Adding a frontend build toolchain, JavaScript framework, database, or third-party HTTP dependency.

## Decisions

### 1. Run a separate read-only dashboard on `127.0.0.1:8766`

walkie-bridge starts a small dashboard server only when enabled. It binds to IPv4 loopback, defaults to port 8766, and exposes no mutation endpoint. A port conflict or dashboard exception is reported as a bounded status/log event and does not stop WebSocket audio/control service on port 8765.

Merging into port 18765 was rejected because that lifecycle belongs to cc-bridge and would couple independent daemons. A native menu-bar app was rejected for this slice because it adds a second runtime and packaging path before the observability contract is proven. A terminal-only board was rejected because the operator needs readable pipeline states, error guidance, and responsive history while testing physical controls.

### 2. Add one thread-safe in-memory runtime projection

`DashboardState` owns a lock-protected current snapshot plus a monotonically sequenced bounded deque of at most 200 events. Bridge components publish semantic transitions such as `watch.connected`, `control.authenticated`, `utterance.started`, `asr.completed`, `route.failed`, `proposal.created`, `task.running`, and `permission.requested`. Publishers pass bounded structured fields; the projector derives the current pipeline stage and counters.

The dashboard never parses terminal logs. Log text remains redacted and independent, while tests can assert the runtime projection directly. A general telemetry bus was rejected as unnecessary abstraction for one in-process consumer.

### 3. Use Python standard-library HTTP plus browser polling

A daemon `ThreadingHTTPServer` serves a static HTML/CSS/JavaScript asset and two JSON endpoints:

- `GET /api/status` returns the complete bounded snapshot and the latest event sequence.
- `GET /api/events?after=<sequence>` returns subsequent events, a cursor-gap flag, and the next sequence.

The page polls status every second and events more frequently only while visible. Responses use `Cache-Control: no-store`, a restrictive Content Security Policy, `X-Content-Type-Options: nosniff`, and same-origin relative requests. Server-sent events were rejected for the first slice because polling is simpler to stop, test, and recover after tab suspension at this event volume.

### 4. Show bounded recognized text locally but never persist it

The latest final transcript and proposal preview may appear in the dashboard because they are essential to diagnose ASR and explicit-target parsing. Each is UTF-8 bounded to 160 characters before storage and is replaced by the next utterance. Events store at most the same bounded preview and are discarded when the process exits or the deque rotates.

The dashboard excludes shared secrets, API keys, PCM, raw request/response bodies, cwd values, surface UUIDs, full terminal output, permission payloads, and unbounded candidate lists. Normal logs continue to contain only IDs, sizes, latency, state, and error codes.

### 5. Make routing failure a first-class pipeline result

`_propose` publishes and logs the bounded error code for every `RouterError`. The snapshot records the recognized transcript, failure code, up to the protocol candidate limit, and a deterministic hint. For example, `target_required` tells the operator to start with “Codex …”, “Claude …”, “OpenCode …”, or “Kimi …”; ambiguity errors instruct the operator to include the displayed session/project label. The watch wire response remains unchanged.

This directly closes the observed gap: successful ASR followed by a route rejection is displayed as “ASR complete → target required,” rather than appearing idle.

### 6. Use a single responsive dashboard layout

The page contains: a top health strip for watch/bridge/control-plane connectivity; a horizontal voice pipeline showing the latest stage and latency; the recognized-text/routing result card; normalized agent-session cards with capabilities; proposal/task/permission cards when present; and a newest-first event timeline. Semantic colors distinguish healthy, active, waiting, and failed states, while text and icons always provide a non-color cue.

The asset is plain HTML/CSS/JavaScript stored beside the bridge dashboard module. It supports narrow and wide Mac browser windows, respects reduced-motion preferences, and requires no CDN or network resource.

## Risks / Trade-offs

- **[A loopback page still displays sensitive spoken text]** → Bound it to 160 characters, keep it memory-only, disable caching, omit persistence/export, and document that anyone using the unlocked Mac can view it.
- **[HTTP thread reads state while asyncio mutates it]** → Centralize all access behind lock-protected immutable snapshot copies and bounded publish methods.
- **[Dashboard failure could affect voice control]** → Start it as an optional daemon thread, isolate handler exceptions, and keep bridge startup successful when the dashboard port is unavailable.
- **[Polling adds repeated work]** → Return small bounded JSON, poll at one-second cadence, and reduce polling while the tab is hidden.
- **[Runtime instrumentation may accidentally leak payloads]** → Use explicit allowlisted event fields and tests that seed secret-looking values and assert they are absent.
- **[A visually healthy dashboard could imply command authority]** → Keep all endpoints GET-only and label the surface “observe only”; physical KEYA/KEYB remains the sole proposal/permission authority.

## Migration Plan

1. Add failing projector and HTTP endpoint tests before instrumenting the running bridge.
2. Implement bounded `DashboardState`, route-error diagnostics, and optional dashboard server with the feature enabled by default on loopback.
3. Add the static responsive UI and verify loading, empty state, connected state, ASR success plus route failure, proposal, running, permission, terminal, and reconnect states.
4. Run existing protocol-v1/v2, fake-watch, cc-bridge, and native firmware regressions; no firmware wire change is expected.
5. Restart walkie-bridge, open port 8766, reproduce the physical utterance, and record screenshot/API evidence.
6. Roll back by setting `WALKIE_DASHBOARD_ENABLED=0`; port 8765 voice/control behavior remains unchanged.

## Open Questions

- Browser-side command actions may be proposed later, but require a separate authority and threat-model change; they are intentionally excluded here.
- Persistent history and multi-watch support remain future changes after the single-watch in-memory dashboard proves useful.
