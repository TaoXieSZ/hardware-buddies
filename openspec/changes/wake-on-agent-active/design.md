## Context

`cardputer-adv-buddy/src/main.cpp` has two independent screen layers:

1. **`clawd::setSleeping`** (line ~506) — the GIF layer: when online+idle+still >3min, clawd plays `sleep.gif`. Purely visual; independent of backlight.
2. **Auto-dim backlight** (lines ~513-522) — the backlight layer: `activity = keyEvent || motion || recording || notes/playback`. If activity, `screenOn()`; else after `SCREEN_OFF_MS` (60s) of no motion+no key, `screenOff()` (brightness→0).

The wake-on-agent-active feature touches **only layer 2**. It adds a new wake trigger: agent activity edges.

Current `BuddyState` (from `cclink::state()`, `link_state.h`) carries `bs.running` and `bs.waiting` counts, already computed each frame. The approval prompt (`bs.promptId`) sets waiting. No new state field is needed — only edge trackers.

## Goals / Non-Goals

**Goals:**
- Screen stays lit while any session is running or waiting (covering permission prompts and AskUserQuestion).
- When all sessions go idle, the existing 60s timer re-dims normally — no permanent override.
- Zero change to the physical wake path and zero change to `sleep.gif` logic.

**Non-Goals:**
- Edge-only "wake once, re-dim after 60s even while still running" (tried first, failed on-device — see D1).
- Waking on `completed`/`error` edges (noisy; not requested).
- Touching the StickC/CoreS3/Tab5 firmware — this is Cardputer-ADV only (though the pattern is portable later).

## Decisions

**D1 — Level-based, not edge-based.**
The wake condition is `bs.running > 0 || bs.waiting > 0` folded into the existing `activity` bool — a *level* condition, evaluated every frame. Rationale (learned on-device): an edge-only wake (0→≥1) fails the motivating case — the agent is usually already running when the 60s dim timer fires, so `g_wasRunning` is already 1 and no edge is ever seen while dimmed. The user's intent ("when an agent is active, the screen is on") is a level semantics, not an edge. Level also matches how the existing `keyEvent`/`motion`/`recording` conditions already work (they're all level-based, re-asserting `screenOn()` each frame).

**D2 — Reuse the `activity` bool, don't add a parallel path.**
Adding `agentActive` to the existing `activity` OR-chain is the smallest change and keeps one dim-decision point. The alternative (a separate `if (g_screenOff && agentActive) screenOn()` block) duplicates logic and risks the two paths disagreeing. `screenOn()` is idempotent (lines 40-43), so calling it every frame while active is free.

**D3 — Use `bs.running`/`bs.waiting` counts, never `cclink::changed()`.**
`cclink::changed()` is true every heartbeat (the state struct is refreshed each push), which would force `screenOn()` forever and the screen would never dim — the exact anti-pattern already called out in the existing code comment at line 30 ("绝不用 cclink::changed()"). The running/waiting counts are the right level signal.

**D4 — Waiting includes permission prompts.**
`bs.waiting > 0` already covers AskUserQuestion and permission prompts (the `waiting` count is the cc-bridge waiting-sessions counter). No special-casing of `bs.promptId` — the waiting level is the trigger, and the existing approval-panel code (lines 148-153) renders the panel once the backlight is up.

**D5 — Re-dim is automatic.**
When all sessions go idle, `agentActive` becomes false, `activity` falls back to physical-only, and the existing 60s timer re-dims. No explicit "stop keeping awake" logic needed.

## Risks / Trade-offs

- **Power**: the screen stays lit for the entire duration of any running/waiting session, not just ~60s after a wake edge. In a long-running agent session this is more backlight-on time. Acceptable — these are exactly the moments the user wants to see, and a dim screen during a running session was the reported bug.
- **`waiting` from an auto-resolved prompt** (e.g. the auto-approve-once path at lines 141-145) keeps the screen on briefly while a prompt the user never sees is pending. Minor: it's a short window and the screen re-dims once waiting clears.
- **Trade-off**: chose level-over-edge deliberately after on-device testing showed edge-only fails the common case (agent already running when dim timer fires). If the user later wants "wake once then re-dim even while running", that's a separate change reverting to edge semantics.
