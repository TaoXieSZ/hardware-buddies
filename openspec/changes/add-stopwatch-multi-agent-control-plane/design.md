## Context

The current StopWatch firmware and walkie-bridge implement protocol v1: one KEYA-bounded PCM utterance is assembled on the Mac, transcribed by DashScope `qwen3-asr-flash`, returned as a correlated transcript, synthesized with macOS `say`, and played on the watch. The firmware state machine has six states and accepts only `transcript`, `audio.start`, `audio.end`, and structured `error` responses.

The monorepo already has most of the existing-session control substrate in `claude-code-buddy/tools/`: cc-bridge aggregates Claude Code plus external Codex, OpenCode, and Kimi Code sessions; `CmuxClient` can enumerate terminal surfaces, focus one stable surface UUID, send text, press Enter, and read visible status; `RouteStager` provides a TTL-bounded stage/confirm/cancel gate. Claude Code and OpenCode already have permission relay paths, while Codex and Kimi Code do not expose an equivalent uniform permission response contract.

This change joins those systems without moving cmux ownership into walkie-bridge. It must preserve the existing audio prototype, existing buddy devices, and all current agent bridges. The StopWatch remains a thin physical interaction terminal; credentials, session identities, repository paths, command routing, and agent processes remain on the Mac.

## Goals / Non-Goals

**Goals:**

- Turn a final ASR transcript with an explicit target into a reviewable command proposal, never an immediate terminal action.
- Require a fresh physical KEYA confirmation before routing the exact staged command to one live Claude Code, Codex, OpenCode, or Kimi Code cmux surface.
- Use stable opaque session keys and an agent-neutral local contract rather than UI ordinals, cwd suffixes, or direct imports of individual bridge internals.
- Authenticate control messages and reject replayed decisions before enabling any terminal side effect.
- Return correlated acceptance, live state, supported permission requests, completion/failure, and bounded summaries to the watch.
- Preserve protocol-v1 audio-only operation as a safe fallback during migration.

**Non-Goals:**

- Starting new agent processes or cmux workspaces.
- Automatically choosing an agent or project when the spoken target is absent or ambiguous.
- Killing, interrupting, or rolling back a command after it has been submitted to a terminal.
- Enabling bypass-permission/yolo modes from voice.
- Providing transport confidentiality for prototype PCM audio; the LAN remains trusted and unexposed.
- Adding offline ASR, streaming partial ASR, mDNS, launchd installation, multi-watch routing, or internet access.
- Claiming exact result attribution when an agent bridge exposes only session-level state and pane text.

## Decisions

### 1. Add protocol v2 alongside the existing audio-only protocol v1

Protocol v2 retains the existing utterance framing and adds authenticated envelopes plus command/task messages. A v1 client may continue to transcribe and receive echo TTS, but the bridge MUST NOT create proposals or invoke the local control plane for it. A v2 client that fails authentication is similarly limited to a structured connection error and no side effects.

This keeps rollback cheap and prevents a partially upgraded device from accidentally gaining command authority. A breaking in-place reinterpretation of v1 was rejected because deployed firmware would not understand proposal or permission states.

### 2. Authenticate control messages with a per-device secret and HMAC-SHA-256

The ignored watch configuration and walkie-bridge environment share a randomly generated 32-byte secret. The handshake exchanges independent device and bridge nonces and mutually proves possession of that secret using HMAC-SHA-256 as defined by RFC 2104. After authentication, each JSON control body is UTF-8 encoded, base64url wrapped, and carried in an envelope containing `session_id`, direction, and a strictly increasing 64-bit sequence number. The MAC input is the exact byte string:

```text
walkie-v2\n<direction>\n<session_id>\n<sequence>\n<body-base64url>
```

The bridge and watch reject an invalid MAC, wrong direction/session, or sequence number that is not greater than the last accepted value. Session IDs and counters reset only after a new mutually authenticated nonce exchange. Audio binary frames remain in the existing ordered utterance stream; they cannot directly cause execution because only an authenticated, server-stored proposal can be confirmed.

This provides origin authentication, integrity, and replay resistance using ESP32's existing mbedTLS primitives without a new dependency. It does not provide confidentiality and is not a replacement for WSS on an untrusted network. A bearer token inside plaintext JSON was rejected because passive capture would make it reusable; a custom encrypted channel was rejected because transport encryption should eventually be WSS rather than bespoke cryptography.

### 3. Keep orchestration in walkie-bridge and cmux ownership in cc-bridge

`MultiAgentRouter` in walkie-bridge owns transcript parsing, alias matching, proposal/task correlation, and the StopWatch-facing lifecycle. It talks to a narrow Unix-socket client rather than importing cc-bridge state or invoking cmux directly.

cc-bridge remains the single local owner of aggregated sessions, `RouteStager`, permission futures, and `CmuxClient`. Its socket gains bounded actions for a normalized snapshot, stage/confirm/cancel, permission resolution, and event polling. The socket MUST be owner-only and MUST NOT expose raw environment variables, credentials, full transcripts, or unbounded terminal output.

Directly importing `CmuxClient` into walkie-bridge was rejected because it would create two competing sources of session identity and permission state. Reusing BLE as the inter-daemon path was rejected because walkie-bridge is a local Mac peer, not another hardware owner.

### 4. Route by an opaque stable session key, never a displayed ordinal

cc-bridge returns a normalized session snapshot:

```text
session_key, agent, label, project_label, state,
capabilities{steer, permission_reply}, revision
```

`session_key` is an opaque Mac-local identifier mapped to the currently verified cmux surface UUID. The watch receives labels and agent type but never the cwd or surface UUID. On confirm, cc-bridge revalidates that the mapping still identifies the same live agent surface before sending text.

Codex's current cwd-based identity can collide when two panes share one directory, and Kimi's title/cwd fallbacks can become ambiguous. Such entries remain visible but `steer=false` until cc-bridge resolves one unique live surface. Guessing the first match was rejected because physical confirmation of the wrong target is still unsafe.

### 5. Require explicit deterministic targeting in the first slice

The router accepts configured project aliases, agent names, and unique live session labels. A transcript produces a proposal only when it resolves exactly one steerable live session. Missing or multiple matches produce a retryable ambiguity response listing a small bounded set of labels; they do not invoke an LLM to silently choose.

The proposal contains `command_id`, agent label, session label, project label, a bounded exact command preview, and an expiry timestamp. No cwd, secret, or hidden rewritten prompt is sent to the watch. Automatic LLM selection remains a future policy layer behind the same resolver contract.

This deterministic rule was chosen because the safety value of physical confirmation disappears if the displayed target or command differs from what will be sent.

### 6. Use a two-phase server-side proposal with idempotent confirmation

The bridge holds at most one pending proposal per authenticated watch. Staging does not call cc-bridge. KEYA sends an authenticated `command.decision=approve`; KEYB sends `reject`. The bridge atomically consumes a matching, unexpired `command_id`, then asks cc-bridge to stage and confirm the exact stored `session_key` and command text. Duplicate, late, mismatched, or replayed decisions are no-ops with a correlated error.

The proposal TTL is 60 seconds. Disconnect, reauthentication, target disappearance, or a newer proposal invalidates the older proposal. This extends the existing last-wins `RouteStager` concept with an explicit command identity so a confirmation can never fire whichever command happens to be pending.

### 7. Model a steered task as a correlated session observation

`task.accepted` means cmux successfully focused the verified surface, sent the exact text, and sent Enter. Subsequent `running`, `waiting_permission`, `completed`, or `failed` events are derived from the target session's existing hook state and are correlated to the one active watch task for that session.

For agents that provide only session-level state, completion means the observed session transitioned from post-dispatch activity back to idle. Its result is explicitly a bounded pane/status snapshot, not a guaranteed model response. Concurrent external input that makes attribution ambiguous terminates watch tracking with `task.failed(code=ambiguous_session_activity)` rather than inventing a result.

Appending hidden task markers to user prompts was rejected because it changes model input. Claiming exact completion from pane text alone was rejected because it is not evidence of causality.

### 8. Advertise permission capabilities per adapter

When cc-bridge has a live, correlated permission future for the tracked session, it emits a bounded permission event with request ID, agent, tool, and redacted hint. The watch enters `waiting_permission`; KEYA approves once and KEYB denies. cc-bridge applies first-response-wins semantics so an answer from another buddy or the terminal invalidates a later watch response.

Sessions without a verified permission reply adapter advertise `permission_reply=false`. The watch may notify the user that terminal approval is required but MUST NOT show actionable A/B approval controls. This avoids falsely promising uniform support for Codex and Kimi Code.

### 9. Extend the watch state machine without duplicating the renderer

The firmware adds `ProposalReview`, `Dispatching`, `Running`, `WaitingPermission`, `Completed`, `Cancelled`, and `Failed` to the existing audio states. The same `WalkieUi` renderer owns all states; new states reuse the established round safe area, key hints, dynamic text layer, and semantic colors rather than creating a second UI path.

KEYA starts PTT only in `Ready`, approves only in `ProposalReview` or actionable `WaitingPermission`, and begins a fresh utterance only after terminal acknowledgement states return to `Ready`. KEYB cancels recording, rejects a proposal/permission, or stops current TTS; after dispatch it does not claim to kill the agent task.

### 10. Bound data, logs, and reconnect behavior

The device stores one proposal, one tracked task, one permission request, and at most the existing 30-second PCM playback buffer. Display strings and event payloads are length-bounded before allocation. Logs contain IDs, agent type, state, byte counts, latency, and error codes, never full spoken commands, terminal output, cwd, shared secrets, or permission details.

After reconnect, the watch performs a fresh authentication and requests a snapshot for its last `task_id`. The bridge may restore observation of a still-live task or return its cached terminal event. It MUST NOT recreate a proposal, resend a command, or repeat a permission decision.

## Risks / Trade-offs

- **[The prototype channel is authenticated but not confidential]** → Keep it on a trusted LAN, prohibit port forwarding, redact commands from logs, and retain WSS as a production prerequisite.
- **[Session identity quality differs by agent]** → Expose `steer=false` for ambiguous mappings and fail closed instead of selecting the first pane.
- **[Session-level hooks cannot prove result causality]** → Label summaries as pane snapshots, allow only one tracked watch task per session, and fail ambiguous observations.
- **[Two physical devices can answer one permission]** → Reuse the single pending future and first-response-wins behavior; stale responses receive a non-actionable terminal event.
- **[A 60-second proposal can become stale]** → Bind every decision to command ID, authenticated session, sequence, target revision, and expiry.
- **[More UI states increase firmware memory and transition complexity]** → Reuse the existing renderer and add host-native state/protocol tests before new assets or hardware flashing.
- **[cc-bridge becomes a dependency of walkie-bridge]** → Keep the interface local, versioned, capability-driven, and fail as `control_plane_unavailable` while preserving audio-only use.

## Migration Plan

1. Extend cc-bridge/control-plane tests and local socket actions without changing existing BLE consumers.
2. Add router, authentication, and protocol-v2 bridge tests with fake control-plane and fake watch peers.
3. Add firmware protocol/state tests, then the new UI states and authenticated message handling.
4. Run both protocol-v1 audio regression and protocol-v2 control tests before flashing.
5. Enable control mode only when the ignored shared secret and cc-bridge capability handshake are both present; otherwise run audio-only.
6. Validate one throwaway cmux session per agent, then run the watch-confirmed route against non-destructive prompts.
7. Roll back by disabling control mode or reflashing the last audio-only firmware; cc-bridge's existing device/session behavior remains compatible.

## Open Questions

- New-session spawn needs a later change to define per-agent launch commands, login checks, permission modes, process ownership, and session-ID capture.
- Production deployment must choose WSS certificate provisioning or a private overlay network before leaving the trusted LAN.
- Exact Codex and Kimi permission adapters remain future work until their installed runtimes expose a verified bidirectional permission API.
