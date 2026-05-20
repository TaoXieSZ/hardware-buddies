// mock.js — drives the same state mutations the firmware applies via
// applyJsonLine(). Cycles through SLEEP→IDLE→BUSY→ATTN→DONE so the UX
// is visible end-to-end without the cc-bridge daemon attached.
//
// `index.html?capture=1` suppresses the cycling timer; capture.js then
// injects a frozen state per screenshot via executeJavaScript.

const { state } = window.__stackchan;
const C = { SLEEP: 0, IDLE: 1, BUSY: 2, ATTN: 3, DONE: 4 };

if (new URLSearchParams(location.search).has('capture')) {
  window.__ready = true;
} else {
  const start = Date.now();
  const script = [
    { at:    0, char: C.SLEEP, msg: 'waiting for prompt...', tool: '' },
    { at: 3000, char: C.IDLE,  msg: 'ready', tool: '' },
    { at: 6000, char: C.BUSY,  msg: 'thinking about the implementation', tool: 'Read' },
    { at:10000, char: C.BUSY,  msg: 'running: mcp__plugin_context-mode__ctx_search', tool: 'CtxSearch' },
    { at:14000, char: C.ATTN,  msg: 'approve Bash(rm -rf /tmp/x)?', tool: 'Bash' },
    { at:18000, char: C.DONE,  msg: 'done: 3 files updated', tool: '' },
    { at:22000, char: C.IDLE,  msg: 'ready', tool: '' },
  ];
  const loopMs = script[script.length - 1].at + 4000;

  setInterval(() => {
    const now = Date.now() - start;
    state.sessionMs = now;
    state.hudTokens = Math.floor(now / 80);
    state.contextPct = Math.min(95, Math.floor(now / 1200));
    state.limit5h = Math.min(80, Math.floor(now / 1500));
    state.limit7d = Math.min(40, Math.floor(now / 3000));
    state.batteryPct = Math.max(10, 87 - Math.floor(now / 30000));

    // apply the newest scripted entry whose `at` has passed; loop at the end
    const t = now % loopMs;
    let cur = script[0];
    for (const s of script) if (t >= s.at) cur = s;
    state.char = cur.char;
    state.msg  = cur.msg;
    state.tool = cur.tool;
  }, 100);
}
