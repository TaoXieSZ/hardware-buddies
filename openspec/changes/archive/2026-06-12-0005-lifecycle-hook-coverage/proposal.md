# 0005 — lifecycle hook coverage (compact, subagents, tool failures)

## Why

The buddy displays (stick HUD, Tab5 dashboard) go silent during several real
Claude Code lifecycle moments because the events were never registered and/or
never mapped:

1. **Compaction is invisible.** A `/compact` (or auto-compact) can take tens of
   seconds; the dashboard keeps showing the stale previous state. `apply_event`
   already had a `PostCompact` branch, but neither `PreCompact` nor `PostCompact`
   was wired into `~/.claude/settings.json` by `install.sh` — the daemon never
   received them. (Observed live: user ran `/compact`, Tab5 showed nothing.)
2. **Subagent fan-out is invisible.** `SubagentStart` / `SubagentStop` aren't
   registered or mapped, so multi-agent turns look like one long opaque tool call.
3. **Tool failures look like successes.** `PostToolUseFailure` isn't registered or
   mapped; the transcript shows the `PreToolUse` line and then moves on.

## What changes

- `install.sh` HOOK_EVENTS_ASYNC gains `PreCompact`, `PostCompact`,
  `SubagentStart`, `SubagentStop`, `PostToolUseFailure` (and the live
  `~/.claude/settings.json` is updated in place the same way).
- `apply_event` (cc-bridge) maps:
  - `PreCompact` → `msg = "compacting…"`, transcript entry
  - `PostCompact` → `msg = "compacted"` (entry already existed)
  - `SubagentStart` → transcript entry with the agent type when present
  - `SubagentStop` → transcript entry
  - `PostToolUseFailure` → `msg = "failed: <tool>"`, transcript entry `✗ <tool> …`
- All five ride the existing universal sound dispatch (`pending_play = name.lower()`)
  — silent no-op unless a matching wav is ever shipped.

## Out of scope

- Firmware changes (the events arrive as ordinary `msg`/`entries` heartbeat
  content; every display renders them already).
- cursor-bridge (Cursor has its own event vocabulary).
- Other unregistered events (ConfigChange, TaskCompleted, Worktree*…) — no
  display value identified yet.
