#!/usr/bin/env node
// Render style previews from gen-cat-styles.mjs to PNG frames at 10fps.
// Run from a dir with canvaskit-wasm installed (cp to /tmp/stackchan-lottie).
import fs from 'node:fs';
import CanvasKitInit from 'canvaskit-wasm/bin/full/canvaskit.js';

const OUT_FPS = 10;
const BASE = '/tmp/stackchan-lottie/public/projects/stackchan-cat-style';
const slugs = JSON.parse(fs.readFileSync(`${BASE}/styles-meta.json`, 'utf8')).map(s => s.slug);

const ck = await CanvasKitInit({
  locateFile: (f) => `/tmp/stackchan-lottie/node_modules/canvaskit-wasm/bin/full/${f}`,
});

for (const slug of slugs) {
  const json = fs.readFileSync(`${BASE}/${slug}/lottie.json`, 'utf8');
  const anim = ck.MakeManagedAnimation(json);
  if (!anim) { console.error(`FAILED: ${slug}`); process.exit(1); }
  const doc = JSON.parse(json);
  const W = doc.w, H = doc.h;
  const srcFps = anim.fps(), dur = anim.duration();
  const outTotal = Math.round(dur * OUT_FPS);

  const dir = `/tmp/stackchan-lottie/style-frames/${slug}`;
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });

  const surface = ck.MakeSurface(W, H);
  const canvas = surface.getCanvas();
  for (let i = 0; i < outTotal; i++) {
    anim.seekFrame((i / OUT_FPS) * srcFps);
    canvas.clear(ck.TRANSPARENT);
    anim.render(canvas, ck.LTRBRect(0, 0, W, H));
    surface.flush();
    const img = surface.makeImageSnapshot();
    fs.writeFileSync(`${dir}/f${String(i).padStart(3, '0')}.png`, Buffer.from(img.encodeToBytes()));
    img.delete();
  }
  anim.delete();
  surface.delete();
  console.log(`✓ ${slug}: ${outTotal} frames`);
}
