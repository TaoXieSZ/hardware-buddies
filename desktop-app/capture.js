// capture.js — headless-ish Electron driver that renders each StackChan
// state to a PNG under desktop-app/screenshots/. Run with:
//   npx electron capture.js
const { app, BrowserWindow } = require('electron');
const path = require('path');
const fs = require('fs');

const SCALE = 3;
const W = 320 * SCALE;
const H = 240 * SCALE;

// Each shot maps state-machine code (0-4) + the field overrides applied
// directly to window.__stackchan.state via executeJavaScript.
const C = { SLEEP: 0, IDLE: 1, BUSY: 2, ATTN: 3, DONE: 4 };
const SHOTS = [
  { name: 'sleep',     s: { char: C.SLEEP, msg: 'waiting for prompt...',                    tool: '',
                            batteryPct: 87, contextPct: 0,  hudTokens: 0,     sessionMs: 0,      limit5h: 0,  limit7d: 0 } },
  { name: 'idle',      s: { char: C.IDLE,  msg: 'ready',                                    tool: '',
                            batteryPct: 87, contextPct: 12, hudTokens: 1200,  sessionMs: 45000,  limit5h: 3,  limit7d: 1 } },
  { name: 'busy',      s: { char: C.BUSY,  msg: 'running: mcp__plugin_context-mode__ctx_search', tool: 'CtxSearch',
                            batteryPct: 72, contextPct: 38, hudTokens: 18400, sessionMs: 420000, limit5h: 22, limit7d: 6 } },
  { name: 'attention', s: { char: C.ATTN,  msg: 'approve Bash(rm -rf /tmp/x)?',             tool: 'Bash',
                            batteryPct: 64, contextPct: 55, hudTokens: 31200, sessionMs: 720000, limit5h: 38, limit7d: 11 } },
  { name: 'done',      s: { char: C.DONE,  msg: 'done: 3 files updated',                    tool: '',
                            batteryPct: 58, contextPct: 60, hudTokens: 35200, sessionMs: 840000, limit5h: 42, limit7d: 13 } },
];

const INDEX_URL = `file://${path.join(__dirname, 'renderer', 'index.html')}?capture=1`;

async function capture(shot) {
  const win = new BrowserWindow({
    width: W, height: H, useContentSize: true,
    show: true, frame: false, backgroundColor: '#000000',
    webPreferences: { contextIsolation: true, nodeIntegration: false },
  });
  await win.loadURL(INDEX_URL);
  // Push the shot's state directly into the renderer, then await the GIF
  // swap + a paint tick.
  await win.webContents.executeJavaScript(`
    (() => {
      Object.assign(window.__stackchan.state, ${JSON.stringify(shot.s)});
      window.__stackchan.repaint();
    })();
    new Promise(r => {
      const t = setInterval(() => {
        const img = document.getElementById('char');
        if (img && img.complete && img.naturalWidth > 0) { clearInterval(t); r(); }
      }, 50);
      setTimeout(() => { clearInterval(t); r(); }, 4000);
    });
  `);
  // Extra paint frame
  await new Promise(r => setTimeout(r, 300));
  const img = await win.webContents.capturePage();
  const out = path.join(__dirname, '..', 'docs', 'stackchan-states', `${shot.name}.png`);
  fs.writeFileSync(out, img.toPNG());
  console.log(`wrote ${out} (${img.getSize().width}x${img.getSize().height})`);
  win.hide();
  await new Promise(r => setTimeout(r, 300));
  win.close();
  await new Promise(r => setTimeout(r, 300));
}

app.whenReady().then(async () => {
  fs.mkdirSync(path.join(__dirname, '..', 'docs', 'stackchan-states'), { recursive: true });
  for (const s of SHOTS) {
    try { await capture(s); }
    catch (e) { console.error(`FAILED ${s.name}:`, e && e.message || e); }
  }
  app.quit();
});

// Prevent app from auto-quitting when the per-shot window closes.
app.on('window-all-closed', (e) => { /* swallow */ });
