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
  A **session = a terminal surface (pane)** with a stable **NATO phonetic
  nickname** (alpha/bravo/…) keyed by surface UUID — addressing never shifts
  when other panes open or close.
  - `list_sessions()` fans out across **all cmux windows** (`window.list` →
    `workspace.list` → `surface.list`), excluding browser surfaces and any
    surface registered in `~/.cache/control-plane/board-surfaces/` (the live
    board panes register themselves so they're never targeted).
  - `route(target, text)` → `surface.focus` + `surface.send_text` +
    `surface.send_key Enter`; `target` is a nickname (`"alpha"`), an
    unambiguous prefix (`"alph"`), or — for back-compat — a 1-based number.
  - `read_surface(surface)` returns a *smart* status line: skips the Claude
    Code banner / OMC HUD / separators and prefers a recap (`※`) or activity
    verb (`✻`) when present. `read_surface_text(surface)` is the raw full
    screen.
  - **targets the stable surface `id` (UUID), never the positional `surface:N` ref.**
  - cwd is the owning workspace's `current_directory` (cmux has no per-pane cwd).
- `stager.py` — `RouteStager`: stage / confirm / cancel, last-wins, TTL auto-expire.
- `board.py` — Mac board: ships by nickname + smart status (text/JSON/`--watch`).
- `say.py` / `chat.py` — keyboard fleet driver (`alpha echo hi`) and Codex-style
  LLM REPL persona-driven by **大副 (First Mate)** via `claude --print`.
- `fleet_layout.sh` — one cmux window: board pane + voice-agent browser surface.
- `smoke_test.py` — safe real-cmux check against a throwaway workspace.

> The whole `control_plane/` module has been **extracted into the standalone
> [`agent-fleet`](https://github.com/TaoXieSZ/agent-fleet) project**. This
> repo will eventually consume it as a dependency.

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

## Voice trigger (Path B — client transcript parse)

Rather than a cloud-reachable ConvoAI MCP tool, the buddy-voice client parses
**your** transcript turns locally and stages matches (fully local, verbatim,
no exposed endpoint). A turn is a command only if it carries an explicit
session marker, so ordinary chat never misfires:

| You say | Staged |
|---|---|
| `2号 跑测试并修复` | session 2 ← "跑测试并修复" |
| `第1个 npm run build` | session 1 ← "npm run build" |
| `session 3 git status` | session 3 ← "git status" |
| `会话2 ls -la` | session 2 ← "ls -la" |
| `今天天气不错` | (no marker → ignored) |

Parser: `buddy-voice/lib/controlPlaneCommand.ts`; wiring:
`hooks/useControlPlaneCommands.ts` → `POST /api/stage-route`. Enable with
`NEXT_PUBLIC_CONTROL_PLANE=1` in `buddy-voice/.env.local`.

## Confirm a staged command

The thumbs-up gesture commits a staged command (thumbs-down cancels). Keyboard
fallback (also lets you test the loop without the camera):

```bash
python -m control_plane.confirm           # 👍 commit
python -m control_plane.confirm cancel     # 👎 cancel
```

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
