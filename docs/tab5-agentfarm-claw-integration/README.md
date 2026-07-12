# Tab5 ESP-Claw × Agent Farm Integration

## Goal

Turn the landscape M5Stack Tab5 into a dedicated `tab5-operator` terminal:

- chat requests go through a restricted Primary-Mac gateway to Agent Farm;
- sanitized Agent Farm lifecycle events drive the Tab5 status UI;
- touch actions remain local-only in the initial release;
- no Agent Farm administrator credential is stored on the device.

## Architecture

```text
Tab5 ESP-Claw
  ├─ OpenAI-compatible chat ──> local bare-Dispatcher gateway
  └─ landscape status UI <──── sanitized local lifecycle events
                                        │
                                        v
                              local agent-host :60620
                                        │
                                        v
                                 tab5-operator
```

Tab5 and the gateway use separate bearer tokens. The gateway loads only
`config.tab5.yaml` plus gitignored `.env.tab5`, fixes every chat request to
`tab5-operator`, and never starts the full local dispatch process.

## Allowed surface

- Read sanitized health, agent lifecycle, task result, and short result summary.
- Send a prompt only to the dedicated `tab5-operator` definition.
- Use local-only touch status/history actions.

## Explicitly excluded

- `/api/config`, reload, rotate, pool refill, approval, notify, and raw trace proxying.
- Arbitrary Agent Farm definition selection.
- Agent-host create/send APIs.
- Run cancellation: Agent Farm currently has no true run-cancel endpoint.
- Dashboard token, agent-host token, full prompts, secrets, or raw config on Tab5.

## Blast radius

- A malformed display build affects only the single Tab5 and is recoverable by
  reflashing the previous firmware.
- A gateway defect can consume Cursor usage only through `tab5-operator`; the
  definition allowlist and `mode: ask` constrain it.
- Stopping the standalone gateway removes the integration without restarting
  the stopped full `dispatch` or the existing local `agent-host`.
- No Mac2 service or configuration is part of the final data path.

## Deployment gates

1. Landscape firmware passes build, boot, visual orientation, touch, and display
   owner-switch tests.
2. Gateway passes authentication, fixed-definition, event redaction, timeout,
   process-lifecycle, and device-forwarding tests.
3. Event-only mode is accepted before LAN chat is enabled.
4. Review the visual impact map in `output/architecture.html`.
5. Verify `tab5-operator` is `mode: ask`, has isolated memory, and has no MCP.
6. Perform one owner-approved device Web Chat and verify the complete return path.

## Rollback

1. Stop the standalone gateway process.
2. Point ESP-Claw back to the previous provider Base URL if chat is still needed.
3. If needed, flash commit `c182a77` to restore the portrait firmware.
