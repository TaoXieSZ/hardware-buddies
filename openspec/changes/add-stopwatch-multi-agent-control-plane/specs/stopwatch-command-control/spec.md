## ADDED Requirements

### Requirement: Protocol-v2 control sessions are mutually authenticated
Before the bridge accepts any command decision or other side-effecting control message, the StopWatch and bridge MUST mutually prove possession of the configured per-device secret with fresh nonces and HMAC-SHA-256. Every subsequent control envelope MUST bind its direction, authenticated session identifier, strictly increasing sequence number, and exact encoded body to a valid MAC.

#### Scenario: Valid mutual authentication
- **WHEN** the StopWatch and bridge exchange fresh nonces and both return valid proofs for the configured device secret
- **THEN** the bridge SHALL establish a new control session with independent inbound and outbound sequence counters

#### Scenario: Invalid authentication proof
- **WHEN** either endpoint receives a missing or invalid authentication proof
- **THEN** it SHALL reject control mode, SHALL NOT create or confirm a command, and SHALL return or display a non-secret authentication error

#### Scenario: Replayed control envelope
- **WHEN** an authenticated envelope repeats or lowers the last accepted sequence number for its direction and session
- **THEN** the receiver SHALL reject it as a replay without changing proposal, task, or permission state

### Requirement: Protocol v1 remains audio-only
The bridge SHALL preserve protocol-v1 transcription and echo behavior during migration, but MUST NOT expose command proposals, command confirmation, session routing, or permission decisions to an unauthenticated protocol-v1 connection.

#### Scenario: Existing protocol-v1 watch connects
- **WHEN** a protocol-v1 client completes a valid utterance
- **THEN** the bridge SHALL return its correlated transcript and configured echo TTS while performing no local agent-control action

### Requirement: Recognized commands are staged as explicit proposals
For an authenticated transcript that resolves exactly one steerable session, the bridge SHALL create a server-side proposal containing a unique command identifier, target revision, expiry, exact command text, and target session key. The watch SHALL display only the bounded command preview, agent, project label, session label, and KEYA/KEYB decision cues.

#### Scenario: Unique target produces a proposal
- **WHEN** an authenticated utterance resolves one live steerable session and a non-empty command
- **THEN** the bridge SHALL send a correlated `command.proposal` and SHALL NOT send text to the target session yet

#### Scenario: Missing or ambiguous target
- **WHEN** the transcript has no explicit target or resolves multiple live sessions
- **THEN** the bridge SHALL return a retryable target error with bounded candidate labels and SHALL NOT create an executable proposal

### Requirement: Physical decisions consume the exact pending proposal
KEYA in proposal-review state SHALL approve the displayed command and KEYB SHALL reject it. The bridge MUST atomically consume only a matching, authenticated, unexpired command identifier whose target revision remains valid.

#### Scenario: User approves a valid proposal
- **WHEN** KEYA produces a valid authenticated approval for the currently displayed unexpired command identifier
- **THEN** the bridge SHALL consume that proposal exactly once and request staged dispatch of its stored target and exact command text

#### Scenario: User rejects a proposal
- **WHEN** KEYB produces a valid authenticated rejection for the currently displayed command identifier
- **THEN** the bridge SHALL discard the proposal, return a correlated cancelled event, and perform no terminal action

#### Scenario: Duplicate or stale approval
- **WHEN** an approval is duplicated, expired, belongs to an older authenticated session, or names a command other than the current proposal
- **THEN** the bridge SHALL reject it without dispatching any command

#### Scenario: Connection closes with a proposal pending
- **WHEN** the authenticated WebSocket disconnects before the proposal is consumed
- **THEN** the bridge SHALL invalidate the proposal and SHALL NOT restore or execute it after reconnect

### Requirement: Watch button behavior is state-specific
The StopWatch SHALL distinguish audio capture, proposal review, task observation, actionable permission, and terminal acknowledgement states so the same physical key cannot accidentally perform the wrong action.

#### Scenario: KEYA is pressed while ready
- **WHEN** the authenticated watch is in ready state and KEYA is pressed
- **THEN** it SHALL start a new push-to-talk utterance and SHALL NOT confirm an older command or permission

#### Scenario: KEYA is pressed during actionable approval
- **WHEN** the watch displays the current proposal or an actionable permission request and KEYA is pressed
- **THEN** it SHALL send only the matching authenticated approval and SHALL NOT start microphone capture

#### Scenario: KEYB is pressed after dispatch
- **WHEN** a task has already been accepted and KEYB is pressed
- **THEN** the watch MAY stop local playback or dismiss the view but MUST NOT claim that the remote agent task was killed or rolled back

### Requirement: Control secrets and command content remain private in local storage and logs
The per-device secret MUST exist only in ignored local configuration on the watch build and Mac. Normal logs MUST NOT include that secret, full spoken commands, full proposals, repository paths, or terminal output.

#### Scenario: Authentication or dispatch fails
- **WHEN** the bridge logs an authentication, routing, or dispatch error
- **THEN** the log SHALL contain bounded identifiers and error codes without secret material or full command text

