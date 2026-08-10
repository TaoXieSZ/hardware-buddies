#!/usr/bin/env node
// Generate StackChan cat-head Lottie scenes (320x240) with Skottie-safe structure:
// - every "gr" group has a trailing "tr" transform
// - every layer has ip/op/st
// Scenes: 1=idle, 2=thinking, 3=talking, 4=error
import fs from 'node:fs';

const W = 320, H = 240, FR = 30;
const CX = W / 2, CY = H / 2 + 6; // face center, slightly below middle

// ---------- helpers ----------
const st0 = (k) => ({ a: 0, k });
const tr = (over = {}) => ({
  ty: 'tr',
  p: st0(over.p ?? [0, 0]),
  a: st0(over.a ?? [0, 0]),
  s: st0(over.s ?? [100, 100]),
  r: st0(over.r ?? 0),
  o: st0(over.o ?? 100),
});
const fill = (c) => ({ ty: 'fl', c: st0(c), o: st0(100), r: 1, bm: 0 });
const stroke = (c, w) => ({ ty: 'st', lc: 2, lj: 2, ml: 4, w: st0(w), c: st0(c), o: st0(100) });
const ellipse = (p, s) => ({ ty: 'el', p: st0(p), s: st0(s) });
const rect = (p, s, r = 0) => ({ ty: 'rc', p: st0(p), s: st0(s), r: st0(r) });
const group = (nm, items, trOver = {}) => ({ ty: 'gr', nm, it: [...items, tr(trOver)] });
// cubic bezier path: verts = [[x,y],...], tangents in/out same length; closed flag
const path = (v, i, o, closed = true) => ({ ty: 'sh', ks: st0({ c: closed, v, i, o }) });

function layer(nm, shapes, op, ksOver = {}) {
  return {
    ddd: 0, ty: 4, nm, sr: 1, ao: 0,
    ks: {
      o: ksOver.o ?? st0(100),
      r: ksOver.r ?? st0(0),
      p: ksOver.p ?? st0([CX, CY]),
      a: ksOver.a ?? st0([0, 0]),
      s: ksOver.s ?? st0([100, 100]),
    },
    shapes,
    ip: 0, op, st: 0,
  };
}

function doc(nm, op, layers) {
  return { v: '5.5.2', fr: FR, ip: 0, op, w: W, h: H, nm, ddd: 0, assets: [], layers };
}

// ---------- palette ----------
const BG        = [0.91, 0.95, 0.98, 1];   // soft blue-gray
const HEAD      = [1.0, 0.85, 0.55, 1];    // warm orange cream
const HEAD_EDGE = [0.82, 0.55, 0.25, 1];
const EAR_IN    = [1.0, 0.62, 0.58, 1];    // pink inner ear
const EYE_DARK  = [0.16, 0.09, 0.04, 1];   // warm dark brown
const NOSE      = [1.0, 0.42, 0.42, 1];
const MOUTH     = [0.45, 0.22, 0.08, 1];
const BLUSH     = [1.0, 0.66, 0.6, 1];
const WHISKER   = [0.62, 0.4, 0.2, 1];
const QMARK     = [1.0, 0.68, 0.2, 1];
const STAR      = [1.0, 0.82, 0.25, 1];
const XEYE      = [0.85, 0.25, 0.2, 1];
const TEAR      = [0.45, 0.75, 1.0, 1];

// ---------- shared face parts (positions relative to layer anchor = face center) ----------
const HEAD_R = 96; // head half-size => head fills most of 240px height

function bgLayer(op) {
  return layer('bg', [group('bg', [rect([0, 0], [W, H]), fill(BG)])], op, { p: st0([CX, CY - 6 + 6]) });
  // note: bg centered on canvas center (CX, CY uses +6 face offset; compensate)
}

function earShapes(droop = 0) {
  // triangle ears via paths; droop>0 rotates tips outward/down
  const zi = [[0,0],[0,0],[0,0]], zo = [[0,0],[0,0],[0,0]];
  const L = group('ear-l', [
    path([[-86, -52], [-64, -108 + droop * 2], [-30, -78]], zi, zo),
    fill(HEAD), stroke(HEAD_EDGE, 5),
  ]);
  const Li = group('ear-l-in', [
    path([[-72, -62], [-62, -92 + droop * 2], [-44, -74]], zi, zo),
    fill(EAR_IN),
  ]);
  const R = group('ear-r', [
    path([[86, -52], [64, -108 + droop * 2], [30, -78]], zi, zo),
    fill(HEAD), stroke(HEAD_EDGE, 5),
  ]);
  const Ri = group('ear-r-in', [
    path([[72, -62], [62, -92 + droop * 2], [44, -74]], zi, zo),
    fill(EAR_IN),
  ]);
  return [Li, L, Ri, R];
}

function headShapes() {
  return [group('head', [ellipse([0, 0], [HEAD_R * 2.1, HEAD_R * 1.9]), fill(HEAD), stroke(HEAD_EDGE, 5)])];
}

function noseMouthWhiskers(mouthMode = 'smile') {
  const parts = [];
  // nose: small triangle
  parts.push(group('nose', [
    path([[0, 16], [-7, 8], [7, 8]], [[0,0],[0,0],[0,0]], [[0,0],[0,0],[0,0]]),
    fill(NOSE),
  ]));
  if (mouthMode === 'smile') {
    // cat "w" mouth: two small arcs via bezier open paths
    parts.push(group('mouth', [
      path([[-16, 26], [-8, 32], [0, 26]], [[0,0],[-4,0],[0,0]], [[0,0],[4,0],[0,0]], false),
      path([[0, 26], [8, 32], [16, 26]], [[0,0],[-4,0],[0,0]], [[0,0],[4,0],[0,0]], false),
      stroke(MOUTH, 4),
    ]));
  } else if (mouthMode === 'sad') {
    parts.push(group('mouth', [
      path([[-14, 34], [0, 27], [14, 34]], [[0,0],[-6,0],[0,0]], [[0,0],[6,0],[0,0]], false),
      stroke(MOUTH, 4),
    ]));
  }
  // whiskers
  const wl = group('whisk-l', [
    path([[-52, 8], [-92, 0]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    path([[-52, 16], [-94, 16]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    path([[-52, 24], [-92, 32]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    stroke(WHISKER, 2.5),
  ]);
  const wr = group('whisk-r', [
    path([[52, 8], [92, 0]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    path([[52, 16], [94, 16]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    path([[52, 24], [92, 32]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    stroke(WHISKER, 2.5),
  ]);
  parts.push(wl, wr);
  return parts;
}

function blushShapes() {
  return [
    group('blush-l', [ellipse([-58, 22], [26, 14]), fill(BLUSH)], { o: 55 }),
    group('blush-r', [ellipse([58, 22], [26, 14]), fill(BLUSH)], { o: 55 }),
  ];
}

// open eyes with shine, optional look-up offset
function eyeShapes(lookUp = 0) {
  const dy = -lookUp;
  const mk = (x, nm) => group(nm, [
    ellipse([x, -14 + dy], [30, 34]), fill(EYE_DARK),
    ellipse([x - 7, -22 + dy], [10, 12]), fill([1, 1, 1, 1]),
    ellipse([x + 8, -8 + dy], [5, 6]), fill([1, 1, 1, 0.85]),
  ]);
  return [mk(-44, 'eye-l'), mk(44, 'eye-r')];
}

// ---------- scene 1: IDLE (breathing + blink) ----------
function sceneIdle() {
  const OP = 90;
  // breathing: face layer scale 100 -> 102 -> 100
  const breathS = {
    a: 1,
    k: [
      { t: 0, s: [100, 100], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [102.5, 102.5], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [100, 100] },
    ],
  };
  // blink: eyes layer scale-y dips to 8% quickly around t=40 and t=78
  const blinkS = {
    a: 1,
    k: [
      { t: 0,  s: [100, 100], o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 38, s: [100, 100], o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 41, s: [100, 8],   o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 44, s: [100, 100], o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 74, s: [100, 100], o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 77, s: [100, 8],   o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 80, s: [100, 100], o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
      { t: 90, s: [100, 100] },
    ],
  };
  const face = layer('face', [
    ...noseMouthWhiskers('smile'),
    ...blushShapes(),
    ...headShapes(),
    ...earShapes(),
  ], OP, { s: breathS });
  // eyes on their own layer so the blink squash doesn't distort the head
  // anchor at eye vertical center (-14) so squash closes toward the middle of the eye
  const eyes = layer('eyes', eyeShapes(), OP, {
    a: st0([0, -14]),
    p: st0([CX, CY - 14]),
    s: blinkS,
  });
  return doc('cat-idle', OP, [eyes, face, bgLayer(OP)]);
}

// ---------- scene 2: THINKING (look up + head tilt + floating "?") ----------
function sceneThinking() {
  const OP = 90;
  // head tilt: rotate -4deg -> 4deg -> -4deg slowly
  const tiltR = {
    a: 1,
    k: [
      { t: 0, s: [-4], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [4], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [-4] },
    ],
  };
  // question mark bobbing
  const bobP = {
    a: 1,
    k: [
      { t: 0, s: [CX + 92, 46], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [CX + 92, 34], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [CX + 92, 46] },
    ],
  };
  const face = layer('face', [
    ...eyeShapes(10), // pupils up
    ...noseMouthWhiskers('smile'),
    ...headShapes(),
    ...earShapes(),
  ], OP, { r: tiltR });
  // "?" drawn as bezier open path + dot
  const qmark = layer('qmark', [
    group('q', [
      path(
        [[-10, -14], [0, -22], [10, -14], [2, -2], [1, 6]],
        [[0, -6], [-6, 0], [0, -5], [5, -4], [0, -3]],
        [[0, 6], [6, 0], [0, 5], [-2, 2], [0, 3]],
        false
      ),
      stroke(QMARK, 6),
    ]),
    group('q-dot', [ellipse([1, 18], [7, 7]), fill(QMARK)]),
  ], OP, { p: bobP });
  return doc('cat-thinking', OP, [qmark, face, bgLayer(OP)]);
}

// ---------- scene 3: TALKING (mouth open/close + sparkle eyes + stars) ----------
function sceneTalking() {
  const OP = 60;
  // mouth: open ellipse scale pulsing (open-close 4x per loop)
  const mouthPulse = {
    a: 1,
    k: [
      { t: 0,  s: [100, 20],  o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 7,  s: [100, 100], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 15, s: [100, 20],  o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 22, s: [100, 100], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 30, s: [100, 20],  o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 37, s: [100, 100], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 45, s: [100, 20],  o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 52, s: [100, 100], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 60, s: [100, 20] },
    ],
  };
  // stars twinkle via opacity
  const twinkle = (phase) => ({
    a: 1,
    k: [
      { t: 0, s: [phase], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 30, s: [100 - phase], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 60, s: [phase] },
    ],
  });
  const star = (x, y, r) => group('star', [
    path(
      [[0, -r], [r * 0.28, -r * 0.28], [r, 0], [r * 0.28, r * 0.28], [0, r], [-r * 0.28, r * 0.28], [-r, 0], [-r * 0.28, -r * 0.28]],
      Array(8).fill([0, 0]), Array(8).fill([0, 0])
    ),
    fill(STAR),
  ], { p: [x, y] });
  const face = layer('face', [
    ...eyeShapes(0),
    // nose + whiskers only (mouth is separate animated layer)
    ...noseMouthWhiskers('none'),
    ...blushShapes(),
    ...headShapes(),
    ...earShapes(),
  ], OP);
  const mouth = layer('mouth', [
    group('mouth-open', [
      ellipse([0, 0], [34, 26]), fill([0.35, 0.1, 0.05, 1]),
      ellipse([0, 7], [18, 12]), fill([1, 0.55, 0.55, 1]),
    ]),
  ], OP, {
    a: st0([0, 0]),
    p: st0([CX, CY + 30]),
    s: mouthPulse,
  });
  const stars = [
    layer('star1', [star(0, 0, 12)], OP, { p: st0([54, 44]), o: twinkle(20) }),
    layer('star2', [star(0, 0, 9)], OP, { p: st0([W - 46, 60]), o: twinkle(80) }),
    layer('star3', [star(0, 0, 7)], OP, { p: st0([W - 70, 26]), o: twinkle(50) }),
  ];
  return doc('cat-talking', OP, [...stars, mouth, face, bgLayer(OP)]);
}

// ---------- scene 4: ERROR (X eyes + droopy ears + sad mouth + tear) ----------
function sceneError() {
  const OP = 60;
  const xeye = (x) => group('xeye', [
    path([[x - 14, -26], [x + 14, -2]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    path([[x + 14, -26], [x - 14, -2]], [[0,0],[0,0]], [[0,0],[0,0]], false),
    stroke(XEYE, 7),
  ]);
  // tear slides down + fades
  const tearP = {
    a: 1,
    k: [
      { t: 0, s: [CX - 62, CY + 4], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 40, s: [CX - 62, CY + 34], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 60, s: [CX - 62, CY + 34] },
    ],
  };
  const tearO = {
    a: 1,
    k: [
      { t: 0, s: [95], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 38, s: [90], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 52, s: [0], o: { x: [0.4], y: [0] }, i: { x: [0.6], y: [1] } },
      { t: 60, s: [0] },
    ],
  };
  const face = layer('face', [
    xeye(-44), xeye(44),
    ...noseMouthWhiskers('sad'),
    ...headShapes(),
    ...earShapes(14), // droopy
  ], OP);
  const tear = layer('tear', [
    group('tear', [
      path(
        [[0, -10], [7, 4], [0, 10], [-7, 4]],
        [[3, -5], [0, -5], [4, 0], [0, 5]],
        [[-3, -5], [0, 5], [-4, 0], [0, -5]]
      ),
      fill(TEAR),
    ]),
  ], OP, { p: tearP, o: tearO });
  return doc('cat-error', OP, [tear, face, bgLayer(OP)]);
}

// noseMouthWhiskers('none') → skip mouth
// (small patch: mode 'none' handled by returning without mouth)

// ---------- write ----------
const base = '/tmp/stackchan-lottie/public/projects/stackchan-cat';
const scenes = [
  ['scene-1', sceneIdle()],
  ['scene-2', sceneThinking()],
  ['scene-3', sceneTalking()],
  ['scene-4', sceneError()],
];
for (const [slug, d] of scenes) {
  fs.mkdirSync(`${base}/${slug}`, { recursive: true });
  fs.writeFileSync(`${base}/${slug}/lottie.json`, JSON.stringify(d));
  console.log(`✓ ${slug}: ${d.nm} (${d.op}f, ${d.layers.length} layers)`);
}
