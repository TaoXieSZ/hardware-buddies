## ADDED Requirements

### Requirement: cc-bridge exposes a bounded local control snapshot
cc-bridge SHALL expose an owner-only Unix-socket action that returns its normalized live Claude Code, Codex, OpenCode, and Kimi Code session snapshot with opaque session keys, bounded labels, states, capability flags, and a monotonic revision. It MUST NOT return credentials, full terminal text, raw cwd values, BLE internals, or unbounded hook payloads.

#### Scenario: Walkie bridge requests the current snapshot
- **WHEN** an authorized local peer sends the snapshot action to the cc-bridge socket
- **THEN** cc-bridge SHALL return the normalized sessions and current revision without changing BLE ownership or session state

#### Scenario: Unauthorized local process opens the socket
- **WHEN** the socket peer does not satisfy the owner-only local access policy
- **THEN** cc-bridge SHALL reject the action and SHALL NOT disclose session metadata or accept a staged route

### Requirement: cc-bridge stages and confirms a stable agent route
cc-bridge SHALL provide command-identified stage, confirm, and cancel actions backed by its existing route stager and cmux client. Confirmation MUST revalidate that the opaque session key still maps uniquely to the same live agent surface before sending the exact staged text.

#### Scenario: Staged route is confirmed
- **WHEN** an authorized peer confirms the current unexpired command identifier and target mapping
- **THEN** cc-bridge SHALL route the exact command once and return a correlated accepted result

#### Scenario: Confirmation names another command
- **WHEN** confirm or cancel names an identifier other than the currently staged command
- **THEN** cc-bridge SHALL preserve or expire the real pending command according to its TTL and SHALL perform no route for the mismatched identifier

### Requirement: cc-bridge provides bounded event polling
cc-bridge SHALL provide a cursor-based local event action for session state, supported permission requests, permission resolution, and terminal status. The event buffer MUST be bounded and MUST signal a cursor gap rather than silently presenting incomplete history as complete.

#### Scenario: Peer polls after its last cursor
- **WHEN** walkie-bridge requests events after a valid retained cursor
- **THEN** cc-bridge SHALL return ordered bounded events and the next cursor without consuming events needed by existing device paths

#### Scenario: Peer cursor fell behind the retained window
- **WHEN** the requested cursor is older than the bounded event buffer
- **THEN** cc-bridge SHALL return a gap indicator and require a fresh snapshot rather than fabricate missing transitions

### Requirement: Permission resolution is shared and first-response-wins
For an agent with a verified permission reply adapter, cc-bridge SHALL allow an authorized local peer to resolve the same pending future used by existing buddy devices. Exactly the first valid response SHALL be applied to the agent.

#### Scenario: StopWatch approves first
- **WHEN** the StopWatch path supplies a valid approve-once decision before any other responder
- **THEN** cc-bridge SHALL resolve the agent permission once and mark later responses stale

#### Scenario: Existing buddy answers first
- **WHEN** an existing buddy resolves the permission before the StopWatch response
- **THEN** existing behavior SHALL remain unchanged and the StopWatch response SHALL be rejected as already resolved

### Requirement: Existing cc-bridge consumers remain compatible
The added local control actions and event observations MUST NOT change existing BLE payloads, session ordering, focus behavior, hook ingestion, question answering, or current `stage_route` callers.

#### Scenario: No walkie-bridge peer is connected
- **WHEN** cc-bridge runs with its existing devices and agent bridges only
- **THEN** all pre-change session, permission, question, gesture-route, and focus behavior SHALL continue without requiring new configuration

