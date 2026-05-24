# Voice control-plane secretary (MVP)

Speak to StackChan → a chosen Claude/Cursor session (running in **cmux**) runs
your command — hands-free, gated by a thumbs-up gesture. Closes the "voice IME
has no Enter" + "can't pick which session" gaps.

Spec: `.omc/specs/deep-interview-voice-control-plane.md`.

## Flow

```
You speak  ──►  Agora ConvoAI agent
                 tool route_to_session(number, text)   (verbatim, no rewrite)
                     │
                     ▼  socket action "stage_route"
              buddy_core daemon ── RouteStager.stage(number, text)   (PENDING, nothing sent yet)
                     │
   thumbs-up 👍 ─────┤ camera → hand_gesture 'approve' → RouteStager.confirm()
   thumbs-down 👎 ───┘ camera → hand_gesture 'deny'    → RouteStager.cancel()
                     │ confirm →
                     ▼
              CmuxClient.route(number, text)
                 cmux send --workspace <uuid> "<text>"   then   send-key Enter
```

Nothing reaches a session until the **gesture confirms** — the safety gate.

## Pieces (this MVP)

- `cmux_control.py` — `CmuxClient` over the cmux CLI (injectable runner).
  - `list_sessions()` ← `cmux rpc workspace.list '{}'` (JSON: id/ref/index/title/cwd/selected)
  - `route(number, text)` → `cmux send --workspace <UUID> "<text>"` + `send-key Enter`
  - `read_status(number)` → `cmux read-screen` last non-empty line
  - **targets the stable `id` (UUID), never the positional `workspace:N` ref.**
- `stager.py` — `RouteStager`: stage / confirm / cancel, last-wins, TTL auto-expire.
- `board.py` — Mac board: numbered sessions + status (text/JSON).
- `smoke_test.py` — safe real-cmux check against a throwaway workspace.

## Gesture wiring (reuses existing camera pipeline — no firmware change)

The StackChan camera already streams frames to the daemon, which classifies hand
gestures for the permission approve/deny flow:

- `tools/buddy_core/hand_gesture.py` → `classify_landmarks()` returns
  `'approve'` (thumbs-up) / `'deny'` (thumbs-down) / `None`.
- `tools/buddy_core/gesture_classifier.py` → `GestureClassifier` debounces so one
  sustained gesture fires exactly once.

Map the **same** classified stream onto the stager:

| Gesture | classify result | Stager call |
|---|---|---|
| 👍 thumbs-up | `approve` | `RouteStager.confirm()` → fires `cmux send` |
| 👎 thumbs-down | `deny` | `RouteStager.cancel()` → drops the pending command |

No new gesture model and no firmware change — only a dispatch line where the
daemon already handles classified gestures.

## Setup

```bash
# cmux CLI on PATH (once):
sudo ln -sf "/Applications/cmux.app/Contents/Resources/bin/cmux" /usr/local/bin/cmux
```

## Verified vs. user-verified

> **Note:** `buddy-voice` is the **separate** Agora ConvoAI quickstart project
> (sibling dir `../buddy-voice`, scaffolded by `agora init`), *not* a folder in
> this repo. Its `app/api/stage-route/route.ts` forwards the agent's
> `route_to_session` tool to this daemon's socket.

- **Agent-verified**: unit tests (`tests/test_cmux_control.py`,
  `tests/test_route_stager.py`, `tests/test_stage_route.py`), the cmux smoke
  test (`smoke_test.py`), and `npx tsc --noEmit` in the separate `../buddy-voice`
  project.
- **USER-verified (hardware/runtime)**: the full voice → thumbs-up → session-runs
  loop on the physical StackChan (camera gesture + live mic).
- **Deferred (phase 2)**: the StackChan **on-screen** board (this MVP ships the
  Mac board only).
