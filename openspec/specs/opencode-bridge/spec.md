# opencode-bridge Specification

## Purpose
TBD - created by archiving change consolidate-standalone-buddy. Update Purpose after archive.
## Requirements
### Requirement: OpenCode session discovery via cmux agent panes
The bridge SHALL read the cmux session JSON and surface panes whose `terminal.agent.kind == "opencode"` as ext-sessions pushed to cc-bridge, keyed by the pane's agent `sessionId`.

#### Scenario: opencode launched via cmux agent path
- **WHEN** an opencode process is launched through cmux's agent-launch mechanism (pane has `terminal.agent.kind = "opencode"` and a `sessionId`)
- **THEN** the bridge MUST emit an ext_sessions message to cc-bridge with `agent: "opencode"` and one row whose `sid` is the cmux `sessionId`, `label` is the customTitle (or cwd basename), and `st: "idle"`

#### Scenario: no opencode panes
- **WHEN** no cmux pane has `agent.kind == "opencode"`
- **THEN** the discovery function MUST return an empty dict and no opencode rows SHALL be pushed

### Requirement: OpenCode session discovery via tty fallback
Because opencode can be launched manually inside a plain cmux terminal pane (no `agent.kind`), the bridge SHALL also discover opencode panes by matching the pane's `ttyName` against the set of ttys running an `opencode` process.

#### Scenario: opencode launched manually in a cmux pane
- **WHEN** an opencode process runs on tty `ttys019` and a cmux pane has `ttyName == "ttys019"` but no `terminal.agent.kind`
- **THEN** the bridge MUST treat that pane as an opencode session, synthesize a stable sid `oc-<paneId[:8]>` (prefixed to avoid collision with real UUIDs), and push it as an ext_session with `agent: "opencode"`

#### Scenario: opencode process with no controlling tty
- **WHEN** an opencode process has tty `??` (no controlling terminal)
- **THEN** it SHALL be excluded from the tty set and no pane SHALL be matched against it

### Requirement: OpenCode permission gating relay
The bridge's opencode plugin SHALL wire `permission.asked` events to cc-bridge's `wait_permission` RPC so the cardputer shows an approval panel tagged `oc`, then translate the device decision back to OpenCode's REST `/permission/{id}/reply`.

#### Scenario: device approves once
- **WHEN** the device returns `decision: "once"`
- **THEN** the plugin MUST POST `{ "reply": "once" }` to OpenCode's `/permission/{id}/reply`

#### Scenario: device denies
- **WHEN** the device returns `decision: "deny"`
- **THEN** the plugin MUST POST `{ "reply": "reject" }`

#### Scenario: device times out or returns ask
- **WHEN** the device returns `null`/`"ask"` or the 30s socket times out
- **THEN** the plugin SHALL NOT POST a reply (lets OpenCode prompt the user inline)

### Requirement: No BLE device ownership
The bridge SHALL own no BLE device; it MUST be push-only, like codex-bridge.

#### Scenario: standalone operation
- **WHEN** the bridge runs with `no_ble=True`
- **THEN** it SHALL NOT scan for or connect to any `OpenCode-*` device; all session/permission traffic MUST flow through cc-bridge's socket as ext_sessions / wait_permission

