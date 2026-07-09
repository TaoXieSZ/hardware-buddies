## 1. Port opencode-bridge (leaf dir, no shared-file risk)

- [ ] 1.1 Copy `claude-desktop-buddy/tools/opencode-bridge/` → `hardware-buddies/claude-code-buddy/tools/opencode-bridge/` (incl. the tty-fallback `bridge.py` + `cardputer-permission.mjs`)
- [ ] 1.2 Verify `buddy_core` import path resolves from the monorepo location (`sys.path.insert(parent)` in bridge.py points at `tools/`)
- [ ] 1.3 Write `install.sh` + `com.opencode-bridge.plist.template` mirroring `codex-bridge/install.sh`, with paths pointing at the **monorepo** location
- [ ] 1.4 Install the plist, `launchctl load`, confirm daemon runs and `/tmp/opencode-bridge.sock` appears
- [ ] 1.5 Live verify: cardputer tab list shows the `oc`-tagged session for the running opencode pane (check cc-bridge log for `EXT RECV agent=opencode n=>0`)

## 2. Port agentfarm-usb-bridge (leaf dir)

- [ ] 2.1 Copy `claude-desktop-buddy/tools/agentfarm-usb-bridge/` → `hardware-buddies/claude-code-buddy/tools/agentfarm-usb-bridge/`
- [ ] 2.2 Confirm `pyserial` is available in the daemon's venv; adjust import path for `buddy_core` if it imports it (it likely does not — it's serial-only)
- [ ] 2.3 Write `install.sh` + `com.agentfarm-usb-bridge.plist.template` pointing at the monorepo path
- [ ] 2.4 Live verify: with the Tab5 connected, run the bridge against the Agent Farm admin API and confirm the device receives a `{"new":true}` firing line + heartbeats

## 3. Port buddy_core ext-session slot fairness

- [ ] 3.1 Diff `claude-desktop-buddy/tools/buddy_core/core.py` vs monorepo copy; apply the ~30-line ext-first slot allocation block to the monorepo copy (preserve monorepo-only lines)
- [ ] 3.2 Rebuild/restart cc-bridge + cursor-bridge + codex-bridge + opencode-bridge so they pick up the new `buddy_core`
- [ ] 3.3 Live verify: with >16 total sessions (force the condition with multiple Claude panes + opencode), confirm opencode rows are NOT starved out (ext-first)

## 4. Port cc-bridge pane-focus + clear-waiting + question loop

- [ ] 4.1 Apply the 3 standalone-only edits to `cc-bridge/bridge.py`: `_clear_waiting(state)` on tool-failure path, `_cmux.focus_by_opencode_sid(sid)` in the selectSession resolver, `cmux_question_loop` in extra_tasks
- [ ] 4.2 Restart cc-bridge; regression-check the cardputer approval flow: trigger a permission prompt, approve once / always / deny / timeout — each behaves as before
- [ ] 4.3 Regression-check "device question dismissed when answered outside device" (monorepo commit `7bf39d5`) still works with the new `cmux_question_loop`
- [ ] 4.4 Verify selectSession from the cardputer now focuses an opencode pane (the `oc-` synthetic sid resolves via `focus_by_opencode_sid`)

## 5. Daemon relocation + standalone retirement

- [ ] 5.1 Unload the standalone's launchd plists (`com.opencode-bridge`, `com.agentfarm-usb-bridge`, and any cc/codex/cursor plists pointing at the standalone path) with `launchctl unload`
- [ ] 5.2 Confirm all bridges now run from the monorepo path (`pgrep -fl bridge` shows monorepo paths only)
- [ ] 5.3 Full parity diff: `diff -r` the standalone `tools/{opencode-bridge,agentfarm-usb-bridge,cc-bridge,buddy_core}` vs monorepo copies — confirm zero meaningful delta (ignoring the monorepo-only `omx-plugin/`)
- [ ] 5.4 Confirm with the user that the standalone's uncommitted scratch (`.claude/worktrees/`, `docs/pitch.md`, etc.) is not wanted
- [ ] 5.5 Archive the standalone: `mv ~/OpenSourceProjects/claude-desktop-buddy ~/OpenSourceProjects/_archive/claude-desktop-buddy-$(date +%Y%m%d)`
- [ ] 5.6 Update root `AGENTS.md` repo-structure note + `claude-code-buddy/CLAUDE.md`/`README.md` to document opencode-bridge and agentfarm-usb-bridge as monorepo-resident daemons

## 6. Commit + verify

- [ ] 6.1 One commit per ported piece (opencode-bridge, agentfarm-usb-bridge, buddy_core slots, cc-bridge focus), each referencing the standalone commit SHA being ported
- [ ] 6.2 Final smoke test: cardputer shows `cc` + `oc` sessions; Tab5 gets Agent Farm triggers; approval flow works end-to-end — all from the monorepo as the single source of truth
