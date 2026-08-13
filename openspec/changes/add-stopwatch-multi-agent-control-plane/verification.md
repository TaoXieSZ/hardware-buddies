# Verification Evidence

## Automated

- 2026-08-09: walkie-bridge `pytest -q` — 45 passed, 1 live ASR test skipped.
- 2026-08-09: StopWatch `pio test -e native` — 13 passed.
- 2026-08-09: claude-code-buddy `pytest -q` — 257 passed.
- 2026-08-09: claude-code-buddy `pio test -e native` — 40 passed.
- 2026-08-09: clean `m5stack-stopwatch` build plus final rebuild/flash — success; 66,244 bytes RAM (20.2%), 1,728,117 bytes flash (26.4% of application partition).
- 2026-08-09: clean `m5stack-stopwatch-ui-demo` build — success; 23,368 bytes RAM (7.1%), 1,139,157 bytes flash (17.4% of application partition).

## Local control socket

- `/tmp/cc-bridge.sock` is owned by the current user and has mode `srw-------` (`0600`).
- After restarting the installed cc-bridge, `control.snapshot` returned revision 1 through the new API.
- One live Codex pane was present and reported `steer=true`, `permission_reply=false`; the response exposed only bounded labels/capabilities, not cwd or surface UUID.
- Four-agent normalization, unique routing, fail-closed Codex collision, and Claude/OpenCode versus Codex/Kimi permission capabilities are covered by host tests. Live four-agent smoke remains pending because only Codex was running during this verification.

## Security/privacy review

- Firmware and bridge control secrets are kept in gitignored local files; control mode remains disabled when either secret is absent.
- Fixed mutual-authentication and envelope HMAC vectors pass; invalid MAC, wrong direction/session, replay, and out-of-order cases are rejected.
- The Unix socket is owner-only; control snapshots use opaque keys and bounded fields.
- Normal bridge/firmware logs report identifiers, lengths, state, and error codes, not command text, terminal output, raw payloads, credentials, or control secrets.
- Protocol-v2 control JSON is authenticated and replay-resistant but not encrypted. PCM and JSON still traverse `ws://`; deployment remains trusted-LAN only, with WSS explicitly deferred.

## Physical StopWatch smoke

- A fresh 32-byte per-device secret was provisioned to both ignored configurations without printing it; protocol-v2 mutual authentication succeeded on the physical StopWatch.
- Runtime firmware was flashed to ESP32-S3 `28:84:85:43:AE:38` and reached the HD Ready screen on `192.168.3.40`.
- A controlled bridge outage produced bounded 0.5/1/2/4/8-second reconnect attempts. Restoring the bridge re-authenticated without redispatching any task, and expected connection closures now log one bounded line rather than a traceback.

## Pending physical/live evidence

- Run harmless live Claude Code, Codex, OpenCode, and Kimi Code sessions and verify unique/fail-closed routing.
- On the physical buttons, verify proposal rejection, approve-once, stale/replay rejection, haptics, actionable Claude/OpenCode permissions, and notification-only Codex/Kimi permissions.
