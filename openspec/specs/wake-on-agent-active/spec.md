# wake-on-agent-active Specification

## Purpose
TBD - created by archiving change wake-on-agent-active. Update Purpose after archive.
## Requirements
### Requirement: Backlight stays on while an agent is active
While any session is running or waiting (which covers permission prompts and AskUserQuestion), the auto-dim backlight layer MUST treat agent activity the same as physical activity — the screen SHALL stay lit so the user sees the busy avatar and approval/question panels. When all sessions go idle, the normal `SCREEN_OFF_MS` (60s) inactivity timer SHALL re-dim the screen.

#### Scenario: agent running, user idle
- **WHEN** at least one session has `bs.running >= 1` and the user does not touch the device
- **THEN** `screenOn()` MUST be called each frame (idempotent) and the screen SHALL NOT dim, regardless of how long the user is idle

#### Scenario: agent waiting (permission prompt / question)
- **WHEN** at least one session has `bs.waiting >= 1` (a permission prompt or AskUserQuestion is pending)
- **THEN** the backlight MUST stay on so the approval/question panel is visible

#### Scenario: all agents idle → re-dim
- **WHEN** all sessions transition to idle (`bs.running == 0 && bs.waiting == 0`) and the user remains physically inactive past `SCREEN_OFF_MS`
- **THEN** the screen SHALL dim exactly as it would with no agent activity

### Requirement: Do not use per-heartbeat change signal
The wake condition MUST use the `bs.running`/`bs.waiting` counts (level), not `cclink::changed()` — which is true every heartbeat and would prevent the screen from ever dimming.

#### Scenario: heartbeat without agent activity
- **WHEN** a heartbeat arrives but no session is running or waiting
- **THEN** the screen SHALL NOT be forced on by the agent condition and the normal dim timer MUST apply

### Requirement: No change to physical wake or sleep.gif
The physical-activity wake path (keypress / IMU motion / recording / notes / playback) and the `sleep.gif` state machine SHALL remain unchanged.

#### Scenario: physical wake still works
- **WHEN** the screen is off and the user presses a key or moves the device
- **THEN** `screenOn()` MUST fire via the existing physical-activity path, independent of agent state

#### Scenario: sleep.gif unaffected
- **WHEN** online + idle + still > 3min
- **THEN** `clawd::setSleeping(true)` behavior SHALL be unchanged regardless of the new agent-active backlight logic

