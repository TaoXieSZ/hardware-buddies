# Voice control-plane secretary — RUNBOOK

Every step to run + operate the voice/gesture control plane for a **fleet of
cmux coding-agent sessions** (Claude Code / Cursor / Codex panes). Speak →
a chosen session runs your verbatim command, gated by a thumbs-up.

Architecture + design: `README.md` · `../../.omc/specs/deep-interview-voice-control-plane.md`

---

## 0. Prerequisites (one-time)

```bash
# cmux CLI on PATH
sudo ln -sf "/Applications/cmux.app/Contents/Resources/bin/cmux" /usr/local/bin/cmux
cmux list-workspaces            # sanity: lists your sessions
```
- A **session = a cmux terminal pane (surface)** — agents run as splits or tabs;
  every terminal pane gets a stable **NATO phonetic nickname** (alpha/bravo/…)
  keyed by surface UUID, persisted in `~/.cache/control-plane/nicknames.json`.
  Names are forever for the pane's lifetime; the board, daemon and LLM all use
  the nickname (positional numbers stay for back-compat only).
- The board now spans **all cmux windows** — splitting the board into its own
  window no longer hides agent panes.
- The board's own pane self-registers so the enumerator skips it. The
  voice-agent browser surface is auto-excluded by type.
- The **cc-bridge daemon** must be running (launchd `com.cc-bridge`, runs
  `tools/cc-bridge/bridge.py`). It owns the socket `/tmp/cc-bridge.sock` + the
  RouteStager.
- **buddy-voice** = the separate Agora ConvoAI quickstart at `../buddy-voice`
  (the mic/ASR front end). StackChan device is optional (only for voice-out +
  the camera gesture).

---

## 1. Daemon: load the control-plane code

The daemon must be running the current repo code (the `route_stager` wiring).
After any change to `tools/buddy_core/` or `tools/cc-bridge/`:

```bash
launchctl kickstart -k gui/$(id -u)/com.cc-bridge
[ -S /tmp/cc-bridge.sock ] && echo "socket up"
tail -f ~/Library/Logs/cc-bridge.err.log     # optional: watch it
```

## 2. Enable the voice trigger

```bash
# buddy-voice/.env.local
NEXT_PUBLIC_CONTROL_PLANE=1
# restart the dev server so it picks up the env var:
#   pkill -f "next dev"; (cd ../buddy-voice && pnpm dev)
```
Open <http://localhost:3000> → **Try it now** (mic permission).

## 3. See the fleet (the board)

```bash
python3 -m control_plane.board          # one-shot numbered sessions + status
python3 -m control_plane.board --watch  # live board, auto-refresh (leave it open)
python3 -m control_plane.board --json
```
The **number** is what you say. `--watch` highlights the focused session and
refreshes every 2 s (`--interval N` to change).

### One-window layout (voice agent + board + sessions in cmux)

cmux surfaces can be terminals **or** browsers, so the whole control plane fits
in one cmux window. One command assembles it:

```bash
tools/control_plane/fleet_layout.sh     # FLEET_VOICE_URL overrides localhost:3000
```
It opens a `fleet board` workspace = live board (terminal) **+** the voice
secretary (browser surface → the Agora voice UI), split side by side. Your
coding-agent sessions stay as the other cmux workspaces/tabs in the window.

## 4. Operate — speak, then confirm

1. **Speak** a command with an explicit session marker:

   | You say | → |
   |---|---|
   | `alpha 跑测试并修复` | alpha ← "跑测试并修复" |
   | `bravo npm run build` | bravo ← "npm run build" |
   | `charlie git status` | charlie ← "git status" |
   | `2号 跑测试并修复` | (legacy) session 2 ← "跑测试并修复" |
   | `今天天气怎么样` | ignored (no marker) |

   It **stages** (nothing sent yet).

2. **Confirm** (commits the staged command into that session + Enter; the
   session auto-pops to the front):
   - 👍 thumbs-up to StackChan's camera (hands-free), **or**
   - `python3 -m control_plane.confirm`     (keyboard fallback)
   - cancel: 👎 / `python3 -m control_plane.confirm cancel`

---

## 5. Verify without voice / camera

```bash
python3 tools/control_plane/demo.py        # board→stage→confirm→execute (throwaway)
python3 tools/control_plane/smoke_test.py  # raw cmux send round-trip (throwaway)
~/.cache/buddy-venv/bin/python -m pytest tests/test_cmux_control.py \
    tests/test_route_stager.py tests/test_stage_route.py -q
```
Live daemon path (stage via socket → confirm → execute), proven 2026-05-24:
```
stage_route  -> {"ok": true}
confirm_route -> {"ok": true, "fired": true}
  ➜ /tmp echo LASTHOP_OK_…   →   LASTHOP_OK_…     # cmux executed it
```

## 6. Troubleshooting

| Symptom | Fix |
|---|---|
| `stage_route -> {"ok":false,"error":"no route_stager"}` | daemon is old code → reload (step 1) |
| `confirm.py: can't reach daemon` | daemon not running → `launchctl kickstart -k gui/$(id -u)/com.cc-bridge` |
| Voice said but nothing staged | marker missing (say "N号 …"); or `NEXT_PUBLIC_CONTROL_PLANE` not set / dev not restarted |
| Routed to the wrong session | numbers shift as workspaces open/close — re-check the board; targeting binds to the cmux **UUID** at confirm time |
| `cmux: command not found` | symlink (step 0); code falls back to `/Applications/cmux.app/Contents/Resources/bin/cmux` |

## 7. State (2026-05-24)

- ✅ Verified live: board, stage(socket), confirm/cancel, cmux execute + auto-focus.
- ✅ Verified: parser grammar (8/8), unit suite (129 passed), buddy-voice tsc.
- ⏳ User hardware/runtime: real mic ASR → stage; camera thumbs-up commit.
- ⏳ Deferred (phase 2): StackChan on-screen board.
