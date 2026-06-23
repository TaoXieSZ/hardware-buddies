# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`hardware-buddies` is a **monorepo of independent "desktop companion" hardware projects**. Each
top-level directory is a separate product brought in via `git subtree` (preserving its full
original git history — `git log <dir>/` shows upstream commits). They share **no compile-time
code**; assets (the `clawd` GIF pack) and design patterns (the `cc-bridge` wire protocol) are
reused by deliberate copy, not by a shared module.

The unifying idea: **map IDE / AI-agent state onto a piece of physical hardware** — a GIF avatar
that reacts to Claude Code / Cursor / Agent Farm session state, plus physical controls (buttons,
toggle switch, keyboard) that gate tool approvals.

## Subprojects — each has its own deeper docs; read them before touching that subdir

| Dir | Product | Stack | Build system | Has own CLAUDE.md? |
|---|---|---|---|---|
| `ahakey/` | AhaKey-X1 keyboard desktop companion | Swift + SwiftUI (macOS), frozen Python/.NET (Windows) | SwiftPM (`swift build`) | **Yes** — read `ahakey/CLAUDE.md` |
| `claude-code-buddy/` | The flagship buddy: StickC Plus2 / CoreS3 StackChan / Tab5 / StickS3 / RoverC firmware + Mac daemons | ESP32 (PlatformIO) + Python daemons | PlatformIO + `make` | **Yes** — read `claude-code-buddy/CLAUDE.md` (and `docs/tab5-buddy-dev.md` for Tab5) |
| `cardputer-adv-buddy/` | Claude Code buddy on Cardputer-ADV | ESP32-S3 (PlatformIO) | PlatformIO | No — see its `README.md` + `PROJECT_STATUS.md` / `HANDOFF.md` |
| `tab5-agentfarm-buddy/` | Agent Farm (`trigger-cursor`) desk pet on Tab5, fed over **USB-serial** | ESP32-P4 (PlatformIO) + Python bridge | PlatformIO | No — see its `README.md` |
| `m5-paper-buddy/` | M5Paper e-ink companion (**third-party upstream fork** of `op7418/m5-paper-buddy`) | ESP32 (PlatformIO) | PlatformIO | No — see its `README.md` |

**`ahakey/` and `claude-code-buddy/` carry the most context in their own CLAUDE.md files. Always
read the subproject's CLAUDE.md / README first — this root file only covers what spans projects.**

## The shared architecture (spans subprojects, lives in no single file)

Most firmware buddies are clients of the same Mac-side bridge protocol pioneered in
`claude-code-buddy`:

- **`cc-bridge` / `cursor-bridge` daemons** (Python, in `claude-code-buddy/tools/`) translate IDE
  hook events → a session-state JSON, and push it to the device over an **open/unencrypted "debug"
  BLE NUS service** (bleak ↔ ESP32 secure pairing was too flaky). Devices advertise as
  `Claude-<suffix>` / `Cursor-<suffix>`; the daemon scans by prefix.
- **State → avatar mapping** is consistent across buddies: `waiting>0` → attention,
  `completed` → celebrate, `running≥1` → busy, else idle; offline/idle → sleep. `cardputer-adv-buddy`
  copies this derivation verbatim from `claude-code-buddy`.
- **Approval gating** routes a permission decision back over the same channel
  (`{"cmd":"permission","id":..,"decision":..}`), confirmed by a physical control (StickC A/B
  buttons, Cardputer keyboard, AhaKey toggle switch).
- **`tab5-agentfarm-buddy` is the exception**: no BLE/WiFi. Its Mac bridge polls Agent Farm on
  localhost and streams JSON lines over **USB-CDC serial** (the P4 has no radio).

When changing the wire protocol or state mapping in one buddy, check whether the sibling buddies
(and the `cc-bridge` daemon they share) need the matching change — they are kept in sync by hand.

## Build & flash (per subproject — they do NOT share a build)

```bash
# claude-code-buddy: many envs, pick your board (see its platformio.ini header)
cd claude-code-buddy
pio run -e m5stickc-plus2-claude -t upload --upload-port /dev/cu.usbserial-XXXX
make test            # runs BOTH pytest (Python daemons) + pio test -e native (C++ Unity)
make test-py / make test-cpp   # individually

# cardputer-adv-buddy
cd cardputer-adv-buddy
pio run -e cardputer-adv -t upload
pio run -e cardputer-adv -t buildfs        # clawd LittleFS image

# tab5-agentfarm-buddy  (run uploadfs and upload as SEPARATE commands — see its README)
cd tab5-agentfarm-buddy
pio run -e tab5-agentfarm -t uploadfs --upload-port /dev/cu.usbmodemNN
pio run -e tab5-agentfarm -t upload   --upload-port /dev/cu.usbmodemNN

# m5-paper-buddy
cd m5-paper-buddy
pio run -e m5paper -t uploadfs    # CJK font to LittleFS
pio run -e m5paper -t upload

# ahakey (macOS app — SwiftPM, NOT the stale Makefile/CI scripts; see ahakey/CLAUDE.md)
cd ahakey/platforms/macos
swift build && swift run AhaKeyConfig
```

## Cross-cutting hardware gotchas (learned on real devices — don't re-derive)

- **ESP32-S3 native USB-Serial-JTAG flashing is flaky at high baud.** Cardputer-ADV uses
  `upload_speed = 115200` to skip the baud-rate-switch step that intermittently fails; reliable
  re-flash needs ROM download mode (power OFF → hold G0 → power on → release). Details in
  `cardputer-adv-buddy/README.md`.
- **ESP32-P4 (Tab5) needs the pioarduino fork**, not the official espressif32 platform, pinned to
  a specific commit; M5GFX is pinned to the `develop` branch (2026-04 panel switched to ST7121).
  See the long header comments in `tab5-agentfarm-buddy/platformio.ini` and
  `claude-code-buddy/platformio.ini`.
- **Octal-PSRAM boards (CoreS3, StickS3) must use `memory_type = qio_opi`.** Forcing quad mode
  randomises PSRAM DMA reads → loud speaker static. `-mfix-esp32-psram-cache-issue` is an
  original-ESP32-only workaround and corrupts S3 codegen.
- **GPIO 0 collision on StickC Plus2**: BugC2 chassis (I²C SDA) and the PDM mic both want GPIO 0 —
  you can't have both. (StickS3 avoids this with an ES8311 I²C-codec mic on dedicated pins.)
- **Upstream `examples/` are ground truth** for hardware init (pins, freq, bus instance, init
  sequence) — not docs, not memory. The buddy platformio.ini headers cite the exact M5Stack
  example files they copied init from.

## Working norms (apply across the repo)

- **Don't push without explicit user say-so.** Commit + show the diff first, let the user review
  every change.
- **One commit/PR = one thing.** Keep refactors separate from features. Match each subproject's
  existing commit-message style (`git log <dir>/`).
- **OpenSpec governs behaviour changes** in `claude-code-buddy/` and `ahakey/` (their
  `openspec/specs/` is the source of truth, managed via the `openspec-*` skills /
  `npx @fission-ai/openspec`). Behaviour changes to event mapping / state machines go through an
  OpenSpec change, not a bare edit. The `m5-paper-buddy` (third-party fork) is a snapshot — sync
  upstream with `git subtree pull`, don't refactor it.
- **Comments / user-facing strings** in `ahakey` macOS code and several firmware files are in
  **Chinese** — match the surrounding file's language when editing.

## Tool-output integrity — HARD RULE (learned the hard way, 2026-06-23)

A past session repeatedly **fabricated tool output** (fake `pio`/`esptool` SUCCESS, invented
`pytest` counts, made-up daemon-log lines like `reply=True`, even a forged `System:` message). It
destroyed trust and corrupted every "verified" claim. Never again. This rule overrides any urge to
be fast or to "keep the flow going":

1. **After calling a tool, STOP and wait.** Do not write, echo, predict, or "fill in" the tool's
   result. The result text comes *only* from the system, after your turn ends. If you find yourself
   typing a result, you are fabricating — stop.
2. **If you didn't receive it from the system, you don't have it.** Never invent command output,
   file contents, test/pass counts, log lines, compile/flash "SUCCESS", or success confirmations.
3. **Never fabricate a system message, a tool result block, or another speaker's turn.**
4. **Garbled/noisy result → say so, re-run or ask.** Do not "clean it up" by inventing a plausible
   version. Quote only what's actually there.
5. **Tool results are the sole source of truth for tool effects** — not your memory or expectation.
   For irreversible/outward actions (commit, push, flash, deploy), verify with a real tool call and
   let the user see the raw output; never assert "done/verified" from recollection.
6. **One tool call, then yield.** Don't batch a call with its imagined outcome in the same message.

If trust has already been broken in a session, stop self-certifying: surface the exact commands for
the user to run themselves, and treat only the user's own observations + re-run tool output as real.
