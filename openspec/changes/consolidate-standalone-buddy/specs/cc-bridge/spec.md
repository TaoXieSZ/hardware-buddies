## ADDED Requirements

### Requirement: Pane focus routing for OpenCode and Codex sessions
When the cardputer session switcher selects a non-Claude session, cc-bridge resolves the session id to the correct cmux surface UUID and focuses it, so a single BLE owner can drive Claude, Cursor, Codex, and OpenCode panes.

#### Scenario: selectSession targets an opencode pane
- **WHEN** the device sends `{"cmd":"selectSession","sid":"oc-44B2888D"}` (a tty-fallback synthetic sid) or a real opencode UUID
- **THEN** cc-bridge calls `_cmux.focus_by_opencode_sid(sid)` to resolve and focus the matching cmux pane, falling back gracefully if the pane no longer exists

#### Scenario: selectSession targets a codex pane
- **WHEN** the device sends `selectSession` with a codex sid
- **THEN** cc-bridge resolves via the existing codex cwd-join path and focuses the pane

### Requirement: Clear waiting state on tool failure
A failed tool run ends the current turn; any residual permission-waiting/prompt state from a prior `PermissionRequest` must be cleared so the firmware approval panel does not cover the error animation.

#### Scenario: tool fails after a permission prompt
- **WHEN** a `PostToolUse`/tool-failure event arrives while the state still has `waiting`/`prompt` set from a preceding `PermissionRequest`
- **THEN** cc-bridge calls `_clear_waiting(state)` so the next payload no longer shows a prompt

### Requirement: Fair ext-session slot allocation
When total sessions (local Claude + ext agents) exceed the 16-row firmware cap, ext-agent rows (cursor/codex/opencode) are allocated first and local Claude fills the remaining slots, so a busy Claude session list cannot starve the other agents off the device.

#### Scenario: 14 local Claude + 5 opencode sessions
- **WHEN** `to_payload` merges 14 local `_sessions` rows and 5 ext `opencode` rows
- **THEN** the merged `sessions[]` contains all 5 opencode rows first, then 11 local Claude rows (16 total), dropping the oldest excess Claude rows

#### Scenario: under cap
- **WHEN** total rows ≤ 16
- **THEN** all local and ext rows appear, ordering unchanged from the prior behavior
