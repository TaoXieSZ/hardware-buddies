## ADDED Requirements

### Requirement: The router consumes a normalized multi-agent session snapshot
The local control plane SHALL expose live Claude Code, Codex, OpenCode, and Kimi Code sessions through one normalized snapshot containing an opaque session key, agent type, bounded labels, state, capability flags, and snapshot revision. Repository paths and cmux surface identifiers MUST remain Mac-local.

#### Scenario: Four agent types are live
- **WHEN** one uniquely identifiable live cmux session exists for each supported agent type
- **THEN** the snapshot SHALL contain four distinct session keys tagged `claude`, `codex`, `opencode`, and `kimi` with their current capability flags

#### Scenario: Session cannot be mapped uniquely
- **WHEN** two candidate surfaces collide under an adapter's available native identity
- **THEN** the snapshot SHALL preserve a non-sensitive visible entry but MUST mark it non-steerable rather than choosing the first surface

### Requirement: Target resolution is explicit and deterministic
The router SHALL resolve only configured project aliases, supported agent names, and unique live session labels. It MUST NOT use an LLM or ordinal position to silently choose among missing or ambiguous targets in this change.

#### Scenario: Explicit agent and unique session alias
- **WHEN** a transcript names a configured agent and an alias that maps to exactly one steerable live session
- **THEN** the router SHALL return that session key and the remaining exact command text for proposal review

#### Scenario: Target ordinal changes after a pane closes
- **WHEN** cmux pane order changes between transcription and proposal creation
- **THEN** resolution SHALL remain bound to the stable session key and SHALL NOT retarget based on the new ordinal

### Requirement: Dispatch uses a two-phase local control-plane operation
The router SHALL stage a command against one live session key and SHALL invoke confirm only after the authenticated watch decision. The local control plane MUST revalidate the target session identity and snapshot revision immediately before sending the exact staged text followed by Enter.

#### Scenario: Target remains live through confirmation
- **WHEN** a valid proposal is approved and its session key still maps to the same steerable cmux surface
- **THEN** the control plane SHALL focus that surface, send the exact staged text using argument-array RPC calls, send Enter separately, and return one accepted task identifier

#### Scenario: Target disappears before confirmation
- **WHEN** a proposal is approved after its target session closes or loses its unique mapping
- **THEN** dispatch SHALL fail closed with `target_stale` and SHALL NOT send the command to another surface

#### Scenario: Command contains shell metacharacters
- **WHEN** a staged command contains quotes, dollar signs, backticks, or newline-like text
- **THEN** the control plane SHALL pass it as RPC data without shell interpolation and SHALL send exactly the reviewed text

### Requirement: All supported agents use the same steer contract
Claude Code, Codex, OpenCode, and Kimi Code adapters SHALL implement the same snapshot and steer capability contract while retaining agent-specific identity discovery internally.

#### Scenario: Adapter supports a uniquely mapped live surface
- **WHEN** any supported adapter returns `steer=true` for a live session
- **THEN** the router SHALL be able to stage and dispatch to it without agent-specific logic in the StopWatch firmware

#### Scenario: Agent bridge is unavailable
- **WHEN** one agent's discovery source is unavailable or malformed
- **THEN** the remaining agent sessions SHALL stay routable and the unavailable agent SHALL be reported as a bounded capability error

### Requirement: New-session spawning is not exposed
The router MUST reject any request to create an agent process, workspace, or session in this change and MUST NOT translate a missing target into a new task spawn.

#### Scenario: User asks to start a new Codex task
- **WHEN** no matching live Codex session exists and the transcript asks to create one
- **THEN** the router SHALL return `spawn_not_supported` and SHALL perform no process or cmux creation

