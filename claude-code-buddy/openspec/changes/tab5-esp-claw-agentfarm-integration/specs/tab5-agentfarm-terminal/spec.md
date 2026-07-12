## ADDED Requirements

### Requirement: Dedicated Tab5 Agent identity
Agent Farm SHALL define `tab5-operator` as an isolated, low-trust definition
for all device-originated conversations.

#### Scenario: Canary definition is loaded
- **WHEN** Agent Farm loads the initial `tab5-operator` configuration
- **THEN** it uses engine `local`, model `sonnet`, mode `ask`, and cwd `/Users/txie/OpenSourceProjects/hardware-buddies`
- **AND** uses independent memory key `tab5-operator`
- **AND** has pool size zero and no trigger

#### Scenario: Initial capability boundary
- **WHEN** the initial canary is inspected
- **THEN** it has no MCP servers
- **AND** cannot call dashboard administration or other Agent Farm definitions

#### Scenario: Configuration-only rollout
- **WHEN** the definition is added but no authorized prompt has been submitted
- **THEN** no Agent mapping, pool Agent, dispatch trace, or Cursor usage is created for `tab5-operator`

### Requirement: Authenticated device event endpoint
The ESP-Claw terminal SHALL expose a bounded HTTP endpoint that accepts gateway
events only when the device bearer is valid. The native HTTP layer SHALL compare
the bearer against the NVS setting before reading the request body or enqueueing
the request to Lua, and SHALL NOT expose the expected token to Lua.

#### Scenario: Valid event arrives
- **WHEN** the gateway posts a supported sanitized event with the correct bearer
- **THEN** the endpoint returns success and applies the event to terminal state

#### Scenario: Bearer is absent or wrong
- **WHEN** an event request has no bearer or a different bearer
- **THEN** the endpoint returns HTTP 401
- **AND** does not read, parse, enqueue, or apply the event body

#### Scenario: Lua application enables the gate
- **WHEN** the Agent Farm Lua application registers its event route
- **THEN** it declares `af_dev_token` as the native bearer setting
- **AND** receives requests only after native authentication succeeds
- **AND** cannot read the configured token value

#### Scenario: Event schema is invalid
- **WHEN** an authenticated request contains unknown version, type, phase, oversized strings, or malformed JSON
- **THEN** the endpoint rejects the event
- **AND** preserves the previous terminal state

#### Scenario: Credential storage
- **WHEN** the device event token is configured
- **THEN** it is stored in NVS
- **AND** is absent from the Skill source, FAT filesystem config, event JSON, and UI

### Requirement: Cooperative terminal runtime
The terminal SHALL run as one long-lived Lua application that can service HTTP
events and LVGL/touch work without starving either path.

#### Scenario: Device finishes booting
- **WHEN** ESP-Claw emits `startup/boot_completed`
- **THEN** the terminal starts as an asynchronous display-exclusive job with no timeout

#### Scenario: UI is idle
- **WHEN** no event or touch input is pending
- **THEN** the loop waits with bounded latency instead of busy-spinning

#### Scenario: HTTP event arrives during animation
- **WHEN** an authenticated event arrives while the pet or task UI is updating
- **THEN** the HTTP callback completes
- **AND** LVGL processing continues without deadlock or display-owner corruption

#### Scenario: User touches during HTTP activity
- **WHEN** touch input occurs while an event request is queued
- **THEN** the touch event is processed within the UI polling budget

### Requirement: Landscape desktop-pet layout
The terminal SHALL present a two-column 1280×720 desktop-pet interface with
clear hierarchy and touch targets sized for the Tab5.

#### Scenario: Connected idle state
- **WHEN** the gateway is connected and no task is running
- **THEN** the left region shows the lobster in idle state and connection status
- **AND** the right region shows the bounded recent-task list

#### Scenario: Task is running
- **WHEN** a running or progress event is accepted
- **THEN** the active task is visually prominent
- **AND** the lobster changes to a busy state

#### Scenario: Task succeeds
- **WHEN** a success event is accepted
- **THEN** the active item is marked successful
- **AND** the lobster briefly shows a completion reaction before returning to idle

#### Scenario: Task fails
- **WHEN** an error event is accepted
- **THEN** the active item is marked failed without displaying a raw error
- **AND** the lobster shows an attention state

### Requirement: Local event rendering without LLM invocation
Lifecycle events SHALL update local terminal state and visuals without
submitting an ESP-Claw LLM request or an Agent Farm dispatch.

#### Scenario: Status event updates UI
- **WHEN** the device receives a valid task lifecycle event
- **THEN** only the local history, active-task view, and pet state are updated
- **AND** no chat-completions request is sent

#### Scenario: Repeated progress event
- **WHEN** progress metadata changes for the same active task
- **THEN** the existing item is updated instead of creating a new task

### Requirement: Bounded and deduplicated task history
The terminal SHALL retain a bounded recent-event history and SHALL prevent
retries or reconnects from producing duplicate visible entries.

#### Scenario: Duplicate event arrives
- **WHEN** an event with the same type, definition, agent, phase, and timestamp is received again
- **THEN** it is ignored

#### Scenario: History reaches capacity
- **WHEN** a new task item arrives at the configured history limit
- **THEN** the oldest completed item is removed
- **AND** the active item remains visible

### Requirement: Explicit safe touch actions
Touch controls SHALL clearly distinguish local actions from actions that
submit a real Agent Farm request.

#### Scenario: Local status action
- **WHEN** the user taps a local status or view action
- **THEN** the terminal changes local presentation only
- **AND** does not call the gateway chat route

#### Scenario: Dispatching touch action
- **WHEN** the user taps an action labeled as sending a task
- **THEN** the terminal requires an explicit confirmation gesture or dialog
- **AND** submits only its fixed documented prompt to `tab5-operator`

#### Scenario: No cancellation contract
- **WHEN** a task is running
- **THEN** the UI does not present a control labeled cancel, stop, abort, or kill

### Requirement: Free-form chat uses the dedicated gateway
When chat is enabled, ESP-Claw SHALL use the Primary-Mac gateway as its
OpenAI-compatible provider and authenticate with the device-facing token.

#### Scenario: User submits Web Chat text
- **WHEN** the user sends a free-form message through ESP-Claw Web Chat
- **THEN** the request reaches only `tab5-operator`
- **AND** the returned Agent Farm reply appears through the normal ESP-Claw response path

#### Scenario: Gateway is unavailable
- **WHEN** the Primary-Mac gateway cannot be reached
- **THEN** the terminal shows a bounded unavailable state
- **AND** retains local event history and touch navigation

### Requirement: Connection and recovery states
The terminal SHALL represent gateway connectivity independently from Agent
task state and recover automatically after transient failures.

#### Scenario: Event heartbeat becomes stale
- **WHEN** no accepted event or heartbeat arrives within the configured stale interval
- **THEN** the terminal marks the gateway disconnected without deleting history

#### Scenario: Gateway reconnects
- **WHEN** a valid event or heartbeat arrives after a stale interval
- **THEN** the terminal returns to connected state
- **AND** does not replay completion animations for historical items

### Requirement: Acceptance evidence
The completed terminal SHALL be accepted only with evidence from the physical
device and both network hops.

#### Scenario: Event-only end-to-end test
- **WHEN** an allowlisted lifecycle event is emitted on Mac2 with gateway chat disabled
- **THEN** the event is sanitized on Mac2, observed by the Primary-Mac gateway, authenticated at the device, and rendered without any Agent dispatch

#### Scenario: Authorized real chat test
- **WHEN** the owner explicitly authorizes one real `tab5-operator` prompt
- **THEN** evidence includes the device request, scoped gateway request, Agent Farm trace, reply on Tab5, and usage attributed to `tab5-operator`

#### Scenario: Rollback test
- **WHEN** the standalone gateway is stopped
- **THEN** existing Agent Farm Feishu and other definitions remain healthy
- **AND** the Tab5 falls back to a disconnected local UI without repeated network churn
