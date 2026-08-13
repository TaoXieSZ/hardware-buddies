#!/usr/bin/env node
// Parameterized cat-head generator for StackChan (320x240 Lottie, Skottie-safe).
//
// Modes:
//   node gen-cat-styles.mjs                 → idle 预览,5 种风格,写到 stackchan-cat-style/
//   node gen-cat-styles.mjs b-tabby --full  → 该风格的全套 5 个状态,写到生产路径
//                                             stackchan-cat/scene-1..5(接 export-gifs.mjs)
//
// Skottie-safe rules: every "gr" group ends with a "tr"; every layer has ip/op/st.
// 层/组内 item 越靠后越垫底(painter 逆序)。
import fs from 'node:fs';

const W = 320, H = 240, FR = 30;
const CX = W / 2, CY = H / 2 + 6;

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

// ---------- expression accents (symbolic colors, style-independent) ----------
const QMARK = [1.0, 0.68, 0.2, 1];
const STAR  = [1.0, 0.82, 0.25, 1];
const XEYE  = [0.85, 0.25, 0.2, 1];
const TEAR  = [0.45, 0.75, 1.0, 1];
const MOUTH_OPEN = [0.35, 0.10, 0.05, 1];
const TONGUE     = [1.0, 0.55, 0.55, 1];

// ---------- style table ----------
const STYLES = {
  'a-cream': {
    label: 'A · 奶油橘(改良)',
    desc: '现行款的精修:更圆的脑袋、更大的高光、更柔和的描边',
    bg: [0.91, 0.95, 0.98, 1], head: [1.0, 0.85, 0.55, 1], edge: [0.82, 0.55, 0.25, 1],
    earIn: [1.0, 0.62, 0.58, 1], eye: [0.16, 0.09, 0.04, 1], nose: [1.0, 0.42, 0.42, 1],
    mouth: [0.45, 0.22, 0.08, 1], blush: [1.0, 0.66, 0.6, 1], whisker: [0.62, 0.4, 0.2, 1],
    strokeW: 5, eyeScale: 1.0, stripes: false, mask: false, patch: false, lineOnly: false,
  },
  'b-tabby': {
    label: 'B · 银虎斑美短',
    desc: '银灰被毛 + 额头 M 纹 + 脸颊条纹 + 白色口鼻,绿眼睛',
    bg: [0.93, 0.95, 0.97, 1], head: [0.78, 0.82, 0.86, 1], edge: [0.35, 0.40, 0.46, 1],
    earIn: [0.95, 0.70, 0.68, 1], eye: [0.24, 0.60, 0.36, 1], nose: [0.85, 0.50, 0.48, 1],
    mouth: [0.30, 0.24, 0.20, 1], blush: [0.88, 0.66, 0.62, 1], whisker: [0.45, 0.48, 0.52, 1],
    muzzleColor: [0.96, 0.97, 0.98, 1],
    strokeW: 5, eyeScale: 1.0, stripes: true, muzzle: true, mask: false, patch: false, lineOnly: false,
  },
  'c-siamese': {
    label: 'C · 暹罗重点色',
    desc: '奶油身体 + 深棕面罩和耳朵,蓝眼睛',
    bg: [0.94, 0.95, 0.97, 1], head: [0.93, 0.88, 0.78, 1], edge: [0.62, 0.52, 0.42, 1],
    earIn: [0.42, 0.32, 0.26, 1], eye: [0.30, 0.56, 0.86, 1], nose: [0.50, 0.38, 0.34, 1],
    mouth: [0.32, 0.24, 0.20, 1], blush: [0.80, 0.62, 0.55, 1], whisker: [0.55, 0.46, 0.38, 1],
    maskColor: [0.38, 0.29, 0.23, 1],
    strokeW: 5, eyeScale: 0.92, stripes: false, mask: true, patch: false, lineOnly: false,
  },
  'd-cow': {
    label: 'D · 黑白奶牛猫',
    desc: '白底 + 黑色斑块(左眼罩 + 右耳),粉鼻子',
    bg: [0.94, 0.95, 0.97, 1], head: [0.97, 0.97, 0.97, 1], edge: [0.20, 0.20, 0.22, 1],
    earIn: [0.98, 0.68, 0.70, 1], eye: [0.12, 0.10, 0.10, 1], nose: [0.98, 0.55, 0.60, 1],
    mouth: [0.25, 0.20, 0.18, 1], blush: [0.96, 0.70, 0.70, 1], whisker: [0.35, 0.35, 0.38, 1],
    patchColor: [0.16, 0.15, 0.17, 1],
    strokeW: 5, eyeScale: 1.0, stripes: false, mask: false, patch: true, lineOnly: false,
  },
  'f-pikachu': {
    label: 'F · 皮卡丘',
    desc: '明黄 + 黑色长耳尖 + 红色电气袋,无胡须',
    bg: [0.96, 0.94, 0.86, 1], head: [1.0, 0.80, 0.16, 1], edge: [0.70, 0.50, 0.10, 1],
    earIn: [1.0, 0.80, 0.16, 1], eye: [0.14, 0.10, 0.08, 1], nose: [0.14, 0.10, 0.08, 1],
    mouth: [0.30, 0.18, 0.06, 1], blush: [0.88, 0.16, 0.16, 1], whisker: [0, 0, 0, 1],
    cheekStyle: 'circles', longEars: true, whiskers: false,
    strokeW: 5, eyeScale: 0.9, stripes: false, mask: false, patch: false, lineOnly: false,
  },
  'e-line': {
    label: 'E · 极简线稿',
    desc: '不上色,粗描边 + 豆豆眼,性冷淡风',
    bg: [0.96, 0.96, 0.95, 1], head: [0.96, 0.96, 0.95, 1], edge: [0.18, 0.18, 0.20, 1],
    earIn: [0.96, 0.96, 0.95, 1], eye: [0.18, 0.18, 0.20, 1], nose: [0.18, 0.18, 0.20, 1],
    mouth: [0.18, 0.18, 0.20, 1], blush: [0.85, 0.85, 0.84, 1], whisker: [0.18, 0.18, 0.20, 1],
    strokeW: 7, eyeScale: 0.55, stripes: false, mask: false, patch: false, lineOnly: true,
  },
};

const HEAD_R = 96;
const zi3 = [[0, 0], [0, 0], [0, 0]];
const zi2 = [[0, 0], [0, 0]];

// ---------- face parts (parameterized) ----------
function bgLayer(S, op) {
  // 锚定画布正中,正好铺满 320x240(不要沿用脸部的 +6 偏移,顶部会露 6px 缝)
  return layer('bg', [group('bg', [rect([0, 0], [W, H]), fill(S.bg)])], op, { p: st0([CX, H / 2]) });
}

function earShapes(S, droop = 0) {
  // 皮卡丘:黄色长耳 + 黑色耳尖,无粉色耳内
  if (S.longEars) {
    const mk = (m) => [
      group(`ear-${m}-tip`, [
        path([[m * 80, -88 + droop], [m * 72, -150 + droop * 2], [m * 50, -76 + droop]], zi3, zi3),
        fill([0.16, 0.13, 0.11, 1]),
      ]),
      group(`ear-${m}`, [
        path([[m * 84, -48], [m * 72, -150 + droop * 2], [m * 34, -64]], zi3, zi3),
        fill(S.head), stroke(S.edge, S.strokeW),
      ]),
    ];
    return [...mk(-1), ...mk(1)];
  }
  const mk = (m) => [
    group(`ear-${m}-in`, [
      path([[m * 72, -62], [m * 62, -92 + droop * 2], [m * 44, -74]], zi3, zi3),
      fill(S.earIn),
    ]),
    group(`ear-${m}`, [
      path([[m * 86, -52], [m * 64, -108 + droop * 2], [m * 30, -78]], zi3, zi3),
      fill(S.head), stroke(S.edge, S.strokeW),
    ]),
  ];
  const parts = [...mk(-1), ...mk(1)];
  if (S.patch) {
    parts.unshift(group('ear-r-patch', [
      path([[86, -52], [64, -108 + droop * 2], [30, -78]], zi3, zi3),
      fill(S.patchColor), stroke(S.edge, S.strokeW),
    ]));
  }
  return parts;
}

function headShapes(S) {
  return [group('head', [
    ellipse([0, 0], [HEAD_R * 2.1, HEAD_R * 1.9]),
    fill(S.head), stroke(S.edge, S.strokeW),
  ])];
}

function stripeShapes(S) {
  const c = S.edge;
  const w = S.strokeW - 1;
  const bar = (nm, x, y0, y1) => group(nm, [
    path([[x, y0], [x, y1]], zi2, zi2, false), stroke(c, w),
  ]);
  return [
    group('m-stripe', [
      path([[-18, -88], [-10, -70], [0, -84], [10, -70], [18, -88]],
           [[0,0],[0,0],[0,0],[0,0],[0,0]], [[0,0],[0,0],[0,0],[0,0],[0,0]], false),
      stroke(c, w),
    ]),
    bar('fore-l', -34, -84, -62), bar('fore-r', 34, -84, -62),
    group('cheek-l', [
      path([[-96, -2], [-74, 2]], zi2, zi2, false),
      path([[-95, 14], [-72, 16]], zi2, zi2, false),
      stroke(c, w),
    ]),
    group('cheek-r', [
      path([[96, -2], [74, 2]], zi2, zi2, false),
      path([[95, 14], [72, 16]], zi2, zi2, false),
      stroke(c, w),
    ]),
  ];
}

function maskShape(S) {
  return [group('mask', [ellipse([0, 6], [128, 104]), fill(S.maskColor)])];
}

function patchShapes(S) {
  return [group('patch-eye-l', [
    ellipse([-54, -28], [96, 88]),
    fill(S.patchColor),
  ], { r: -18 })];
}

// 白色口鼻区(美短/加白)
function muzzleShape(S) {
  return [group('muzzle', [ellipse([0, 46], [118, 72]), fill(S.muzzleColor)])];
}

function noseMouthWhiskers(S, mode = 'smile') {
  const parts = [];
  parts.push(group('nose', [
    path([[0, 16], [-7, 8], [7, 8]], zi3, zi3),
    fill(S.nose),
  ]));
  if (mode === 'smile') {
    parts.push(group('mouth', [
      path([[-16, 26], [-8, 32], [0, 26]], [[0,0],[-4,0],[0,0]], [[0,0],[4,0],[0,0]], false),
      path([[0, 26], [8, 32], [16, 26]], [[0,0],[-4,0],[0,0]], [[0,0],[4,0],[0,0]], false),
      stroke(S.mouth, S.lineOnly ? 5 : 4),
    ]));
  } else if (mode === 'sad') {
    parts.push(group('mouth', [
      path([[-14, 34], [0, 27], [14, 34]], [[0,0],[-6,0],[0,0]], [[0,0],[6,0],[0,0]], false),
      stroke(S.mouth, 4),
    ]));
  }
  if (S.whiskers === false) return parts;   // 皮卡丘没有胡须
  parts.push(group('whisk-l', [
    path([[-52, 8], [-92, 0]], zi2, zi2, false),
    path([[-52, 16], [-94, 16]], zi2, zi2, false),
    path([[-52, 24], [-92, 32]], zi2, zi2, false),
    stroke(S.whisker, 2.5),
  ]));
  parts.push(group('whisk-r', [
    path([[52, 8], [92, 0]], zi2, zi2, false),
    path([[52, 16], [94, 16]], zi2, zi2, false),
    path([[52, 24], [92, 32]], zi2, zi2, false),
    stroke(S.whisker, 2.5),
  ]));
  return parts;
}

function blushShapes(S) {
  if (S.lineOnly) return [];
  if (S.cheekStyle === 'circles') {
    // 皮卡丘电气袋:实心红圆
    return [
      group('cheek-l', [ellipse([-64, 30], [30, 28]), fill(S.blush)]),
      group('cheek-r', [ellipse([64, 30], [30, 28]), fill(S.blush)]),
    ];
  }
  return [
    group('blush-l', [ellipse([-58, 22], [26, 14]), fill(S.blush)], { o: 55 }),
    group('blush-r', [ellipse([58, 22], [26, 14]), fill(S.blush)], { o: 55 }),
  ];
}

function eyeShapes(S, lookUp = 0) {
  const dy = -lookUp;
  const sc = S.eyeScale;
  const mk = (x, nm) => {
    if (S.lineOnly) {
      return group(nm, [ellipse([x, -14 + dy], [30 * sc * 0.55, 34 * sc * 0.55]), fill(S.eye)]);
    }
    return group(nm, [
      ellipse([x, -14 + dy], [30 * sc, 34 * sc]), fill(S.eye),
      ellipse([x - 7, -22 + dy], [11, 13]), fill([1, 1, 1, 1]),
      ellipse([x + 8, -8 + dy], [5, 6]), fill([1, 1, 1, 0.85]),
    ]);
  };
  return [mk(-44, 'eye-l'), mk(44, 'eye-r')];
}

// 特征层 item 顺序(数组越靠后越垫底):五官 → 腮红 → 口鼻白/斑纹/斑块/面罩 → 头 → 耳朵
function faceItems(S, mouthMode, droop = 0) {
  return [
    ...noseMouthWhiskers(S, mouthMode),
    ...blushShapes(S),
    ...(S.muzzle ? muzzleShape(S) : []),
    ...(S.stripes ? stripeShapes(S) : []),
    ...(S.patch ? patchShapes(S) : []),
    ...(S.mask ? maskShape(S) : []),
    ...headShapes(S),
    ...earShapes(S, droop),
  ];
}

// ---------- scene: IDLE (breathing + blink) ----------
function sceneIdle(S) {
  const OP = 90;
  const breathS = {
    a: 1,
    k: [
      { t: 0, s: [100, 100], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [102.5, 102.5], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [100, 100] },
    ],
  };
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
  const face = layer('face', faceItems(S, 'smile'), OP, { s: breathS });
  const eyes = layer('eyes', eyeShapes(S), OP, {
    a: st0([0, -14]),
    p: st0([CX, CY - 14]),
    s: blinkS,
  });
  return doc('cat-idle', OP, [eyes, face, bgLayer(S, OP)]);
}

// ---------- scene: THINKING (look up + head tilt + "?") ----------
function sceneThinking(S) {
  const OP = 90;
  const tiltR = {
    a: 1,
    k: [
      { t: 0, s: [-4], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [4], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [-4] },
    ],
  };
  const bobP = {
    a: 1,
    k: [
      { t: 0, s: [CX + 92, 46], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [CX + 92, 34], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [CX + 92, 46] },
    ],
  };
  const face = layer('face', [
    ...eyeShapes(S, 10),
    ...faceItems(S, 'smile'),
  ], OP, { r: tiltR });
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
  return doc('cat-thinking', OP, [qmark, face, bgLayer(S, OP)]);
}

// ---------- scene: TALKING (mouth open/close + stars) ----------
function sceneTalking(S) {
  const OP = 60;
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
    ...eyeShapes(S, 0),
    ...faceItems(S, 'none'),
  ], OP);
  const mouth = layer('mouth', [
    group('mouth-open', [
      ellipse([0, 0], [34, 26]), fill(MOUTH_OPEN),
      ellipse([0, 7], [18, 12]), fill(TONGUE),
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
  return doc('cat-talking', OP, [...stars, mouth, face, bgLayer(S, OP)]);
}

// ---------- scene: ERROR (X eyes + droopy ears + sad mouth + tear) ----------
function sceneError(S) {
  const OP = 60;
  const xeye = (x) => group('xeye', [
    path([[x - 14, -26], [x + 14, -2]], zi2, zi2, false),
    path([[x + 14, -26], [x - 14, -2]], zi2, zi2, false),
    stroke(XEYE, 7),
  ]);
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
    ...faceItems(S, 'sad', 14),
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
  return doc('cat-error', OP, [tear, face, bgLayer(S, OP)]);
}

// ---------- scene: SLEEP (closed eyes + floating Z's + breathing) ----------
function sceneSleep(S) {
  const OP = 90;
  const breathS = {
    a: 1,
    k: [
      { t: 0, s: [100, 100], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 45, s: [102.5, 102.5], o: { x: [0.42], y: [0] }, i: { x: [0.58], y: [1] } },
      { t: 90, s: [100, 100] },
    ],
  };
  const closedEye = (x, nm) => group(nm, [
    path([[x - 14, -10], [x, -24], [x + 14, -10]],
         [[0, 0], [-8, 0], [0, 0]], [[0, 0], [8, 0], [0, 0]], false),
    stroke(S.eye, 5),
  ]);
  const zLayer = (i) => {
    const t0 = i * 30, t1 = t0 + 30;
    const px = 78 + i * 10;
    return layer(`z${i}`, [group('z', [
      path([[0, 0], [16, 0], [0, -15], [16, -15]],
           [[0, 0], [0, 0], [0, 0], [0, 0]], [[0, 0], [0, 0], [0, 0], [0, 0]], false),
      stroke(S.eye, 4),
    ])], OP, {
      p: { a: 1, k: [
        { t: t0, s: [CX + px, CY - 56], o: { x: [0.5], y: [0] }, i: { x: [0.5], y: [1] } },
        { t: t1, s: [CX + px + 14, CY - 108] },
      ]},
      o: { a: 1, k: [
        { t: t0, s: [0] },
        { t: t0 + 8, s: [100] },
        { t: t1 - 8, s: [100] },
        { t: t1, s: [0] },
      ]},
      s: { a: 1, k: [
        { t: t0, s: [70, 70] },
        { t: t1, s: [110, 110] },
      ]},
    });
  };
  const face = layer('face', [
    closedEye(-44, 'eye-l'),
    closedEye(44, 'eye-r'),
    ...faceItems(S, 'smile'),
  ], OP, { s: breathS });
  return doc('cat-sleep', OP, [zLayer(0), zLayer(1), zLayer(2), face, bgLayer(S, OP)]);
}

// ---------- main ----------
const argStyle = process.argv[2];
const full = process.argv.includes('--full');

if (full) {
  const S = STYLES[argStyle];
  if (!S) { console.error(`unknown style: ${argStyle}`); process.exit(1); }
  const base = '/tmp/stackchan-lottie/public/projects/stackchan-cat';
  const scenes = [
    ['scene-1', sceneIdle(S)],
    ['scene-2', sceneThinking(S)],
    ['scene-3', sceneTalking(S)],
    ['scene-4', sceneError(S)],
    ['scene-5', sceneSleep(S)],
  ];
  for (const [slug, d] of scenes) {
    fs.mkdirSync(`${base}/${slug}`, { recursive: true });
    fs.writeFileSync(`${base}/${slug}/lottie.json`, JSON.stringify(d));
    console.log(`✓ ${slug}: ${d.nm} [${argStyle}]`);
  }
} else {
  const base = '/tmp/stackchan-lottie/public/projects/stackchan-cat-style';
  const meta = [];
  for (const [slug, S] of Object.entries(STYLES)) {
    const d = sceneIdle(S);
    fs.mkdirSync(`${base}/${slug}`, { recursive: true });
    fs.writeFileSync(`${base}/${slug}/lottie.json`, JSON.stringify(d));
    meta.push({ slug, label: S.label, desc: S.desc });
    console.log(`✓ ${slug}: ${S.label}`);
  }
  fs.writeFileSync(`${base}/styles-meta.json`, JSON.stringify(meta, null, 2));
}
