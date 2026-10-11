#!/usr/bin/env python3
"""Render the direct-to-Muse reading pages with the real font image, for review before flashing.

Mirrors include/muse_layout.h (wrapping) and the reading-page geometry in src/walkie_ui.cpp, drawing the
exact VLW glyphs from fonts/muse_fonts.bin. Sample text is made up; no personal data.

    python3 tools/muse-fonts/preview_reading.py            # -> ui-preview/output/reading-pages.png
"""

from __future__ import annotations

import math
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
CX = CY = 233
TEXT_R = 190
LABEL_TOP, LABEL_PX, LABEL_R = 76, 24, 206
BAND = (112, 366)
HINT_TOP, HINT_PX, HINT_R = 376, 24, 206
HANG = set("，。、！？；：）》」』”’…％,.!?;:)%")
MISSING, ELLIPSIS = 0x25A1, 0x2026


def rgb565(c: int) -> tuple[int, int, int]:
    r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


WHITE, MUTED, SURFACE = rgb565(0xFFFF), rgb565(0x7BEF), rgb565(0x10A2)
MINT, AMBER, CORAL, VIOLET, CYAN = rgb565(0x47B6), rgb565(0xFDC9), rgb565(0xFBAA), rgb565(0xB47F), rgb565(0x563F)
KEY_A, KEY_B, BLACK = rgb565(0xFE20), rgb565(0x3D9F), (0, 0, 0)


class Vlw:
    def __init__(self, data: bytes):
        count, _, self.px, _, _, _ = struct.unpack(">6i", data[:24])
        self.glyphs: dict[int, tuple[int, int, int, int, int, int]] = {}
        ptr = 24 + count * 28
        self.max_ascent = self.max_descent = 0
        for i in range(count):
            cp, h, w, adv, top, left, _ = struct.unpack(">7i", data[24 + i * 28 : 52 + i * 28])
            self.glyphs[cp] = (h, w, adv, top, left, ptr)
            if (cp > 0xFF or 0x20 < cp < 0xA0 and cp != 0x7F) and cp != 0x3000:
                self.max_ascent = max(self.max_ascent, top)
                self.max_descent = max(self.max_descent, h - top)
            ptr += w * h
        self.data = data

    def advance(self, cp: int) -> int:
        g = self.glyphs.get(cp)
        return g[2] if g else 0

    def draw(self, img: Image.Image, text: str, x: int, top: int, color, line_height: int) -> None:
        y = top + (line_height - (self.max_ascent + self.max_descent)) // 2
        for ch in text:
            g = self.glyphs.get(ord(ch))
            if not g:
                continue
            h, w, adv, gtop, left, ptr = g
            if w:
                mask = Image.frombytes("L", (w, h), self.data[ptr : ptr + w * h])
                img.paste(color, (x + left, y + self.max_ascent - gtop), mask)
            x += adv


def load_fonts(path: Path) -> dict[int, Vlw]:
    data = path.read_bytes()
    magic, count = struct.unpack("<4sI", data[:8])
    assert magic == b"MFN1", "not a muse font image"
    fonts = {}
    for i in range(count):
        px, off, length = struct.unpack("<3I", data[8 + i * 12 : 20 + i * 12])
        fonts[px] = Vlw(data[off : off + length])
    return fonts


# --- include/muse_layout.h, line for line ------------------------------------------------------------

def line_height(px: int) -> int:
    return px * 5 // 4


def chord(radius: int, y0: int, y1: int) -> int:
    d = max(abs(y0 - CY), abs(y1 - CY))
    return 0 if d >= radius else 2 * int(math.sqrt(radius * radius - d * d))


def tokenize(text: str, font: Vlw) -> list[dict]:
    tokens: list[dict] = []
    word = lambda c: 0x20 < ord(c) < 0x7F and c not in HANG  # noqa: E731
    for ch in text:
        if ch in " \n\r\t　":
            if tokens and not tokens[-1]["space"]:
                tokens.append({"cps": [" "], "width": font.advance(32), "space": True, "hang": False})
            continue
        if ord(ch) < 0x20:
            continue
        cp = ord(ch)
        if font.advance(cp) <= 0:
            ch = chr(MISSING)
        w = font.advance(ord(ch))
        if word(ch) and tokens and not tokens[-1]["space"] and word(tokens[-1]["cps"][-1]):
            tokens[-1]["cps"].append(ch)
            tokens[-1]["width"] += w
            continue
        tokens.append({"cps": [ch], "width": w, "space": False, "hang": ch in HANG})
    while tokens and tokens[-1]["space"]:
        tokens.pop()
    return tokens


def fill_line(tokens: list[dict], nxt: int, limit: int, font: Vlw) -> tuple[int, str, int]:
    line, used = "", 0
    while nxt < len(tokens) and tokens[nxt]["space"]:
        nxt += 1
    while nxt < len(tokens):
        t = tokens[nxt]
        if t["space"] and nxt + 1 < len(tokens) and used + t["width"] + tokens[nxt + 1]["width"] > limit:
            break
        if used + t["width"] <= limit or (t["hang"] and line):
            line += "".join(t["cps"])
            used += t["width"]
            nxt += 1
            continue
        if line and (len(t["cps"]) == 1 or t["width"] <= limit):
            break
        taken = 0
        while taken < len(t["cps"]):
            w = font.advance(ord(t["cps"][taken]))
            if used + w > limit and not (not line and taken == 0):
                break
            line += t["cps"][taken]
            used += w
            taken += 1
        t["cps"] = t["cps"][taken:]
        t["width"] = sum(font.advance(ord(c)) for c in t["cps"])
        if not t["cps"]:
            nxt += 1
        break
    return nxt, line, used


def layout(text: str, sizes: list[int], fonts: dict[int, Vlw]) -> tuple[int, int, list[tuple[str, int]], bool]:
    top_band, bottom_band = BAND
    for px in sizes:
        lh = line_height(px)
        for n in range(1, max(1, (bottom_band - top_band) // lh) + 1):
            top = (top_band + bottom_band) // 2 - n * lh // 2

            def attempt(shrink: int):
                tokens = tokenize(text, fonts[px])
                nxt, lines = 0, []
                for i in range(n):
                    if nxt >= len(tokens):
                        break
                    limit = chord(TEXT_R, top + i * lh, top + (i + 1) * lh) - shrink
                    nxt, line, used = fill_line(tokens, nxt, max(limit, px), fonts[px])
                    lines.append((line, used))
                while nxt < len(tokens) and tokens[nxt]["space"]:
                    nxt += 1
                return lines if nxt >= len(tokens) else None

            best = attempt(0)
            if best is None:
                continue
            shrink = px // 2
            while n > 1 and (narrower := attempt(shrink)) is not None:
                best, shrink = narrower, shrink + px // 2
            return px, top, best, False
    px = sizes[-1]
    font, lh = fonts[px], line_height(px)
    n = max(1, (bottom_band - top_band) // lh)
    top = (top_band + bottom_band) // 2 - n * lh // 2
    tokens, nxt, lines = tokenize(text, font), 0, []
    ell = font.advance(ELLIPSIS)
    for i in range(n):
        limit = chord(TEXT_R, top + i * lh, top + (i + 1) * lh)
        last = i == n - 1
        nxt, line, used = fill_line(tokens, nxt, limit - ell if last else limit, font)
        if last:
            while line and (line[-1] == " " or line[-1] in HANG or used + ell > limit):
                used -= font.advance(ord(line[-1]))
                line = line[:-1]
            line += chr(ELLIPSIS)
            used += ell
        lines.append((line, used))
    return px, top, lines, True


def clip_line(text: str, limit: int, font: Vlw) -> tuple[str, int]:
    cps = [c for t in tokenize(text, font) for c in t["cps"]]
    used = sum(font.advance(ord(c)) for c in cps)
    if used > limit:
        ell = font.advance(ELLIPSIS)
        while cps and used + ell > limit:
            used -= font.advance(ord(cps.pop()))
        cps.append(chr(ELLIPSIS))
        used += ell
    return "".join(cps), used


# --- src/walkie_ui.cpp reading page ------------------------------------------------------------------

def draw_hints(img: Image.Image, fonts: dict[int, Vlw], hints: list[tuple[str, str, tuple]]) -> None:
    font, lh = fonts[HINT_PX], line_height(HINT_PX)
    d = ImageDraw.Draw(img)
    chip, gap_label, gap_group = 26, 6, 20
    widths = [chip + gap_label + sum(font.advance(ord(c)) for c in label) for _, label, _ in hints]
    x = CX - (sum(widths) + gap_group * (len(hints) - 1)) // 2
    cy = HINT_TOP + lh // 2
    for (key, label, color), w in zip(hints, widths):
        d.ellipse((x, cy - chip // 2, x + chip, cy + chip // 2), fill=color)
        kw = font.advance(ord(key))
        font.draw(img, key, x + (chip - kw) // 2, HINT_TOP, BLACK, lh)
        font.draw(img, label, x + chip + gap_label, HINT_TOP, MUTED, lh)
        x += w + gap_group


def page(fonts, ring, label, label_color, body, hints, progress=None) -> Image.Image:
    img = Image.new("RGB", (466, 466), BLACK)
    d = ImageDraw.Draw(img)
    d.ellipse((CX - 216, CY - 216, CX + 216, CY + 216), outline=SURFACE, width=2)
    if progress is None:
        d.ellipse((CX - 216, CY - 216, CX + 216, CY + 216), outline=ring, width=5)
    else:
        d.arc((CX - 216, CY - 216, CX + 216, CY + 216), -90, -90 + 360 * progress, fill=ring, width=5)
    if label:
        lf = fonts[LABEL_PX]
        lh = line_height(LABEL_PX)
        text, w = clip_line(label, chord(LABEL_R, LABEL_TOP, LABEL_TOP + lh), lf)
        lf.draw(img, text, CX - w // 2, LABEL_TOP, label_color, lh)
    px, top, lines, _ = layout(body, [32, 24], fonts)
    lh = line_height(px)
    for i, (line, w) in enumerate(lines):
        fonts[px].draw(img, line, CX - w // 2, top + i * lh, WHITE, lh)
    draw_hints(img, fonts, hints)
    return img


def chrome(fonts, state: str, title: str, detail: str, color, hints) -> Image.Image:
    """Illustrated pages: the PNG artwork with its English copy painted over (src/walkie_ui.cpp drawMuseChrome)."""
    img = Image.open(ROOT / "ui-preview" / "assets" / "rgb565" / f"{state}.png").convert("RGB")
    d = ImageDraw.Draw(img)
    small, body = fonts[24], fonts[32]

    def line(text, font, limit, top, col):
        text, w = clip_line(text, limit, font)
        font.draw(img, text, CX - w // 2, top, col, line_height(font.px))

    art = rgb565(0x0841)
    d.rectangle((130, 50, 335, 79), fill=art)
    line("OpenMuse", small, 200, 50, MUTED)
    d.rectangle((100, 262, 365, 305), fill=art)
    line(title, body, 260, 264, WHITE)
    d.rectangle((70, 306, 395, 347), fill=art)
    if detail:
        line(detail, small, 300, 310, color)
    d.rectangle((124, 352, 341, 409), fill=art)
    draw_hints(img, fonts, hints)
    return img


def main() -> None:
    fonts = load_fonts(ROOT / "fonts" / "muse_fonts.bin")
    A, B = KEY_A, KEY_B
    pages = [
        ("确认（倒数中）", page(fonts, VIOLET, "2 秒后发送", VIOLET, "我这个月的订阅有哪些要续费",
                         [("A", "现在发", A), ("B", "取消", B)], progress=2 / 3)),
        ("回答", page(fonts, MINT, "问：明天有什么安排", MUTED, "明天上午九点有组会，十点前要交周报，下午三点体检。",
                     [("A", "再问", A), ("B", "好的", B)])),
        ("提醒 · 长条目", page(fonts, AMBER, "提醒 1/3", AMBER,
                          "会员订阅将于 10 月 15 日自动续费 ¥68，如果不再需要，请在到期前到账户设置里关闭自动续费。",
                          [("A", "再问", A), ("B", "下一条", B)])),
        ("提醒 · 最后一条", page(fonts, AMBER, "提醒 3/3", AMBER, "周六上午十点取快递，驿站 B 区 3-12。",
                            [("A", "再问", A), ("B", "知道了", B)])),
        ("出错", page(fonts, CORAL, "没发出去", CORAL, "今天的提问次数用完了",
                     [("A", "重说", A), ("B", "返回", B)])),
        ("超长回答（截断）", page(fonts, MINT, "问：总结一下这周所有邮件里提到的截止日期和需要我处理的事情", MUTED,
                            "本周共有五件事：周一交季度报告；周二确认会议室；周三回复供应商报价；周四提交报销单据并附发票；"
                            "周五前完成代码评审，另外还有两封邮件提到下周的出差安排需要你确认行程和酒店。",
                            [("A", "再问", A), ("B", "好的", B)])),
    ]
    pages += [
        ("主页（有提醒）", chrome(fonts, "ready", "可以提问", "有 2 条提醒", MINT, [("A", "按住说话", A), ("B", "看提醒", B)])),
        ("在听", chrome(fonts, "recording", "在听", "", CORAL, [("A", "松开发送", A), ("B", "取消", B)])),
        ("在想", chrome(fonts, "transcribing", "在想", "Muse 在想…", CYAN, [])),
    ]
    cols, cell, pad, cap = 3, 466, 24, 44
    rows = -(-len(pages) // cols)
    sheet = Image.new("RGB", (cols * (cell + pad) + pad, rows * (cell + pad + cap) + pad), (24, 24, 27))
    title_font = fonts[24]
    for i, (name, img) in enumerate(pages):
        x = pad + (i % cols) * (cell + pad)
        y = pad + (i // cols) * (cell + pad + cap)
        title_font.draw(sheet, name, x, y, (220, 220, 220), 30)
        sheet.paste(img, (x, y + cap))
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "ui-preview" / "output" / "reading-pages.png"
    sheet.save(out)
    print(out)


if __name__ == "__main__":
    main()
