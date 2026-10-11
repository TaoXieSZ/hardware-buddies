#!/usr/bin/env python3
"""Build the anti-aliased CJK font partition for the direct-to-Muse StopWatch build.

The reading pages (confirm, answer, reminder, error) need large smooth text; the built-in
efontCN bitmaps top out at 24 px and are 1-bit. This rasterises an OFL CJK font (Source Han
Sans SC) into LovyanGFX/M5GFX VLW fonts — 8-bit alpha glyphs — for every GB2312 character,
and packs them into one image that is written to the `fonts` flash partition
(partitions_muse.csv).

Image layout (little-endian header, VLW bodies are big-endian as the format requires):
    "MFN1" | u32 count | count x (u32 px, u32 offset, u32 length) | pad to 4 KiB | VLW ...

Usage:
    python3 tools/muse-fonts/make_fonts.py --font SourceHanSansSC-Normal.otf
Get the font from https://github.com/adobe-fonts/source-han-sans (OFL-1.1).
Output: fonts/muse_fonts.bin (gitignored, ~12 MB).
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

SUPERSAMPLE = 4
ROOT = Path(__file__).resolve().parents[2]


def charset() -> list[int]:
    """GB2312 punctuation (row 1), circled numbers (row 2), full-width ASCII (row 3), all 6763 hanzi, ASCII."""
    cps = set(range(0x20, 0x7F))
    for hi in [0xA1, 0xA2, 0xA3, *range(0xB0, 0xF8)]:
        for lo in range(0xA1, 0xFF):
            try:
                cps.add(ord(bytes([hi, lo]).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    cps.update(ord(c) for c in "…—“”‘’·•¥€£°©®™×÷±～")
    return sorted(c for c in cps if c <= 0xFFFF)


def glyph(font: ImageFont.FreeTypeFont, cp: int, stroke: int) -> tuple[bytes, int, int, int, int, int]:
    """(alpha, width, height, x_advance, top_extent, left_extent) at the final size."""
    ch = chr(cp)
    advance = round(font.getlength(ch) / SUPERSAMPLE)
    x0, y0, x1, y1 = font.getbbox(ch, anchor="ls", stroke_width=stroke)
    if x1 <= x0 or y1 <= y0:
        return b"", 0, 0, advance, 0, 0
    # Snap the box to the supersample grid so the origin lands on a whole output pixel.
    gx0, gy0 = (x0 // SUPERSAMPLE) * SUPERSAMPLE, (y0 // SUPERSAMPLE) * SUPERSAMPLE
    gx1, gy1 = -(-x1 // SUPERSAMPLE) * SUPERSAMPLE, -(-y1 // SUPERSAMPLE) * SUPERSAMPLE
    big = Image.new("L", (gx1 - gx0, gy1 - gy0), 0)
    ImageDraw.Draw(big).text((-gx0, -gy0), ch, font=font, fill=255, anchor="ls", stroke_width=stroke, stroke_fill=255)
    small = big.reduce(SUPERSAMPLE)
    box = small.getbbox()
    if box is None:
        return b"", 0, 0, advance, 0, 0
    small = small.crop(box)
    left = gx0 // SUPERSAMPLE + box[0]
    top = -(gy0 // SUPERSAMPLE + box[1])  # pixels above the baseline
    return small.tobytes(), small.width, small.height, advance, top, left


def vlw(font_path: str, px: int, stroke: int, cps: list[int]) -> bytes:
    font = ImageFont.truetype(font_path, px * SUPERSAMPLE)
    cmap = TTFont(font_path, lazy=True).getBestCmap()
    ascent, descent = (round(v / SUPERSAMPLE) for v in font.getmetrics())
    headers, bitmaps, kept = [], [], 0
    for cp in cps:
        if cp not in cmap:
            continue  # not in the font: leave it out so the device can tell
        alpha, w, h, adv, top, left = glyph(font, cp, stroke)
        if w > 255 or adv > 255:
            raise ValueError(f"U+{cp:04X} too wide for VLW at {px}px")
        headers.append(struct.pack(">7i", cp, h, w, adv, top, left, 0))
        bitmaps.append(alpha)
        kept += 1
    head = struct.pack(">6i", kept, 11, px, 0, ascent, descent)
    return head + b"".join(headers) + b"".join(bitmaps)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", required=True, help="Source Han Sans SC (OTF/TTF)")
    parser.add_argument("--sizes", default="24,32", help="pixel sizes, comma separated")
    parser.add_argument("--stroke", type=int, default=1, help="extra weight in 1/4 px (0 = font weight)")
    parser.add_argument("--out", default=str(ROOT / "fonts" / "muse_fonts.bin"))
    args = parser.parse_args()

    cps = charset()
    sizes = [int(s) for s in args.sizes.split(",")]
    bodies = [vlw(args.font, px, args.stroke, cps) for px in sizes]
    table = struct.calcsize("<4sI") + len(sizes) * 12
    offset = -(-table // 4096) * 4096
    entries, blob = [], bytearray()
    for px, body in zip(sizes, bodies):
        entries.append(struct.pack("<3I", px, offset + len(blob), len(body)))
        blob += body
        blob += b"\0" * (-len(blob) % 4096)
    image = struct.pack("<4sI", b"MFN1", len(sizes)) + b"".join(entries)
    image += b"\0" * (offset - len(image)) + blob
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(image)
    print(f"{out} {len(image)} bytes, {len(cps)} code points, sizes {sizes}")


if __name__ == "__main__":
    main()
