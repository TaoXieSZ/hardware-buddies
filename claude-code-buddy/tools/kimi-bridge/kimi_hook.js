#!/usr/bin/env node
//
// Kimi Code CLI hook → kimi-bridge daemon shim.
//
// Kimi fires this for each registered hook event. Its hook payload is ALREADY
// Claude-Code-shaped: hook_event_name is one of SessionStart / UserPromptSubmit
// / PreToolUse / PostToolUse / Stop / PermissionRequest / SessionEnd /
// PostCompact, and the fields (session_id, cwd, tool_name, tool_input, prompt)
// are exactly what bridge.py's apply_event() reads. So this is a near-identity
// forwarder — we whitelist the fields we care about and write one JSON line to
// the bridge's Unix socket, then exit immediately.
//
// Kimi-specific extras vs. codex_hook.js:
//   - Interrupt / StopFailure: passed through by name; bridge.py treats them
//     as Stop-equivalents (turn aborted → idle).
//   - PostToolUseFailure: mapped to hook_event_name="PostToolUse" with
//     failure:true (+ error), so the bridge's existing PostToolUse failure
//     branch (msg="failed: <tool>", "!fail" entry) is reused unchanged.
//
// Any failure (daemon down, socket missing, bad JSON, etc.) exits 0 silently —
// the hook MUST NOT block Kimi on a side channel that may be offline.
//
// Permission echo (button-on-device allow/deny) is NOT wired here — the
// PermissionRequest event is forwarded fire-and-forget, so the device still
// SHOWS the waiting state; it just doesn't gate Kimi yet.
//
// Wired up by tools/kimi-bridge/install.sh into ~/.kimi-code/config.toml
// ([[hooks]] TOML blocks).

'use strict';

const fs  = require('fs');
const net = require('net');

const SOCKET_PATH = process.env.KIMI_BRIDGE_SOCKET || '/tmp/kimi-bridge.sock';
const TIMEOUT_MS  = 500; // never slow Kimi down for our side channel

const KNOWN = new Set([
    'SessionStart', 'SessionEnd', 'UserPromptSubmit', 'Stop',
    'PreToolUse', 'PostToolUse', 'PermissionRequest', 'PostCompact',
    'Interrupt', 'StopFailure', 'PostToolUseFailure',
]);

// ─── Kimi → Claude-Code-shaped event (near-identity) ───────────────────
function translate(ev) {
    let name = ev.hook_event_name || ev.event || '';
    if (!KNOWN.has(name)) return null;   // drop unknown events silently

    const sid = ev.session_id || ev.sessionId || ev.conversation_id || 'anon';
    const cwd = ev.cwd || ev.workspace || '';
    const out = { hook_event_name: name, session_id: sid };
    if (cwd) out.cwd = String(cwd);

    if (name === 'UserPromptSubmit') {
        const prompt = ev.prompt || ev.user_prompt || ev.text || '';
        if (prompt) out.prompt = String(prompt).slice(0, 200);
    } else if (name === 'Stop') {
        const txt = ev.last_assistant_message || ev.text || '';
        if (txt) out.last_assistant_message = String(txt).slice(0, 200);
    } else if (name === 'PreToolUse' || name === 'PostToolUse'
               || name === 'PostToolUseFailure' || name === 'PermissionRequest') {
        out.tool_name = ev.tool_name || ev.tool || 'tool';
        const ti = ev.tool_input || {};
        // Keep only the small descriptive bits apply_event surfaces.
        const slim = {};
        if (ti.command)     slim.command     = String(ti.command).slice(0, 200);
        if (ti.description) slim.description = String(ti.description).slice(0, 120);
        if (ti.file_path)   slim.file_path   = String(ti.file_path).slice(0, 200);
        if (Object.keys(slim).length) out.tool_input = slim;
        if (ev.tool_use_id) out.tool_use_id = ev.tool_use_id;
        if (name === 'PostToolUseFailure') {
            // Reuse the bridge's PostToolUse failure branch unchanged.
            out.hook_event_name = 'PostToolUse';
            out.failure = true;
            const err = ev.error || ev.message || '';
            if (err) out.error = String(err).slice(0, 120);
        }
    }
    // Interrupt / StopFailure / SessionStart / SessionEnd / PostCompact:
    // name + session_id + cwd is all the bridge needs.
    return out;
}

// ─── main ──────────────────────────────────────────────────────────────
function main() {
    let raw;
    try {
        raw = fs.readFileSync(0, 'utf8'); // stdin
    } catch (_) {
        process.exit(0);
    }
    if (!raw) process.exit(0);

    let ev;
    try {
        ev = JSON.parse(raw);
    } catch (_) {
        process.exit(0);
    }

    if (process.env.KIMI_HOOK_DEBUG === '1') {
        try {
            fs.appendFileSync(
                '/tmp/kimi-hook-debug.jsonl',
                JSON.stringify({ ts: Date.now(), ev }) + '\n'
            );
        } catch (_) {}
    }

    const translated = translate(ev);
    if (!translated) process.exit(0);

    const payload = JSON.stringify(translated) + '\n';

    const sock = net.createConnection(SOCKET_PATH);
    let done = false;
    const finish = () => {
        if (done) return;
        done = true;
        try { sock.end(); } catch (_) {}
        process.exit(0);
    };
    sock.setTimeout(TIMEOUT_MS, finish);
    sock.on('error',   finish);
    sock.on('connect', () => {
        try {
            sock.write(payload, finish);
        } catch (_) {
            finish();
        }
    });

    setTimeout(finish, TIMEOUT_MS + 100).unref();
}

main();
