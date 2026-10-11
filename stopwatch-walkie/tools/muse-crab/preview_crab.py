#!/usr/bin/env python3
"""Keyframes and an animated preview of the crab in every OpenMuse mode, for review before flashing.

The crab comes from include/muse_crab.h itself (compiled into dump_frames); the page around it mirrors
src/walkie_ui.cpp drawMusePage / drawReadingPage, with the real fonts from fonts/muse_fonts.bin.

    python3 tools/muse-crab/preview_crab.py   # -> ui-preview/output/crab-keyframes.png, crab-modes.gif
"""

from __future__ import annotations

import math
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "muse-fonts"))
import preview_reading as rp  # noqa: E402

CRAB, EYE = (0xD9, 0x77, 0x57), (0x1E, 0x1A, 0x17)
ACT = {"connecting": 0, "home": 1, "listening": 2, "thinking": 3, "confirm": 4, "answer": 5, "reminder": 6,
       "error": 7, "asleep": 8}
TITLE_TOP, DETAIL_TOP = 256, 300
BIG_U, BIG_FEET = 9, 236  # include/muse_crab.h kCrabBigU / kCrabBigFeet
CX = CY = 233


def dump_tool() -> Path:
    exe = Path(tempfile.gettempdir()) / "muse_crab_dump_frames"
    subprocess.run(["c++", "-std=c++17", "-I", str(ROOT / "include"), str(ROOT / "tools/muse-crab/dump_frames.cpp"),
                    "-o", str(exe)], check=True)
    return exe


def crab_rects(exe: Path, queries: list[tuple]) -> list[list[tuple]]:
    text = "".join(f"{ACT[a]} {t} {now} {lvl} {look}\n" for a, t, now, lvl, look in queries)
    out = subprocess.run([str(exe)], input=text, capture_output=True, text=True, check=True).stdout
    frames = []
    for line in out.splitlines():
        frames.append([tuple(int(v) for v in r.split(",")) for r in line.split(";") if r])
    return frames


def ring(d: ImageDraw.ImageDraw, color, mode: str, now: int, progress: float | None = None, level: int = 0):
    box = (CX - 216, CY - 216, CX + 216, CY + 216)
    d.ellipse(box, outline=rp.SURFACE, width=2)
    if mode in ("connecting", "thinking"):
        start = (now // 8) % 360  # lgfx degrees: 0 = 3 o'clock, clockwise
        d.arc(box, start, start + 72, fill=color, width=5)
    elif mode == "listening":
        pulse = 2 + int((math.sin(now * 0.008) + 1) * 2)
        d.ellipse(box, outline=color, width=4 + pulse)
    elif progress is not None:
        d.arc(box, 270, 270 + 360 * progress, fill=color, width=5)
    else:
        d.ellipse(box, outline=color, width=5)


def head_angle(mode: str, now: int, progress: float | None) -> float:
    if mode == "connecting":
        return (now // 8) % 360 + 72
    if mode == "confirm" and progress is not None:
        return 270 + 360 * progress
    return 270


def extras(d: ImageDraw.ImageDraw, mode: str, now: int, level: int, color, unread: int, fonts):
    if mode == "listening":
        lvl = level / 1000
        for side, heights in ((-1, (22, 40, 58, 30)), (1, (30, 58, 40, 22))):
            for i, base in enumerate(heights):
                gap = 8 * BIG_U + 12 + i * 12  # beside the claws, outward
                x = CX - gap - 7 if side < 0 else CX + gap
                shape = 0.6 + 0.4 * abs(math.sin(now * 0.012 + i * 0.75 + (side > 0) * 1.3))
                h = max(8, int(base * (0.3 + 0.7 * lvl) * shape))
                cy = BIG_FEET - 5 * BIG_U
                d.rounded_rectangle((x, cy - h // 2, x + 6, cy - h // 2 + h), radius=3, fill=color)
    elif mode == "thinking":
        for k in range(3):
            a = now * 0.004 + k * 2.0944
            x, y = CX + 30 * math.cos(a), BIG_FEET - 10 * BIG_U - 26 + 7 * math.sin(a)
            d.ellipse((x - 6, y - 6, x + 6, y + 6), fill=color)
    elif mode in ("home", "asleep") and unread:
        bx, by = CX + 8 * BIG_U + 8, BIG_FEET - 10 * BIG_U - 8
        d.ellipse((bx - 13, by - 13, bx + 13, by + 13), fill=rp.CORAL)


def page(fonts, exe_frame, mode: str, now: int, spec: dict) -> Image.Image:
    color = spec["color"]
    progress = spec.get("progress")
    if mode in ("confirm", "answer", "reminder", "error"):
        img = rp.page(fonts, color, spec["label"], rp.MUTED if spec.get("quiet") else color, spec["body"],
                      spec["hints"], progress=progress)
        d = ImageDraw.Draw(img)
    else:
        img = Image.new("RGB", (466, 466), rp.BLACK)
        d = ImageDraw.Draw(img)
        ring(d, color, mode, now, level=spec.get("level", 0))
        extras(d, mode, now, spec.get("level", 0), color, spec.get("unread", 0), fonts)
        big, small = fonts[32], fonts[24]
        t, w = rp.clip_line(spec["title"], 260, big)
        big.draw(img, t, CX - w // 2, TITLE_TOP, rp.WHITE, rp.line_height(32))
        if spec.get("detail"):
            t, w = rp.clip_line(spec["detail"], 300, small)
            small.draw(img, t, CX - w // 2, DETAIL_TOP, color, rp.line_height(24))
        rp.draw_hints(img, fonts, spec["hints"])
        if spec.get("unread"):
            n = str(spec["unread"])
            bx, by = CX + 8 * BIG_U + 8, BIG_FEET - 10 * BIG_U - 8
            small.draw(img, n, bx - small.advance(ord(n)) // 2, by - 15, rp.BLACK, 30)
    for x, y, w, h, eye in exe_frame:
        d.rectangle((x, y, x + w - 1, y + h - 1), fill=EYE if eye else CRAB)
    return img


def main() -> None:
    fonts = rp.load_fonts(ROOT / "fonts" / "muse_fonts.bin")
    exe = dump_tool()
    A, B = rp.KEY_A, rp.KEY_B
    modes = [
        ("connecting", "连接中", {"color": rp.AMBER, "title": "连接中", "detail": "正在连接个人云", "hints": []}),
        ("home", "主页", {"color": rp.MINT, "title": "可以提问", "detail": "有 2 条提醒", "unread": 2,
                        "hints": [("A", "按住说话", A), ("B", "看提醒", B)]}),
        ("listening", "在听", {"color": rp.CORAL, "title": "在听", "level": 650,
                             "hints": [("A", "松开发送", A), ("B", "取消", B)]}),
        ("thinking", "在想", {"color": rp.CYAN, "title": "在想", "detail": "Muse 在想…", "hints": [("B", "取消", B)]}),
        ("confirm", "确认", {"color": rp.VIOLET, "label": "2 秒后发送", "body": "我这个月的订阅有哪些要续费",
                           "hints": [("A", "现在发", A), ("B", "取消", B)]}),
        ("answer", "回答", {"color": rp.MINT, "label": "问：明天有什么安排", "quiet": True,
                          "body": "明天上午九点有组会，十点前要交周报，下午三点体检。",
                          "hints": [("A", "再问", A), ("B", "好的", B)]}),
        ("reminder", "提醒", {"color": rp.AMBER, "label": "提醒 1/3", "body": "周六上午十点取快递，驿站 B 区 3-12。",
                            "hints": [("A", "再问", A), ("B", "下一条", B)]}),
        ("error", "出错", {"color": rp.CORAL, "label": "没发出去", "body": "网络太慢，靠近路由器再试",
                         "hints": [("A", "重说", A), ("B", "返回", B)]}),
        ("asleep", "闲置两分钟（变暗）", {"color": rp.MINT, "title": "可以提问", "hints": [("A", "按住说话", A)]}),
    ]
    times = [0, 60, 120, 200, 340, 900]  # ms after entering the mode: anticipation, action, follow-through, idle
    base_now = 3000
    frames_per_mode: dict[str, list[Image.Image]] = {}
    for mode, _, spec in modes:
        queries, pages = [], []
        for t in times:
            now = base_now + t
            prog = None
            if mode == "confirm":
                prog = max(0.0, 1 - (t + 1000) / 3000)
                spec["progress"] = prog
            queries.append((mode, t, now, spec.get("level", 0), head_angle(mode, now, prog)))
        for (mode_q, t, now, _, _), rects in zip(queries, crab_rects(exe, queries)):
            if mode == "confirm":
                spec["progress"] = max(0.0, 1 - (t + 1000) / 3000)
            img = page(fonts, rects, mode, now, spec)
            if mode == "asleep":
                img = Image.eval(img, lambda v: v * 35 // 100)
            pages.append(img)
        frames_per_mode[mode] = pages

    # Contact sheet: one row per mode, the six keyframes at half size.
    cell, pad, cap, left = 233, 10, 26, 150
    sheet = Image.new("RGB", (left + len(times) * (cell + pad) + pad, len(modes) * (cell + pad) + cap + pad), (24, 24, 27))
    small = fonts[24]
    for j, t in enumerate(times):
        small.draw(sheet, f"{t} ms", left + j * (cell + pad) + 80, 0, (180, 180, 180), 26)
    for i, (mode, name, _) in enumerate(modes):
        y = cap + i * (cell + pad)
        small.draw(sheet, name, 12, y + cell // 2 - 15, (230, 230, 230), 30)
        for j, img in enumerate(frames_per_mode[mode]):
            sheet.paste(img.resize((cell, cell), Image.LANCZOS), (left + j * (cell + pad), y))
    out_dir = ROOT / "ui-preview" / "output"
    sheet.save(out_dir / "crab-keyframes.png")

    # Animated: each mode for 2.4 s at 12.5 fps (the firmware frame rate), in sequence.
    gif = []
    for mode, name, spec in modes:
        queries = []
        for k in range(30):
            t = k * 80
            now = base_now + t
            prog = max(0.0, 1 - t / 3000) if mode == "confirm" else None
            queries.append((mode, t, now, 300 + int(500 * abs(math.sin(k / 3))) if mode == "listening" else 0,
                            head_angle(mode, now, prog)))
        for (mode_q, t, now, lvl, _), rects in zip(queries, crab_rects(exe, queries)):
            if mode == "confirm":
                spec["progress"] = max(0.0, 1 - t / 3000)
            if mode == "listening":
                spec["level"] = lvl
            img = page(fonts, rects, mode, now, spec)
            if mode == "asleep":
                img = Image.eval(img, lambda v: v * 35 // 100)
            fonts[24].draw(img, name, 12, 8, (150, 150, 150), 30)
            gif.append(img.resize((350, 350), Image.LANCZOS))
    gif[0].save(out_dir / "crab-modes.gif", save_all=True, append_images=gif[1:], duration=80, loop=0)
    print(out_dir / "crab-keyframes.png")
    print(out_dir / "crab-modes.gif")


if __name__ == "__main__":
    main()
