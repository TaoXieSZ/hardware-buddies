## Why

The ST7121-batch M5Stack Tab5 now runs ESP-Claw, but it is still an isolated
portrait-oriented AI device while the user's real agents live in Agent Farm.
Connecting the two creates a useful physical terminal only if landscape UI,
production credentials, agent identity, and failure boundaries are designed as
one system rather than added as ad-hoc HTTP calls.

## What Changes

- Make the complete ESP-Claw display stack use a 1280×720 landscape coordinate
  system with the USB/power connector on the left, while retaining the ST7121
  panel's native 720×1280 DSI timing.
- Keep rendering and ST7123 touch coordinates aligned across the boot UI,
  emote renderer, LVGL, and Lua display APIs.
- Run a dedicated, least-privilege runtime on the Primary MacBook. It reuses
  Agent Farm's Dispatcher and local agent-host but starts no Feishu channel,
  cron, sweep, or warm pool.
- Translate ESP-Claw's OpenAI-compatible chat requests into fixed-definition
  `tab5-operator` dispatches and forward only in-process, allowlisted, sanitized
  lifecycle events to the device.
- Add a dedicated `tab5-operator` definition in `mode: ask`, with independent
  memory, no warm pool, and no high-risk MCP tools in the initial release.
- Replace the portrait main experience with a landscape desktop-pet terminal:
  pet/status on the left, active/recent Agent Farm work on the right, and a
  small set of safe touch actions.
- Stage rollout as build/flash validation → scoped read-only SSE observe →
  canary definition → device event forwarding → explicitly authorized real
  dispatch, with a rollback at every boundary.

## Capabilities

### New Capabilities

- `tab5-esp-claw-landscape`: Global landscape rendering, logical geometry, PPA
  rotation, touch mapping, and orientation acceptance criteria for the ST7121
  Tab5 variant.
- `agentfarm-device-gateway`: Primary-Mac standalone runtime, credential
  separation, event redaction, fixed-definition dispatch, and fail-closed
  rollout behavior.
- `tab5-agentfarm-terminal`: The dedicated `tab5-operator` identity and the
  landscape desktop-pet task/status interaction exposed on Tab5.

### Modified Capabilities

None. This ESP-Claw firmware is separate from the existing
`claude-code-buddy/src/tab5/` USB dashboard and does not change its wire protocol
or `tab5-dashboard-ui` requirements.

## Impact

- ESP-Claw source:
  `/Users/txie/OpenSourceProjects/esp-claw/application/edge_agent/` and common
  display/Lua components.
- Agent Farm dispatch source:
  `/Users/txie/OpenSourceProjects/agent-farm/dispatch/`, plus a local
  `tab5-operator` definition and standalone Primary-Mac gateway process.
- Hardware: one Tab5 identified by MAC `80:f1:b2:d1:51:7d`; the previous
  portrait ESP-Claw firmware remains the device rollback image.
- Security: the device receives only a dedicated gateway token and device-event
  token. It receives no dashboard or agent-host credential.
- Operations: the stopped local full dispatch remains stopped, preventing
  Feishu double-consumption. Existing Feishu routing and Mac2 configuration are
  outside this integration path and remain unchanged.
