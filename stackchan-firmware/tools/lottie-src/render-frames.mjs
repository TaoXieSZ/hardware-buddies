#!/usr/bin/env node
// Render Lottie scenes to PNG frames using canvaskit-wasm Skottie (same renderer as the player).
// Usage: node render-frames.mjs <scene-slug> [frames...]  e.g. node render-frames.mjs scene-1 0 30 45
import fs from 'node:fs';
import CanvasKitInit from 'canvaskit-wasm/bin/full/canvaskit.js';

const slug = process.argv[2] || 'scene-1';
const frames = process.argv.slice(3).map(Number);
const jsonPath = `/tmp/stackchan-lottie/public/projects/stackchan-cat/${slug}/lottie.json`;
const json = fs.readFileSync(jsonPath, 'utf8');

const ck = await CanvasKitInit({
  locateFile: (f) => `/tmp/stackchan-lottie/node_modules/canvaskit-wasm/bin/full/${f}`,
});

const anim = ck.MakeManagedAnimation(json);
if (!anim) { console.error('FAILED to parse animation'); process.exit(1); }
const doc = JSON.parse(json);
const W = doc.w, H = doc.h;
const fps = anim.fps(), dur = anim.duration();
const total = Math.round(dur * fps);
console.log(`${slug}: ${W}x${H} fps=${fps} duration=${dur.toFixed(2)}s totalFrames=${total}`);

const pick = frames.length ? frames : [0, Math.floor(total / 2), total - 1];
const surface = ck.MakeSurface(W, H);
const canvas = surface.getCanvas();

for (const f of pick) {
  anim.seekFrame(f);
  canvas.clear(ck.TRANSPARENT);
  anim.render(canvas, ck.LTRBRect(0, 0, W, H));
  surface.flush();
  const img = surface.makeImageSnapshot();
  const png = img.encodeToBytes();
  const out = `/tmp/stackchan-lottie/frames/${slug}-f${f}.png`;
  fs.mkdirSync('/tmp/stackchan-lottie/frames', { recursive: true });
  fs.writeFileSync(out, Buffer.from(png));
  img.delete();
  console.log(`✓ ${out}`);
}
anim.delete();
surface.delete();
