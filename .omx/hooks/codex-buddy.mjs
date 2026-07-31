// codex-buddy — oh-my-codex (OMX) hook plugin.
//
// The SUPPORTED way to feed Codex session state to codex-bridge on a machine
// where oh-my-codex owns Codex's hook dispatch. OMX only runs hooks it manages
// (its native codex-native-hook.js) plus plugins discovered fresh from
// <cwd>/.omx/hooks/*.mjs on every event — so this file needs NO hooks.json edit
// and NO trust-hash forging. openspec change cardputer-codex-sessions.
//
// It forwards each event to the codex-bridge daemon socket in the same
// Claude-Code-shaped form bridge.py's apply_event() reads. Fire-and-forget with
// a short timeout so it never slows Codex.
//
// Install: copy (or symlink) into <project>/.omx/hooks/codex-buddy.mjs for each
// project where you run Codex (OMX plugin dirs are per-cwd). See README.

import { createConnection } from 'net';

const SOCKET = process.env.CODEX_BRIDGE_SOCKET || '/tmp/codex-bridge.sock';

// OMX's own event taxonomy → Claude-Code hook_event_name. Only used as a
// fallback; when the original Codex payload is present we forward its
// hook_event_name verbatim (higher fidelity — includes UserPromptSubmit, which
// OMX's taxonomy has no direct equivalent for).
const OMX_EVENT_MAP = {
    'session-start': 'SessionStart',
    'pre-tool-use': 'PreToolUse',
    'post-tool-use': 'PostToolUse',
    'stop': 'Stop',
    'turn-complete': 'Stop',
    'session-end': 'SessionEnd',
    'needs-input': 'PermissionRequest',
    'run.blocked_on_user': 'PermissionRequest',
    'blocked': 'PermissionRequest',
};

export async function onHookEvent(event, _sdk) {
    const ctx = (event && event.context) || {};
    const payload = ctx.payload || {};

    // Prefer the exact Codex hook name carried in the sanitized payload; else
    // map OMX's normalized event name.
    const name = payload.hook_event_name || OMX_EVENT_MAP[event && event.event];
    if (!name) return;

    const cwd = ctx.cwd || ctx.project_path || payload.cwd || '';
    const msg = {
        hook_event_name: name,
        session_id: (event && event.session_id) || payload.session_id || 'codex',
    };
    if (cwd) msg.cwd = String(cwd);

    if (name === 'UserPromptSubmit') {
        const p = payload.prompt || payload.user_prompt || '';
        if (p) msg.prompt = String(p).slice(0, 200);
    } else if (name === 'PreToolUse' || name === 'PostToolUse' || name === 'PermissionRequest') {
        msg.tool_name = payload.tool_name || payload.tool || 'tool';
        const ti = payload.tool_input || {};
        const slim = {};
        if (ti.command) slim.command = String(ti.command).slice(0, 200);
        if (ti.description) slim.description = String(ti.description).slice(0, 120);
        if (ti.file_path) slim.file_path = String(ti.file_path).slice(0, 200);
        if (Object.keys(slim).length) msg.tool_input = slim;
    } else if (name === 'Stop') {
        const t = payload.last_assistant_message || payload.text || '';
        if (t) msg.last_assistant_message = String(t).slice(0, 200);
    }

    await send(JSON.stringify(msg) + '\n');
}

function send(line) {
    return new Promise((resolve) => {
        let done = false;
        const fin = () => { if (done) return; done = true; try { sock.end(); } catch (_) {} resolve(); };
        const sock = createConnection(SOCKET);
        sock.setTimeout(400, fin);
        sock.on('error', fin);
        sock.on('connect', () => { try { sock.write(line, fin); } catch (_) { fin(); } });
        setTimeout(fin, 500).unref();
    });
}
