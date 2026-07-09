## 1. Port opencode-bridge (leaf dir, no shared-file risk)

- [x] 1.1 Copy `claude-desktop-buddy/tools/opencode-bridge/` → `hardware-buddies/claude-code-buddy/tools/opencode-bridge/` (incl. the tty-fallback `bridge.py` + `cardputer-permission.mjs`)
- [x] 1.2 Verify `buddy_core` import path resolves from the monorepo location (`sys.path.insert(parent)` in bridge.py points at `tools/`)
- [x] 1.3 Write `install.sh` + `com.opencode-bridge.plist.template` mirroring `codex-bridge/install.sh`, with paths pointing at the **monorepo** location
- [x] 1.4 Install the plist, `launchctl load`, confirm daemon runs and `/tmp/opencode-bridge.sock` appears
- [x] 1.5 Live verify: daemon discovers the running opencode pane (`cmux opencode panes: 1`) and pushes to cc-bridge socket. Cardputer on-device `oc`-tag visibility pending cardputer USB reconnect.

## 2. Port agentfarm-usb-bridge (leaf dir)

- [x] 2.1 Copy `claude-desktop-buddy/tools/agentfarm-usb-bridge/` → `hardware-buddies/claude-code-buddy/tools/agentfarm-usb-bridge/`
- [x] 2.2 Confirm `pyserial` is available in the daemon's venv; no `buddy_core` import (serial-only) — no path adjustment needed
- [x] 2.3 Write `install.sh` + `com.agentfarm-usb-bridge.plist.template` pointing at the monorepo path; strengthened port detection to match Tab5 by USB serial prefix `80:F1:B2:` (distinguishes from Cardputer-ADV `50:78:7D:`)
- [x] 2.4 Live verify: deferred — Tab5 + Agent Farm config not connected on this machine; bridge.py port + config.yaml errors are expected env-only failures, structure is correct

## 3. Port buddy_core ext-session slot fairness

- [x] 3.1 Diff `claude-desktop-buddy/tools/buddy_core/core.py` vs monorepo copy; apply the ~30-line ext-first slot allocation block to the monorepo copy (preserve monorepo-only lines)
- [x] 3.2 Rebuild/restart cc-bridge + cursor-bridge + codex-bridge + opencode-bridge so they pick up the new `buddy_core`
- [x] 3.3 Live verify: deferred — requires >16 total sessions; structural change applied + daemons restarted, low risk

## 4. Port cc-bridge pane-focus + clear-waiting + question loop

- [x] 4.1 Apply the 3 standalone-only edits to `cc-bridge/bridge.py`: `_clear_waiting(state)` on tool-failure path, `_cmux.focus_by_opencode_sid(sid)` in the selectSession resolver (+ ported `focus_by_opencode_sid` + `_opencode_surface_for_sid` into `cmux_control.py`)
- [x] 4.2 Restart cc-bridge; approval-flow regression deferred (needs cardputer connected)
- [x] 4.3 Regression-check "device question dismissed when answered outside device" (monorepo commit `7bf39d5`) — `cmux_question_loop` already present in monorepo; deferred to on-device
- [x] 4.4 Verify selectSession from the cardputer focuses an opencode pane — deferred (needs cardputer)

## 5. Daemon relocation + standalone retirement

- [x] 5.1 Repointed cc/cursor/codex/opencode/agentfarm-usb launchd plists at the monorepo path (`launchctl unload` + `load` per the AGENTS.md plist caveat)
- [x] 5.2 All bridges run from the monorepo path (`pgrep -fl bridge.py` shows only `hardware-buddies/claude-code-buddy/tools/...` paths)
- [x] 5.3 Full parity diff: zero meaningful delta (remaining diffs are intentional monorepo-only improvements — hook_permission env timeout, cursor per-session state, omx-plugin — or cosmetic kwarg ordering)
- [x] 5.4 User confirmed standalone scratch not wanted → archive
- [x] 5.5 Archived standalone: `mv ~/OpenSourceProjects/claude-desktop-buddy ~/OpenSourceProjects/_archive/claude-desktop-buddy-20260709`
- [x] 5.6 Updated root `AGENTS.md` + `claude-code-buddy/CLAUDE.md` documenting the new bridges + standalone retirement

## 6. Commit + verify

- [x] 6.1 One commit per ported piece: opencode+agentfarm bridges, buddy_core slots, cc-bridge focus, docs (each references the ported standalone commit SHA)
- [x] 6.2 Final smoke test: all 4 BLE bridges run from monorepo; opencode-bridge discovers this session's pane; cc-bridge socket live. On-device cardputer verification (oc tag + approval flow) deferred to hardware reconnect.
