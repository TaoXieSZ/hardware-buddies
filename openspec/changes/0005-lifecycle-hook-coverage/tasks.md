# Tasks

- [x] Delta spec with GIVEN/WHEN/THEN scenarios.
- [x] Failing tests in `tests/test_cc_bridge.py` for PreCompact / PostCompact msg,
      SubagentStart / SubagentStop entries, PostToolUseFailure msg+entry.
- [x] Map the five events in `tools/cc-bridge/bridge.py:apply_event`.
- [x] Add the five events to `HOOK_EVENTS_ASYNC` in `tools/cc-bridge/install.sh`.
- [x] Wire the five events into the live `~/.claude/settings.json`.
- [x] `pytest -q` green.
- [ ] `openspec archive 0005-lifecycle-hook-coverage` — merge the delta into
      `openspec/specs/daemon-event-mapping/spec.md`.
