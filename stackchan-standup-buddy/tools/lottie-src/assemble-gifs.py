#!/usr/bin/env python3
"""Assemble PNG frame sequences into looping GIFs for the StackChan firmware.
Frames come from export-gifs.mjs (canvaskit/Skottie render, 320x240 @10fps).
Composites onto opaque background (GIF has no alpha; AnimatedGIF on ESP32 wants opaque).
"""
import os
import glob
from PIL import Image

SRC = "/tmp/stackchan-lottie/gif-frames"
DST = "/Users/txie/OpenSourceProjects/agent-farm/stackchan-firmware/data/characters/cat"
BG = (232, 242, 250)  # matches scene bg (0.91,0.95,0.98)
DURATION_MS = 100     # 10fps

os.makedirs(DST, exist_ok=True)

for name in ["cat_idle", "cat_thinking", "cat_talking", "cat_error"]:
    files = sorted(glob.glob(f"{SRC}/{name}/f*.png"))
    if not files:
        raise SystemExit(f"no frames for {name}")
    frames = []
    for f in files:
        im = Image.open(f).convert("RGBA")
        bg = Image.new("RGBA", im.size, BG + (255,))
        bg.alpha_composite(im)
        # quantize to 128 colors — flat cartoon art, keeps size small
        frames.append(bg.convert("RGB").quantize(colors=128, method=Image.MEDIANCUT))
    out = f"{DST}/{name}.gif"
    frames[0].save(
        out, save_all=True, append_images=frames[1:],
        duration=DURATION_MS, loop=0, optimize=True,
    )
    kb = os.path.getsize(out) / 1024
    print(f"✓ {out}: {len(frames)} frames, {kb:.0f} KB")
