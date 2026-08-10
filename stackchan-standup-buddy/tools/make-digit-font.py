#!/usr/bin/env python3
"""Render countdown digit glyphs (0-9 and ':') from a TTF into an alpha-mask
binary for the StackChan firmware (M5GFX has no TTF support compiled in).

Output: data/fonts/poke-digits.bin
  header: 'PDGF', u8 count, u8 w, u8 h
  then count glyphs of w*h bytes, row-major alpha 0..255, order: 0123456789:
Also writes /tmp/poke-digits-preview.png for a visual check.
"""
import struct
from PIL import Image, ImageDraw, ImageFont

GLYPHS = "0123456789:"
W = H = 64
RENDER = 54          # Geist Mono SemiBold,平滑抗锯齿
TTF = "data/fonts/GeistMono.ttf"
WEIGHT = 600         # variable font 字重
OUT = "data/fonts/poke-digits.bin"

font = ImageFont.truetype(TTF, RENDER)
try:
    font.set_variation_by_axes([WEIGHT])
except Exception:
    pass
masks = []
preview = Image.new("L", (W * len(GLYPHS), H), 0)

for i, ch in enumerate(GLYPHS):
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = font.getbbox(ch)
    # 水平居中,底部对齐(数字无降部;Geist Mono 自带标准冒号)
    dx = (W - (x1 - x0)) // 2 - x0
    dy = H - 6 - y1
    d.text((dx, dy), ch, font=font, fill=255)
    masks.append(img.tobytes())
    preview.paste(img, (i * W, 0))

with open(OUT, "wb") as f:
    f.write(b"PDGF")
    f.write(struct.pack("BBB", len(GLYPHS), W, H))
    for m in masks:
        f.write(m)

preview.save("/tmp/poke-digits-preview.png")
print(f"✓ {OUT}: {len(GLYPHS)} glyphs {W}x{H}, {4 + W * H * len(GLYPHS)} bytes")
