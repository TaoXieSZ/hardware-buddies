## Why

The StopWatch prototype now proves the bidirectional voice path, but recognized speech only echoes back to the user and cannot safely control the Mac's coding-agent sessions. The repository already exposes Claude Code, Codex, OpenCode, and Kimi Code session state plus a staged cmux routing primitive, so the next smallest product slice is a watch-confirmed, agent-neutral control plane for steering an existing local session.

## What Changes

- Extend the StopWatch-to-Mac protocol beyond transcription with correlated command proposals, approve/reject decisions, accepted/running/permission/completed/failed task events, and reconnect-safe terminal results.
- Add an authenticated, replay-resistant control session before any message can cause a local terminal action; keep the existing trusted-LAN deployment boundary and explicitly retain plaintext-audio confidentiality as a prototype limitation.
- Add a Mac-side Multi-Agent Router that reads a normalized live-session snapshot, resolves an explicitly addressed project/agent/session, stages the exact proposed command, and dispatches only after physical confirmation on the StopWatch.
- Reuse the existing cc-bridge control plane and cmux `surface.send_text` path to steer live Claude Code, Codex, OpenCode, and Kimi Code sessions through one adapter contract.
- Add StopWatch proposal-review, dispatching, running, waiting-permission, completed, cancelled, and failed states with state-specific KEYA/KEYB behavior, tactile cues, and bounded result summaries.
- Forward supported agent permission requests and structured progress/results to the watch without exposing credentials or full terminal history.
- Keep new-session spawning, automatic LLM selection without an explicit target, yolo/bypass-permission modes, offline ASR, mDNS, production launchd installation, and internet exposure out of this change.

## Capabilities

### New Capabilities

- `stopwatch-command-control`: define authenticated command proposal, physical confirmation, cancellation, correlation, replay protection, and on-device control states.
- `multi-agent-session-routing`: define normalized discovery, deterministic target resolution, staged steer dispatch, and adapter behavior for live Claude Code, Codex, OpenCode, and Kimi Code sessions.
- `stopwatch-agent-feedback`: define task lifecycle, permission request, progress, result-summary, vibration, and reconnect behavior returned to the StopWatch.

### Modified Capabilities

- `cc-bridge`: expose the existing live multi-agent session and staged cmux route capabilities through a bounded local interface consumable by walkie-bridge, while preserving cc-bridge as the sole owner of its device and agent session aggregation.

## Impact

- **StopWatch firmware**: `stopwatch-walkie/include/`, `stopwatch-walkie/src/`, native tests, UI assets/state renderer, and the WebSocket protocol contract.
- **Walkie bridge**: `stopwatch-walkie/tools/walkie-bridge/` gains authentication, proposal/task orchestration, a router interface, cc-bridge/control-plane integration, and tests.
- **Existing control plane**: `claude-code-buddy/tools/control_plane/`, `tools/buddy_core/`, and `tools/cc-bridge/` gain only the local query/event seams required by walkie-bridge; existing BLE/session behavior remains compatible.
- **Agent bridges**: existing Claude Code, Codex, OpenCode, and Kimi Code discovery/focus behavior is reused; no agent CLI dependency or launch command is added in this slice.
- **Security**: a per-device local secret is added to ignored configuration; DashScope and agent credentials remain Mac-only and are never sent to the watch or logged.
