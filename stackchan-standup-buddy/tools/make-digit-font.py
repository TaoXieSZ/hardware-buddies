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
RENDER = 56          # Pokemon Classic is an 8px-grid font; 56 = 7x, crisp pixels
TTF = "data/fonts/PokemonClassic.ttf"
OUT = "data/fonts/poke-digits.bin"

font = ImageFont.truetype(TTF, RENDER)
masks = []
preview = Image.new("L", (W * len(GLYPHS), H), 0)

# 数字基准像素尺寸:用 '8' 的字形高度推一个"像素点"边长,手工拼像素风冒号
_0x, _0y, _1x, _1y = font.getbbox("8")
dot = max(5, round((_1y - _0y) / 6))   # 数字约 6 点高

for i, ch in enumerate(GLYPHS):
    img = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(img)
    if ch == ":":
        # 两个像素方块,与数字同点阵风格
        cx = W // 2
        cy = H - 8 - (_1y - _0y) // 2
        d.rectangle([cx - dot // 2, cy - dot * 2, cx + dot // 2, cy - dot], fill=255)
        d.rectangle([cx - dot // 2, cy + dot // 2, cx + dot // 2, cy + dot * 1.5], fill=255)
    else:
        x0, y0, x1, y1 = font.getbbox(ch)
        # 水平居中,底部对齐(数字无降部)
        dx = (W - (x1 - x0)) // 2 - x0
        dy = H - 8 - y1
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
