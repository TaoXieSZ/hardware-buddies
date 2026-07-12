## 1. Reconcile and lock the proven baseline

- [x] 1.1 Review the current ESP-Claw landscape diff against `tab5-esp-claw-landscape` and record any requirement not yet covered by code or evidence
- [x] 1.2 Add host-testable cases for 90° rectangle mapping, logical clipping, source stride, physical bounds, and in-place framebuffer rejection
- [x] 1.3 Build the full `m5stack_tab5_st7121` ESP-Claw image with ESP-IDF v5.5.4 and retain the successful size/build evidence
- [x] 1.4 Reflash only after confirming MAC `80:f1:b2:d1:51:7d`, then capture boot evidence for logical 1280×720 and physical 720×1280
- [x] 1.5 Run the physical four-corner, center, edge, drag-direction, partial-refresh, animation, and display-owner-switch acceptance checks with USB on the left
- [x] 1.6 Keep ST7121 board support and landscape behavior as separate reviewable commits so the upstream board-support PR is not coupled to the local orientation policy
- [x] 1.7 Set the ST7121 default backlight to 40 percent and verify the USB-C-area idle temperature is materially lower than the 100-percent baseline

## 2. Complete and land the scoped Agent Farm surface

- [x] 2.1 Review the local bare-Dispatcher gateway, fixed-definition chat, in-process sanitizer, and checks against every `agentfarm-device-gateway` scenario
- [x] 2.2 Add subprocess coverage proving SIGTERM closes the listener and active SSE request without PM2 forced termination
- [x] 2.3 Ensure the standard Tab5 check command runs scoped-route integration, OpenAI contract, sanitizer, cross-chunk SSE, response cleanup, and device-forwarding tests
- [x] 2.4 Run TypeScript build plus every dispatch check suite using the same Node ABI as the local runtime
- [x] 2.5 Verify the full local dispatch stays stopped while the dedicated gateway starts no Feishu, cron, sweep, or warm-pool work
- [x] 2.6 Verify local event-only mode is loopback-only, has chat disabled, forwards heartbeat, and creates zero Agent Farm dispatches
- [x] 2.7 Commit the local bare-Dispatcher gateway separately from device firmware and preserve deployment/rollback notes in the runbook

## 3. Add cooperative Lua HTTP support

- [x] 3.1 Extend `lua_module_http_server` request objects with the `Authorization` header without exposing or logging unrelated headers
- [x] 3.2 Add bounded `app:poll(timeout_ms)` support that processes at most one queued HTTP callback and returns control to the Lua application
- [x] 3.3 Keep `serve_forever()` backward compatible and implement it in terms of the same request-dispatch primitive
- [x] 3.4 Add Lua-module tests for GET/POST callbacks, native NVS bearer gating before body reads, timeout/no-request behavior, callback errors, stop requests, and application cleanup
- [x] 3.5 Verify a loop alternating `app:poll()` and `lvgl.process_events()` remains responsive under simultaneous HTTP and touch activity

## 4. Add device credential and event ingress

- [x] 4.1 Add an NVS-backed `agentfarm_device_token` setting with masked Web configuration and no value in logs, FATFS, Skill source, or UI
- [x] 4.2 Package an `agent_farm` ESP-Claw Skill and long-running Lua application under the storage image
- [x] 4.3 Register a bounded `/api/lua/agentfarm/event` POST route protected by `app:require_bearer_setting("af_dev_token")` before reading or decoding event JSON
- [x] 4.4 Validate event version, type, phase, definition, safe string lengths, numeric ranges, and body size before changing terminal state
- [x] 4.5 Implement event identity deduplication and bounded recent-task history that preserves the active task
- [x] 4.6 Add tests proving unauthorized, malformed, unsupported, duplicate, and oversized events leave state unchanged
- [x] 4.7 Start the persistent display-exclusive Agent Farm terminal automatically on `startup/boot_completed`

## 5. Build the landscape Agent Farm terminal UI

- [x] 5.1 Implement the 1280×720 two-column shell with lobster/status on the left, active/recent work on the right, and a bottom action region
- [x] 5.2 Map running, progress, success, error, idle, and disconnected states to distinct pet reactions without displaying raw errors
- [x] 5.3 Update repeated progress events in place and play completion reactions only for new live events
- [x] 5.4 Implement gateway heartbeat/staleness tracking independently from active task state and retain history across reconnects
- [x] 5.5 Add local-only touch navigation/status actions that never call chat or dispatch
- [x] 5.6 Keep the initial release free of dispatching touch shortcuts; require confirmation and a fixed prompt if one is added later
- [x] 5.7 Ensure the UI exposes no cancel/stop/abort control while Agent Farm lacks a true run-cancel contract
- [x] 5.8 Verify touch hitboxes, bounded recent rows, typography, pet reactions, dirty-region refresh, and disconnected behavior on the physical Tab5

## 6. Enable sanitized event forwarding

- [x] 6.1 Configure a distinct device-event bearer on the Primary Mac and in Tab5 NVS without printing either value
- [x] 6.2 Switch the local gateway from off to forward while keeping chat disabled and LAN chat ingress closed
- [x] 6.3 Prove the local gateway reconstructs events from an allowlist before posting to the device
- [x] 6.4 Emit a safe allowlisted lifecycle fixture and verify it reaches the device UI without an ESP-Claw LLM call or Agent Farm dispatch
- [x] 6.5 Verify device timeout/rejection produces only a redacted warning and later heartbeat delivery recovers

## 7. Enable fixed-definition chat

- [ ] 7.1 Reserve stable home-LAN addresses for the Primary Mac and Tab5 and document the chosen gateway Base URL
- [x] 7.2 Configure ESP-Claw's OpenAI-compatible provider to use the gateway with the device-facing token
- [x] 7.3 Explicitly enable LAN binding and chat only after bearer, prompt-size, concurrency, generic-error, and fixed-definition checks pass
- [x] 7.4 Confirm request-supplied model, definition, dynamic flag, or session selector cannot escape `tab5-operator`
- [x] 7.5 Perform one owner-authorized real Web Chat prompt and capture the device request, fixed local gateway request, Agent Farm trace, response, and usage attribution
- [x] 7.6 Verify `tab5-operator` remains `mode: ask`, uses independent memory, has no pool/trigger, and has no MCP servers after first creation

## 8. Rollback, persistence, and handoff

- [x] 8.1 Stop the standalone gateway and verify Feishu, existing definitions, and agent-host remain healthy while Tab5 enters a bounded disconnected state
- [x] 8.2 Verify stopping/restarting the local gateway requires no Mac2 action and does not delete device history
- [x] 8.3 Verify the portrait ESP-Claw image at commit `c182a77` remains flashable as the device rollback
- [x] 8.4 Persist the gateway process only after end-to-end acceptance and document how to disable it without changing other PM2 services
- [x] 8.5 Update the Tab5 ESP-Claw runbook with environment-variable names, current addresses, build/flash steps, validation evidence, thermal fix, and rollback paths
- [x] 8.6 Split commits/PRs by landscape foundation, Agent Farm scoped gateway, Lua HTTP runtime, terminal UI, and operational documentation; do not push without explicit review
