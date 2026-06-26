# codex-buddy OMX plugin

`codex-buddy.mjs` is an [oh-my-codex](https://www.npmjs.com/package/oh-my-codex)
(OMX) **hook plugin** that feeds Codex session state to `codex-bridge`. It is the
SUPPORTED way to drive the cardputer from Codex on a machine where OMX owns
Codex's hook dispatch. openspec change `cardputer-codex-sessions`.

## Why a plugin (and not a hooks.json entry)

On a machine running oh-my-codex, OMX owns Codex hook trust: its trust config
(`~/.codex/config.toml [hooks.state]`) only trusts OMX's own native hook
(`codex-native-hook.js`). A hand-added entry in `~/.codex/hooks.json` is **never
invoked** by Codex — verified empirically (a `codex exec` with the raw hook +
`--dangerously-bypass-hook-trust` produced zero hook calls).

OMX's native hook (which IS trusted and always runs) instead fans every event
out to plugins it discovers — fresh on every event — from `<cwd>/.omx/hooks/*.mjs`.
That's the clean, no-trust-forging, no-hooks.json-edit extension point. A plugin
exports `onHookEvent(event, sdk)`; OMX passes a normalized envelope whose
`context.payload` carries the original Codex hook payload (`hook_event_name`,
`tool_name`, `tool_input`, `prompt`, …) and `context.cwd` the working directory.

This plugin forwards each event to the codex-bridge daemon socket in the same
Claude-Code-shaped form `bridge.py`'s `apply_event()` reads — preferring the
exact `payload.hook_event_name` when present (highest fidelity, includes
`UserPromptSubmit`), falling back to OMX's event taxonomy.

## Install (per project — OMX plugin dirs are per-cwd)

OMX discovers plugins from `<cwd>/.omx/hooks/`, so drop (or symlink) this file
into each project where you run Codex:

```bash
mkdir -p <project>/.omx/hooks
cp tools/codex-bridge/omx-plugin/codex-buddy.mjs <project>/.omx/hooks/
# verify OMX sees it:
cd <project> && omx hooks status     # → Discovered plugins: 1 - codex-buddy.mjs
omx hooks test                       # dispatches a synthetic event to the plugin
```

`codex-bridge` must be running (it owns the cardputer BLE link via cc-bridge).
Plugins are enabled by default (`OMX_HOOK_PLUGINS=0` to disable).

## Known limits

- **Per-cwd**: OMX has no global plugin dir; the plugin only loads for Codex
  sessions in dirs where it's present.
- **`codex exec` does not dispatch plugins** — only interactive Codex sessions
  do. Use a real interactive session to see it drive the device.
- OMX's own event taxonomy has no direct `UserPromptSubmit`/"thinking" event;
  fidelity for that state relies on the original payload being present in
  `context.payload`.
