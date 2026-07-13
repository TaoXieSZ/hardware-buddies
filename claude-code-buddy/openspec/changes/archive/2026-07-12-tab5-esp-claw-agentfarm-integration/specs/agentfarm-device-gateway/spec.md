## ADDED Requirements

### Requirement: Local gateway credential isolation
The integration SHALL use dedicated device-facing chat and event tokens. The
Tab5 SHALL receive no dashboard or agent-host credential.

#### Scenario: Local gateway starts safely
- **WHEN** the local gateway starts
- **THEN** its device-facing token is present
- **AND** it reads any agent-host secret only from the local process environment

#### Scenario: Device token is missing
- **WHEN** the local gateway token is absent
- **THEN** startup fails closed

#### Scenario: Device targets an unrelated service
- **WHEN** the device-facing token is presented to dashboard or agent-host
- **THEN** that service rejects it

### Requirement: Fixed-definition dispatch route
The local gateway SHALL accept a prompt but always target the configured
`tab5-operator` definition through a bare local Dispatcher.

#### Scenario: Valid scoped dispatch
- **WHEN** an authenticated caller posts a non-empty prompt within the size limit to `/v1/chat/completions`
- **THEN** dispatch receives the prompt for `tab5-operator`

#### Scenario: Caller attempts definition override
- **WHEN** the request includes another definition, dynamic flag, session ID, model, or other dispatch selector
- **THEN** those selectors are ignored
- **AND** only `tab5-operator` can be targeted

#### Scenario: Definition is absent
- **WHEN** `tab5-operator` is not present in the live Agent Farm configuration
- **THEN** the local gateway fails startup
- **AND** no Agent is created

#### Scenario: Prompt is invalid
- **WHEN** the prompt is empty, non-string, or exceeds 16000 characters
- **THEN** the request is rejected without calling Agent Farm dispatch

### Requirement: Sanitized in-process lifecycle events
The local gateway SHALL subscribe to its bare Dispatcher's event bus, reconstruct
lifecycle events from an explicit field allowlist, and filter definitions
through a configured allowlist.

#### Scenario: Allowed task starts
- **WHEN** an allowlisted definition emits a dispatch-attempt event
- **THEN** the gateway emits version, task type, running phase, definition, safe agent ID, and timestamp to the device

#### Scenario: Allowed task completes
- **WHEN** an allowlisted definition emits a dispatch-result event
- **THEN** the gateway emits success or error phase and safe duration metadata
- **AND** omits raw error text and reply content

#### Scenario: Sensitive or unsupported event arrives
- **WHEN** an event contains prompt text, reply text, sender identity, raw error, filesystem path, config data, or an unsupported event type
- **THEN** those fields are not emitted to the device stream

#### Scenario: Other definition emits an event
- **WHEN** a definition outside the visible-definition allowlist emits an event
- **THEN** the event is dropped

### Requirement: Fail-closed gateway modes
The Primary-Mac gateway SHALL default to loopback binding with chat and event
forwarding disabled, and SHALL require explicit configuration for each wider
capability.

#### Scenario: Default startup
- **WHEN** the gateway starts without explicit enable flags
- **THEN** it listens only on `127.0.0.1`
- **AND** does not register `/v1/chat/completions`
- **AND** does not post events

#### Scenario: Full local dispatch remains stopped
- **WHEN** the dedicated gateway is running
- **THEN** the full local dispatch service remains stopped
- **AND** no Feishu channel, cron scheduler, sweep, or warm pool is started by the gateway

#### Scenario: Forward mode lacks device configuration
- **WHEN** forward mode is selected without both device event URL and device event token
- **THEN** gateway startup fails

### Requirement: OpenAI-compatible chat contract
When explicitly enabled, the gateway SHALL provide an authenticated
OpenAI-compatible chat-completions route backed only by the local
`tab5-operator` Dispatcher.

#### Scenario: Non-stream request
- **WHEN** a valid request omits `stream` or sets it to false
- **THEN** the gateway returns one JSON `chat.completion` response

#### Scenario: Stream request
- **WHEN** a valid request sets `stream: true`
- **THEN** the gateway returns SSE chunks containing the assistant role and content, a `finish_reason: stop` chunk, and `[DONE]`

#### Scenario: Conversation history is replayed
- **WHEN** the request contains multiple historical messages
- **THEN** only the newest non-empty user string is forwarded to Agent Farm

#### Scenario: Client supplies a model
- **WHEN** the request model differs from `tab5-operator`
- **THEN** the gateway ignores it and reports the fixed definition identity

#### Scenario: Concurrent request arrives
- **WHEN** one chat request is still in flight and another arrives
- **THEN** the second request receives HTTP 429
- **AND** no second dispatch is submitted

#### Scenario: Upstream exposes an error detail
- **WHEN** Mac2 dispatch fails or returns an internal error
- **THEN** the device receives a generic failure response
- **AND** upstream secret, path, or error detail is not exposed

### Requirement: Authenticated device event forwarding
Forward mode SHALL send sanitized events to the Tab5 event endpoint with a
dedicated bearer and without credentials in the JSON body.

#### Scenario: Event forwarding succeeds
- **WHEN** the gateway receives a valid allowlisted lifecycle event in forward mode
- **THEN** it posts only the sanitized event object to the configured device URL
- **AND** sends the device token in the `Authorization` header

#### Scenario: Agent Farm is idle
- **WHEN** forward mode is enabled and no lifecycle event is emitted
- **THEN** the gateway sends a sanitized `link/heartbeat` event every five seconds
- **AND** does not invoke Agent Farm dispatch

#### Scenario: Device rejects or times out
- **WHEN** the device returns a non-success status or does not respond within the delivery timeout
- **THEN** the gateway records a redacted delivery warning
- **AND** keeps the upstream SSE connection and later events active

### Requirement: Graceful process lifecycle
The standalone gateway SHALL release its listener and active SSE request when
stopped.

#### Scenario: PM2 sends SIGTERM
- **WHEN** the gateway receives SIGTERM or SIGINT
- **THEN** it aborts the SSE request, closes the HTTP server, and exits without waiting for a forced kill
