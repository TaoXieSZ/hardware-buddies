# StopWatch Bridge Dashboard Evidence

## Automated verification

- walkie-bridge: `61 passed, 1 skipped` (the opt-in live DashScope test was skipped).
- cc-bridge affected suite: `257 passed`.
- native firmware state/protocol suite: `13 passed`.
- Python static compilation, JavaScript syntax check, and `git diff --check`: passed.
- Router regression coverage confirms `target_required`, `target_not_found`,
  `target_ambiguous`, `spawn_not_supported`, and
  `control_plane_unavailable` surface an actionable dashboard state with zero
  command dispatch.

## Physical-device evidence

On 2026-08-09 the real StopWatch connected to the restarted bridge, visibly
passed protocol-v2 authentication, disconnected during a bridge restart, and
then reconnected and reauthenticated without restoring an old proposal. The
bounded status and event response is recorded in
[data/live-ready-status.json](./data/live-ready-status.json).

The earlier physical utterance in this session produced 154240 PCM bytes and a
successful DashScope ASR result in 1138 ms, but no proposal. The new regression
reproduces this exact stop point as `ASR complete -> target_required` when the
recognized text lacks an explicit Agent/alias prefix. Full physical proposal,
reject, approve-once, permission, and terminal-state acceptance remains a
hands-on follow-up because those actions require the watch buttons and a live
target task.

## Browser evidence

- [Wide target-required diagnosis](./assets/dashboard-wide-target-required.png)
- [Narrow permission state](./assets/dashboard-narrow-permission.png)
- [Live physical-watch ready state](./assets/dashboard-live-ready.png)

The narrow review found the five-step pipeline clipped at the right edge. It
was changed to a vertical timeline below the responsive breakpoint, then
rechecked with zero document-level horizontal overflow. The live review also
found repeated control snapshots filling the event tape and stale browser
events surviving a daemon restart; snapshot publication is now change-only and
the client clears history when the server cursor rolls back.
