# Tab5 P1 — agent dashboard: detailed design

**Status:** Approved direction (layout C + clawd avatar, user-picked) ·
**Date:** 2026-06-10 · **Parent:** [tab5-buddy.md](tab5-buddy.md)

## What P1 ships

A WiFi-connected touch dashboard on the Tab5 that shows every connected
coding-agent session (cc-bridge = Claude Code, cursor-bridge = Cursor; cmux
multi-session later), streams each session's transcript, and resolves
permission prompts with on-screen Approve/Deny buttons. A small clawd avatar
in the sidebar carries the buddy personality, animating with the selected
session's state.

## Screen layout (1280×720 landscape, rotation 3)

```
┌──────────────┬─────────────────────────────────────────────────────────┐
│  AGENTS      │  Claude Code — BUSY · Bash(pio run)            14:32    │ header 64px
│ ┌──────────┐ ├─────────────────────────────────────────────────────────┤
│ │● Claude  │ │  > Read src/main.cpp                                    │
│ │  BUSY    │ │  > Edit drawMicUI()                                     │
│ └──────────┘ │  > Bash pio run -e m5stack-tab5        ✓                │
│ ┌──────────┐ │  > TodoWrite (3 items)                                  │
│ │○ Cursor  │ │  ...                                                    │ transcript
│ │  IDLE 5m │ │  (touch-drag to scroll, newest pinned at bottom)        │ (scrolling)
│ └──────────┘ │                                                         │
│              ├─────────────────────────────────────────────────────────┤
│   (clawd     │  ⚠ PERMISSION  Bash(git push origin main)               │ banner 140px
│    avatar    │  ┌─────────────────┐        ┌─────────────────┐         │ (only when
│    ~200px)   │  │    ✓ ALLOW      │        │    ✗ DENY       │         │  ATTN)
│ WiFi▂▄ 🔋100 │  └─────────────────┘        └─────────────────┘         │
└──────────────┴─────────────────────────────────────────────────────────┘
   240px                              1040px
```

- **Sidebar (240px):** one entry per feed — status dot (state color: IDLE
  grey / BUSY blue / ATTN amber blink / DONE green / ERROR red), agent name,
  state word + staleness ("5m" if no heartbeat). Tap to select. Bottom:
  **clawd avatar** (~200px, GIF expression follows the *selected* session's
  state — reuses the character-pack state→GIF mapping), then a WiFi-RSSI +
  battery strip.
- **Header (64px):** selected agent, state, current tool/msg, clock.
- **Transcript:** the heartbeat `lines` buffer, accumulated client-side into
  a ~200-entry ring (the stick only shows 8 lines; Tab5 appends each new
  `lineGen` delta). Size-2 text ≈ 26px rows ≈ 17 visible rows; touch-drag
  scrolls, auto-follows tail unless the user scrolled up.
- **Permission banner (140px, ATTN only):** tool name + two large buttons
  (≥56px tall — thumb-sized). Replaces the transcript's bottom strip;
  also flashes the sidebar entry of a *non-selected* session in ATTN so
  prompts can't be missed.

## Architecture

```
cc-bridge daemon ──┐ TCP NDJSON :8771      Tab5 firmware (src/tab5/)
                   ├──────────────────▶  feed[0..N] ── parse ── SessionModel[N]
cursor-bridge ─────┘ TCP NDJSON :8772                              │
        ▲                                                  ui render (M5GFX)
        └──── {"btn":"a"|"b"} on touch ◀── touch hit-test ─┘
```

- **Transport: raw TCP + newline-delimited JSON** — the *same* line protocol
  the sticks speak over BLE NUS, so the daemons' `make_on_stick_line` and
  heartbeat emit are reused verbatim. No WebSocket: nothing on the path
  needs HTTP upgrade/framing, and stackchan's wifi frame-ingest (port 8770)
  already set the NDJSON-over-TCP precedent. Tab5 is the **client** (daemon
  IPs are stable, device IPs aren't); host comes from `wifi_secrets.ini`,
  one port per daemon (cc 8771, cursor 8772 — 8770 is stackchan's).
- **Daemon side:** new `TcpServerWriter` in `tools/buddy_core/core.py` —
  asyncio TCP server that mirrors every heartbeat line it already sends to
  BLE, and feeds inbound lines to the existing dispatcher. Enabled by env
  (`CC_BRIDGE_TAB5_PORT` / `CURSOR_BRIDGE_TAB5_PORT`), off by default.
  Permission answers reuse the stick button vocabulary: Tab5 sends
  `{"btn":"a"}` (allow) / `{"btn":"b"}` (deny) — zero new daemon logic.
- **Firmware modules** (`src/tab5/`, isolated like stackchan):
  - `net.cpp` — WiFi join (creds from wifi_secrets.ini build flags, same
    mechanism as stackchan) + N reconnecting TCP feeds
  - `model.h` — per-feed SessionModel: state, msg, tool, token count,
    transcript ring, lineGen delta tracking (port of the data.h lineGen
    stash-compare fix)
  - `ui.cpp` — sidebar/header/transcript/banner render to a PSRAM sprite
  - `avatar.cpp` — AnimatedGIF clawd playback, state→GIF map from the
    existing character pack (LittleFS upload, same flash_character.py flow)
  - `main.cpp` — loop: feeds → model → dirty-check → render → touch
- **Touch:** M5.Touch hit zones; debounce; pressed-state highlight.

## Risks / open items

1. **Arduino WiFi on P4 is the #1 risk** — it rides ESP-Hosted to the C6.
   pioarduino 55.x bundles the hosted stack, but this is exactly the kind of
   thing that needs a smoke test before any UI work. → **Milestone 0.**
2. Avatar GIF assets are 135×240-portrait-era; on Tab5 they render in a
   ~200px box — fine unscaled-ish, but check frame decode cost on P4
   (AnimatedGIF is CPU-decode; PPA can scale later if needed).
3. Screen-off policy: reuse StackChan's "no state change for N s" rule, NOT
   the stick's (daemon heartbeats every 2s would keep it awake forever);
   tap-to-wake via touch (no IMU gymnastics needed — touch stays powered).
4. wifi_secrets.ini placeholder needs the two new port keys committed so
   fresh clones parse (lesson from the stackchan audio_port latent bug).

## Milestones (each independently demoable)

| # | Deliverable | Gate |
|---|---|---|
| **M0** | ✅ **DONE 2026-06-11.** WiFi smoke: hello-world + WiFi join + RSSI on screen | PASSED — IP obtained over the C6 radio path. Required a one-shot C6 firmware update first: factory units ship esp-hosted slave 1.4.1, incompatible with the Arduino 2.8.x host (scans fail silently); `tools/tab5-c6-updater/` flashes esp32c6-v2.8.5 over SDIO (see its README for the full landmine list). Board must be `m5stack-tab5-p4`, NOT esp32-p4-evboard (wrong SDIO pins → WL_STOPPED). |
| **M1** | `TcpServerWriter` in buddy_core + cc-bridge flag; Tab5 single feed renders header+transcript live | Real Claude Code session visible on Tab5 |
| **M2** | Touch Approve/Deny round-trip | A real PreToolUse permission resolved by tapping the screen |
| **M3** | Second feed (cursor-bridge) + sidebar switching + ATTN cross-flash | Both agents monitored simultaneously |
| **M4** | clawd avatar + screen-off/tap-wake + polish | Daily-drivable desk device |
```
