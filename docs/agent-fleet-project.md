# Agent Fleet — new project proposal

> Spin the voice control-plane secretary out of the StackChan-buddy repo into
> its own product: a **hands-free control plane for a fleet of coding agents**.

## The pitch

You run many coding agents in parallel (Claude Code / Cursor / Codex panes in
**cmux**). Driving them means constant keyboard + window-switching. Agent Fleet
turns that into: **glance at a board → say "session 2, run the tests" →
thumbs-up → it runs** — without touching the keyboard. A physical avatar
(StackChan) is an optional "face" that speaks, shows the board, and reads your
gestures.

It's a *conductor* for a fleet of agents, not a chatbot.

## Why a separate project

What we built lives in `claude-desktop-buddy/tools/control_plane/`, but its core
has **nothing to do with StackChan**: cmux routing + staging + board + confirm
is a standalone control plane. StackChan is one optional peripheral (voice-out,
glanceable board, camera gesture). Separating makes the control plane usable by
anyone with cmux, with or without the hardware.

## Name candidates
`fleet` · `conductor` · `coxswain` · `helm` · `skipper` · `cmux-conductor`

## What moves into the new repo (already built, reusable)
| From here | Role in new project |
|---|---|
| `tools/control_plane/cmux_control.py` | session enumeration + UUID-targeted routing |
| `tools/control_plane/stager.py` | gesture-gated stage→confirm/cancel |
| `tools/control_plane/board.py` | the fleet board |
| `tools/control_plane/confirm.py` | confirm/cancel CLI |
| `tools/buddy_core` socket server + `hand_gesture`/`gesture_classifier` | daemon + gesture recognition (extract the non-BLE parts) |
| buddy-voice `controlPlaneCommand.ts` + `useControlPlaneCommands` + `/api/stage-route` | voice→stage front end |

## Architecture (proposed)
```
fleet/
  core/        cmux routing, stager, board (Python) — from control_plane/
  daemon/      socket server + gesture classifier (extract from buddy_core)
  voice/       ASR → parse "N号 …" → stage   (web)
  board-ui/    Mac menubar / dashboard
  peripherals/
    stackchan/ optional: voice-out + on-device board + camera gesture (this repo)
```

## Decoupling decisions to make (shapes the project)
1. **ASR source.** Today = Agora ConvoAI (cloud, conversational). For a pure
   control plane, the browser **Web Speech API** (free, local, no cloud, no
   account) may be enough — drops the Agora dependency entirely. Or keep Agora
   if you also want the chat agent. → biggest fork.
2. **Gesture input.** Camera thumbs-up works from any webcam (Mac built-in),
   not just StackChan — gesture confirm need not require the hardware.
3. **StackChan = optional peripheral**, not the center.
4. **Targets beyond cmux?** v1 = cmux only (clean). Later: iTerm2 (`write text`),
   tmux (`send-keys`).

## Roadmap
- **v0 (done — this repo's MVP):** cmux routing, stage/confirm, board,
  voice-parse, manual confirm. Verified live.
- **v1:** standalone repo; Web Speech ASR (no cloud); menubar board; keyboard +
  webcam-gesture confirm. Usable with zero hardware.
- **v2:** StackChan peripheral (voice-out, on-device board, camera gesture);
  iTerm2/tmux targets.
- **v3:** richer fleet ops — broadcast, "whichever is waiting", per-agent status
  from `read-screen`, notifications.

## Open questions for you
- Project name?
- ASR: Web Speech (local, drop Agora) vs keep Agora ConvoAI (chat + voice-out)?
- StackChan: central avatar, or optional peripheral?
- New repo location (e.g. `~/OpenSourceProjects/<name>`), public or private?

## Next step
Once name + ASR choice are set, scaffold `<name>/` with `core/` extracted from
`control_plane/` (it's already self-contained + tested), a thin daemon, and a
minimal voice front end. The control-plane code is the seed — it lifts cleanly.
