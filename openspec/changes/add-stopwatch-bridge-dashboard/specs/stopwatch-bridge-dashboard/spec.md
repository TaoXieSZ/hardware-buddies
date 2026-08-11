## ADDED Requirements

### Requirement: Walkie bridge exposes an independent loopback dashboard
The walkie-bridge SHALL expose an optional read-only HTTP dashboard on `127.0.0.1:8766` by default. The dashboard MUST remain separate from the cc-bridge dashboard, MUST expose no command or approval mutation endpoint, and MUST NOT make the audio/control WebSocket unavailable when its own server cannot start.

#### Scenario: Dashboard starts with walkie-bridge
- **WHEN** walkie-bridge starts with dashboard support enabled and the configured loopback port is available
- **THEN** the dashboard page and bounded status/event endpoints SHALL become available without changing the WebSocket listener on port 8765

#### Scenario: Dashboard port is unavailable
- **WHEN** the configured dashboard port cannot be bound
- **THEN** walkie-bridge SHALL report a bounded dashboard error and SHALL continue serving the StopWatch audio/control path

#### Scenario: Client attempts a mutation request
- **WHEN** a browser sends a non-GET request or requests an undefined command, approval, rejection, or cancellation endpoint
- **THEN** the dashboard server SHALL reject it without changing proposal, permission, task, or watch state

### Requirement: Dashboard projects the live StopWatch pipeline
The dashboard SHALL display bounded current state for watch connectivity, protocol version, control authentication, utterance capture, ASR, deterministic routing, proposal review, task lifecycle, permission state, TTS, cc-bridge health, and normalized live agent sessions. State SHALL be derived from semantic bridge transitions rather than parsing logs.

#### Scenario: Watch completes an utterance
- **WHEN** a connected watch starts recording, submits audio, and receives a final ASR result
- **THEN** the dashboard SHALL advance through recording and ASR stages and display final byte count, latency, and recognized-text preview

#### Scenario: Control proposal becomes a task
- **WHEN** a uniquely routed proposal is created and physically approved on the watch
- **THEN** the dashboard SHALL display the correlated proposal, accepted task, agent/session labels, and subsequent running or terminal state

#### Scenario: Watch disconnects and reauthenticates
- **WHEN** the WebSocket disconnects and later establishes a fresh authenticated protocol-v2 session
- **THEN** the dashboard SHALL show the offline/reconnecting interval followed by the new authenticated state without claiming that an old proposal was restored

### Requirement: Routing failures are visible and actionable
Every deterministic routing failure after successful ASR SHALL update the dashboard with its bounded error code, bounded candidate labels, and a deterministic targeting hint while preserving the existing fail-closed watch behavior.

#### Scenario: Transcript omits an explicit target
- **WHEN** ASR succeeds but the transcript does not begin with a configured alias or supported agent target
- **THEN** the dashboard SHALL show `target_required`, the recognized preview, and a hint to begin with Claude, Codex, OpenCode, Kimi, or a configured alias

#### Scenario: Transcript resolves multiple sessions
- **WHEN** the target agent or label matches more than one steerable live session
- **THEN** the dashboard SHALL show `target_ambiguous` with bounded candidate labels and SHALL state that no command was sent

#### Scenario: Local control plane is unavailable
- **WHEN** walkie-bridge cannot obtain a valid cc-bridge control snapshot
- **THEN** the dashboard SHALL show `control_plane_unavailable`, mark cc-bridge unhealthy, and SHALL NOT present a proposal as actionable

### Requirement: Dashboard state and history are bounded and memory-only
The bridge SHALL retain at most one current snapshot and 200 recent dashboard events in process memory. Recognized text and proposal previews MUST be truncated at a valid UTF-8 boundary to 160 characters. Dashboard state MUST NOT be written to disk or normal logs.

#### Scenario: Transcript exceeds the preview limit
- **WHEN** ASR returns more than 160 UTF-8 characters
- **THEN** the dashboard state SHALL store a visibly truncated preview while routing continues to use the separately bounded protocol command text

#### Scenario: Event capacity is exceeded
- **WHEN** more than 200 dashboard events are published
- **THEN** the oldest events SHALL be discarded and an event request behind the retained cursor SHALL receive an explicit cursor-gap indication

#### Scenario: Process restarts
- **WHEN** walkie-bridge stops and starts again
- **THEN** previous transcript previews and dashboard events SHALL not be restored from disk

### Requirement: Dashboard excludes secrets and unsafe internal data
Dashboard responses MUST use an allowlisted schema and MUST NOT contain the control secret, DashScope key, PCM/audio bodies, raw HTTP/WebSocket payloads, cwd values, cmux surface UUIDs, raw terminal output, credentials, or unbounded permission details. HTTP responses SHALL disable caching and apply restrictive browser security headers.

#### Scenario: Runtime contains secret-looking values
- **WHEN** bridge configuration, agent state, or an error contains credential-like or internal values
- **THEN** status and event JSON SHALL omit those values while retaining bounded non-secret error codes and labels

#### Scenario: Browser loads dashboard resources
- **WHEN** the dashboard page or JSON endpoints return successfully
- **THEN** responses SHALL include no-store caching and same-origin security headers and SHALL load no CDN or remote asset

### Requirement: Dashboard remains usable across live and empty states
The browser UI SHALL update without a full-page reload, SHALL remain understandable without color alone, SHALL support narrow and wide Mac browser windows, and SHALL clearly distinguish disconnected, listening, recording, transcribing, target-required, proposal, running, permission, completed, and failed states.

#### Scenario: No watch is connected
- **WHEN** the dashboard loads before any StopWatch connection
- **THEN** it SHALL render a stable empty state that explains the expected bridge endpoint and does not show stale activity

#### Scenario: Browser tab resumes after suspension
- **WHEN** a hidden dashboard tab misses events and becomes visible again
- **THEN** it SHALL refresh the complete status snapshot and continue from the latest valid event cursor

#### Scenario: Reduced motion is requested
- **WHEN** the browser reports a reduced-motion preference
- **THEN** the dashboard SHALL avoid non-essential animation while preserving every state and status cue
