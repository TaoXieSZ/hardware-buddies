## Context

`hardware-buddies/claude-code-buddy/` is a `git subtree` snapshot (import commit `971335e`, 2026-06-17) of upstream `TaoXieSZ/claude-code-buddy`. A separate standalone clone at `~/OpenSourceProjects/claude-desktop-buddy/` tracks the **same** upstream but kept evolving. Both trees now carry different commits, so neither is a strict superset.

A concrete failure mode already happened: the tty-fallback opencode discovery fix (this session) landed only in the standalone and was invisible to the monorepo — the cardputer showed no `oc` session until the standalone bridge ran. Two trees = silent drift.

Diffing the two `tools/` trees (standalone → monorepo, `diff` exit 0 on shared files) gives the exact port surface:

| Delta | Standalone-only (port IN) | Monorepo-only (keep) |
|---|---|---|
| `opencode-bridge/` | whole dir (session discovery + permission gating + tty fallback) | — |
| `agentfarm-usb-bridge/` | whole dir (USB-CDC Agent Farm → Tab5) | — |
| `cc-bridge/bridge.py` | `_clear_waiting` on tool-fail; `_cmux.focus_by_opencode_sid(sid)` pane focus; `cmux_question_loop` extra task | — |
| `buddy_core/core.py` | ext-sessions **slot allocation**: ext-first so local Claude can't crowd out cursor/codex/opencode rows (~30 lines) | — |
| `codex-bridge/` | — | `omx-plugin/` (Codex auto-drive) |

**Key simplification:** the two `codex-bridge/bridge.py` are byte-identical (`diff` exit 0). There is no codex-bridge reconciliation — the earlier "two implementations" worry was a misread of divergent commit messages on the same file. Only `omx-plugin/` differs, and the monorepo already has it.

## Goals / Non-Goals

**Goals:**
- One working tree: `hardware-buddies/` is the only place `claude-code-buddy` tools are edited.
- The cardputer/tab5 sees `oc` (opencode) sessions when the opencode-bridge daemon runs from the monorepo path — parity with the standalone.
- `agentfarm-usb-bridge` runs from the monorepo path and feeds the Tab5.
- cc-bridge pane-focus + ext-session slot fairness land in the monorepo copy.
- Standalone clone retired: its launchd plists unloaded; dir archived (not deleted — preserves any uncommitted scratch until confirmed empty).

**Non-Goals:**
- Re-importing the subtree or rebasing onto upstream. The subtree stays a manual snapshot; this change ports specific features in as hand-authored commits.
- Merging the standalone's `src/` firmware, `desktop-app/`, `docs/`, `mac-helper/`. The monorepo's `claude-code-buddy/` already carries its own firmware/src — the standalone's firmware is a separate, older line and is NOT ported (it would conflict with the subtree's firmware). Only `tools/` deltas are ported.
- Unifying `openspec/` histories. The standalone's openspec changes stay archived in the standalone dump; only the live monorepo openspec matters going forward.
- Touching `ahakey/`, `cardputer-adv-buddy/`, `tab5-agentfarm-buddy/`, `m5-paper-buddy/` — out of scope.

## Decisions

**D1 — Port by hand-authored commits, not `git subtree pull`.**
The histories diverged; a `subtree pull` would replay the standalone's entire post-import history (incl. its `src/`, `desktop-app/` changes that would collide with the subtree's own evolution) and produce messy conflicts. Instead, port only the `tools/` deltas listed above as 4 focused commits, each mirroring the standalone's commit intent. Slower but conflict-free and reviewable.

**D2 — Port order: leaf dirs first, shared-file edits last.**
1. `opencode-bridge/` (new dir, no shared-file risk) → install + verify `oc` appears.
2. `agentfarm-usb-bridge/` (new dir) → install + verify Tab5 feed.
3. `buddy_core/core.py` ext-session slot fairness → rebuild daemons.
4. `cc-bridge/bridge.py` pane-focus + `_clear_waiting` + `cmux_question_loop` → verify Cursor/Codex/OpenCode focus + question dismissal.
Each step is independently verifiable and revertable.

**D3 — codex-bridge: no action.** Files are identical; the monorepo already has `omx-plugin/` the standalone lacks. Leave untouched.

**D4 — Daemon relocation: repoint launchd plists, don't copy the standalone's.**
Write fresh `com.opencode-bridge.plist` / `com.agentfarm-usb-bridge.plist` in `claude-code-buddy/tools/<bridge>/` (matching the cc-bridge/codex-bridge install.sh pattern) that point at the **monorepo** path, then `launchctl unload` the standalone's plists and `load` the new ones. The standalone's install scripts are not run from the monorepo — they hardcode the standalone path.

**D5 — Retirement is gated on parity, not automatic.**
Retire the standalone only after: (a) `diff` of ported files shows zero meaningful delta, (b) all 4 daemons (cc/codex/cursor/opencode + agentfarm-usb) run from the monorepo path and the cardputer shows `oc`, (c) Tab5 receives Agent Farm triggers. Archive to `~/OpenSourceProjects/_archive/claude-desktop-buddy-<date>/`, not `rm`.

## Risks / Trade-offs

- **buddy_core ext-session slot change is load-bearing for all agents.** The ext-first allocation changes which sessions appear when ≥16 total exist. Low risk in practice (rarely hit 16) but touches every buddy's payload — verify with a multi-agent live session before retiring standalone.
- **cc-bridge `cmux_question_loop` + `_clear_waiting`** change question/permission panel timing. Must regression-check the cardputer approval flow (ask/once/always/deny) and the "device question dismissed when answered outside device" behavior the monorepo already has (commit `7bf39d5`).
- **launchd plist path drift** — if the monorepo path changes (it won't, but) daemons break silently. Acceptable; documented in install.sh.
- **Standalone has uncommitted scratch** (`.claude/worktrees/`, `docs/pitch.md`, etc.). Before archiving, confirm with the user that nothing there is wanted; archive preserves it regardless.
- **Trade-off**: hand-porting loses the standalone's granular commit history for these features. Acceptable — the monorepo commit messages will reference the standalone commit SHAs being ported.
