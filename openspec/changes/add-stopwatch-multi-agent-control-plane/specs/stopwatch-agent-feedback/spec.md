## ADDED Requirements

### Requirement: Agent task events remain correlated and ordered
Every dispatched command SHALL receive a unique task identifier. The bridge SHALL accept and forward only monotonically ordered lifecycle events associated with that task and its target session.

#### Scenario: Command is accepted by cmux
- **WHEN** the local control plane successfully focuses the verified surface, sends the reviewed text, and submits Enter
- **THEN** the bridge SHALL emit `task.accepted` with the matching command and task identifiers

#### Scenario: Late event belongs to an older task
- **WHEN** a session event names a task that is no longer the watch's tracked task
- **THEN** the bridge and watch SHALL ignore it without replacing the current display state

### Requirement: Session activity is reported without overstating causality
The bridge SHALL map verified post-dispatch session activity to running, waiting-permission, completed, or failed states. When an adapter provides only session-level evidence, any completion summary MUST be identified as a bounded pane/status snapshot rather than an exact agent response.

#### Scenario: Session runs and returns to idle
- **WHEN** the target session becomes active after dispatch and later returns to idle without an error signal
- **THEN** the bridge SHALL emit a completed event with a bounded summary and `summary_source` identifying the observed source

#### Scenario: Concurrent activity makes attribution ambiguous
- **WHEN** the bridge cannot distinguish the watch command from other input or a competing tracked task on the same session
- **THEN** it SHALL stop tracking with `ambiguous_session_activity` rather than report a fabricated completion

### Requirement: Permission feedback is capability-driven
The session snapshot SHALL state whether a session has a verified bidirectional permission adapter. Only a correlated live permission request from such an adapter may display actionable KEYA/KEYB approval controls.

#### Scenario: Supported permission request arrives
- **WHEN** a tracked session emits a bounded permission request with a live request identifier and `permission_reply=true`
- **THEN** the watch SHALL enter waiting-permission state, vibrate, show the agent/tool/redacted hint, and map KEYA to approve-once and KEYB to deny

#### Scenario: Session lacks permission reply support
- **WHEN** a tracked Codex or Kimi session reports a permission-like waiting state without a verified reply adapter
- **THEN** the watch MAY notify the user to answer in the terminal but MUST NOT show an actionable physical approval control

#### Scenario: Permission was resolved elsewhere
- **WHEN** another buddy or the terminal resolves the request before the watch decision arrives
- **THEN** the first resolution SHALL win and the stale watch response SHALL produce a non-actionable resolved event

### Requirement: Feedback is bounded for the watch
All proposal, progress, permission, and result fields sent to the StopWatch MUST have protocol-defined maximum lengths before allocation. The watch SHALL hold at most one proposal, one tracked task, one permission request, and the existing bounded TTS PCM buffer.

#### Scenario: Agent result exceeds the display limit
- **WHEN** a session summary or error detail is larger than the protocol maximum
- **THEN** the bridge SHALL truncate it at a valid UTF-8 boundary with an explicit truncation indicator before transmission

### Requirement: Reconnect restores observation without repeating side effects
After a fresh authenticated reconnect, the watch MAY request the status of its last task identifier. The bridge SHALL return the current or cached terminal state but MUST NOT recreate proposals, resend commands, or repeat permission decisions.

#### Scenario: Watch reconnects while task is running
- **WHEN** the watch reauthenticates and requests a task that remains associated with a live running session
- **THEN** the bridge SHALL restore running observation without dispatching another command

#### Scenario: Watch reconnects after task completion
- **WHEN** the watch reauthenticates and requests a task whose bounded terminal event remains cached
- **THEN** the bridge SHALL return that event once as state synchronization and SHALL NOT synthesize new execution

### Requirement: Haptic and audio feedback never change task authority
Vibration and TTS SHALL communicate proposal, permission, and completion states but MUST NOT themselves approve, reject, retry, or dispatch a command. KEYB MAY stop playback without mutating an accepted agent task.

#### Scenario: User cancels completion TTS
- **WHEN** the watch is playing a completed-task summary and the user presses KEYB
- **THEN** playback SHALL stop and microphone readiness SHALL recover without changing the completed task state on the Mac

