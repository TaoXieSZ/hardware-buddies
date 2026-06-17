# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this repo is

`AhakeyAI/desktop` — the official desktop companion for the **AhaKey-X1** keyboard (Vibecoding Keyboard). Two independent platform trees with different runtimes; they share no code:

- `platforms/macos/` — **active development**. Swift + SwiftUI, native CoreBluetooth. This is where almost all work happens.
- `platforms/windows/` — frozen baseline. Four imported Python/.NET subprojects (`Capswriter-master`, `wxcloudrun-flask-main`, `vibe_code_config_tool-master`, `BLE_tcp_bridge_for_vibe_code-master`). Do not refactor unless explicitly asked.

The app does two things: (1) configure the keyboard over BLE (key mapping, OLED art, LED), and (2) reflect IDE/agent state on the keyboard's LED light bar and gate tool approvals via the keyboard's physical toggle switch.

## macOS build & run (the only actively-built target)

All commands run from `platforms/macos/`. It's a SwiftPM package (`Package.swift`, swift-tools-version 5.9, deploys macOS 12+).

```bash
cd platforms/macos
swift build                 # build both executables
swift build -c release
swift run AhaKeyConfig       # run the GUI app directly
```

Two products in one package:
- **`AhaKeyConfig`** — the SwiftUI GUI app. Target `path: "Sources"` and **excludes `Agent/`**. Embeds an Info.plist into the binary via linker `-sectcreate __TEXT __info_plist` so TCC (Bluetooth/Speech permissions) recognizes it; Debug and Release use different plists (`Packaging/AhaKeyConfig-EmbeddedInfo*.plist`).
- **`ahakeyconfig-agent`** — a separate CLI executable from `Sources/Agent/`. Built as its own target because it must run headless as a LaunchAgent and as a hook subprocess.

### ⚠️ Build-script drift (verify before trusting docs)

`make build` / `make install` call `./scripts/build.sh`, and `.github/workflows/release.yml` calls `scripts/build.sh` + `scripts/package_dmg.sh` and references `platforms/macos/client/`. **None of those scripts exist in the tree, and there is no `platforms/macos/client/` directory** — the actual code is at `platforms/macos/`. The README and `docs/` also still point at the old `platforms/macos/client/` layout and a `platforms/macos/README.md` that is absent. Treat README/`docs/`/CI paths as stale; `swift build` is the source of truth for building. Only `build.local.env` (codesigning hints) is intentionally gitignored.

## Architecture (the part that spans files)

### Agent: one binary, two modes (`Sources/Agent/main.swift`)
- **Daemon mode** (`ahakeyconfig-agent --socket /tmp/ahakey.sock`): a long-lived LaunchAgent that holds the BLE connection (`AhaKeyAgent.swift`) and listens on a Unix socket. It exists so LED state can keep updating *after the GUI app is closed*. `AhaKeyAgent` is a deliberately minimal, self-contained BLE client — it does **not** share code with `Sources/BLE/` (it inlines its own status parser, kept structurally in sync with `AhaKeyProtocol.swift`).
- **Hook mode** (`ahakeyconfig-agent hook <EventName>`): IDEs `exec` this per event. It connects to the daemon over the socket, pushes the LED state, and for approval events prints the decision JSON the IDE expects. See `HookClient.swift`.

### IDE hook integration — the core feature (`Sources/Agent/HookClient.swift`)
The keyboard's physical **toggle switch (拨杆)** is the auth gate: `switchState == 0` → **auto** (approve tools), non-zero → **manual** (ask). The daemon reads the switch over BLE and returns it on the socket reply; `HookClient` maps it to each IDE's protocol:
- **Claude Code** `PermissionRequest` → emits `{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"|"ask"}}}`. Other Claude events (`PreToolUse`, `PostToolUse`, `SessionStart`, `Stop`, etc.) just push a fire-and-forget LED state value.
- **Cursor** `preToolUse`/`beforeShellExecution`/`beforeMCPExecution` → `{"permission":"allow"|"deny"|"ask"}`. In auto mode it also syncs `~/.cursor/cli-config.json` and `~/.cursor/permissions.json` (`terminalAllowlist`) and restores a snapshot otherwise — see `CursorCliLeverSync.swift` / `CursorPermissionsJsonLeverSync.swift`.
- **Codex** (`CodexPermissionRequest`, `Codex*` events) → auto mode outputs `behavior=allow`; manual mode hands control back to Codex (Codex has no `ask`).

`eventMap` in `HookClient.swift` is the single registry of which event name maps to which behavior — start there when adding/adjusting IDE support.

### Hook installation (`Sources/Utilities/AgentManager.swift`)
`AgentManager.install()` (1) writes the LaunchAgent plist to `~/Library/LaunchAgents`, (2) optionally `launchctl load`s it (only when the daemon is chosen to own the BLE connection — avoids fighting the GUI for the single GATT connection), and (3) writes hook entries pointing at the agent binary into `~/.claude/settings.json`, `~/.cursor/hooks.json`, and `~/.codex/config.toml`. `uninstall()` reverses all three and cleans the socket. BLE-ownership is arbitrated by `bluetoothConnectionOwner` (GUI vs daemon) — only one process can hold the keyboard at a time.

### BLE protocol (`Sources/BLE/AhaKeyProtocol.swift`)
Frames are `AA BB <cmd> <payload> CC DD`. Service UUID `0x7340`, command char `0x7343`, notify char `0x7344`; device name prefix `"vibe code"`. Key commands: `0x73` update custom key, `0x82` update OLED picture, `0x90` update IDE state → LED color (this is what hook events drive). `AhaKeyBLEManager.swift` is the GUI-side manager; the agent has its own slimmer copy.

### GUI structure (`Sources/Views/`, `Sources/Models/`)
`AhaKeyConfigApp.swift` is the entry point. `ContentView` → `AhaKeyStudioView` hosts the tabs: `KeyMappingView` (4-key × 3-mode mapping), `OLEDManagerView` (push bitmap art, encoded by `Utilities/OLEDFrameEncoder.swift` / `OLEDManagerView`), `DeviceInfoView`. Voice/speech input lives in `Utilities/NativeSpeechTranscriptionService.swift`, `VoiceRelayService.swift`, `VoiceStatusHUDController.swift`. `Models/AhaKeyStudioModels.swift` holds the shared model layer.

> Note: the README advertises a large "Voice Agent" subsystem (`VoiceAgentOrchestrator`, `LLMClient`, `FeishuContactBook`, `AhaKeyDesignSystem`, etc.). Those types are **not in `Sources/`** — the README is aspirational/ahead of the committed code. Trust the file tree over the README.

## Conventions

- Code comments and user-facing strings are in **Chinese**; match that when editing macOS Swift files.
- The agent target intentionally duplicates small bits of BLE logic rather than sharing a module — keep the two parsers structurally in sync by hand if you touch the wire format.
- Build artifacts (`.app`, `.dmg`, `.exe`, `.msi`) are never committed; releases ship only via GitHub Releases.
