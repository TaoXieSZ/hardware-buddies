# AGENTS.md

OpenCode-specific guidance for the `hardware-buddies` monorepo. Complements
`CLAUDE.md` — read both, then the subproject's own CLAUDE.md/README before
touching that directory. `CLAUDE.md` holds the full shared-architecture
narrative (cc-bridge protocol, state→avatar mapping) and the long
cross-cutting hardware-gotcha list; this file stays compact and operational.

## Repo structure

Independent hardware projects, each brought in via `git subtree`. **No shared
build** — always `cd <subdir>` first.

| Dir | Status | Build | Read first |
|---|---|---|---|
| `ahakey/` | Active (macOS SwiftUI) | `swift build` in `platforms/macos/` | `ahakey/CLAUDE.md` |
| `claude-code-buddy/` | Active (ESP32 + Python) | `pio run -e <env>` | `claude-code-buddy/CLAUDE.md` |
| `cardputer-adv-buddy/` | Active (ESP32-S3) | `pio run -e cardputer-adv` | `README.md` + `HANDOFF.md` |
| `tab5-agentfarm-buddy/` | Active (ESP32-P4) | `pio run -e tab5-agentfarm` | `README.md` |
| `m5-paper-buddy/` | **Frozen fork** (third-party) | `pio run -e m5paper` | `README.md` |

## Gotchas that aren't obvious

- **ahakey macOS Makefile is stale** — `ahakey/platforms/macos/Makefile` calls
  `./scripts/build.sh` which doesn't exist. The real source of truth is
  `swift build` (and `swift run AhaKeyConfig`) run from `ahakey/platforms/macos/`.
- **Tab5 work in claude-code-buddy lives in `src/tab5/` + `tools/buddy_core/`.**
  Read `docs/tab5-buddy-dev.md` before touching either — it's the handoff doc
  with full port status and one-shot C6 firmware-update notes.
- **tab5-agentfarm-buddy flash order matters** — `uploadfs` then `upload` as
  **separate** commands (device hard-resets after uploadfs and the port
  re-enumerates, so a chained firmware connect can't reattach).
- **cardputer-adv-buddy**: ESP32-S3 native USB-Serial-JTAG is flaky at high
  baud. `upload_speed=115200` already set to skip the baud-switch step.
  Reliable re-flash needs ROM download mode (power OFF → hold G0 → power on →
  release). Details in its `README.md`. See also the flashing postmortem
  section below — a wrong-platform flash bricked this device once.
- **BLE advertised names come from the BT MAC, not the USB serial number.**
  Firmware builds the name from `esp_read_mac(ESP_MAC_BT)`; on ESP32-S3 the
  BT MAC = base MAC **+ 1** (S3 defaults to 2 universal MACs — unlike classic
  ESP32 where BT = base+2). The USB-JTAG serial number IS the base MAC, so
  never derive the advertised name from it: cardputer-ADV's USB serial ends
  `7A:FC` but it advertises `Claude-7AFD`. When in doubt, run a 10s bleak
  scan for ground truth instead of computing. Full story + debug checklist:
  `docs/cc-bridge-ble-connection-issue.md`. Changing `CC_BRIDGE_*` env in the
  plist requires `launchctl unload` + `load` (`kickstart -k` does not re-read
  it).
- **cc-bridge / cursor-bridge / codex-bridge / opencode-bridge launchd
  plists run from the monorepo path**
  (`hardware-buddies/claude-code-buddy/tools/<bridge>/bridge.py`). The
  standalone clone `~/OpenSourceProjects/claude-desktop-buddy/` was retired
  2026-07-09 (openspec change `consolidate-standalone-buddy`). Each bridge
  has its own `install.sh` that writes the plist + venv. `opencode-bridge`
  also repoints `~/.config/opencode/opencode.json`'s plugin entry at its
  `cardputer-permission.mjs`. `agentfarm-usb-bridge` auto-detects the Tab5
  by USB serial prefix `80:F1:B2:` (distinguishing it from the Cardputer-ADV
  which is also VID 303A but `50:78:7D:`).
- **m5-paper-buddy**: do not refactor. It's a `git subtree` snapshot of
  `op7418/m5-paper-buddy`. Only sync upstream via `git subtree pull`.
- **Root `README.md` lists only 3 of the 5 subprojects** — trust the table
  above and `CLAUDE.md`, not the root README, for the current subproject set.

## Flashing discipline — postmortem 2026-07-08 (black-screen bricking)

An agent "fixed" a black Cardputer-ADV screen and left it boot-looping. Root
cause: the device had been flashed with a build from the **wrong PlatformIO
platform**, and every later patch treated symptoms. Rules distilled:

1. **Platforms are pinned per subproject — never mix them.**
   `cardputer-adv-buddy` = official `espressif32@6.7.0` (Arduino v2.x,
   IDF 4.4). Tab5 projects = the pioarduino fork (Arduino v3.x, IDF 5.5).
   A pioarduino build on the Cardputer's PSRAM-less ESP32-S3FN8 enables
   SPIRAM and 16MB default partitions → `Failed to init external RAM!` →
   abort → boot loop → black screen. Always build from inside the subproject
   dir with its own env: `cd cardputer-adv-buddy && pio run -e cardputer-adv`.
2. **`custom_sdkconfig` only exists on pioarduino.** On the official
   `espressif32` platform it is silently ignored (the Arduino core ships
   precompiled — sdkconfig cannot be changed there). Adding it to
   cardputer-adv-buddy's `platformio.ini` is a no-op, not a fix.
3. **Black screen / dead device → capture serial FIRST, before theorizing.**
   8 seconds of boot log answers everything: bootloader IDF version
   (`ESP-IDF 5.x` on cardputer-adv = wrong-platform firmware; correct is
   4.4), reported flash size (16MB on cardputer = wrong board config; the
   ADV is 8MB), bootloader compile timestamp (who flashed what, when), and
   the partition table. Pick the port by USB serial number, never by port
   number: cardputer = `50:78:7D:…`, Tab5 = `80:F1:B2:…` (ports flip).
4. **Two consecutive mid-flash `Device not configured` drops → stop
   retrying.** Go to ROM download mode and write all four images
   (bootloader / partitions / firmware / littlefs @ 0x310000) in one
   esptool call with `--before no_reset` — exact command in
   `cardputer-adv-buddy/README.md`. Then clean power-cycle (OFF→ON, no G0).
5. **Recovery from a wrong-platform flash** = revert config + clean rebuild:
   `git checkout -- platformio.ini && rm -rf .pio/build && pio run -e
   cardputer-adv`, then flash per rule 4. Don't patch sdkconfig around a
   fundamentally wrong build.

## OpenSpec — behavior changes require it

In `ahakey/` and `claude-code-buddy/`, behavior changes (event mapping, state
machine, wire protocol) go through OpenSpec:

1. `/opsx:propose` — spec + design + tasks
2. implement to spec
3. `/opsx:archive` — merge delta into `openspec/specs/`

`m5-paper-buddy` and `tab5-agentfarm-buddy` are not OpenSpec-governed.

## Cross-buddy sync

The `cc-bridge` wire protocol (`claude-code-buddy/tools/`) is shared by most
buddies by hand-copy. Changing the state mapping, permission JSON, or BLE
protocol in one buddy means checking all the others (and the daemon).

## Conventions

- Chinese in `ahakey` macOS code — match surrounding file language.
- Comments/user strings in several firmware files also in Chinese — match it.
- Don't push without user say-so. Commit + show diff first.
- One commit per logical change.
- No root-level CI or lint commands. Each subproject manages its own
  (claude-code-buddy has `make test` / `make test-py` / `make test-cpp`).
