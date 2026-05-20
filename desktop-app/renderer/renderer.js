// renderer.js — pixel-for-pixel port of src/stackchan/character_chan.cpp.
// All geometry and colors mirror the firmware exactly; ctx.scale(3) puts
// the firmware's 320×240 logical canvas into a 960×720 macOS window.

const SCALE = 3;
const canvas = document.getElementById('lcd');
const ctx = canvas.getContext('2d');
ctx.scale(SCALE, SCALE);

// ---- Palette (RGB565 → hex from character_chan.cpp) ----------------------
const SCREEN_BG     = '#000000';
const CARD_FILL     = '#F7F3DF';
const CARD_BORDER   = '#AAA69D';
const CARD_SHADOW   = '#BDAEA0';
const CARD_DIV      = '#E8E2D6';
const CARD_TEXT     = '#794F27';
const CARD_TEXT_SEC = '#9F927D';
const CARD_BW = 2;
const CARD_SHADOW_DY = 4;

// ---- Geometry (CHAR_BOX, BUBBLE, TOOL_CHIP, HUD) -------------------------
const HUD_Y = 2, HUD_H = 50;
const CHAR_BOX_X = 4, CHAR_BOX_Y = 58, CHAR_BOX_W = 176, CHAR_BOX_H = 162;
const BUBBLE_X = 184, BUBBLE_Y = 60, BUBBLE_W = 132, BUBBLE_H = 132;
const BUBBLE_R = 14, BUBBLE_PAD = 8, BUBBLE_HEAD_H = 20;
const TOOL_CHIP_X = 184, TOOL_CHIP_Y = 200, TOOL_CHIP_W = 132, TOOL_CHIP_H = 32;
const TOOL_CHIP_R = 16;

// ---- States --------------------------------------------------------------
const CHAR_SLEEP = 0, CHAR_IDLE = 1, CHAR_BUSY = 2, CHAR_ATTENTION = 3,
      CHAR_CELEBRATE = 4, CHAR_ERR = 5, CHAR_HEART = 6;
const STATE_GIF = {
  [CHAR_SLEEP]:     'sleep.gif',
  [CHAR_IDLE]:      'idle.gif',
  [CHAR_BUSY]:      'busy_0.gif',
  [CHAR_ATTENTION]: 'attention.gif',
  [CHAR_CELEBRATE]: 'celebrate.gif',
  [CHAR_ERR]:       'dizzy.gif',
  [CHAR_HEART]:     'heart.gif',
};
function accentForState(s) {
  switch (s) {
    case CHAR_ATTENTION: return '#E05A5A';
    case CHAR_BUSY:      return '#F5C31C';
    case CHAR_IDLE:      return '#19C8B9';
    case CHAR_CELEBRATE: return '#6FBA2C';
    case CHAR_SLEEP:     return '#9A835A';
    default:             return '#9A835A';
  }
}
function headerTextForState(s) {
  return s === CHAR_BUSY ? CARD_TEXT : '#FFFFFF';
}
function labelForState(s) {
  return ['SLEEP', 'IDLE', 'BUSY', 'ATTN', 'DONE', 'ERR', '<3'][s] || '';
}

// ---- Live state ----------------------------------------------------------
const state = {
  char: CHAR_SLEEP,
  msg: 'waking up...',
  tool: '',
  contextPct: 0,
  batteryPct: 87,
  model: 'claude-opus-4-7',
  hudTokens: 0,
  sessionMs: 0,
  limit5h: 0,
  limit7d: 0,
};

// ---- ACNH card primitive — shadow + border + inset fill ------------------
function roundRect(x, y, w, h, r) {
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.arcTo(x + w, y,     x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x,     y + h, r);
  ctx.arcTo(x,     y + h, x,     y,     r);
  ctx.arcTo(x,     y,     x + w, y,     r);
  ctx.closePath();
}
function drawAcnhCard(x, y, w, h, r, fill) {
  ctx.fillStyle = SCREEN_BG;
  ctx.fillRect(x, y, w, h + CARD_SHADOW_DY);
  ctx.fillStyle = CARD_SHADOW; roundRect(x, y + CARD_SHADOW_DY, w, h, r); ctx.fill();
  ctx.fillStyle = CARD_BORDER; roundRect(x, y, w, h, r); ctx.fill();
  ctx.fillStyle = fill;
  roundRect(x + CARD_BW, y + CARD_BW, w - 2 * CARD_BW, h - 2 * CARD_BW, Math.max(0, r - CARD_BW));
  ctx.fill();
}

// ---- Token / duration formatters (firmware fmtTokens / fmtDuration) ------
function fmtTokens(t) {
  if (t >= 1_000_000) return `${Math.floor(t / 1_000_000)}.${Math.floor(t / 100_000) % 10}M`;
  if (t >= 1000)       return `${Math.floor(t / 1000)}.${Math.floor(t / 100) % 10}k`;
  return `${t}`;
}
function fmtDuration(ms) {
  const s = Math.floor(ms / 1000);
  if (s < 60)   return `${s}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m`;
  return `${Math.floor(s / 3600)}h${Math.floor((s % 3600) / 60)}m`;
}

// ---- Drawing primitives --------------------------------------------------
// No background param: the firmware uses opaque text bg, but here every
// card region is fully repainted before its text so a bg fill is moot.
function setText(font, color, align, baseline) {
  ctx.font = font;
  ctx.fillStyle = color;
  ctx.textAlign = align;
  ctx.textBaseline = baseline;
}

function drawHud() {
  const x = 4, w = 320 - 8;
  drawAcnhCard(x, HUD_Y, w, HUD_H, 16, CARD_FILL);

  const pad = CARD_BW + 10;
  const row1y = HUD_Y + 15;
  const row2y = HUD_Y + 35;

  // Row 1 left — model
  setText('bold 11px -apple-system, "Helvetica Neue", sans-serif', CARD_TEXT, 'left', 'middle');
  let model = state.model || '—';
  const modelMax = w / 2;
  while (ctx.measureText(model).width > modelMax && model.length > 1) model = model.slice(0, -1);
  ctx.fillText(model, x + pad, row1y);

  // Row 1 right — ctx %
  ctx.textAlign = 'right';
  ctx.fillText(`${state.contextPct}% ctx`, x + w - pad, row1y);

  // Row 2 left — duration · tokens
  ctx.textAlign = 'left';
  ctx.fillStyle = CARD_TEXT_SEC;
  ctx.fillText(`${fmtDuration(state.sessionMs)}  ${fmtTokens(state.hudTokens)} tok`, x + pad, row2y);

  // Row 2 right — limits
  ctx.textAlign = 'right';
  ctx.fillText(`5h ${state.limit5h}%  7d ${state.limit7d}%`, x + w - pad, row2y);
}

function drawBubble() {
  const s = state.char;
  const accent = accentForState(s);
  drawAcnhCard(BUBBLE_X, BUBBLE_Y, BUBBLE_W, BUBBLE_H, BUBBLE_R, CARD_FILL);

  // Header strip — rounded top, square bottom (clipped via overdraw)
  const sx = BUBBLE_X + CARD_BW;
  const sy = BUBBLE_Y + CARD_BW;
  const sw = BUBBLE_W - 2 * CARD_BW;
  const sr = BUBBLE_R - CARD_BW;
  ctx.save();
  ctx.beginPath();
  roundRect(sx, sy, sw, BUBBLE_HEAD_H, sr);
  ctx.clip();
  ctx.fillStyle = accent;
  ctx.fillRect(sx, sy, sw, BUBBLE_HEAD_H);
  ctx.restore();
  // Divider
  ctx.strokeStyle = CARD_DIV;
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(sx, sy + BUBBLE_HEAD_H + 0.5);
  ctx.lineTo(sx + sw, sy + BUBBLE_HEAD_H + 0.5);
  ctx.stroke();

  // Header label
  setText('bold 11px -apple-system, "Helvetica Neue", sans-serif',
    headerTextForState(s), 'left', 'middle');
  ctx.fillText(labelForState(s), sx + 8, sy + BUBBLE_HEAD_H / 2);

  // Body — word-wrapped msg
  setText('bold 10px -apple-system, "Helvetica Neue", sans-serif',
    CARD_TEXT, 'left', 'top');
  const bodyY = BUBBLE_Y + CARD_BW + BUBBLE_HEAD_H + BUBBLE_PAD;
  const bodyH = BUBBLE_H - CARD_BW - BUBBLE_HEAD_H - 2 * BUBBLE_PAD;
  const lineH = 14;
  const maxLines = Math.floor(bodyH / lineH);
  drawWrapped(state.msg, BUBBLE_X + BUBBLE_PAD, bodyY,
              BUBBLE_W - 2 * BUBBLE_PAD, lineH, maxLines);
}

function drawWrapped(text, x, y, maxW, lineH, maxLines) {
  if (!text || maxLines <= 0) return;
  let line = '', drawn = 0, cy = y;
  const flush = (s) => {
    ctx.fillText(s, x, cy);
    cy += lineH;
    drawn++;
  };
  for (let i = 0; i < text.length && drawn < maxLines; i++) {
    const c = text[i];
    const candidate = line + c;
    if (ctx.measureText(candidate).width > maxW) {
      // backtrack to last break char
      let b = line.length - 1;
      while (b > 0 && !' _-:.'.includes(line[b])) b--;
      if (b <= 0) { flush(line); line = c; }
      else        { flush(line.slice(0, b + 1)); line = line.slice(b + 1).replace(/^ +/, '') + c; }
      if (drawn >= maxLines) return;
    } else {
      line = candidate;
    }
  }
  if (line && drawn < maxLines) flush(line);
}

function drawToolChip() {
  const accent = accentForState(state.char);
  if (!state.tool) {
    ctx.fillStyle = SCREEN_BG;
    ctx.fillRect(TOOL_CHIP_X, TOOL_CHIP_Y, TOOL_CHIP_W, TOOL_CHIP_H + CARD_SHADOW_DY);
    return;
  }
  drawAcnhCard(TOOL_CHIP_X, TOOL_CHIP_Y, TOOL_CHIP_W, TOOL_CHIP_H, TOOL_CHIP_R, CARD_FILL);
  const dotCx = TOOL_CHIP_X + CARD_BW + 12;
  const dotCy = TOOL_CHIP_Y + TOOL_CHIP_H / 2;
  ctx.fillStyle = accent;
  ctx.beginPath(); ctx.arc(dotCx, dotCy, 4, 0, Math.PI * 2); ctx.fill();

  setText('bold 11px -apple-system, "Helvetica Neue", sans-serif',
    CARD_TEXT, 'left', 'middle');
  let label = state.tool.toUpperCase();
  const textX = dotCx + 10;
  const maxW = TOOL_CHIP_X + TOOL_CHIP_W - CARD_BW - 10 - textX;
  while (ctx.measureText(label).width > maxW && label.length > 1) {
    label = label.slice(0, -1);
    if (label.length >= 2) label = label.slice(0, -2) + '..';
  }
  ctx.fillText(label, textX, dotCy);
}

// ---- Zelda hearts (5 × 20%) ---------------------------------------------
const N_HEARTS = 5, HEART_W = 14, HEART_H = 12, HEART_GAP = 4;
function drawHeart(cx, cy, full) {
  const HEART_FULL = '#E05A5A', HEART_EMPTY = '#310000', HEART_BORDER = '#600000';
  // Single clean path so fill + stroke share the same outline (no blur halo).
  // 14×11 heart, anchored to (cx,cy) as the indent between the two lobes.
  ctx.beginPath();
  ctx.moveTo(cx, cy + 1);
  ctx.bezierCurveTo(cx, cy - 4, cx - 7, cy - 4, cx - 7, cy);
  ctx.bezierCurveTo(cx - 7, cy + 3, cx - 3, cy + 5, cx, cy + 8);
  ctx.bezierCurveTo(cx + 3, cy + 5, cx + 7, cy + 3, cx + 7, cy);
  ctx.bezierCurveTo(cx + 7, cy - 4, cx, cy - 4, cx, cy + 1);
  ctx.closePath();
  ctx.fillStyle = full ? HEART_FULL : HEART_EMPTY;
  ctx.fill();
  ctx.strokeStyle = HEART_BORDER;
  ctx.lineWidth = 0.7;
  ctx.stroke();
}
function drawHearts() {
  const stripY = CHAR_BOX_Y + CHAR_BOX_H + 2;
  const stripH = HEART_H + 2;
  ctx.fillStyle = SCREEN_BG;
  ctx.fillRect(0, stripY, 320, stripH);
  if (state.batteryPct < 0) return;
  const pct = Math.max(0, Math.min(100, state.batteryPct));
  const nFull = pct === 0 ? 0 : Math.min(N_HEARTS, Math.ceil(pct / 20));
  const rowW = N_HEARTS * HEART_W + (N_HEARTS - 1) * HEART_GAP;
  const startX = CHAR_BOX_X + (CHAR_BOX_W - rowW) / 2 + HEART_W / 2;
  const cy = stripY + HEART_H / 2;
  for (let i = 0; i < N_HEARTS; i++) {
    drawHeart(startX + i * (HEART_W + HEART_GAP), cy, i < nFull);
  }
}

// ---- Full frame paint ----------------------------------------------------
function repaint() {
  ctx.fillStyle = SCREEN_BG;
  ctx.fillRect(0, 0, 320, 240);
  drawHud();
  drawBubble();
  drawToolChip();
  drawHearts();
  updateCharacterGif();
  if (charLoadFailed) drawCharFallback();
}

// Character pack — single source of truth. Override with ?char=calico etc.
const CHARACTER = new URLSearchParams(location.search).get('char') || 'clawd';

let lastGif = '';
let charLoadFailed = false;
const charImg = document.getElementById('char');
charImg.addEventListener('error', () => {
  charLoadFailed = true;
  drawCharFallback();
});
charImg.addEventListener('load', () => { charLoadFailed = false; });

// Drawn into CHAR_BOX when the GIF asset can't load (wrong pack name,
// missing file) so the panel reads as "no character" instead of a
// silent black void.
function drawCharFallback() {
  ctx.fillStyle = SCREEN_BG;
  ctx.fillRect(CHAR_BOX_X, CHAR_BOX_Y, CHAR_BOX_W, CHAR_BOX_H);
  setText('bold 11px -apple-system, "Helvetica Neue", sans-serif',
    CARD_TEXT_SEC, 'center', 'middle');
  const cx = CHAR_BOX_X + CHAR_BOX_W / 2;
  const cy = CHAR_BOX_Y + CHAR_BOX_H / 2;
  ctx.fillText('no character', cx, cy - 8);
  ctx.fillText(`"${CHARACTER}"`, cx, cy + 8);
}

function updateCharacterGif() {
  const want = `../../characters/${CHARACTER}/${STATE_GIF[state.char] || 'sleep.gif'}`;
  if (want !== lastGif) {
    charImg.src = want;
    lastGif = want;
  }
}

// Initial paint + 200ms refresh so HUD duration ticks.
repaint();
setInterval(repaint, 200);

// Exposed for mock.js
window.__stackchan = { state, repaint };
