## Why

The same upstream (`TaoXieSZ/claude-code-buddy`) lives in two places: a standalone clone at `~/OpenSourceProjects/claude-desktop-buddy/` and a `git subtree` snapshot at `hardware-buddies/claude-code-buddy/`. They have **diverged** — each accumulated independent commits (standalone got `opencode-bridge`, `agentfarm-usb-bridge`, pane-focus, a second `codex-bridge`; the monorepo subtree got cursor permission relay and cmux `agent.kind` codex detection). Maintaining both means work silently drifts (the tty-based opencode discovery fix just landed only in the standalone and was invisible to the monorepo). One source of truth is needed.

## What Changes

- Port the standalone-only features into the monorepo's `claude-code-buddy/` subtree as new commits:
  - `opencode-bridge/` (OpenCode session discovery + permission gating, incl. the tty fallback for manually-launched opencode panes)
  - `agentfarm-usb-bridge/` (USB-serial Agent Farm bridge)
  - pane-focus for OpenCode/Codex in `cc-bridge` (commit `38fc2a8`)
- Reconcile the two `codex-bridge` implementations: keep the monorepo version (cmux `agent.kind` detection, already wired) as the base and fold in any standalone-only improvements; do not carry two copies.
- Update monorepo install docs / launchd plints so `opencode-bridge` and `agentfarm-usb-bridge` are first-class daemons in `hardware-buddies/claude-code-buddy/tools/`.
- **Retire the standalone clone**: after parity is verified (diff of ported files, daemons running from the monorepo path, cardputer shows `oc` sessions), archive `~/OpenSourceProjects/claude-desktop-buddy/`. The monorepo becomes the only working tree.
- **BREAKING** (for the standalone workflow only): the standalone clone is no longer the dev surface — its launchd plists point at the standalone path and must be repointed at the monorepo path.

## Capabilities

### New Capabilities
- `opencode-bridge`: OpenCode CLI session discovery (cmux agent pane + tty fallback for manual launches) and permission-gating relay to `cc-bridge`, so cardputer/tab5 show `oc`-tagged sessions alongside Claude/Cursor/Codex.
- `agentfarm-usb-bridge`: USB-CDC serial bridge streaming Agent Farm state to a buddy device (no BLE/radio), for the P4 desk-pet.

### Modified Capabilities
- `cc-bridge`: add pane-focus routing for OpenCode and Codex sessions (selectSession resolves to the right cmux surface), so the cardputer session switcher can target non-Claude agents.

## Impact

- **Code**: new dirs `claude-code-buddy/tools/opencode-bridge/`, `claude-code-buddy/tools/agentfarm-usb-bridge/`; edits to `claude-code-buddy/tools/cc-bridge/bridge.py` (pane focus) and `buddy_core/core.py` (no behavior change expected — ext_sessions already supports arbitrary agent names). Possible edits to `codex-bridge/bridge.py` if standalone improvements are folded in.
- **Docs**: `claude-code-buddy/CLAUDE.md`, `README.md`, install scripts must mention opencode-bridge/agentfarm-usb-bridge; root `AGENTS.md` repo-structure table gains the two bridges.
- **Ops**: 2 new launchd plists (`com.opencode-bridge.plist`, `com.agentfarm-usb-bridge.plist`) repointed at the monorepo path; standalone plists unloaded.
- **Risk**: codex-bridge reconciliation is the risky part — two implementations diverged on session identity (cwd-join vs cmux agent.kind). Must not regress the working monorepo codex feed.
- **Non-goal**: rebasing the subtree onto upstream or changing the `git subtree` sync model. The subtree stays a snapshot; this change ports specific features in, it does not re-import.
