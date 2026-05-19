// mock.js — drives the same state mutations the firmware applies via
// applyJsonLine(). Cycles through SLEEP→IDLE→BUSY→ATTN→DONE so the UX
// is visible end-to-end without the cc-bridge daemon attached.

const { state, repaint } = window.__stackchan;
const start = Date.now();
const C = { SLEEP: 0, IDLE: 1, BUSY: 2, ATTN: 3, DONE: 4 };

// Frozen-state mode for screenshot capture:
//   index.html?state=busy&msg=...&tool=...&battery=80&ctx=42
// When `state` query param is present we skip the cycling timer entirely.
const params = new URLSearchParams(location.search);
// `?capture=1` → suppress the cycling timer; capture.js will inject state.
if (params.has('capture')) {
  window.__ready = true;
} else if (params.has('state')) {
  const name = params.get('state').toUpperCase();
  if (name in C) state.char = C[name];
  if (params.has('msg'))     state.msg = params.get('msg');
  if (params.has('tool'))    state.tool = params.get('tool');
  if (params.has('battery')) state.batteryPct = +params.get('battery');
  if (params.has('ctx'))     state.contextPct = +params.get('ctx');
  if (params.has('model'))   state.model = params.get('model');
  if (params.has('tokens'))  state.hudTokens = +params.get('tokens');
  if (params.has('session')) state.sessionMs = +params.get('session');
  if (params.has('l5h'))     state.limit5h = +params.get('l5h');
  if (params.has('l7d'))     state.limit7d = +params.get('l7d');
  repaint();
  // Signal capture script that the static frame is rendered.
  setTimeout(() => { window.__ready = true; }, 600);
} else {

const script = [
  { at:    0, char: C.SLEEP, msg: 'waiting for prompt...', tool: '' },
  { at: 3000, char: C.IDLE,  msg: 'ready', tool: '' },
  { at: 6000, char: C.BUSY,  msg: 'thinking about the implementation', tool: 'Read' },
  { at:10000, char: C.BUSY,  msg: 'running: mcp__plugin_context-mode__ctx_search', tool: 'CtxSearch' },
  { at:14000, char: C.ATTN,  msg: 'approve Bash(rm -rf /tmp/x)?', tool: 'Bash' },
  { at:18000, char: C.DONE,  msg: 'done: 3 files updated', tool: '' },
  { at:22000, char: C.IDLE,  msg: 'ready', tool: '' },
];

setInterval(() => {
  const now = Date.now() - start;
  // session timer (drives HUD duration row)
  state.sessionMs = now;
  // fake-rising token count
  state.hudTokens = Math.floor(now / 80);
  state.contextPct = Math.min(95, Math.floor(now / 1200));
  state.limit5h = Math.min(80, Math.floor(now / 1500));
  state.limit7d = Math.min(40, Math.floor(now / 3000));
  // slow battery drain
  state.batteryPct = Math.max(10, 87 - Math.floor(now / 30000));

  // apply newest scripted entry whose `at` has passed; loop after the last
  const t = now % (script[script.length - 1].at + 4000);
  let cur = script[0];
  for (const s of script) if (t >= s.at) cur = s;
  state.char = cur.char;
  state.msg  = cur.msg;
  state.tool = cur.tool;
}, 100);
}
