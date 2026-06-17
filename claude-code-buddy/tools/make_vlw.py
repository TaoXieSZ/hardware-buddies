#!/usr/bin/env python3
"""Generate VLW smooth fonts (TFT_eSPI / LovyanGFX format) from a TTF/TTC.

The Tab5 dashboard loads these from LittleFS (/fonts/*.vlw) for anti-aliased
text — the built-in efontCN/FreeSans bitmaps look jagged on the 720p panel.
Format matches M5GFX's VLWfont::loadFont (lgfx_fonts.cpp): 24-byte header,
28 bytes of big-endian int32 metadata per glyph (sorted by unicode — the
loader binary-searches), then concatenated 8-bit alpha bitmaps in the same
order. BMP code points only (loader stores uint16).

Uses PIL's FreeType binding — no extra deps beyond Pillow (+fontTools when
mixing faces, for exact cmap lookups).

Supports the Apple-style two-face mix: --latin-font renders every glyph it
actually covers below U+3000 (ASCII, digits, punctuation, symbols) while
--font keeps the CJK. Baselines align because VLW stores per-glyph dY
relative to each face's own baseline. --variation / --latin-variation pick
a named instance of a variable font (e.g. SF Pro "Semibold").

Examples (the exact set the firmware expects):
  python3 tools/make_vlw.py --font PingFang.ttc --index 3 \
      --latin-font /System/Library/Fonts/SFNS.ttf --px 30 \
      --charset full --out data/fonts/main30.vlw
  ... see tools/make_vlw.py --help and src/tab5/ui.cpp for the full list.
"""

import argparse
import struct
import sys
from PIL import Image, ImageDraw, ImageFont

# CJK strings hard-coded in src/tab5/ui.cpp + room for daemon messages.
UI_LITERALS = (
    "权限请求允许拒绝已批准待确认键盘等待任务调整强调色为珊瑚橙"
    "压缩上下文中完成子代理失败运行思考就绪会话连接断开秒分钟"
)
SYMBOLS = "✓✗⚠·•—–…→←≈°"


def charset_chars(name: str) -> set[str]:
    chars = {chr(c) for c in range(0x20, 0x7F)}          # ASCII
    chars |= set(SYMBOLS) | set(UI_LITERALS)
    chars |= {chr(c) for c in range(0x3000, 0x3018)}     # CJK punctuation
    chars |= {chr(c) for c in range(0xFF01, 0xFF5F)}     # fullwidth forms
    if name == "ui":
        return chars
    if name == "full":
        # GB2312 level 1 (3755 most common hanzi) — derived from the codec
        # itself so no external frequency list is needed.
        for hi in range(0xB0, 0xD8):
            for lo in range(0xA1, 0xFF):
                try:
                    chars.add(bytes((hi, lo)).decode("gb2312"))
                except UnicodeDecodeError:
                    pass
        return chars
    raise SystemExit(f"unknown charset {name!r} (use: ui, full)")


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, ascent: int):
    """Returns (w, h, xadv, dY, dX, bitmap bytes). Empty glyphs → w=h=0."""
    xadv = max(0, round(font.getlength(ch)))
    bbox = font.getbbox(ch)  # top-left origin; baseline sits at y=ascent
    if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return 0, 0, xadv, 0, 0, b""
    x0, y0, x1, y1 = bbox
    pad = 8  # absorbs negative bearings / over-ascent without clipping
    img = Image.new("L", (x1 - x0 + 2 * pad, y1 - y0 + 2 * pad), 0)
    ImageDraw.Draw(img).text((pad - x0, pad - y0), ch, font=font, fill=255)
    g = img.crop((pad, pad, pad + (x1 - x0), pad + (y1 - y0)))
    w, h = g.size
    dY = ascent - y0      # top of bitmap above baseline
    dX = x0
    if w > 255 or h > 255 or xadv > 255 or not (-128 <= dX <= 127):
        raise SystemExit(f"glyph {ch!r} metrics overflow VLW fields: "
                         f"w={w} h={h} adv={xadv} dX={dX}")
    return w, h, xadv, dY, dX, g.tobytes()


def font_cmap(path: str, index: int) -> set[int]:
    from fontTools.ttLib import TTFont
    tf = TTFont(path, fontNumber=index if path.lower().endswith(".ttc") else -1)
    return set(tf.getBestCmap().keys())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--font", required=True, help="TTF/TTC path (CJK / main face)")
    ap.add_argument("--index", type=int, default=0, help="face index in a TTC")
    ap.add_argument("--variation", default="", help="named instance (variable font)")
    ap.add_argument("--latin-font", default="", help="optional second face for <U+3000")
    ap.add_argument("--latin-index", type=int, default=0)
    ap.add_argument("--latin-variation", default="")
    ap.add_argument("--px", type=int, required=True, help="pixel size")
    ap.add_argument("--charset", default="ui", help="ui | full (ASCII+GB2312-L1)")
    ap.add_argument("--out", required=True, help="output .vlw path")
    a = ap.parse_args()

    font = ImageFont.truetype(a.font, a.px, index=a.index)
    if a.variation:
        font.set_variation_by_name(a.variation)
    ascent, descent = font.getmetrics()

    latin, latin_cmap, latin_ascent = None, set(), 0
    if a.latin_font:
        latin = ImageFont.truetype(a.latin_font, a.px, index=a.latin_index)
        if a.latin_variation:
            latin.set_variation_by_name(a.latin_variation)
        latin_ascent = latin.getmetrics()[0]
        latin_cmap = font_cmap(a.latin_font, a.latin_index)

    chars = sorted(c for c in charset_chars(a.charset) if 0x20 <= ord(c) <= 0xFFFF)

    meta, bitmaps = [], []
    for ch in chars:
        if latin and ord(ch) < 0x3000 and ord(ch) in latin_cmap:
            w, h, xadv, dY, dX, bm = render_glyph(latin, ch, latin_ascent)
        else:
            w, h, xadv, dY, dX, bm = render_glyph(font, ch, ascent)
        meta.append(struct.pack(">7i", ord(ch), h, w, xadv, dY, dX, 0))
        bitmaps.append(bm)

    with open(a.out, "wb") as f:
        f.write(struct.pack(">6i", len(meta), 11, a.px, 0, ascent, descent))
        f.writelines(meta)
        f.writelines(bitmaps)

    total = 24 + sum(len(m) for m in meta) + sum(len(b) for b in bitmaps)
    print(f"{a.out}: {len(meta)} glyphs, {total/1024:.0f} KB "
          f"({font.getname()[0]} {font.getname()[1]} @ {a.px}px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
