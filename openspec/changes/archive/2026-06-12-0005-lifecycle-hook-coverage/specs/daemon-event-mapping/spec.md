# daemon-event-mapping (delta)

## ADDED Requirements

### Requirement: Compaction visibility

`apply_event` MUST surface context compaction so the displays explain the
pause instead of showing stale state.

#### Scenario: Compaction starts
- GIVEN any `BuddyState`
- WHEN a `PreCompact` event arrives
- THEN `state.msg` becomes `"compacting…"` and a transcript entry is added

#### Scenario: Compaction ends
- GIVEN any `BuddyState`
- WHEN a `PostCompact` event arrives
- THEN `state.msg` becomes `"compacted"` and a `"compacted"` transcript entry is added

### Requirement: Subagent visibility

`apply_event` MUST add transcript entries for subagent lifecycle events.

#### Scenario: Subagent starts with a type
- GIVEN a `SubagentStart` event carrying `agent_type`
- WHEN it is applied
- THEN a transcript entry naming that agent type is added

#### Scenario: Subagent stops
- GIVEN a `SubagentStop` event
- WHEN it is applied
- THEN a `"subagent done"` transcript entry is added

### Requirement: Tool failure visibility

`apply_event` MUST distinguish failed tool calls from successful ones.

#### Scenario: Tool fails
- GIVEN a `PostToolUseFailure` event with `tool_name == "Bash"`
- WHEN it is applied
- THEN `state.msg` becomes `"failed: Bash"` and a transcript entry starting with `"✗ Bash"` is added

### Requirement: Lifecycle event registration

`install.sh` SHALL register `PreCompact`, `PostCompact`, `SubagentStart`,
`SubagentStop`, `PostToolUseFailure` as async hooks alongside the existing set,
so the daemon actually receives them.

#### Scenario: Fresh install registers the lifecycle events
- GIVEN a fresh run of `tools/cc-bridge/install.sh`
- WHEN the hook entries are written to `~/.claude/settings.json`
- THEN `PreCompact`, `PostCompact`, `SubagentStart`, `SubagentStop` and
  `PostToolUseFailure` each carry an async hook invoking `hook.py`
