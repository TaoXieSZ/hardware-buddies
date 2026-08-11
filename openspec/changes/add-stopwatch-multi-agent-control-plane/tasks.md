## 1. Protocol-v2 Contracts and Regression Baseline

- [x] 1.1 Add shared protocol-v2 constants, field-length limits, handshake/envelope examples, and fixed HMAC-SHA-256 test vectors without changing protocol-v1 behavior.
- [x] 1.2 Add failing Python tests for valid mutual authentication, invalid proofs, wrong direction/session, replayed or out-of-order envelopes, and authenticated body decoding.
- [x] 1.3 Add failing native firmware tests for the same authentication vectors, sequence handling, and protocol-v1 audio-only fallback.
- [x] 1.4 Lock the current protocol-v1 transcription and echo-TTS behavior with regression tests before enabling any control-plane path.

## 2. cc-bridge Local Control Interface

- [x] 2.1 Add failing cc-bridge/control-plane tests for normalized four-agent session snapshots, opaque session keys, capability flags, bounded fields, monotonic revisions, and ambiguous Codex/Kimi mappings that fail closed.
- [x] 2.2 Extend `CmuxClient` with exact live-surface validation and argument-array route-by-surface behavior while preserving existing nickname, focus, question, and gesture routes.
- [x] 2.3 Extend `RouteStager` with command identifiers, target revisions, TTL validation, and idempotent matching confirm/cancel operations while keeping existing callers compatible.
- [x] 2.4 Add owner-only Unix-socket actions for control snapshot, command-identified stage/confirm/cancel, and bounded cursor-based event polling.
- [x] 2.5 Add a bounded event ring for session state, supported permission requests/resolutions, and terminal status, including explicit cursor-gap behavior.
- [x] 2.6 Add permission resolution through the existing pending future with first-response-wins semantics and capability flags that leave unsupported Codex/Kimi approvals non-actionable.
- [x] 2.7 Run cc-bridge, buddy-core, control-plane, Codex bridge, and OpenCode bridge regression tests to prove existing BLE/session consumers remain compatible.

## 3. Walkie Bridge Authentication and Multi-Agent Router

- [x] 3.1 Add ignored/config-example fields for a 32-byte per-device control secret, project/session aliases, proposal TTL, cc-bridge socket path, and control-mode enablement without logging configured values.
- [x] 3.2 Implement the protocol-v2 mutual challenge/response and authenticated envelope codec in Python until the authentication/replay tests pass.
- [x] 3.3 Add failing router tests for explicit agent/project/session aliases, unique-target success, missing/ambiguous/stale targets, unavailable agent discovery, `spawn_not_supported`, and exact text preservation with shell metacharacters.
- [x] 3.4 Implement a bounded cc-bridge control client and deterministic `MultiAgentRouter` that returns proposals only for one uniquely steerable live session.
- [x] 3.5 Implement one-pending-proposal storage per authenticated watch with command ID, target revision, exact text, 60-second expiry, atomic consume, and disconnect invalidation.
- [x] 3.6 Implement authenticated proposal decisions so approve performs command-identified stage/confirm, reject performs no route, and duplicate/stale/mismatched decisions remain side-effect free.
- [x] 3.7 Implement correlated task observation, bounded terminal-event caching, reconnect snapshot, cursor-gap recovery, and explicit ambiguous-session failure without resending a command.
- [x] 3.8 Add log-redaction tests and enforce bounded identifiers/metrics only for authentication, proposal, routing, permission, and task events.

## 4. StopWatch Protocol, State Machine, and UI

- [x] 4.1 Extend failing native state-machine tests for proposal review, dispatching, running, actionable/non-actionable permission, completed, cancelled, failed, reconnect, and state-specific KEYA/KEYB behavior.
- [x] 4.2 Implement the protocol-v2 nonce handshake, HMAC-SHA-256 verification/signing, envelope sequence checks, and ignored local secret configuration using the existing ESP32 mbedTLS runtime.
- [x] 4.3 Extend the firmware protocol parser and serializers for command proposal/decision, task lifecycle, permission request/decision/resolution, bounded text fields, and task snapshot messages.
- [x] 4.4 Implement the expanded `AudioLoop`/control state machine so KEYA records only in ready, confirms only the displayed actionable item, and KEYB never claims to cancel an already submitted agent task.
- [x] 4.5 Extend the existing `WalkieUi` renderer and offline showcase with proposal, dispatching, running, permission, completed, cancelled, and failed presentations without adding a parallel renderer.
- [x] 4.6 Integrate proposal/permission vibration patterns, result-summary display and TTS, KEYB playback cancellation, and reliable Mic/Speaker recovery while keeping feedback side-effect free.
- [x] 4.7 Verify device allocations remain bounded to one proposal, one task, one permission request, and the existing 30-second TTS buffer; add overflow/error tests where host testing is practical.

## 5. End-to-End Control-Plane Integration

- [x] 5.1 Add fake-watch/fake-control-plane integration tests for transcript-to-proposal, reject, approve-once, target disappearance, replay, permission first-response-wins, unsupported permission notification, task completion, and reconnect without redispatch.
- [x] 5.2 Run all StopWatch native tests, walkie-bridge tests/static checks, and affected claude-code-buddy Python/JavaScript tests, fixing only regressions introduced by this change.
- [x] 5.3 Run clean `m5stack-stopwatch` and `m5stack-stopwatch-ui-demo` builds with the pinned official toolchain and inspect firmware/PSRAM size headroom.
- [ ] 5.4 Exercise the local cc-bridge API against throwaway or harmless live Claude Code, Codex, OpenCode, and Kimi Code cmux sessions, confirming unique routing and fail-closed ambiguity without exposing commands in logs.
- [ ] 5.5 Flash the runtime firmware and verify on the physical StopWatch that rejection sends nothing, one approval sends exactly once, stale/replayed approval sends nothing, and disconnect/reconnect never redispatches.
- [ ] 5.6 Verify actionable permission approval for currently supported adapters and notification-only behavior for unsupported adapters without changing existing buddy responses.
- [x] 5.7 Perform a security and privacy review of socket permissions, ignored secrets, HMAC vectors, replay tests, field bounds, log redaction, and trusted-LAN documentation before enabling control mode by default.

## 6. Documentation and Architecture Reconciliation

- [x] 6.1 Document protocol-v2 configuration, secret generation/provisioning, explicit targeting grammar, supported-agent capability matrix, audio-only fallback, startup order, and rollback in `stopwatch-walkie/README.md`.
- [x] 6.2 Update the StopWatch product design and interactive architecture map so NOW/NEXT labels match the implemented steer-only scope and continue to identify spawn, WSS, offline ASR, mDNS, and launchd as future work.
- [ ] 6.3 Record real-device and four-agent smoke evidence in this change, then run `openspec validate add-stopwatch-multi-agent-control-plane` with no errors before requesting review.
