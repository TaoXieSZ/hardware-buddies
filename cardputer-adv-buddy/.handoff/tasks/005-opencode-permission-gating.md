# Task Spec: OpenCode permission gating on the cardputer

- **ID**: 005-opencode-permission-gating
- **Workdir**: /Users/txie/OpenSourceProjects/claude-desktop-buddy
- **Executor**: opencode
- **Created**: 2026-07-06 12:10
- **Iteration**: 0

## Goal

When an OpenCode session asks for a tool permission (bash / edit / write / …), the
approval prompt must appear on the cardputer device (tagged "oc"), let the user
decide with the device keys, and route that decision back to OpenCode — exactly
like Claude and Cursor permissions already do. Today OpenCode permissions never
reach the cardputer, so nothing pops up.

Deliverable: ONE new OpenCode plugin (JS/ESM). The cc-bridge daemon and the
cardputer firmware need NO changes — they already handle external-agent
permissions via the `wait_permission` socket protocol and render an `agent` tag.

## Background / Context (all locked — do not re-litigate)

**Why a plugin (not the python bridge):** OpenCode surfaces permission requests
ONLY through its in-process plugin SDK `event` hook (`permission.asked`). The
existing python `opencode-bridge` (polls cmux for the session list) cannot see
these events. So permission gating must live in a JS plugin.

**The full round-trip we are wiring:**
1. OpenCode fires `permission.asked` → our plugin.
2. Plugin sends a `wait_permission` request to cc-bridge's unix socket.
3. cc-bridge (single BLE owner) shows the prompt on the cardputer, tagged "oc",
   and BLOCKS the socket up to `timeout`, then writes the decision back on the
   SAME connection.
4. Plugin maps the decision and POSTs it to OpenCode's own REST API.

**cc-bridge `wait_permission` protocol** — confirmed at
`/Users/txie/OpenSourceProjects/claude-desktop-buddy/tools/buddy_core/core.py:1320`
(`_handle_wait_permission`). This is the SAME path cursor-bridge uses. Request
(one JSON line to the socket, then read one JSON line back, then the connection
closes):
- send: `{"action":"wait_permission","id":<reqId>,"tool":<toolName>,"hint":<str≤120>,"session_id":<sid>,"agent":"opencode","timeout":<seconds>}`
- receive: `{"decision":"<once|deny|always|ask>"}` (`ask` = user didn't decide in
  time / no capable device → OpenCode should fall back to its own TUI prompt).
- The `agent:"opencode"` field makes the device tag the prompt "oc" AND makes
  cc-bridge skip its own session bookkeeping (correct — this is an external agent).
- The cardputer IS treated as permission-capable (its prefix `Claude-7AFD` lacks
  "SC", so cc-bridge's `has_stick` check passes).

**Firmware decision strings** (confirmed
`/Users/txie/OpenSourceProjects/hardware-buddies/cardputer-adv-buddy/src/main.cpp:167`
and `src/cclink.cpp:177`): the device sends back `once` / `deny` / `always`. The
device auto-hides the prompt after 30 s (`APPROVAL_SAFETY_MS`), so a requested
`timeout` of ~28 s is the practical max.

**OpenCode permission API** (confirmed against the SDK + two working reference
plugins, OpenCode v1.17.13):
- `permission.asked` event `properties`: `{ id, permission, patterns?, metadata?, always?, sessionID? }`
  (`id` is the requestID; some versions omit `sessionID` — fall back to the last
  seen session id).
- Reply: `POST ${serverUrl.origin}/permission/${id}/reply` with body
  `{ "reply": "once" | "always" | "reject" }`.

**Decision mapping (device → OpenCode reply):**
| device decision | OpenCode reply | action |
|---|---|---|
| `once`   | `once`   | POST reply |
| `always` | `always` | POST reply |
| `deny`   | `reject` | POST reply |
| `ask` / null / timeout | — | do NOT POST (let OpenCode show its own prompt) |

## Reference implementations to COPY FROM (do not reinvent)

- **Socket helper — copy verbatim:** `sendAndWaitResponse(json, timeoutMs)` in
  `/Users/txie/.config/opencode/plugins/vibe-island.js` (the function that opens a
  unix socket, writes the JSON, waits for the reply on the same connection, parses
  it on `end`). Reuse it as-is, only changing the socket path to cc-bridge's.
- **Plugin shape + hint derivation + reply POST:** same file, the
  `dispatchMapped` PermissionRequest branch and the `permission.asked` mapping
  (bash → `patterns.join(" && ")`; edit/write → `patterns[0]`).
- **Alternate reply-via-SDK-client fallback** (optional, if fetch to serverUrl
  fails): `/Users/txie/OpenSourceProjects/clawd-on-desk/hooks/opencode-plugin/index.mjs`
  around line 491 (`_ctxClient._client.post({url, body})`).

## Files in Scope

| File | Change |
|------|--------|
| /Users/txie/OpenSourceProjects/claude-desktop-buddy/tools/opencode-bridge/cardputer-permission.mjs | create: the new plugin |
| /Users/txie/.config/opencode/opencode.json | modify: append the plugin's absolute path to the `plugin` array |

## Plan

1. Create `cardputer-permission.mjs` (ESM, `export default async (ctx) => ({...})`).
   - `const SOCKET = process.env.CC_BRIDGE_SOCKET || "/tmp/cc-bridge.sock";`
   - Copy `sendAndWaitResponse` from vibe-island.js verbatim; point it at `SOCKET`.
   - Track `lastSessionId` from `session.created` / `session.updated` /
     `message.updated` events (for the `sessionID` fallback).
   - Return `{ event: async ({ event }) => { … } }`.
2. In the `event` handler, act only on `event.type === "permission.asked"`:
   - `const p = event.properties || {};` require `p.id` (else return).
   - `tool = p.permission || "tool"`.
   - `hint`: if `p.permission === "bash"` → `(p.patterns||[]).join(" && ")`;
     if `edit`/`write` → `(p.patterns||[])[0] || ""`; else → `""`. Truncate to 120.
   - `sid = p.sessionID || lastSessionId || "anon"`.
   - Build the `wait_permission` payload above with `agent:"opencode"`,
     `timeout: 28`.
   - `const resp = await sendAndWaitResponse(payload, 30000);`
   - Map `resp?.decision` per the table. If it maps to a reply, POST it.
3. Reply POST: `fetch(`${origin}/permission/${encodeURIComponent(p.id)}/reply`,
   { method:"POST", headers:{"Content-Type":"application/json"},
   body: JSON.stringify({ reply }) })` where `origin = ctx.serverUrl?.origin ||
   "http://127.0.0.1:4096"`. Wrap in try/catch; on failure optionally fall back to
   `ctx.client?._client?.post({url:`/permission/${p.id}/reply`, body:{reply}})`.
4. Register: append the absolute plugin path to the `plugin` array in
   `/Users/txie/.config/opencode/opencode.json` (keep the existing clawd-on-desk
   entry; valid JSON, no trailing comma).

## Constraints

- Do NOT edit cc-bridge, buddy_core, the python opencode-bridge, or any firmware —
  they are already correct. Only the two files in scope.
- Do NOT reformat unrelated JSON in opencode.json — append one array element.
- Never block OpenCode's event loop: all socket/fetch work is async, fire the
  handler without `await`-ing it from the top-level event tick if that risks
  stalling (mirror vibe-island's `.then(...)` style).
- Scope is PERMISSION only. Do NOT implement `question.asked` handling (future).
- File is `.mjs` (always ESM) so no package.json is needed.

## Acceptance Criteria

- [ ] `cardputer-permission.mjs` exists and passes `node --check`.
- [ ] It sends a `wait_permission` JSON with `action`, `id`, `tool`, `hint`,
      `session_id`, `agent:"opencode"`, `timeout` to `CC_BRIDGE_SOCKET`.
- [ ] Decision mapping is exactly: once→once, always→always, deny→reject,
      ask/null→no reply.
- [ ] Reply is `POST /permission/{id}/reply` with `{reply}`.
- [ ] opencode.json is valid JSON and lists the plugin's absolute path.

## Verification

```bash
# 1. Syntax
node --check /Users/txie/OpenSourceProjects/claude-desktop-buddy/tools/opencode-bridge/cardputer-permission.mjs

# 2. opencode.json is valid JSON and includes the plugin
python3 -c "import json,sys; d=json.load(open('/Users/txie/.config/opencode/opencode.json')); p=d.get('plugin',[]); print('OK' if any('cardputer-permission' in x for x in p) else 'MISSING'); sys.exit(0 if any('cardputer-permission' in x for x in p) else 1)"

# 3. End-to-end against a FAKE cc-bridge socket (no device needed): start a node
#    unix-socket server that accepts one line and replies {"decision":"once"}, set
#    CC_BRIDGE_SOCKET to it, import the plugin, invoke its event handler with a
#    fake permission.asked, and assert a reply POST fires with reply="once".
#    Write this harness to /tmp/oc_perm_test.mjs and run: node /tmp/oc_perm_test.mjs
#    Expected stdout: "PASS reply=once"
```

Final device verification (MANUAL, by the user, not the executor): run an OpenCode
session, trigger a bash permission, confirm the cardputer shows an "oc"-tagged
approval, press space (once) / esc (deny) / a (always), and confirm OpenCode
proceeds/aborts accordingly.

## Notes / known coordination point

Two other plugins also answer OpenCode permissions on this machine:
`~/.config/opencode/plugins/vibe-island.js` and the clawd-on-desk
`opencode-plugin`. They only actually reply when their own product (Vibe Island /
Clawd desktop) is running and answers. If you observe a permission being
auto-answered by another responder before the cardputer decision arrives, the
user must disable the competing plugin — do NOT modify those files as part of this
task; just report it.
