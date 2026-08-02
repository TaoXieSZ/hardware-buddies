#!/usr/bin/env node
// Export all 4 stackchan-cat scenes to PNG frame sequences at 10fps.
// A Python/Pillow step assembles them into GIFs afterwards.
import fs from 'node:fs';
import CanvasKitInit from 'canvaskit-wasm/bin/full/canvaskit.js';

const OUT_FPS = 10;
const SCENES = ['scene-1', 'scene-2', 'scene-3', 'scene-4'];
const NAMES = { 'scene-1': 'cat_idle', 'scene-2': 'cat_thinking', 'scene-3': 'cat_talking', 'scene-4': 'cat_error' };

const ck = await CanvasKitInit({
  locateFile: (f) => `/tmp/stackchan-lottie/node_modules/canvaskit-wasm/bin/full/${f}`,
});

for (const slug of SCENES) {
  const json = fs.readFileSync(`/tmp/stackchan-lottie/public/projects/stackchan-cat/${slug}/lottie.json`, 'utf8');
  const anim = ck.MakeManagedAnimation(json);
  if (!anim) { console.error(`FAILED: ${slug}`); process.exit(1); }
  const doc = JSON.parse(json);
  const W = doc.w, H = doc.h;
  const srcFps = anim.fps(), dur = anim.duration();
  const srcTotal = Math.round(dur * srcFps);
  const outTotal = Math.round(dur * OUT_FPS);

  const dir = `/tmp/stackchan-lottie/gif-frames/${NAMES[slug]}`;
  fs.rmSync(dir, { recursive: true, force: true });
  fs.mkdirSync(dir, { recursive: true });

  const surface = ck.MakeSurface(W, H);
  const canvas = surface.getCanvas();
  for (let i = 0; i < outTotal; i++) {
    const srcFrame = (i / OUT_FPS) * srcFps; // seekFrame accepts fractional frames
    anim.seekFrame(srcFrame);
    canvas.clear(ck.TRANSPARENT);
    anim.render(canvas, ck.LTRBRect(0, 0, W, H));
    surface.flush();
    const img = surface.makeImageSnapshot();
    fs.writeFileSync(`${dir}/f${String(i).padStart(3, '0')}.png`, Buffer.from(img.encodeToBytes()));
    img.delete();
  }
  anim.delete();
  surface.delete();
  console.log(`✓ ${NAMES[slug]}: ${outTotal} frames @ ${OUT_FPS}fps (${W}x${H})`);
}
